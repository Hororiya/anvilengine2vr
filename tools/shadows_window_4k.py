import argparse,frida,json,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--pid',type=int,required=True);p.add_argument('--hwnd',required=True)
p.add_argument('--output',type=Path,default=Path('artifact/shadows-native/clarity/window-4k-result.json'));a=p.parse_args()
s=frida.attach(a.pid)
x=s.create_script(r'''
const hwnd=ptr('HWND_SLOT');
const getLong=new NativeFunction(Module.getGlobalExportByName('GetWindowLongPtrW'),'pointer',['pointer','int']);
const adjust=new NativeFunction(Module.getGlobalExportByName('AdjustWindowRectEx'),'int',['pointer','uint','int','uint']);
const setPos=new NativeFunction(Module.getGlobalExportByName('SetWindowPos'),'int',['pointer','pointer','int','int','int','int','uint']);
const client=new NativeFunction(Module.getGlobalExportByName('GetClientRect'),'int',['pointer','pointer']);
const proc=getLong(hwnd,-4);let hooks=[];let calls=0;
rpc.exports.resize=()=>{
 if(Process.mainModule.name.toLowerCase()!=='acshadows.exe')throw Error('wrong process');
 const getOwner=new NativeFunction(Module.getGlobalExportByName('GetWindowThreadProcessId'),'uint',['pointer','pointer']);
 const owner=Memory.alloc(4);if(!getOwner(hwnd,owner)||owner.readU32()!==Process.id)throw Error('window does not belong to target game');
 hooks.push(Interceptor.attach(proc,{onEnter(args){this.mm=args[0].equals(hwnd)&&args[1].toUInt32()===0x24 ? args[3] : null;},onLeave(){if(this.mm){calls++;this.mm.add(8).writeS32(8192);this.mm.add(12).writeS32(8192);this.mm.add(32).writeS32(8192);this.mm.add(36).writeS32(8192);}}}));
 const r=Memory.alloc(16);r.writeS32(0);r.add(4).writeS32(0);r.add(8).writeS32(3840);r.add(12).writeS32(2160);
 adjust(r,getLong(hwnd,-16).toUInt32(),0,getLong(hwnd,-20).toUInt32());
 const width=r.add(8).readS32()-r.readS32(),height=r.add(12).readS32()-r.add(4).readS32();
 return {requestedClient:[3840,2160],requestedWindow:[width,height],result:setPos(hwnd,ptr(0),0,0,width,height,0x14),windowProc:proc.toString()};
};
rpc.exports.finish=()=>{const r=Memory.alloc(16);client(hwnd,r);const o={width:r.add(8).readS32(),height:r.add(12).readS32(),minmaxCalls:calls};hooks.forEach(h=>h.detach());hooks=[];return o;};
'''.replace('HWND_SLOT',a.hwnd))
x.load()
try:
 r={'request':x.exports_sync.resize()};time.sleep(1);r['actual']=x.exports_sync.finish();print(json.dumps(r));a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(r,indent=2),encoding='utf8')
finally:x.unload();s.detach()
