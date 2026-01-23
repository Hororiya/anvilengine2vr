# Assassin's Creed Odyssey VR Mod

A VR mod for Assassin's Creed Odyssey that enables immersive virtual reality gameplay using OpenXR.

## Features

- **Runtime:** OpenXR
- **Store:** Any (supports all game store versions)
- Full 6DOF head tracking
- Head aim
- HUD scale adjustment

## Requirements

- Assassin's Creed Odyssey
- A VR headset compatible with OpenXR
- SteamVR or other OpenXR-compatible runtime

## Installation

1. Download the latest release from the [Releases](https://github.com/mutars/acodyssey-vr/releases) page
2. Extract the contents to your Assassin's Creed Odyssey game folder (where `ACOdyssey.exe` is located)
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
   git clone --recursive https://github.com/mutars/acodyssey-vr.git
   cd acodyssey-vr
   ```

2. Configure and build:
   ```bash
   cmake --preset default
   cmake --build build --config Release
   ```

3. The built DLL will be placed in the `artifact` folder

## Credits

This project is based on [REFramework](https://github.com/praydog/REFramework) by praydog.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
