# WiVRn Pico Native Client

A WiVRn client for Pico Neo 2 and similar Pico headsets that do not support OpenXR.

Uses the PvrSDK-Native (GLES-based) instead of OpenXR for VR rendering and tracking.

## Building

```bash
# From the WiVRn root directory, build the Pico Native client APK:
gradle -b client/pico_native/build.gradle assembleRelease

# Or for debug:
gradle -b client/pico_native/build.gradle assembleDebug
```

The APK will be in `client/pico_native/build/outputs/apk/`.

## CMake Options

- `WIVRN_BUILD_PICO_NATIVE_CLIENT=ON` - Enables building the `wivrn-neo2` module
- The standard WiVRn client (`WIVRN_BUILD_CLIENT`) is not required

## Architecture

- `main.cpp` - PvrSDK initialization, GLES setup, render loop, tracking, controller input
- `wivrn_client_pico.h/cpp` - Standalone networking (no OpenXR application.h dependency)
- `AndroidManifest.xml` - Pico-specific manifest (no OpenXR permissions)
- `build.gradle` - Separate Gradle build for the Neo 2 APK

## SDK Location

PvrSDK-Native is in `external/pvrsdk-native/` (extracted from the AAR).
The Pico Native XR SDK headers are in `external/pico-native-sdk/include/`.

## TODO

- Haptic feedback forwarding
- Controller 6DOF position tracking verification
