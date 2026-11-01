# re2_frame_controllers

Steam Frame controller support for **Resident Evil 2 (2019, RT build)** in VR through
[REFramework](https://github.com/praydog/REFramework) VR. Works on **OpenXR** and **OpenVR (SteamVR)**.

**Download:** see [Releases](../../releases). The user guide (install, controls, options,
troubleshooting) is in [pkg/README.txt](pkg/README.txt).

## Features

- Xbox-like layout on the Frame controllers (A/B/X/Y, LB/RB, D-pad, View, Menu)
- Frame B/Y (right controller) fed into the left-hand actions RE2 expects
- Working sticks on OpenXR (Touch profile reported to REFramework)
- D-pad in all menus (title, pause, inventory, files, new documents)
- OpenVR: pointer for REFramework's in-headset menu (SteamVR shows no laser with Frame)
- Rebind any action for both runtimes in one INI

## Install

Copy the `reframework` folder from the release zip into the game folder (next to `re2.exe`).

## Build

MinGW-w64 on Linux:

    ./build.sh 0    # release, no log
    ./build.sh 1    # debug, writes reframework/plugins/re2_frame_controllers_log.txt

Output: `build/re2_frame_controllers.dll`. Package it with `pkg/` (INI, layer manifest, README).

`test/` holds the Wine mock tests (fake OpenXR runtime, fake openvr_api.dll, hosts that emulate
REFramework).

## How it works

One DLL acts as:

- a REFramework plugin
- an OpenXR API layer (enabled for the game process only)
- a hook on SteamVR's IVRInput/IVROverlay

For details, see `pkg/README.txt` and the comments in `src/re2_frame_controllers.cpp`.

## License

MIT (see `LICENSE`). Third-party code in `third_party/` keeps its own license:

| Component | License |
|---|---|
| MinHook | BSD-2-Clause |
| OpenXR SDK headers | Apache-2.0 |
| OpenVR header | BSD-3-Clause |
| REFramework API.h | MIT |
| nlohmann/json | MIT |

## Credits

- praydog for REFramework
- Khronos Group for OpenXR
- Valve for OpenVR and the Steam Frame docs
- Tsuda Kageyu for MinHook
- Niels Lohmann for nlohmann/json
