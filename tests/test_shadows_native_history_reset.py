"""Compile the release history workaround in a non-game fixture only."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import frida

spec=importlib.util.spec_from_file_location('history_reset',Path(__file__).resolve().parents[1]/'tools/shadows_native_history_reset.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)


def test_compile_and_control():
    process=subprocess.Popen([sys.executable,'-c','import time;time.sleep(10)'])
    session=frida.attach(process.pid);script=session.create_script(module.agent_source())
    try:
        script.load();assert script.exports_sync.status()['calls']==0
        assert script.exports_sync.configure(True)['enabled']
        result=script.exports_sync.finish()
        assert not result['enabled'] and result['active']==0 and result['writes']==0
    finally:
        script.unload();session.detach();process.terminate();process.wait(timeout=3)


if __name__=='__main__':test_compile_and_control();print('PASS')
