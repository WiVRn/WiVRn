# Building

# Server (PC)

## Dependencies

WiVRn requires avahi-client, eigen3, libpulse, libsystemd, nlohmann_json.

It also requires at least one encoder:

 * For vaapi (AMD/Intel), it requires ffmpeg with vaapi and libdrm support, as well as vaapi drivers for the GPU
 * For nvenc (Nvidia), it requires cuda and nvidia driver
 * For x264 (software encoding), it requires libx264

Some distributions such as Fedora don't ship h264 and h265 encoders and need specific repositories.

## Compile

From your checkout directory, with automatic detection of encoders
```bash
cmake -B build-server . -GNinja -DWIVRN_BUILD_CLIENT=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-server
```

It is possible to force specific encoders, by adding options
```
-DWIVRN_USE_VAAPI=ON
-DWIVRN_USE_X264=ON
-DWIVRN_USE_NVENC=ON
```

Additionally, if your environment requires absolute paths inside the OpenXR runtime manifest, you can add `-DWIVRN_OPENXR_INSTALL_ABSOLUTE_RUNTIME_PATH=ON` to the build configuration.

# Client (headset)

#### Build dependencies
As Arch package names: git git-lfs pkgconf glslang cmake jre17-openjdk librsvg

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
