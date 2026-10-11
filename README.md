# AnvilEngine2VR - Assassin's Creed VR Mod

A VR mod for Assassin's Creed games using the AnvilNext 2.0 engine that enables immersive virtual reality gameplay using OpenXR.

## Supported Games

- Assassin's Creed Odyssey
- Assassin's Creed Valhalla
- Assassin's Creed Mirage

### Planned / In Development

- Assassin's Creed Origins (see [development guide](games/Origins/README.md))

## Features

- **Runtime:** OpenXR
- **Store:** Any (supports all game store versions)
- Full 6DOF head tracking
- Head aim
- HUD scale adjustment

## Requirements

- One of the supported Assassin's Creed games
- A VR headset compatible with OpenXR
- SteamVR or other OpenXR-compatible runtime

## Installation

1. Download the latest release for your game from the [Releases](https://github.com/mutars/anvilengine2vr/releases) page
2. Extract the contents to your game folder (where the game executable is located)
3. The mod uses DXGI hooking by default - the `dxgi.dll` file should be placed in the game root directory

## In-Game Settings

For the best VR experience, configure the following in-game settings:

- **Windowed Mode:** On
- **Vsync:** Off
- **Frame Cap:** Off
- **HDR:** Off
- **Depth of Field:** Off

## Known Limitations

- Dialogs are rendered in stereo but displayed in letterbox format
- Water and some visual effects are not visible when looking in the opposite direction to the character

## Building from Source

### Prerequisites

- CMake 3.27 or higher
- Visual Studio with C++23 support
- Git

### Build Steps

1. Clone the repository with submodules:
   ```bash
   git clone --recursive https://github.com/mutars/anvilengine2vr.git
   cd anvilengine2vr
   ```

2. Configure and build:
   ```bash
   cmake --preset default
   cmake --build build --config Release
   ```

3. The built DLLs will be placed in the `artifact` folder, organized by game

## Credits

This project is based on [REFramework](https://github.com/praydog/REFramework) by praydog.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
# Experimental Shadows VR 0.1 preview

The local-release path for Assassin's Creed Shadows is documented in
[the preview README](tools/shadows-release/README.md). It provides game-world
stereo and head tracking with a standalone Windows build, portable launchers,
main-view model LOD control and the currently tested temporary DLSS history-reset
workaround. This is an experimental derivative preview, not a completed per-eye
temporal reconstruction implementation or an official upstream release.

Build native targets with `cmake -S tools/shadows-release -B build/shadows-release -A x64`,
then follow [BUILD.md](tools/shadows-release/BUILD.md) and run
`python tools/shadows-release/package_release.py --version 0.1` to create local
runtime/source ZIPs. No private `vrframework` submodule is needed for this path.
The package builder does not upload files or create GitHub releases.
