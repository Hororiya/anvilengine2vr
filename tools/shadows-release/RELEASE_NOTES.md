# 0.1 — local release candidate, 2026-10-12

## Tested profile
KRVR / Apple Vision Pro / RTX 5090, game-world stereo and head-driven camera.
Source target 3840×2160; main model LOD bias -0.5; camera forward 2.5 game units;
frame generation OFF; temporary DLSS history reset ON.

## User-confirmed development results
- Head rotation changes the game camera, with side/back geometry visible.
- Native stereo is visible in the headset.
- Native-only LOD control avoids the severe regression of the old JS callback.
- Per-frame DLSS history reset removes the observed static binocular disparity
  jump / double image with frame generation disabled.

## Remaining limitations
- Alternating-eye rendering; this is not simultaneous dual-eye game simulation.
- Proper per-eye temporal accumulation and N-2 camera/MV correction are pending.
- History reset can soften picture quality.
- Source 4K does not prove internal native 4K. Some graphics/window operations
  can reduce source size; the launcher requests 4K on start.
- Verified on one local setup; broader game versions/headsets remain untested.
- Game-side VRS, formal mip bias, motion controllers and full restart-free
  runtime/device-loss recovery are not shipped features.

## Distribution
The packager never pushes or uploads. Source is submitted separately to the
user-selected repository; no GitHub Release or archive upload is performed by
this workflow. Runtime and source archives use explicit allowlists.
Build reports and archive CRC verification replace unnecessary whole-game hashing.
