"""Native-only Shadows MPH main-view LOD controller.

The producer hook executes entirely inside a Frida CModule.  Python and
JavaScript only change a small atomic control block and take low-frequency
snapshots; neither runtime is entered for each producer call.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import math
import os
import time
from pathlib import Path

import frida


RVA = 0x41215F0
SIGNATURE = "4157415641554154565755534881ece8000000"
ALLOWED_CALLER_RVAS = (0x4112B11, 0x4113C89, 0x411DB24, 0x413510A)
MIN_BIAS = -0.5
MAX_BIAS = 0.0
SCALE_ONE_Q16 = 1 << 16
STATE_SIZE = 52


def bias_to_scale(bias: float) -> float:
    value = float(bias)
    if not math.isfinite(value) or not MIN_BIAS <= value <= MAX_BIAS:
        raise ValueError(f"geometry_lod_bias outside [{MIN_BIAS},{MAX_BIAS}]")
    return 2.0 ** -value


def bias_to_scale_q16(bias: float) -> int:
    return round(bias_to_scale(bias) * SCALE_ONE_Q16)


def read_control(path: Path) -> float:
    return float_control(json.loads(path.read_text(encoding="utf-8-sig")))


def float_control(value: object) -> float:
    if isinstance(value, dict):
        value = value.get("geometry_lod_bias", 0.0)
    bias = float(value)
    bias_to_scale(bias)  # validation
    return bias


CMODULE_SOURCE = r'''
#include <gum/guminterceptor.h>
#include <glib.h>

typedef struct {
  gpointer base_address;
  gpointer allocation_base;
  guint32 allocation_protect;
  guint16 partition_id;
  guint16 alignment;
  gsize region_size;
  guint32 state;
  guint32 protect;
  guint32 type;
  guint32 padding;
} MemoryBasicInformation;

typedef struct {
  volatile gint enabled;
  volatile gint scale_q16;
  volatile gint validate_memory;
  volatile gint active;
  volatile gint calls;
  volatile gint accepted;
  volatile gint neutral_calls;
  volatile gint writes;
  volatile gint errors;
  volatile gint sample_captured;
  volatile guint32 sample_before_bits;
  volatile guint32 sample_after_bits;
  volatile guint32 sample_caller_rva;
} State;

typedef struct {
  guint64 wrapper;
  gint accepted;
  gint reserved;
} InvocationData;

extern State state;
extern gsize virtual_query(gconstpointer address,
                           MemoryBasicInformation *information,
                           gsize length);

#define MEM_COMMIT 0x1000
#define PAGE_NOACCESS 0x01
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOPY 0x08
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_GUARD 0x100
#define SCALE_ONE_Q16 65536
#define FIELD_OFFSET 0x1210

static gboolean region_contains(const MemoryBasicInformation *m,
                                guint64 address, gsize size) {
  guint64 begin = (guint64) m->base_address;
  guint64 end = begin + (guint64) m->region_size;
  return end >= begin && address >= begin && address <= end &&
         size <= end - address;
}

static gboolean query_access(guint64 address, gsize size, gboolean write) {
  MemoryBasicInformation m;
  guint32 access;
  if (address == 0 || virtual_query((gconstpointer) address, &m, sizeof(m)) != sizeof(m))
    return FALSE;
  if (m.state != MEM_COMMIT || (m.protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
      !region_contains(&m, address, size))
    return FALSE;
  if (!write)
    return TRUE;
  access = m.protect & 0xff;
  return access == PAGE_READWRITE || access == PAGE_WRITECOPY ||
         access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY;
}

static gboolean allowed_caller(guint64 rva) {
  return rva == 0x4112b11 || rva == 0x4113c89 ||
         rva == 0x411db24 || rva == 0x413510a;
}

/* The control block is naturally aligned and the target is x86-64.  Aligned
   32-bit volatile stores are atomic; g_atomic_int_add(..., 0) supplies the
   full-barrier read used by callbacks. */
static gint atomic_read(volatile gint *value) {
  return g_atomic_int_add((gint *) value, 0);
}

static void atomic_write(volatile gint *target, gint value) {
  *target = value;
  g_atomic_int_add((gint *) target, 0);
}

void configure(gint enabled, gint scale_q16, gint validate_memory) {
  if (scale_q16 < SCALE_ONE_Q16)
    scale_q16 = SCALE_ONE_Q16;
  if (scale_q16 > 92682)
    scale_q16 = 92682;
  atomic_write(&state.scale_q16, scale_q16);
  atomic_write(&state.validate_memory, validate_memory != 0);
  atomic_write(&state.enabled, enabled != 0);
}

void reset_sample(void) {
  atomic_write(&state.sample_captured, 0);
}

void on_enter(GumInvocationContext *ctx) {
  InvocationData *data = GUM_IC_GET_INVOCATION_DATA(ctx, InvocationData);
  guint64 caller = (guint64) gum_invocation_context_get_return_address(ctx);
  guint64 base = (guint64) GUM_IC_GET_FUNC_DATA(ctx, gpointer);
  guint64 rva = caller - base;
  gint enabled;
  gint scale_q16;

  data->accepted = FALSE;
  g_atomic_int_add((gint *) &state.active, 1);
  g_atomic_int_add((gint *) &state.calls, 1);
  if (!allowed_caller(rva))
    return;

  enabled = atomic_read(&state.enabled);
  scale_q16 = atomic_read(&state.scale_q16);
  if (!enabled || scale_q16 == SCALE_ONE_Q16) {
    /* A true no-op: do not fetch args[2] and do not touch wrapper/buffer. */
    g_atomic_int_add((gint *) &state.neutral_calls, 1);
    return;
  }

  data->wrapper = (guint64) gum_invocation_context_get_nth_argument(ctx, 2);
  data->accepted = TRUE;
  g_atomic_int_add((gint *) &state.accepted, 1);
}

void on_leave(GumInvocationContext *ctx) {
  InvocationData *data = GUM_IC_GET_INVOCATION_DATA(ctx, InvocationData);
  guint64 wrapper;
  guint64 buffer;
  guint64 field;
  gint scale_q16;
  float before;
  float after;
  union { float f; guint32 u; } before_bits, after_bits;

  if (!data->accepted) {
    g_atomic_int_add((gint *) &state.active, -1);
    return;
  }
  wrapper = data->wrapper;
  if (wrapper == 0 || (wrapper & 7) != 0 || wrapper > (~(guint64) 0) - 0x50)
    goto error;
  /* Default hot path trusts the signature- and caller-gated producer lifetime:
     it just filled this caller-owned block and the caller consumes it after
     return.  VirtualQuery is reserved for explicit diagnostic mode because a
     syscall on every invocation defeats this controller's purpose. */
  if (atomic_read(&state.validate_memory) &&
      !query_access(wrapper + 0x50, sizeof(guint64), FALSE))
    goto error;
  buffer = *(guint64 *) (wrapper + 0x50);
  if (buffer == 0 || (buffer & 3) != 0 || buffer > (~(guint64) 0) - FIELD_OFFSET)
    goto error;
  field = buffer + FIELD_OFFSET;
  if (atomic_read(&state.validate_memory) && !query_access(field, sizeof(float), TRUE))
    goto error;

  before = *(float *) field;
  scale_q16 = atomic_read(&state.scale_q16);
  if (before == before && before > 0.0f && before < 3.402823466e+38f &&
      scale_q16 >= SCALE_ONE_Q16) {
    after = before * ((float) scale_q16 / (float) SCALE_ONE_Q16);
    if (after == after && after > 0.0f && after < 3.402823466e+38f) {
      *(float *) field = after;
      g_atomic_int_add((gint *) &state.writes, 1);
      if (atomic_read(&state.sample_captured) == 0) {
        before_bits.f = before;
        after_bits.f = after;
        state.sample_before_bits = before_bits.u;
        state.sample_after_bits = after_bits.u;
        state.sample_caller_rva = (guint32)
            ((guint64) gum_invocation_context_get_return_address(ctx) -
             (guint64) GUM_IC_GET_FUNC_DATA(ctx, gpointer));
        atomic_write(&state.sample_captured, 1);
      }
    }
  }
  g_atomic_int_add((gint *) &state.active, -1);
  return;

error:
  g_atomic_int_add((gint *) &state.errors, 1);
  g_atomic_int_add((gint *) &state.active, -1);
}
'''


AGENT = r'''
const cfg=CONFIG,base=Process.mainModule.base;
const state=Memory.alloc(CFG_STATE_SIZE);state.writeByteArray(new Uint8Array(CFG_STATE_SIZE));
const virtualQuery=Process.getModuleByName('kernel32.dll').getExportByName('VirtualQuery');
const cm=new CModule(CMODULE,{state,virtual_query:virtualQuery});
let hook=null;
const configureNative=new NativeFunction(cm.configure,'void',['int','int','int']);
const resetSampleNative=new NativeFunction(cm.reset_sample,'void',[]);
function hex(p,n){return Array.from(new Uint8Array(p.readByteArray(n))).map(x=>x.toString(16).padStart(2,'0')).join('')}
function f32(bits){const b=new ArrayBuffer(4),v=new DataView(b);v.setUint32(0,bits,true);return v.getFloat32(0,true)}
function status(){
 const sample=state.add(36).readS32()!==0;
 return {enabled:state.readS32()!==0,scaleQ16:state.add(4).readS32(),validateMemory:state.add(8).readS32()!==0,active:state.add(12).readS32(),
  calls:state.add(16).readS32(),accepted:state.add(20).readS32(),neutralCalls:state.add(24).readS32(),
  writes:state.add(28).readS32(),errors:state.add(32).readS32(),sample:sample?{
   before:f32(state.add(40).readU32()),after:f32(state.add(44).readU32()),callerRva:'0x'+state.add(48).readU32().toString(16)}:null};
}
rpc.exports.compileinfo=()=>({cmodule:true,nativeCallbacks:true,stateSize:CFG_STATE_SIZE});
rpc.exports.start=()=>{
 if(Process.mainModule.name.toLowerCase()!=='acshadows.exe')throw Error('wrong process');
 const address=base.add(cfg.rva),actual=hex(address,19);
 if(actual!==cfg.signature)throw Error('producer signature mismatch '+actual);
 configureNative(0,65536,0);
 hook=Interceptor.attach(address,{onEnter:cm.on_enter,onLeave:cm.on_leave},base);
 return {producerRva:cfg.rva,fieldOffset:0x1210,modifiedViews:[0],nativeCallbacks:true,
  perCallJavaScript:false,callerRvas:cfg.callers};
};
rpc.exports.configure=(enabled,scaleQ16,validateMemory)=>{configureNative(enabled?1:0,scaleQ16,validateMemory?1:0);return status();};
rpc.exports.resetsample=()=>{resetSampleNative();return status();};
rpc.exports.status=status;
rpc.exports.finish=()=>{
 configureNative(0,65536,0);
 if(hook){hook.detach();hook=null;Interceptor.flush();}
 return status();
};
'''


def make_agent() -> str:
    config = {"rva": RVA, "signature": SIGNATURE, "callers": list(ALLOWED_CALLER_RVAS)}
    return (AGENT.replace("CONFIG", json.dumps(config))
                 .replace("CFG_STATE_SIZE", str(STATE_SIZE))
                 .replace("CMODULE", json.dumps(CMODULE_SOURCE)))


def _atomic_write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2), encoding="utf-8")
    temporary.replace(path)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--duration", type=float, default=0)
    parser.add_argument("--control", type=Path, default=Path("artifact/shadows-native/lod-mip/control.json"))
    parser.add_argument("--output", type=Path, default=Path("artifact/shadows-native/lod-mip/model-lod-native-runtime.json"))
    parser.add_argument("--experimental", action="store_true", help="Explicit opt-in for live native-hook diagnostics")
    parser.add_argument("--validate-memory", action="store_true", help="Diagnostic only: call VirtualQuery in the native hot path")
    args = parser.parse_args()
    if not args.experimental:
        parser.error("native LOD hook requires --experimental")
    if args.duration < 0:
        parser.error("negative duration")

    kernel32 = ctypes.windll.kernel32
    kernel32.CreateEventW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_wchar_p]
    kernel32.CreateEventW.restype = ctypes.c_void_p
    kernel32.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    stop_name = f"Local\\AnvilShadowsVR.ModelLodNative.Stop.{os.getpid()}"
    stop_event = kernel32.CreateEventW(None, 1, 0, stop_name)
    if not stop_event:
        raise ctypes.WinError()
    result = {"pid": args.pid, "hostPid": os.getpid(), "stopEvent": stop_name}
    session = frida.attach(args.pid)
    script = session.create_script(make_agent())
    detached = {"reason": None}
    loaded = False
    started = False
    finished = False
    session.on("detached", lambda reason, *unused: detached.update(reason=str(reason)))

    def finish_safely() -> None:
        """Detach first, then retain CModule until every entered callback leaves."""
        nonlocal finished
        if finished or not started or detached["reason"] is not None:
            return
        result["finish"] = script.exports_sync.finish()
        while result["finish"]["active"] and detached["reason"] is None:
            time.sleep(0.005)
            result["finish"] = script.exports_sync.status()
        finished = detached["reason"] is None

    try:
        script.load()
        loaded = True
        result["start"] = script.exports_sync.start()
        started = True
        bias = read_control(args.control)
        result["configuredBias"] = bias
        script.exports_sync.configure(bias != 0.0, bias_to_scale_q16(bias), args.validate_memory)
        print(json.dumps(result), flush=True)
        deadline = time.monotonic() + args.duration if args.duration else None
        mtime = args.control.stat().st_mtime_ns
        next_status = 0.0
        while (deadline is None or time.monotonic() < deadline) and detached["reason"] is None:
            if kernel32.WaitForSingleObject(stop_event, 200) == 0:
                break
            current = args.control.stat().st_mtime_ns
            if current != mtime:
                bias = read_control(args.control)
                result["configuredBias"] = bias
                script.exports_sync.configure(bias != 0.0, bias_to_scale_q16(bias), args.validate_memory)
                mtime = current
            if time.monotonic() >= next_status:
                result["status"] = script.exports_sync.status()
                _atomic_write_json(args.output, result)
                next_status = time.monotonic() + 1.0

        if detached["reason"] is None:
            finish_safely()
    finally:
        if detached["reason"] is not None:
            result["targetDetached"] = detached["reason"]
        elif started and not finished:
            # This also covers invalid control files and other exceptions raised
            # after installation.  Never unload CModule while active > 0.
            finish_safely()
        if loaded and (finished or not started or detached["reason"] is not None):
            try:
                script.unload()
            except Exception as error:
                result["unloadError"] = str(error)
        if finished or not started or detached["reason"] is not None:
            try:
                session.detach()
            except Exception:
                pass
        kernel32.CloseHandle(stop_event)
        _atomic_write_json(args.output, result)
    print(json.dumps(result.get("finish", {})), flush=True)


if __name__ == "__main__":
    main()
