"""Quick original-camera prototype: before canonical update and visibility snapshot.
No executable-file hashing, no second XR session, bounded automatic rollback.
"""
import argparse
import json
from pathlib import Path
import time
import ctypes
import os
import frida

AGENT=r'''
const cfg=CONFIG,b=Process.mainModule.base,hs=[],records=[];let props=null,pending=null,enabled=true,n=0,applied=0,normal=0,active=0;
let dll=null,compute=null,shutdown=null;const errors={};
let colorDll=null,publisher=null,publisherSwap=null,createPublisher=null,publishEye=null,destroyPublisher=null,publisherIdle=null,retiredPublishers=[],rebuilds=0;
let pairBase=null,pairNear=0,pairFar=0;
let cameraForward=cfg.cameraForward||0,pairForward=0;
let publishing=false,queues=[],metadataQueue=[],sourceFrame=0,presentCalls=0,published=[0,0],droppedTags=0;
function bytes(p,n){return Array.from(new Uint8Array(p.readByteArray(n))).map(x=>x.toString(16).padStart(2,'0')).join('')}
function floats(p,n){return Array.from(new Float32Array(p.readByteArray(n*4)))}
function restore(){if(pending){for(const c of pending.chunks){if(bytes(c.p,c.size)===c.written)c.p.writeByteArray(c.original)}pending=null}}
function projectionVP(projection,c2w,low){
 // Column-major storage; C2W input columns right/forward/up/position.
 const t=[c2w[12]+low[0],c2w[13]+low[1],c2w[14]+low[2]],r=c2w.slice(0,3),u=c2w.slice(8,11),back=c2w.slice(4,7).map(v=>-v);
 const v=new Float32Array(16);v[15]=1;
 [r,u,back].forEach((axis,row)=>{for(let col=0;col<3;col++)v[row+col*4]=axis[col];v[row+12]=-(axis[0]*t[0]+axis[1]*t[1]+axis[2]*t[2])});
 const out=new Float32Array(16);for(let col=0;col<4;col++)for(let row=0;row<4;row++){let sum=0;for(let k=0;k<4;k++)sum+=projection[row+k*4]*v[k+col*4];out[row+col*4]=sum}return out;
}
rpc.exports.describe=()=>({name:Process.mainModule.name,path:Process.mainModule.path});
rpc.exports.start=()=>{
 const load=new NativeFunction(Module.getGlobalExportByName('LoadLibraryW'),'pointer',['pointer']);
 const getProc=new NativeFunction(Module.getGlobalExportByName('GetProcAddress'),'pointer',['pointer','pointer']);
 function symbol(handle,name){const p=getProc(handle,Memory.allocUtf8String(name));if(p.isNull())throw Error('missing export '+name);return p;}
 for(const s of cfg.sites)if(bytes(b.add(s.rva),24)!==s.signature)throw Error('entry signature mismatch');
 dll=load(Memory.allocUtf16String(cfg.helper));if(dll.isNull())throw Error('helper load failed');
 compute=new NativeFunction(symbol(dll,cfg.paired?'AnvilShadowsCameraBridgeComputePairedLive':'AnvilShadowsCameraBridgeComputeLive'),'uint32',['pointer','pointer','double','double','double','uint32','pointer']);
 shutdown=new NativeFunction(symbol(dll,'AnvilShadowsCameraBridgeShutdown'),'void',[]);
 new NativeFunction(symbol(dll,'AnvilShadowsCameraBridgeReset'),'uint32',[])();
 if(cfg.colorHelper){
  colorDll=load(Memory.allocUtf16String(cfg.colorHelper));if(colorDll.isNull())throw Error('color helper load failed');
  createPublisher=new NativeFunction(symbol(colorDll,'AnvilShadowsNativeColor_CreatePublisher'),'uint32',['pointer','pointer','pointer','pointer']);
  publishEye=new NativeFunction(symbol(colorDll,'AnvilShadowsNativeColor_PublishEye'),'uint32',['pointer','pointer','pointer']);
  destroyPublisher=new NativeFunction(symbol(colorDll,'AnvilShadowsNativeColor_DestroyPublisher'),'void',['pointer']);
  const idleAddress=getProc(colorDll,Memory.allocUtf8String('AnvilShadowsNativeColor_PublisherIdle'));
  if(!idleAddress.isNull())publisherIdle=new NativeFunction(idleAddress,'uint32',['pointer']);
  const address=Memory.alloc(32),getAddresses=new NativeFunction(symbol(colorDll,'AnvilShadowsNativeColor_GetHookAddresses'),'uint32',['pointer']);
  if(getAddresses(address)!==0)throw Error('graphics hook discovery failed');
  const present=address.add(8).readPointer(),execute=address.add(16).readPointer();
  hs.push(Interceptor.attach(execute,{onEnter(args){if(publishing||!enabled)return;const q=args[0];
   queues=queues.filter(v=>!v.equals(q));queues.push(q);if(queues.length>8)queues.shift();}}));
  hs.push(Interceptor.attach(present,{onEnter(args){
   // DXGI_PRESENT_TEST does not display a rendered frame and must not consume
   // an eye tag or trigger a backbuffer copy.
   if((args[2].toUInt32()&1)!==0)return;
   if(publishing||!enabled||metadataQueue.length===0)return;presentCalls++;
   publishing=true;try{
    if(publisherIdle){retiredPublishers=retiredPublishers.filter(p=>{if(publisherIdle(p)){destroyPublisher(p);return false;}return true;});}
    if(publisher&&publisherSwap&&!publisherSwap.equals(args[0])){retiredPublishers.push(publisher);publisher=null;publisherSwap=null;rebuilds++;}
    if(retiredPublishers.length>=3){errors.retirementBusy=(errors.retirementBusy||0)+1;return;}
    if(!publisher){const output=Memory.alloc(Process.pointerSize);let status=0;
     const candidates=cfg.graphicsQueue?[ptr(cfg.graphicsQueue)]:queues.slice().reverse();
     for(const queue of candidates){output.writePointer(ptr(0));status=createPublisher(args[0],queue,Memory.allocUtf16String(cfg.mapping),output);
      if(status===0){publisher=output.readPointer();publisherSwap=args[0];break;}}
     if(!publisher){errors['color_create_'+status]=(errors['color_create_'+status]||0)+1;return;}
    }
    const meta=metadataQueue.shift(),result=Memory.alloc(40),status=publishEye(publisher,meta.data,result);
    if(status===0)published[meta.eye]++;else{errors['color_publish_'+status]=(errors['color_publish_'+status]||0)+1;if(status===10&&publisherIdle){retiredPublishers.push(publisher);publisher=null;publisherSwap=null;rebuilds++;}}
   }catch(e){errors['color:'+e]=(errors['color:'+e]||0)+1}finally{publishing=false;}
  }}));
 }
 hs.push(Interceptor.attach(b.add(0x43dbad0),{onEnter(args){try{
  const p=args[0].add(0x98).readPointer().add(0x50).readPointer();
  if(p.readPointer().equals(b.add(0x8ec1048))&&(!props||!p.equals(props))){
   if(props){try{restore();}catch(e){pending=null;}metadataQueue=[];if(n%2)n++;pairBase=null;errors.primaryCameraReplaced=(errors.primaryCameraReplaced||0)+1;}
   props=p;
  }
 }catch(e){}}}));
 hs.push(Interceptor.attach(b.add(0x7803c0),{onEnter(args){this.accepted=false;this.out=null;
  if(!props||!args[0].equals(props)||!this.returnAddress.equals(b.add(0x408c363)))return;
  this.accepted=true;this.props=props;active++;restore();if(!enabled){normal++;return;}
  try{if(!props.add(0x600).readPointer().isNull()){errors.linkedSource=(errors.linkedSource||0)+1;return;}
   const p=props.add(0x290),out=Memory.alloc(272),eye=cfg.aer?(n%2):0;
   const chunks=[{p,size:80}];if(cfg.hmdProjection)chunks.push({p:props.add(0x24c),size:4},{p:props.add(0x2e0),size:4},{p:props.add(0x330),size:64},{p:props.add(0x2f0),size:64});
   for(const c of chunks)c.original=c.p.readByteArray(c.size);
   if(cfg.paired&&eye===0){pairBase=Memory.alloc(80);pairBase.writeByteArray(p.readByteArray(80));pairNear=props.add(0x250).readFloat();pairFar=props.add(0x254).readFloat();pairForward=cameraForward;
    for(let i=0;i<3;i++)pairBase.add(48+i*4).writeFloat(pairBase.add(48+i*4).readFloat()+pairBase.add(16+i*4).readFloat()*pairForward);
   }
   let base=cfg.paired?pairBase:p;
   if(!cfg.paired&&cameraForward!==0){base=Memory.alloc(80);base.writeByteArray(p.readByteArray(80));for(let i=0;i<3;i++)base.add(48+i*4).writeFloat(base.add(48+i*4).readFloat()+base.add(16+i*4).readFloat()*cameraForward);}
   const status=compute(base,base.add(64),1.0,cfg.paired?pairNear:props.add(0x250).readFloat(),cfg.paired?pairFar:props.add(0x254).readFloat(),eye,out);
   if(status!==0){if(cfg.paired&&eye===1)n++;errors[status]=(errors[status]||0)+1;return;}
   this.pairId=cfg.paired?Math.floor(n/2)+1:0;if(cfg.aer)n++;
   p.writeByteArray(out.add(80).readByteArray(64));p.add(64).writeByteArray(out.add(144).readByteArray(16));
   if(cfg.hmdProjection){const f=floats(out.add(64),4),scale=props.add(0x5f8).readFloat();
    const conservativeVertical=2*Math.max(Math.abs(f[2]),Math.abs(f[3]));
    props.add(0x24c).writeFloat(conservativeVertical/(Number.isFinite(scale)&&scale>0.1?scale:1));
   }
   for(const c of chunks)c.written=bytes(c.p,c.size);pending={chunks};this.out=out;this.eye=eye;applied++;
   this.record={eye,poseSequence:out.add(8).readU64().toString(),inputC2W:floats(out.add(80),16),
     sourceOrientation:floats(out.add(32),4),beforeVP:floats(props.add(0x2f0),16)};
  }catch(e){restore();errors[String(e)]=(errors[String(e)]||0)+1}
 },onLeave(ret){if(!this.accepted)return;const props=this.props;try{
  if(this.out&&cfg.hmdProjection){
   const originalProjection=floats(props.add(0x330),16),projection=new Float32Array(floats(this.out.add(160),16));
   if(!cfg.asymmetricProjection){
    // Shared wide render envelope avoids alternating principal-point shifts
    // while the game still has a single temporal-history path. Real eye
    // positions remain different; submission metadata matches this render FOV.
    const f=floats(this.out.add(64),4),horizontal=Math.max(Math.abs(Math.tan(f[0])),Math.abs(Math.tan(f[1]))),
      vertical=Math.max(Math.abs(Math.tan(f[2])),Math.abs(Math.tan(f[3])));
    projection[0]=1/horizontal;projection[5]=1/vertical;projection[8]=0;projection[9]=0;
   }
   // Keep the game's depth coefficients/clip range; replace eye optics only.
   for(const i of [10,11,14,15])projection[i]=originalProjection[i];
   props.add(0x330).writeByteArray(projection.buffer);
   const c2w=floats(props.add(0x290),16),low=floats(props.add(0x2d0),4);
   props.add(0x2f0).writeByteArray(projectionVP(projection,c2w,low).buffer);
   props.add(0x2e0).writeFloat(projection[5]/projection[0]);
   if(pending)for(const c of pending.chunks)c.written=bytes(c.p,c.size);
   this.record.eyeProjection=Array.from(projection);this.record.hmdProjection=true;
  }
  if(this.out&&cfg.colorHelper){const meta=Memory.alloc(88);meta.writeByteArray(new Uint8Array(88));meta.writeU32(88);meta.add(4).writeU32(1);
   meta.add(8).writeU64(++sourceFrame);meta.add(16).writeU64(this.out.add(8).readU64());meta.add(24).writeS64(this.out.add(16).readS64());
   meta.add(32).writeU32(this.eye);meta.add(36).writeU32(this.pairId);meta.add(40).writeByteArray(this.out.add(32).readByteArray(16));meta.add(56).writeByteArray(this.out.add(48).readByteArray(12));
   const p=floats(props.add(0x330),16),fov=new Float32Array([Math.atan((p[8]-1)/p[0]),Math.atan((p[8]+1)/p[0]),Math.atan((p[9]+1)/p[5]),Math.atan((p[9]-1)/p[5])]);
   meta.add(68).writeByteArray(fov.buffer);metadataQueue.push({data:meta,eye:this.eye});if(metadataQueue.length>8){metadataQueue.shift();droppedTags++;}
  }
  if(this.out&&records.length<64){this.record.updatedC2W=floats(props.add(0x290),16);
   this.record.updatedBasis=floats(props.add(0x370),12);this.record.updatedVP=floats(props.add(0x2f0),16);records.push(this.record)}
 }finally{active--;}}}));
 return {mode:'original-camera-before-visibility',aer:cfg.aer,colorCapture:!!cfg.colorHelper,hmdProjection:cfg.hmdProjection,paired:cfg.paired};
};
rpc.exports.stop=()=>{enabled=false;return {active,pending:!!pending}};
rpc.exports.configurecamera=value=>{if(!Number.isFinite(value)||value< -2||value>3)throw Error('camera forward outside [-2,3]');cameraForward=value;return {cameraForward};};
function gpuIdle(){return !publisherIdle||((!publisher||publisherIdle(publisher)!==0)&&retiredPublishers.every(p=>publisherIdle(p)!==0));}
rpc.exports.status=()=>({active,pending:!!pending,normal,applied,presentCalls,published,publishing,gpuIdle:gpuIdle(),errors,rebuilds,cameraForward,pairForward,retiredPublishers:retiredPublishers.length,primaryProps:props?props.toString():null});
rpc.exports.finish=()=>{enabled=false;if(active||publishing||!gpuIdle())throw Error('callback or GPU copy active');restore();hs.forEach(h=>h.detach());hs.length=0;Interceptor.flush();
 if(publisher){destroyPublisher(publisher);publisher=null;}
 for(const old of retiredPublishers)destroyPublisher(old);retiredPublishers=[];publisherSwap=null;
 const free=new NativeFunction(Module.getGlobalExportByName('FreeLibrary'),'int',['pointer']);let colorFreed=null;if(colorDll){colorFreed=free(colorDll)!==0;colorDll=null;}
 if(shutdown){shutdown();shutdown=null;compute=null;}let freed=null;if(dll){freed=free(dll)!==0;dll=null;}
 return {applied,normal,errors,records,helperFreed:freed,colorFreed,presentCalls,published,droppedTags};};
'''

