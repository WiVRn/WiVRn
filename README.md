# WiVRn

[![License: GPL v3](images/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0) ![CI](https://github.com/Meumeu/WiVRn/workflows/Build/badge.svg) ![Format](https://github.com/Meumeu/WiVRn/workflows/Format/badge.svg)

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

### Notes for OpenVR and Steam
WiVRn only provides an OpenXR runtime. For games that use OpenVR, [OpenComposite](https://gitlab.com/znixian/OpenOVR/) should be used to translate the APIs.

If using Wine/Proton, it will probe for OpenVR at startup, so even for OpenXR applications, OpenComposite is required.

If using Steam, games will be sandboxed by pressure vessel, which is not OpenXR aware yet.
- Files under `/usr` are mapped into `/run/host/usr`, if OpenComposite or WiVRn are installed there, make sure that `XR_RUNTIME_JSON` uses `/run/host/usr` prefix and that `VR_OVERRIDE` or `~/.config/openvr/openvrpaths.vrpath` also uses this configuration.
- WiVRn uses a socket to communicate between the application and the server process, this socket needs to be whitelisted as `PRESSURE_VESSEL_FILESYSTEMS_RW=$XDG_RUNTIME_DIR/wivrn_comp_ipc`.

### Audio
When headset is connected, wivrn-server will create a virtual output device named WiVRn. It is not selected as default and you should either assign the application to the device when it is running, or mark it as default. To do so you can use `pavucontrol` or your desktop environment's configuration panel. Please note that in `pavucontrol` it will appear as a virtual device.

For microphone, you first have to enable it on the settings tabs on the headset (and give permission when prompted). It will then appear as a virtual input device named WiVRn(microphone) and also needs to be assigned like for output device.

# Configuration
Configuration is done on server side, in `$XDG_CONFIG_HOME/wivrn/config.json` or if `$XDG_CONFIG_HOME` is not set, `$HOME/.config/wivrn/config.json`.

All elements are optional and have default values.

See [configuration](docs/configuration.md) for configurable items.

# Credits
WiVRn uses the following software:
- [ambientCG](https://ambientcg.com/)
- [Avahi](https://www.avahi.org/)
- [Boost.Locale](https://github.com/boostorg/locale)
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
