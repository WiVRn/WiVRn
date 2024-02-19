# WiVRn

[![License: GPL v3](images/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0) ![CI](https://github.com/Meumeu/WiVRn/workflows/Build/badge.svg)

OpenXR streaming application

<img src="images/wivrn.svg" width="180">

WiVRn lets you run OpenXR applications on a computer and display them on a standalone headset.

# Installation
## Server (PC)

See [building](docs/building.md).

## Client (headset)
### Prebuilt apk
Download apk from [Releases](https://github.com/Meumeu/WiVRn/releases).
Install with adb (headset connected to PC), developer mode must be enabled.
```bash
adb install WiVRn.apk
```

### Compilation

See [building](docs/building.md#client-headset).

# Usage

## Set WiVRn as default OpenXR runtime

In order to set WiVRn as the default OpenXR runtime, you can run the collowing commands:
```bash
mkdir -p ~/.config/openxr/1/
ln --relative --symbolic --force build-server/openxr_wivrn-dev.json ~/.config/openxr/1/active_runtime.json
```

Alternatively, setting the environment `XR_RUNTIME_JSON="${PWD}/build-server/openxr_wivrn-dev.json"` will set it for the current shell only.

## Running

### Prerequisites
Avahi must be running:
```bash
systemctl enable --now avahi-daemon
```

If a firewall is installed, open port 5353/UDP for avahi.
Open ports 9757/UDP+TCP for WiVRn itself.

### Running
On the computer, run `wivrn-server`, from checkout directory
```bash
build-server/server/wivrn-server
```
Then, on headset, launch WiVRn from the App Library, in "unknown sources" section.

You should now see your server in the list, click connect, screen will show "waiting for video stream".
Now on your computer you can run an OpenXR application, and it will show on your headset, enjoy!

# Configuration
Configuration is done on server side, in `$XDG_CONFIG_HOME/wivrn/config.json` or if `$XDG_CONFIG_HOME` is not set, `$HOME/.config/wivrn/config.json`.

All elements are optional and have default values.

See [configuration](docs/configuration.md) for configurable items.

# Credits
WiVRn uses the following software:
- [ambientCG](https://ambientcg.com/)
- [Avahi](https://www.avahi.org/)
- [Boost.PFR](https://github.com/boostorg/pfr)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [fastgltf](https://github.com/spnda/fastgltf)
- [ffmpeg](https://ffmpeg.org/) optional, for hardware encoding on AMD/Intel
- [FreeType](https://freetype.org/)
- [glm](http://glm.g-truc.net/)
- [HarfBuzz](https://harfbuzz.github.io/)
- [Monado](https://monado.freedesktop.org/)
- [nvenc](https://developer.nvidia.com/nvidia-video-codec-sdk) optional, for hardware encoding on Nvidia
- [Roboto](https://fonts.google.com/specimen/Roboto)
- [sd-bus](https://www.freedesktop.org/software/systemd/man/sd-bus.html)
- [spdlog](https://github.com/gabime/spdlog)
- [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- [WebXR input profiles](https://www.npmjs.com/package/@webxr-input-profiles/motion-controllers)
- [x264](https://www.videolan.org/developers/x264.html) optional, for software encoding