def finish_camera(script, detached, wait=time.sleep, now=time.monotonic):
    """Disable, retain hooks/helpers until CPU and copy fences drain, then finish.

    A launcher may time out and report this host as remaining. Never force-unload
    its script/DLL while callbacks or queued copies are still using them.
    """
    script.exports_sync.stop()
    grace_end = now() + 2
    while detached['reason'] is None:
        status = script.exports_sync.status()
        if status['active'] == 0 and not status['publishing'] and status.get('gpuIdle', True):
            if not status['pending'] or status['normal'] > 0 or now() >= grace_end:
                return script.exports_sync.finish()
        wait(.01)
    return None

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pid',type=int,required=True);p.add_argument('--duration',type=float,default=5)
    p.add_argument('--aer',action='store_true');p.add_argument('--output',type=Path,default=Path('artifact/shadows-native/early-camera-prototype.json'))
    p.add_argument('--native-color-helper',type=Path)
    p.add_argument('--graphics-queue',help='Known live ID3D12CommandQueue pointer from the game debugger; no queue scan')
    p.add_argument('--hmd-projection',action='store_true',help='Use actual asymmetric headset eye FOV and coherent pre-visibility VP')
    p.add_argument('--asymmetric-projection',action='store_true',help='Experimental distinct eye principal points; requires same-eye temporal history')
    p.add_argument('--paired',action='store_true',help='Latch both eyes to one XR sample and one base game camera per AER pair')
    p.add_argument('--camera-math-helper',type=Path,default=Path('build/full/Release/shadows_camera_math_bridge.dll'))
    p.add_argument('--camera-control',type=Path,help='Hot control JSON: forward_game_units, bounded [-2,3]; zero restores original distance')
    args=p.parse_args()
    if not 0<=args.duration<=86400:p.error('duration must be 0..86400; zero runs until the stop event')
    if args.graphics_queue:
        try:
            queue_value=int(args.graphics_queue,0)
            if not 0<queue_value<2**64:raise ValueError()
        except ValueError:p.error('--graphics-queue must be a nonzero 64-bit address')
    sites=[{'rva':0x43dbad0,'signature':'4157415641554154565755534881ec28020000c578298c24'},
           {'rva':0x7803c0,'signature':'4157415641554154565755534881ecd8000000c5f829bc24'}]
    cfg={'sites':sites,'helper':str(args.camera_math_helper.resolve(strict=True)),
         'aer':args.aer or args.native_color_helper is not None,'colorHelper':str(args.native_color_helper.resolve(strict=True)) if args.native_color_helper else None,
         'mapping':r'Local\AnvilShadowsVR.NativeColor.v1','graphicsQueue':args.graphics_queue,'hmdProjection':args.hmd_projection,
         'asymmetricProjection':args.asymmetric_projection,'paired':args.paired,'cameraForward':0}
    def camera_control():
        value=float(json.loads(args.camera_control.read_text(encoding='utf-8-sig')).get('forward_game_units',0))
        if not -2<=value<=3:raise ValueError('forward_game_units outside [-2,3]')
        return value
    if args.camera_control:cfg['cameraForward']=camera_control()
    kernel=ctypes.windll.kernel32
    kernel.CreateEventW.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_wchar_p];kernel.CreateEventW.restype=ctypes.c_void_p
    kernel.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
    kernel.CloseHandle.argtypes=[ctypes.c_void_p]
    stop_name=f'Local\\AnvilShadowsVR.Camera.Stop.{os.getpid()}'
    stop_event=kernel.CreateEventW(None,1,0,stop_name)
    if not stop_event:raise RuntimeError('stop event creation failed')
    session=frida.attach(args.pid);script=None;loaded=False;finished=False;result={'config':cfg,'stopEvent':stop_name}
    detached={'reason':None};session.on('detached',lambda reason,*unused:detached.update(reason=str(reason)))
    try:
        script=session.create_script(AGENT.replace('CONFIG',json.dumps(cfg)));script.load();loaded=True
        identity=script.exports_sync.describe()
        if identity['name'].lower()!='acshadows.exe' or Path(identity['path']).name.lower()!='acshadows.exe':raise RuntimeError('wrong process')
        result['identity']=identity;result['start']=script.exports_sync.start();print(json.dumps(result['start']),flush=True)
        print(json.dumps({'stopEvent':stop_name}),flush=True)
        next_status=0.0
        camera_mtime=args.camera_control.stat().st_mtime_ns if args.camera_control else None
        deadline=time.monotonic()+args.duration if args.duration else None
        while detached['reason'] is None and (deadline is None or time.monotonic()<deadline):
            if kernel.WaitForSingleObject(stop_event,200)==0:break
            if args.camera_control:
                current=args.camera_control.stat().st_mtime_ns
                if current!=camera_mtime:
                    script.exports_sync.configurecamera(camera_control());camera_mtime=current
            if time.monotonic()>=next_status:
                live={"pid":args.pid,"hostPid":os.getpid(),"status":script.exports_sync.status()}
                path=args.output.with_suffix('.live.json');path.parent.mkdir(parents=True,exist_ok=True)
                temp=path.with_suffix('.tmp');temp.write_text(json.dumps(live,indent=2),encoding='utf8');temp.replace(path)
                next_status=time.monotonic()+2
        if detached['reason'] is None:
            extra=finish_camera(script,detached)
            if extra is not None:result.update(extra);finished=True
    finally:
        if loaded and not finished and detached['reason'] is None:
            extra=finish_camera(script,detached)
            if extra is not None:result.update(extra);finished=True
        if detached['reason'] is not None:result['targetDetached']=detached['reason']
        if script and (finished or not loaded or detached['reason'] is not None):
            try:script.unload()
            except Exception as e:result['unloadError']=str(e)
        if finished and detached['reason'] is None:
            verify=session.create_script("rpc.exports.read=rs=>rs.map(r=>Array.from(new Uint8Array(Process.mainModule.base.add(r).readByteArray(24))).map(x=>x.toString(16).padStart(2,'0')).join(''))")
            verify.load();actual=verify.exports_sync.read([x['rva'] for x in sites]);result['restored']=[a==s['signature'] for a,s in zip(actual,sites)]
            verify.unload()
        if finished or not loaded or detached['reason'] is not None:session.detach()
        kernel.CloseHandle(stop_event);args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps({k:result.get(k) for k in ['applied','normal','errors','restored','helperFreed','presentCalls','published','droppedTags']}))

if __name__=='__main__':main()
