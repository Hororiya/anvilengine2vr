"""Validate a freshly extracted local candidate without starting/attaching VR."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import zipfile


def extract(archive_path, directory):
    directory.mkdir(parents=True,exist_ok=False)
    with zipfile.ZipFile(archive_path) as archive:
        if archive.testzip() is not None:raise ValueError('ZIP CRC failed')
        for name in archive.namelist():
            if not (directory/name).resolve().is_relative_to(directory.resolve()):raise ValueError('Unsafe archive path')
        archive.extractall(directory)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--release-dir',type=Path,required=True)
    p.add_argument('--version',default='0.1')
    p.add_argument('--verify-dir',type=Path,required=True)
    a=p.parse_args()
    runtime=a.verify_dir/'runtime';source=a.verify_dir/'source'
    extract(a.release_dir/f'ShadowsVR-{a.version}-win64.zip',runtime)
    extract(a.release_dir/f'ShadowsVR-{a.version}-source.zip',source)
    results=[]
    def run(label,command,cwd=None):
        r=subprocess.run(command,cwd=cwd,capture_output=True,text=True)
        results.append({'check':label,'exitCode':r.returncode,'output':r.stdout+r.stderr})
        if r.returncode:raise RuntimeError(label+' failed: '+r.stdout+r.stderr)
    try:
        ps=r'''& { param([string]$Root) $ErrorActionPreference='Stop'; $errorsFound=@(); Get-ChildItem -LiteralPath $Root -Filter *.ps1 -Recurse | ForEach-Object { $t=$null;$e=$null; [Management.Automation.Language.Parser]::ParseFile($_.FullName,[ref]$t,[ref]$e)|Out-Null; $errorsFound+=@($e) }; if($errorsFound){$errorsFound;exit 1}; 'PASS: PowerShell syntax' } '''
        ps+="'"+str(runtime.resolve()).replace("'","''")+"'"
        run('PowerShell syntax',['powershell','-NoProfile','-Command',ps])
        # This does not import scripts, enter OpenXR, or attach any game.
        for path in (runtime/'tools').glob('*.py'):
            compile(path.read_text(encoding='utf-8-sig'),str(path),'exec')
        results.append({'check':'Python syntax','exitCode':0})
        run('Extracted preflight',['powershell','-NoProfile','-File',str((runtime/'Start-ShadowsVR.ps1').resolve()),'-ValidateOnly','-PythonPath',sys.executable])
        run('Native stop-event self-test',[str((runtime/'bin/ShadowsVisionPro.exe').resolve()),'--control-self-test','--log',str((a.verify_dir/'native-selftest.log').resolve())])
        for name in ('test_shadows_model_lod_native.py','test_shadows_native_history_reset.py','test_shadows_camera_cleanup.py'):
            run(name,[sys.executable,str((source/'tests'/name).resolve())],source)
    finally:
        a.verify_dir.mkdir(parents=True,exist_ok=True)
        (a.verify_dir/'verification.json').write_text(json.dumps({'version':a.version,'startsGameOrVR':False,'headsetRetest':False,'checks':results},indent=2),encoding='utf8')
    print(json.dumps({'version':a.version,'checksPassed':len(results),'report':str((a.verify_dir/'verification.json').resolve())},indent=2))


if __name__=='__main__':main()
