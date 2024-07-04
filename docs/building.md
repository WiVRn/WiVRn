# Building

# Server (PC)

## Dependencies

WiVRn requires avahi-client, eigen3, gettext, libpulse, libsystemd, nlohmann_json.

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

Force specific audio backends
```
-DWIVRN_USE_PIPEWIRE=ON
-DWIVRN_USE_PULSEAUDIO=ON
```

Systemd service and pretty hostname support
```
-DWIVRN_USE_SYSTEMD=ON
```

Additionally, if your environment requires absolute paths inside the OpenXR runtime manifest, you can add `-DWIVRN_OPENXR_INSTALL_ABSOLUTE_RUNTIME_PATH=ON` to the build configuration.

# Client (headset)

#### Build dependencies
As Arch package names: git pkgconf glslang cmake jre17-openjdk librsvg cli11

#### Android environment
Download [sdkmanager](https://developer.android.com/tools/sdkmanager) commandline tool and extract it to any directory.
Create your `ANDROID_HOME` directory, for instance `~/Android`.

Review and accept the licenses with
```bash
sdkmanager --sdk_root="${HOME}/Android" --licenses
```

#### Apk signing
Your device may refuse to install an unsigned apk, so you must create signing keys before building the client
```
# Create key, then enter the password and other information that the tool asks for
keytool -genkey -v -keystore ks.keystore -alias default_key -keyalg RSA -keysize 2048 -validity 10000

# Substitute your password that you entered in the command above instead of YOUR_PASSWORD
echo signingKeyPassword="YOUR_PASSWORD" > gradle.properties
```
Once you have generated the keys, the apk will be automatically signed at build time

#### Client build
From the main directory.
```bash
export ANDROID_HOME=~/Android
export JAVA_HOME=/usr/lib/jvm/openjdk-bin-17/

./gradlew assembleStandardRelease
# Or for Pico assemblePico
```

Outputs will be in `build/outputs/apk/standard/release/WiVRn-standard-release.apk`

#### Install apk with adb
Before using adb you must enable usb debugging on your device:
 * Pico - https://developer.picoxr.com/document/unity-openxr/set-up-the-development-environment/ (see first step)
 * Quest - https://developer.oculus.com/documentation/unity/unity-env-device-setup/#headset-setup (see "Set Up Meta Headset" and "Test an App on Headset" until step 4)

Also add your device in udev rules: https://wiki.archlinux.org/title/Android_Debug_Bridge#Adding_udev_rules

Then connect the device via usb to your computer and execute the following commands
```
# Start adb server
adb start-server

# Check if the device is connected
adb devices

# Install standard apk
adb install build/outputs/apk/standard/release/WiVRn-standard-release.apk

# Or install pico apk
adb install build/outputs/apk/pico/release/WiVRn-pico-release.apk

# When you're done, you can stop the adb server
abd kill-server
```
