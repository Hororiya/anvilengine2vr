"""Offline tests for the native-only Shadows model-LOD controller."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys

import frida


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "shadows_model_lod_native", ROOT / "tools" / "shadows_model_lod_native.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def test_bias_is_bounded_and_q16() -> None:
    assert MODULE.bias_to_scale_q16(0.0) == 65536
    assert MODULE.bias_to_scale_q16(-0.5) == round((2.0 ** 0.5) * 65536)
    assert abs(MODULE.bias_to_scale(-0.25) - 2.0 ** 0.25) < 1e-12
    for invalid in (-0.50001, 0.01, float("nan")):
        try:
            MODULE.bias_to_scale(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError(f"accepted invalid bias {invalid!r}")


def test_native_source_has_no_per_call_js_or_shadow_slot_loop() -> None:
    source = MODULE.CMODULE_SOURCE
    assert "GUM_IC_GET_INVOCATION_DATA" in source
    assert "FIELD_OFFSET 0x1210" in source
    assert "for (" not in source
    assert "virtual_query" in source
    assert "gum_invocation_context_get_nth_argument(ctx, 2)" in source
    neutral = source.index("if (!enabled || scale_q16 == SCALE_ONE_Q16)")
    argument = source.index("gum_invocation_context_get_nth_argument(ctx, 2)")
    assert neutral < argument
    agent = MODULE.make_agent()
    assert "onEnter:cm.on_enter,onLeave:cm.on_leave" in agent
    assert "Process.findRangeByAddress" not in agent
    assert "atomic_read(&state.validate_memory)" in source


def test_cmodule_compiles_in_non_game_process_without_hooking() -> None:
    process = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(15)"])
    session = script = None
    try:
        session = frida.attach(process.pid)
        # CModule construction happens at script load; start() is deliberately not
        # called, so this test neither checks nor hooks a game process.
        script = session.create_script(MODULE.make_agent())
        script.load()
        info = script.exports_sync.compileinfo()
        assert info == {"cmodule": True, "nativeCallbacks": True, "stateSize": 52}
        initial = script.exports_sync.status()
        assert initial["calls"] == 0
        configured = script.exports_sync.configure(True, MODULE.bias_to_scale_q16(-0.25), False)
        assert configured["enabled"] is True
        assert configured["scaleQ16"] == MODULE.bias_to_scale_q16(-0.25)
        assert configured["calls"] == 0
        neutral = script.exports_sync.configure(False, MODULE.SCALE_ONE_Q16, False)
        assert neutral["enabled"] is False
    finally:
        if script is not None:
            script.unload()
        if session is not None:
            session.detach()
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()


if __name__ == "__main__":
    test_bias_is_bounded_and_q16()
    test_native_source_has_no_per_call_js_or_shadow_slot_loop()
    test_cmodule_compiles_in_non_game_process_without_hooking()
    print("PASS")
