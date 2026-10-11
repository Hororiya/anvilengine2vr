# Assassin's Creed Shadows VR — 0.1

An experimental native game-world stereo adapter for **AnvilEngine2VR**.
This preview packages the currently tested **KRVR + Apple Vision Pro** setup.
It is not an official Ubisoft, Apple, KRVR or upstream AnvilEngine2VR release.

## What is included

- Actual alternating left/right game-camera rendering, not a virtual screen.
- Head tracking applied before the game's visibility/culling update.
- Matched stereo-pair submission and shared-texture generation recovery.
- A 3840×2160 source-window request; source size is not proof of native internal
  DLSS rendering resolution.
- Main-view-only model LOD bias, default **-0.5**; native C callbacks avoid the
  previous per-call JavaScript/range-query overhead.
- Camera forward adjustment, default **2.5 game units**; this does not change IPD.
- **Temporary per-frame DLSS history reset**, enabled in the tested profile.

## Important preview limitation

The history-reset workaround removed static binocular double images in the
tested session, but it discards temporal accumulation and can soften the image.
**Independent per-eye DLSS histories and N-2 motion-vector correction are not
finished.** Do not describe this preview as the completed temporal reconstruction
solution. Disabling the workaround can reintroduce double images.

Eye-gaze input has been observed on the tested headset, but this preview does
**not** enable game-side gaze VRS or promise an FPS gain. Texture mip bias is not
implemented by this package. Do not confuse model LOD with mip or sharpening.

## Requirements

- Windows x64, Direct3D 12, a game installation matching the adapter's checked
  function signatures. Other patches are not guaranteed compatible.
- A working **KRVR OpenXR runtime** and Vision Pro client, already connected.
  No KRVR software, account information or device credentials are included.
- Python 3.13 was tested; install the pinned Frida dependency from requirements.txt.
- Microsoft Visual C++ x64 runtime for the included native binaries.
- The default history-reset profile requires the game's NVIDIA DLSS/Streamline
  path. Other GPU/upscaler combinations have not been validated by this release.
- Start the game yourself and enter a playable scene. Keep frame generation OFF.
  Save progress before initial use of any experimental mod.

The package does not install itself into the game directory, edit the game EXE,
change game graphics settings or change KRVR registry settings.

## Quick start (PowerShell)

Extract the runtime ZIP to a new folder. From that folder:

```powershell
py -3.13 -m venv .venv
& .\.venv\Scripts\python.exe -m pip install -r .\requirements.txt
& .\Start-ShadowsVR.ps1 -ValidateOnly
& .\Start-ShadowsVR.ps1
```

See `Get-Help .\Start-ShadowsVR.ps1` / script parameters for an explicit Python
executable or game PID. If another development/preview camera controller is
already attached, stop that session first; do not inject a second copy.

The launcher briefly creates an XR pose bootstrap, starts the camera publisher,
gracefully ends the bootstrap, then starts native stereo. It also starts the
native LOD controller and the configured history-reset workaround.

### Hot adjustment

```powershell
& .\Set-ShadowsVR.ps1 -LodBias -0.25 -CameraForward 2.5
```

Supported LOD bias: **[-0.5, 0]**. Supported forward offset: **[-2, 3]**.
Changes are reversible and apply without restarting the game. LOD 0 is neutral;
stop the package to remove its hooks entirely. Check `Set-ShadowsVR.ps1` for the
available options; this preview deliberately does not expose unsafe experiments.

### Stop / rollback

```powershell
& .\Stop-ShadowsVR.ps1
```

This stops only controllers recorded in this package's session and waits for
graceful hook cleanup. It does not close the game, kill KRVR or delete saves.
The enlarged game window can remain 3840×2160 after stopping; change its size in
the game normally if desired. Existing settings/saves are not overwritten.

## Troubleshooting

- **Wrong game version / signature mismatch:** stop. A mismatch is a compatibility
  check, not a reason to bypass the guard or scan/patch arbitrary offsets.
- **Black image after headset reconnect:** check `logs/` and the package session.
  Stop/restart the package gracefully. Device-wide/runtime loss is not guaranteed
  to recover automatically.
- **Static double image:** ensure FG is OFF and the tested history-reset profile
  remains enabled. Do not treat matching frame metadata as pixel-content proof.
- **Soft picture:** distinguish source resolution, internal upscaler resolution,
  transport resolution and the history-reset limitation. More LOD is not a
  universal fix for blur; excessive sharpening can create halos/shimmer.
- **Slow picture:** reduce model LOD bias toward 0. Source-camera cadence and XR
  submission cadence are different; submitted-frame counts are not GPU FPS.

## Release validation scope

The prior development session was headset-tested. The local candidate workflow checks
fresh native builds, script syntax, dependency/preflight behavior, executable
control self-test and ZIP contents. It does not restart the running game to
re-run a headset acceptance test of the extracted package. See RELEASE_NOTES.md.
The packager checks the installed native build manifest, x64 PE headers, declared
file sizes and ZIP CRC/member sets. `verify_release.py` separately checks an
extracted package's syntax, preflight, native control self-test and non-game
fixture tests. These checks are not proof of arbitrary binary authenticity.

## Source / license

The source ZIP contains the minimal standalone build path and runtime sources.
It does not require the upstream private `vrframework` submodule. See BUILD.md
and THIRD_PARTY_NOTICES.md. MIT license and upstream attribution are preserved.

No game binaries/assets, machine logs, PID/session files, private keys,
headset/network addresses or Frida/Python installations are distributed.
