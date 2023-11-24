# WiVRn

[![License: GPL v3](images/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0) ![CI](https://github.com/Meumeu/WiVRn/workflows/Build/badge.svg)

OpenXR streaming application

WiVRn lets you run OpenXR applications on a computer and display them on a standalone headset.

# Installation
## Server (PC)

### Dependencies

WiVRn requires ffmpeg (for AMD/Intel), cuda (Nvidia), x264 (software) for video encoding.
Other dependencies include avahi-client, eigen3, libpulse, nlohmann_json.

Note that some distributions don't ship h264/h265 support in their mesa builds.
On Fedora this can be solved by replacing mesa:
```bash
dnf swap mesa-va-drivers mesa-va-drivers-freeworld
```

### Compilation
From your checkout directory
```bash
cmake -B build-server . -GNinja -DWIVRN_BUILD_CLIENT=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-server

# Set WiVRn as the active OpenXR runtime, delete ~/.config/openxr/1/active_runtime.json after you are done using WiVRn
mkdir -p ~/.config/openxr/1/
ln --relative --symbolic --force build-server/openxr_wivrn-dev.json ~/.config/openxr/1/active_runtime.json
```

## Client (headset)
### Prebuilt apk
Download apk from [Releases](https://github.com/Meumeu/WiVRn/releases).
Install with adb (headset connected to PC), developer mode must be enabled.
```bash
adb install WiVRn.apk
```

### Compilation
#### Build dependencies
As Arch package names: git git-lfs pkgconf glslang cmake jre17-openjdk

#### Android environment
Download [sdkmanager](https://developer.android.com/tools/sdkmanager) commandline tool and extract it to any directory.
Create your `ANDROID_HOME` directory, for instance `~/Android`.

Review and accept the licenses with
```bash
sdkmanager --sdk_root="${HOME}/Android" --licenses
```

#### Client build
For Pico only: setup git lfs

From the main directory.
```bash
export ANDROID_HOME=~/Android
export JAVA_HOME=/usr/lib/jvm/openjdk-bin-17/

./gradlew assembleStandardRelease
# Or for Pico assemblePico
```

Outputs will be in `build/outputs/apk/standard/release/WiVRn-standard-release-unsigned.apk`

# Usage
On the computer, run `wivrn-server`, from checkout directory
```bash
build-server/server/wivrn-server
```
Then, on headset, launch WiVRn from the App Library, in "unknown sources" section.

You will briefly have a "Waiting for connection" screen, followed by "Waiting for video stream".
Now on your computer you can run an OpenXR application, and it will show on your headset, enjoy!

# Configuration
Configuration is done on server side, in `$XDG_CONFIG_HOME/wivrn/config.json` or if `$XDG_CONFIG_HOME` is not set, `$HOME/.config/wivrn/config.json`.

All elements are optional and have default values.

## Configurable items:
### `scale`
Default value: `1`

Controls the size of the video stream, either a number between 0 and 1 or a pair of numbers between 0 and 1. If two numbers are provided the first one is horizontal scale and the second vertical.
Scaling is applied in a foveated fashion: the center has a 1:1 ratio and the rest is scaled so that the total number of pixels matches the desired scale.

#### Examples:
```json
{
	"scale": 0.5
}
```
The x and y resolution of the streamed video are half the size on the headsed, reduces the required bandwidth and encoding/decoding time by about 4.

```json
{
	"scale": [0.75, 0.5]
}
```
Scales x by a 0.75 factor, and y by a 0.5 factor.

### `encoders`
A list of encoders to use.

WiVRn has the ability to split the video in blocks that are processed independently, this may use resources more effectively and reduce latency.
All the provided encoders are put into groups, groups are executed concurrently and items within a group are processed sequentially.

#### `encoder`
Default value: `nvenc` if Nvidia GPU and compiled with cuda, `vaapi` for all other GPU when compiled with ffmpeg, else `x264`.

Identifier of the encoder, one of `x264` (software encoding), `nvenc` (Nvidia hardware encoding), `vaapi` (AMD/Intel hardware encoding)

#### `codec`
Default value: `h265`

One of `h264` or `h265`. If using `x264` encoder, value is ignored and `h264` is used.

#### `bitrate`
Default value: `50000000` (50Mb/s)

Bitrate of the video, in bit/s.

#### `width`, `height`, `offset_x`, `offset_y` (advanced)
Default values: full image (`width` = 1, `height` = 1, `offset_x` = 0, `offset_y` = 0)

Specifies the portion of the video to encode: all values are in 0, 1 range. Left eye image ranges from x 0 to x 0.5 and y 0 to 1, Rigth eye is x from 0.5 to 1 and y 0 to 1.

#### `group` (very advanced)
Default value: First unused group identifier, if omitted on all encoders, they are executed concurrently.

Identifier (number) of the encoder group. Encoders with the same identifier are executed sequentially, in the order they are defined in the configuration. Encoders with different identifiers are executed concurrently.

#### Examples
1. Simple encoder
```json
{
	"encoders": [
		{
			"encoder": "vaapi",
			"bitrate": 50000000,
			"codec": "h265"
		}
	]
```
Creates a single encoder, using vaapi hardware encoding, h265 video codec (HEVC) and 50Mb/s bitrate.

2. Hardware + software encoder
```json
{
	"encoders": [
		{
			"encoder": "vaapi",
			"bitrate": 25000000,
			"codec": "h265",
			"width": 0.5,
			"height": 1,
			"offset_x": 0,
			"offset_y": 0
		},
		{
			"encoder": "x264",
			"bitrate": 25000000,
			"codec": "h264",
			"width": 0.5,
			"height": 1,
			"offset_x": 0.5,
			"offset_y": 0
		}
	]
}
```
Creates a hardware encoder for left eye, and a software encoder for right eye.

3. 2 Hardware encoders
```json
{
	"encoders": [
		{
			"encoder": "vaapi",
			"bitrate": 25000000,
			"codec": "h265",
			"width": 0.5,
			"height": 1,
			"offset_x": 0,
			"offset_y": 0,
			"group": 0,
		},
		{
			"encoder": "vaapi",
			"bitrate": 25000000,
			"codec": "h264",
			"width": 0.5,
			"height": 1,
			"offset_x": 0.5,
			"offset_y": 0,
			"group": 0,
		}
	]
}
```
Creates two hardware encoders, one for left eye and one for right eye, executed sequentially as they have the same `group`.
This allows the left eye image to be encoded faster than the full image would be, so network transfer starts earlier, and decoding starts earlier. While the total encoding, transfer and decoding time remain the same or are longer, this can reduce the latency.

#### `options` (very advanced)
Default value: unset

Json object of additional options to pass directly to ffmpeg `avcodec_open2`'s `option` parameter.

### `application`
Default value: unset

An application to start when connection with the headset is established, can be a string or an array of strings if parameters need to be provided.

#### Example
```json
{
	"application": ["steam", "steam://launch/275850/VR"]
}
```
Launch No Man's Sky in VR mode on Steam when connection with headset is established.

# TODO
* More testing with [OpenComposite](https://gitlab.com/znixian/OpenOVR) for OpenVR support
* Latency improvement
* Application launcher
* Support more headsets

# Credits
WiVRn uses the following software:
- [Avahi](https://www.avahi.org/)
- [Boost.PFR](https://github.com/boostorg/pfr)
- [ffmpeg](https://ffmpeg.org/) optional, for hardware encoding on AMD/Intel
- [FreeType](https://freetype.org/)
- [glm](http://glm.g-truc.net/)
- [HarfBuzz](https://harfbuzz.github.io/)
- [Monado](https://monado.freedesktop.org/)
- [nvenc](https://developer.nvidia.com/nvidia-video-codec-sdk) optional, for hardware encoding on Nvidia
- [sd-bus](https://www.freedesktop.org/software/systemd/man/sd-bus.html)
- [spdlog](https://github.com/gabime/spdlog)
- [x264](https://www.videolan.org/developers/x264.html) optional, for software encoding
