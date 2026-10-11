# Assassin's Creed Shadows VR standalone build

This directory builds the current standalone Shadows VR prototype without the
private/upstream `extern/vrframework` submodule. It produces these release
artifacts from the repository's current sources:

- `ShadowsVisionPro.exe` — OpenXR consumer with native stereo-pair handling,
  reconnect recovery, and optional eye-gaze input.
- `shadows_camera_math_bridge.dll` — paired-eye camera math bridge, including
  the cached-right-eye resume path used by the current prototype.
- `shadows_native_color_bridge.dll` — game-side native color publisher.
- `ShadowsColorReceiver.dll` — consumer-side build of the color bridge ABI.

## Requirements

- Windows 10/11 x64.
- Visual Studio 2022 C++ workload and Windows SDK.
- CMake 3.27 or newer.
- OpenXR-SDK. Resolution order is:
  1. `ANVIL_OPENXR_SOURCE_DIR` (by default the existing
     `build/full/_deps/openxr-src` checkout);
  2. when that cache variable is empty, an installed `OpenXRConfig.cmake`
     package;
  3. otherwise, the pinned public OpenXR-SDK revision
     `458984d7f59d1ae6dc1b597d94b02e4f7132eaba`.

The project supplies the CMake 3.5 policy floor required to configure this
pinned OpenXR-SDK revision with CMake 4.x.

The pinned OpenXR loader is static in this Windows build, so no
`openxr_loader.dll` is required beside the executable. If an explicitly
selected installed package exposes a shared loader, its runtime DLL is
installed beside `ShadowsVisionPro.exe` automatically.
That shared-loader developer build is not accepted by the **0.1 portable
profile**. For this release, use the pinned public SDK/source build and static
loader; the packager rejects shared linkage and stray shared-loader DLLs.

## Configure and build

Run from the repository root:

```powershell
cmake -S tools/shadows-release -B build/shadows-release -A x64
cmake --build build/shadows-release --config Release --parallel
```

This build directory is intentionally separate from the binaries used by a
running game or headset session. Building it does not attach to, stop, restart,
or overwrite that session.

On a fresh source extraction, the build fetches the pinned public OpenXR SDK if
no installed SDK or existing cache is available. To reuse a cache explicitly,
add `-DANVIL_OPENXR_SOURCE_DIR="PATH_TO_EXISTING_OPENXR_SDK"`; the directory must
already exist. No private VR framework is required.

## Create the local release directory

```powershell
cmake --install build/shadows-release --config Release `
  --prefix build/shadows-release/package
```

The package directory contains the four runtime binaries and a `licenses`
directory. Preserve both the AnvilEngine2VR and OpenXR-SDK license notices in
published archives. Microsoft system DLLs are not redistributed. The target
machine needs the current Microsoft Visual C++ x64 Redistributable.

## Source provenance

| Artifact | Repository sources |
| --- | --- |
| `ShadowsVisionPro.exe` | `tools/shadows_vr_screen.cpp`, `tools/shadows_eye_gaze.hpp`, `common/include/anvilvr/*` |
| `shadows_camera_math_bridge.dll` | `tools/shadows_camera_math_bridge.cpp`, `games/Shadows/sdk/EyeCamera.cpp`, `common/src/CameraReprojection.cpp` |
| Color bridge DLLs | `tools/shadows_native_color_bridge.cpp`, `common/include/anvilvr/ShadowsNativeColorBridge.hpp` |
| OpenXR loader | Khronos OpenXR-SDK at the pinned revision above, or the explicitly selected installed package |

## Runtime scope

These targets package the current native stereo transport and camera bridge.
The launcher/injection scripts and game-version-specific runtime discovery are
kept as source-side tooling and are not silently installed by this CMake
project. The current diagnostic DLSS history-reset mode is likewise external
to these four native binaries; it should be documented separately before a
public release is tagged.
