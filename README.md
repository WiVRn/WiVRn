# WiVRn
OpenXR streaming application

WiVRn lets you run OpenXR applications on a computer and display them on a standalone headset.

# Installation
## Server (PC)
From your checkout directory
```bash
cmake -B build-server . -GNinja -DWIVRN_BUILD_CLIENT=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-server

# Set WiVRn as the active OpenXR runtime, delete ~/.config/openxr/1/active_runtime.json after you are done using WiVRn
mkdir -p ~/.config/openxr/1/
ln --relative --symbolic --force build-server/openxr_wivrn-dev.json ~/.config/openxr/1/active_runtime.json
```

## Client (headset)
At this early stage of development, we do not have stable releases. Only Oculus Quest is supported.
Download apk from [Releases](https://github.com/Meumeu/WiVRn/releases).
Install with adb (headset connected to PC), developer mode must be activated on Quest.
```bash
adb install WiVRn-oculus.apk
```

# Usage
On the computer, run `wivrn-server`, from checkout directory
```bash
build-server/server/wivrn-server
```
Then, on headset, launch WiVRn from the App Library, in "unknown sources" section.

You will briefly have a "Waiting for connection" screen, followed by "Waiting for video stream".
Now on your computer you can run an OpenXR application, and it will show on your headset, enjoy!

# TODO
* Sound support (playback and recording)
* SteamVR support with [OpenComposite](https://gitlab.com/znixian/OpenOVR)
* Latency improvement
* Application launcher
* Support more headsets

# Credits
WiVRn uses the following software:
- [Monado](https://monado.freedesktop.org/)
- [glm](http://glm.g-truc.net/)
- [Boost.PFR](https://github.com/boostorg/pfr)
- [spdlog](https://github.com/gabime/spdlog)
- [FreeType](https://freetype.org/)
- [HarfBuzz](https://harfbuzz.github.io/)
- [x264](https://www.videolan.org/developers/x264.html), [ffmpeg](https://ffmpeg.org/) and/or [nvenc](https://developer.nvidia.com/nvidia-video-codec-sdk) depending on the compilation options
- [Avahi](https://www.avahi.org/)
- [sd-bus](https://www.freedesktop.org/software/systemd/man/sd-bus.html)

![GitLicense](https://gitlicense.com/badge/Meumeu/WiVRn) ![CI](https://github.com/Meumeu/WiVRn/workflows/Build/badge.svg)
