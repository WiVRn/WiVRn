# WiVRn
OpenXR streaming application

WiVRn lets you run OpenXR applications on a computer and display them on a standalone headset.

# Installation
## Server (PC)
From your checkout directory
```bash
cmake -B build-server . -GNinja -DWIVRN_BUILD_CLIENT=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build -B build-server

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
