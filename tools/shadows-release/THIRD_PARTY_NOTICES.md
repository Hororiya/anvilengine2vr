# Third-party notices

## AnvilEngine2VR
Upstream: https://github.com/mutars/anvilengine2vr
MIT License, copyright (c) 2024 mutars. Full text: LICENSE.
This is an experimental derivative preview, not an upstream author's release.

## OpenXR SDK / loader
Upstream: https://github.com/KhronosGroup/OpenXR-SDK
Apache License 2.0. Full text included as licenses/OpenXR-SDK-LICENSE.txt.
OpenXR notices/credits supplied by the pinned SDK are retained where available.
The loader is statically linked in the default build (or shipped as a DLL when
using a shared-loader SDK); the active runtime (KRVR) is installed separately.

## Frida
Upstream: https://frida.re/ and https://github.com/frida/frida
The pinned Python package is an external dependency, not bundled in this ZIP.
Installed Frida package metadata identifies the wxWindows Library Licence 3.1;
refer to that package's license text and third-party notices.

## Microsoft platform dependencies
Windows SDK, DirectX / DXGI, C++/WinRT and Visual C++ toolchain are required to
build/run the adapter. They are not included as SDK source or installers.
The package's build instructions identify the required tools; system DLLs and
Visual C++ runtime installers must come from their respective vendor.

## Not distributed
No Ubisoft game executable/assets, KRVR server/client software, NVIDIA DLSS
binaries, private vrframework submodule, account data or device credentials are
included. The source signature checks and adapter implementation are our mod
code, not a redistributed game installation.
