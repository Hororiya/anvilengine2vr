"""Native-only DLSS history isolation diagnostic; not per-eye history repair.

No FG setter, resource probe, DLL load, or geometry/camera change. Reset is
applied only during slSetConstants and restored immediately after it returns.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import time
import frida

C_SOURCE=r'''
#include <gum/guminterceptor.h>
#include <glib.h>
typedef struct { volatile gint enabled,active,calls,valid,writes,restores,viewport,original; } State;
typedef struct { guint8 *reset; gint restore; guint8 original; } Data;
extern State state;
void on_enter(GumInvocationContext *ctx){
 Data *d=GUM_IC_GET_INVOCATION_DATA(ctx,Data);d->restore=0;
 g_atomic_int_add((gint*)&state.active,1);g_atomic_int_add((gint*)&state.calls,1);
 guint8 *p=(guint8*)gum_invocation_context_get_nth_argument(ctx,0);
 if(!p||*(guint32*)(p+8)!=0xdcd35ad7||*(guint16*)(p+12)!=0x4e4a||*(guint16*)(p+14)!=0x4bad||*(guint64*)(p+16)!=0xfe3ab29ec4e00ca9ULL||*(guint64*)(p+24)!=2)return;
 guint8 original=p[447];if(original>1)return;
 g_atomic_int_add((gint*)&state.valid,1);state.original=original;
 guint8 *v=(guint8*)gum_invocation_context_get_nth_argument(ctx,2);if(v)state.viewport=*(guint32*)(v+32);
 if(g_atomic_int_add((gint*)&state.enabled,0)&&original!=1){
  d->reset=p+447;d->original=original;d->restore=1;*d->reset=1;g_atomic_int_add((gint*)&state.writes,1);
 }
}
void on_leave(GumInvocationContext *ctx){
 Data *d=GUM_IC_GET_INVOCATION_DATA(ctx,Data);
 if(d->restore&&*d->reset==1){*d->reset=d->original;g_atomic_int_add((gint*)&state.restores,1);}
 g_atomic_int_add((gint*)&state.active,-1);
}
'''
AGENT=r'''
const state=Memory.alloc(32);state.writeByteArray(new Uint8Array(32));
const cm=new CModule(C_SOURCE,{state});let hook=null;
function status(){return {enabled:state.readS32()!==0,active:state.add(4).readS32(),calls:state.add(8).readS32(),valid:state.add(12).readS32(),writes:state.add(16).readS32(),restores:state.add(20).readS32(),viewport:state.add(24).readS32(),originalReset:state.add(28).readS32()};}
rpc.exports.start=()=>{
 if(Process.mainModule.name.toLowerCase()!=='acshadows.exe')throw Error('wrong process');
 const address=Process.getModuleByName('sl.interposer.dll').getExportByName('slSetConstants');
 hook=Interceptor.attach(address,{onEnter:cm.on_enter,onLeave:cm.on_leave});return status();
};
rpc.exports.configure=value=>{state.writeS32(value?1:0);return status();};
rpc.exports.status=status;
rpc.exports.finish=()=>{state.writeS32(0);if(hook){hook.detach();hook=null;Interceptor.flush();}return status();};
'''

def agent_source(): return AGENT.replace('C_SOURCE',json.dumps(C_SOURCE))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pid',type=int,required=True)
    p.add_argument('--duration',type=float,default=0)
    p.add_argument('--reset-history',action='store_true')
    p.add_argument('--output',type=Path,default=Path('artifact/shadows-native/clarity/native-history-reset.json'))
    a=p.parse_args()
    if a.duration<0:p.error('negative duration')
    k=ctypes.WinDLL('kernel32',use_last_error=True)
    k.CreateEventW.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_wchar_p];k.CreateEventW.restype=ctypes.c_void_p
    k.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_uint32];k.CloseHandle.argtypes=[ctypes.c_void_p]
    stop=f'Local\\AnvilShadowsVR.TemporalReset.Stop.{os.getpid()}'
    event=k.CreateEventW(None,1,0,stop)
    if not event:raise ctypes.WinError(ctypes.get_last_error())
    session=frida.attach(a.pid);script=session.create_script(agent_source());started=False
    detached={'reason':None};session.on('detached',lambda reason,*args:detached.update(reason=str(reason)))
    result={'pid':a.pid,'hostPid':os.getpid(),'stopEvent':stop,'diagnosticOnly':True,'changesFrameGeneration':False}
    def save():
        a.output.parent.mkdir(parents=True,exist_ok=True);tmp=a.output.with_suffix('.tmp')
        tmp.write_text(json.dumps(result,indent=2),encoding='utf8');tmp.replace(a.output)
    try:
        script.load();result['start']=script.exports_sync.start();started=True
        script.exports_sync.configure(a.reset_history)
        end=time.monotonic()+a.duration if a.duration else None
        while detached['reason'] is None and (end is None or time.monotonic()<end):
            if k.WaitForSingleObject(event,1000)==0:break
            result['status']=script.exports_sync.status();save()
    finally:
        if started and detached['reason'] is None:
            result['finish']=script.exports_sync.finish()
            while result['finish']['active'] and detached['reason'] is None:
                time.sleep(.005);result['finish']=script.exports_sync.status()
        if detached['reason'] is not None:result['targetDetached']=detached['reason']
        script.unload();session.detach();k.CloseHandle(event);save()
    print(json.dumps(result),flush=True)

if __name__=='__main__':main()
