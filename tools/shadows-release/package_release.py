"""Build local, allowlisted runtime/source ZIPs. Never publishes or changes Git."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[2]
RELEASE = ROOT / 'tools' / 'shadows-release'
RUNTIME_PY = ('shadows_scene_camera.py', 'shadows_window_4k.py',
              'shadows_model_lod_native.py', 'shadows_native_history_reset.py')
NATIVE_SOURCE = ('shadows_vr_screen.cpp', 'shadows_eye_gaze.hpp',
                 'shadows_camera_math_bridge.cpp', 'shadows_native_color_bridge.cpp')
BINARIES = ('ShadowsVisionPro.exe', 'shadows_camera_math_bridge.dll',
            'shadows_native_color_bridge.dll', 'ShadowsColorReceiver.dll')
DOCS = ('README.md', 'BUILD.md', 'RELEASE_NOTES.md', 'THIRD_PARTY_NOTICES.md')
LAUNCHERS = ('Start-ShadowsVR.ps1', 'Stop-ShadowsVR.ps1', 'Set-ShadowsVR.ps1')
COMMON_FILES = ('include/anvilvr/CameraReprojection.hpp', 'src/CameraReprojection.cpp',
                'include/anvilvr/ShadowsCameraMathBridge.hpp',
                'include/anvilvr/ShadowsNativeColorBridge.hpp',
                'include/anvilvr/ShadowsPoseTransport.hpp',
                'include/anvilvr/ShadowsStereoPairMatcher.hpp',
                'include/anvilvr/ShadowsConsumerRecoveryPolicy.hpp')


def wrapper(name):
    parameters = {
        'Start-ShadowsVR.ps1': "[int]$GamePid=0,[string]$SettingsPath='', [string]$PythonPath='', [Alias('DryRun')][switch]$ValidateOnly",
        'Stop-ShadowsVR.ps1': '[int]$WaitSeconds=15',
        'Set-ShadowsVR.ps1': '[Nullable[double]]$CameraForward,[Nullable[double]]$LodBias',
    }
    return (f"[CmdletBinding()]\nparam({parameters[name]})\n"
            f"& (Join-Path $PSScriptRoot 'tools\\shadows-release\\{name}') @PSBoundParameters\n")


def assemble(destination, files):
    """Copy only explicit files. Existing unrelated files never enter the ZIP."""
    paths = []
    for relative, source in sorted(files.items()):
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(source, Path):
            if not source.is_file(): raise FileNotFoundError(source)
            shutil.copy2(source, target)
        else:
            target.write_text(source, encoding='utf8')
        paths.append(relative)
    return paths


def archive(directory, paths, archive_path):
    with zipfile.ZipFile(archive_path, 'w', zipfile.ZIP_DEFLATED) as archive:
        for relative in sorted(paths): archive.write(directory / relative, relative)
    with zipfile.ZipFile(archive_path) as archive:
        bad = archive.testzip()
        if bad: raise RuntimeError(f'ZIP CRC failed: {bad}')
        if set(archive.namelist()) != set(paths): raise RuntimeError('ZIP allowlist mismatch')
    return {'path': str(archive_path), 'bytes': archive_path.stat().st_size, 'files': len(paths), 'crcVerified': True}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-dir', type=Path, default=ROOT/'build/shadows-release/package')
    parser.add_argument('--output-dir', type=Path, default=ROOT/'artifact/releases')
    parser.add_argument('--version', default='0.1')
    args=parser.parse_args()
    if not all(c.isalnum() or c in '.-' for c in args.version): parser.error('invalid version')
    native_manifest_path=args.native_dir/'native-build-manifest.json'
    native_manifest=json.loads(native_manifest_path.read_text(encoding='utf-8-sig'))
    if native_manifest.get('configuration')!='Release' or native_manifest.get('architecture')!='x64':
        raise ValueError('Native build must be Release/x64')
    if native_manifest.get('openxr_loader_linkage')!='static':
        raise ValueError('The 0.1 portable profile requires the pinned statically linked OpenXR build')
    if (args.native_dir/'openxr_loader.dll').exists():
        raise ValueError('Unexpected shared OpenXR DLL in the static 0.1 native package; use a clean install directory')
    declared={item['name']:item['size'] for item in native_manifest['artifacts']}
    if set(declared)!=set(BINARIES): raise ValueError('Native artifact contract mismatch')
    for name in BINARIES:
        path=args.native_dir/name
        if path.stat().st_size!=declared[name]: raise ValueError(f'Native build size mismatch: {name}')
        with path.open('rb') as stream:
            if stream.read(2)!=b'MZ': raise ValueError(f'Not a PE: {name}')
            stream.seek(0x3c);offset=struct.unpack('<I',stream.read(4))[0]
            stream.seek(offset)
            if stream.read(4)!=b'PE\x00\x00' or struct.unpack('<H',stream.read(2))[0]!=0x8664:
                raise ValueError(f'Not an x64 PE: {name}')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    runtime_name=f'ShadowsVR-{args.version}-win64'
    source_name=f'ShadowsVR-{args.version}-source'
    runtime_dir=args.output_dir/runtime_name
    source_dir=args.output_dir/source_name
    runtime={'LICENSE': ROOT/'LICENSE', 'requirements.txt': RELEASE/'requirements.txt',
             'native-build-manifest.json':native_manifest_path,
             '.gitignore': 'state/\nlogs/\nartifact/\n.venv/\nsettings.json\n__pycache__/\n'}
    for name in DOCS: runtime[name]=RELEASE/name
    for name in LAUNCHERS:
        runtime[f'tools/shadows-release/{name}']=RELEASE/name
        runtime[name]=wrapper(name)
    for name in ('settings.default.json','requirements.txt'):
        runtime[f'tools/shadows-release/{name}']=RELEASE/name
    for name in RUNTIME_PY: runtime[f'tools/{name}']=ROOT/'tools'/name
    for name in BINARIES: runtime[f'bin/{name}']=args.native_dir/name
    loader=args.native_dir/'openxr_loader.dll'
    if loader.is_file(): runtime['bin/openxr_loader.dll']=loader
    for path in sorted((args.native_dir/'licenses').glob('*')):
        if path.is_file(): runtime[f'licenses/{path.name}']=path
    required_license='licenses/OpenXR-SDK-LICENSE.txt'
    if required_license not in runtime: raise RuntimeError('OpenXR license missing')

    source={relative:path for relative,path in runtime.items() if not relative.startswith('bin/')}
    for name in NATIVE_SOURCE: source[f'tools/{name}']=ROOT/'tools'/name
    for name in COMMON_FILES: source[f'common/{name}']=ROOT/'common'/name
    for name in ('EyeCamera.cpp','EyeCamera.hpp'): source[f'games/Shadows/sdk/{name}']=ROOT/'games/Shadows/sdk'/name
    for name in ('CMakeLists.txt','package_release.py','verify_release.py'):
        source[f'tools/shadows-release/{name}']=RELEASE/name
    for name in ('shadows_stereo_pair_matcher_tests.cpp','shadows_consumer_recovery_policy_tests.cpp',
                 'shadows_eye_gaze_tests.cpp','test_shadows_model_lod_native.py',
                 'test_shadows_native_history_reset.py','test_shadows_camera_cleanup.py'):
        source[f'tests/{name}']=ROOT/'tests'/name

    manifest={'version':args.version,'channel':'experimental-preview','localOnly':True,
              'createdUtc':datetime.now(timezone.utc).isoformat(),
              'nativeBuild':'standalone MSVC x64 Release; no private vrframework dependency',
              'openxrLoader':'dynamic' if native_manifest['openxr_loader_linkage']=='shared' else 'statically linked',
              'historyMode':'temporary per-frame reset, not completed independent-eye history',
              'settings':json.loads((RELEASE/'settings.default.json').read_text(encoding='utf-8-sig')),
              'runtimeFiles':sorted(runtime),'sourceFiles':sorted(source)}
    text=json.dumps(manifest,indent=2,ensure_ascii=False)+'\n'
    runtime['release-manifest.json']=text;source['release-manifest.json']=text
    runtime_paths=assemble(runtime_dir,runtime);source_paths=assemble(source_dir,source)
    reports=[archive(runtime_dir,runtime_paths,args.output_dir/(runtime_name+'.zip')),
             archive(source_dir,source_paths,args.output_dir/(source_name+'.zip'))]
    report={'version':args.version,'published':False,'gitCommandsExecuted':False,'archives':reports}
    (args.output_dir/'release-package-report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
