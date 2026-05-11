# Debugging WiVRn

This guide covers debugging techniques for WiVRn, including log collection, crash analysis, performance profiling, and common troubleshooting scenarios.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Collecting Logs](#collecting-logs)
  - [HMD Client Logs](#hmd-client-logs)
  - [Server Logs](#server-logs)
- [Starting Components for Debugging](#starting-components-for-debugging)
  - [HMD Client](#hmd-client)
  - [Server](#server)
  - [XR Applications](#xr-applications)
- [Crash Debugging](#crash-debugging)
  - [Server Crashes](#server-crashes)
  - [HMD Client Crashes](#hmd-client-crashes)
- [Performance Debugging](#performance-debugging)
  - [Timing Analysis](#timing-analysis)
  - [Perfetto Profiling](#perfetto-profiling)
- [USB Tunneling](#usb-tunneling)
- [Game Debugging](#game-debugging)
- [Environment Variables Reference](#environment-variables-reference)
- [Configuration Files](#configuration-files)
- [External Resources](#external-resources)
- [Remote Native Debugging (Advanced)](#remote-native-debugging-advanced)

---

## Prerequisites

### ADB Setup

The Android Debug Bridge ([ADB](https://developer.android.com/tools/adb)) is required for debugging the HMD client. Connect your headset to your PC via a USB data cable and authorize the connection when prompted in the HMD.

### Clean Installation

When debugging, ensure the HMD client matches the server version. Perform a clean reinstall:

```bash
# Find and uninstall existing installation
WIVRN_PKG=$(adb shell pm list packages | grep wivrn | cut -d: -f2)
adb uninstall ${WIVRN_PKG}

# Install the debug APK
adb install /path/to/wivrn-standard-debug.apk
```

---

## Collecting Logs

### HMD Client Logs

The primary method to collect logs from the HMD is through [ADB logcat](https://developer.android.com/tools/logcat).

Watch for crashes (FATAL priority):

```bash
adb logcat '*:F'
```

Record ERROR and above:

```bash
adb logcat '*:E' | tee -a errors-$(date "+%Y-%m-%d-%H").log
```

WiVRn-only logs (verbose):

```bash
adb logcat '*:S' WiVRn:V
```

Watch WiVRn client logs:

```bash
adb logcat | grep WiVRn
```

Watch Android system logs related to WiVRn:

```bash
adb logcat | grep wivrn
```

### Server Logs

Start the server with debug logging enabled:

```bash
XRT_LOG=debug XRT_COMPOSITOR_LOG=debug wivrn-server
```

To also dump video frames for analysis:

```bash
XRT_LOG=debug XRT_COMPOSITOR_LOG=debug WIVRN_DUMP_VIDEO=/tmp/video-dump wivrn-server
```

The video dump can be played back with `mpv` or other media players to verify what video was being sent to the HMD.

There will be multiple files in the directory, one for each video stream per codec. For example, `/tmp/video-dump-0.h264`.

---

## Starting Components for Debugging

### HMD Client

The package name varies by build variant:
- `org.meumeu.wivrn` - for app store builds
- `org.meumeu.wivrn.github` - for GitHub release builds
- `org.meumeu.wivrn.local` - for locally built APK
- `org.meumeu.wivrn.github.testing` - for CI runs
- `org.meumeu.wivrn.github.nightly` - for APK repo

Find your installed package with:

```bash
adb shell pm list packages | grep wivrn
```

Start via ADB:

```bash
WIVRN_PKG=$(adb shell pm list packages | grep wivrn | cut -d: -f2)
adb shell am start ${WIVRN_PKG}/org.meumeu.wivrn.MainActivity
```

Connect to a specific server:

Replace `ADDRESS=localhost` with the address of the server you want to connect to.

```bash
WIVRN_PKG=$(adb shell pm list packages | grep wivrn | cut -d: -f2)
ADDRESS=localhost
adb shell am start -a android.intent.action.VIEW -d "wivrn://${ADDRESS}" ${WIVRN_PKG}
```

Stop the client:

```bash
WIVRN_PKG=$(adb shell pm list packages | grep wivrn | cut -d: -f2)
adb shell am force-stop ${WIVRN_PKG}
```

### Server

Start the server with debug logging:

```bash
XRT_LOG=debug XRT_COMPOSITOR_LOG=debug wivrn-server
```

The server will publish itself on the network via mDNS/Zeroconf for discovery by the HMD client.

### XR Applications

Basic OpenXR application:

```bash
xrgears
```

To run Steam games via OpenVR, make sure to add the Steam launch options presented by the server output or in the dashboard. This is required to configure the OpenVR runtime correctly.

Some games have options to control window behavior. Try adding `-windowed` or `-window -w 1280 -h 1024` to the game launch options to allow moving/resizing the local render window. Each game may have different options, check the game's documentation.

> Note: Games linked to OpenVR require [OpenComposite](https://gitlab.com/znixian/OpenOVR) or [xrizer](https://github.com/Supreeeme/xrizer) to translate calls to OpenXR.

---

## Crash Debugging

### Server Crashes

#### Systemd

Core dumps are managed by [systemd-coredump](https://www.freedesktop.org/software/systemd/man/latest/coredumpctl.html).

View the most recent crash:

```bash
coredumpctl info
```

Start a debugger on the most recent crash:

```bash
coredumpctl -1 debug
```

Inside GDB/LLDB:

```
# Select frame 2
f 2

# View local variables
info locals

# Print backtrace
bt
```

#### Non-Systemd Systems

On systems without systemd-coredump, core dumps are handled by the kernel directly.

Check where core dumps are written:

```bash
cat /proc/sys/kernel/core_pattern
```

To set a simple core pattern:

```bash
sudo sysctl -w kernel.core_pattern=/tmp/core.%e.%p.%t
```

This writes cores to `/tmp/` with the format `core.<executable>.<pid>.<timestamp>`.

Analyze a core dump with GDB:

```bash
gdb /usr/bin/wivrn-server /tmp/core.wivrn-server.12345.1706234567
```

Inside GDB:

```
# Print backtrace
bt

# View all threads
info threads

# Switch to thread 2
thread 2

# Print backtrace for all threads
thread apply all bt
```

### HMD Client Crashes

Copy the traceback from the HMD logs using logcat:

```bash
adb logcat '*:F' | grep -A 50 wivrn
```

---

## Performance Debugging

### Timing Analysis

WiVRn can record detailed timing information for analysis.

Capture timing data:

```bash
WIVRN_DUMP_TIMINGS=/tmp/wivrn-timings.csv wivrn-server
```

### Perfetto Profiling

[Perfetto](https://perfetto.dev/) provides detailed system-wide profiling.

#### HMD Profiling

Enable persistent tracing on Android:

```bash
adb shell setprop persist.traced.enable 1
```

Open the [Perfetto UI](https://ui.perfetto.dev/), connect to your device, and configure tracing as needed.

See the [Android tracing quickstart](https://perfetto.dev/docs/quickstart/android-tracing) for detailed instructions.

#### Server Profiling

Ensure WiVRn is built with `XRT_FEATURE_TRACING=ON` to enable [Monado tracing with Perfetto](https://monado.pages.freedesktop.org/monado/tracing-perfetto.html).

Start with tracing enabled:

```bash
XRT_TRACING=true wivrn-server
```

---

## USB Tunneling

For debugging network issues or testing with a wired connection, you can tunnel WiVRn over USB.

Set up the tunnel and connect:

```bash
WIVRN_PKG=$(adb shell pm list packages | grep wivrn | cut -d: -f2)
adb reverse tcp:9757 tcp:9757
adb shell am start -a android.intent.action.VIEW -d "wivrn+tcp://localhost:9757" ${WIVRN_PKG}
```

You are also able to make this connection through the dashboard.

---

## Game Debugging

### Proton/Wine Games

Enable additional logging for Windows compatibility layers:

```
PROTON_LOG=1 WINEDEBUG=vrclient,openxr,steam %command%
```

### OpenComposite

For games using OpenVR, and you are using [OpenComposite](https://gitlab.com/znixian/OpenOVR) to translate calls to OpenXR. 

The OpenComposite log can be found at `$HOME/.local/state/OpenComposite/logs/opencomposite.log`.

### XRizer

To debug [xrizer](https://github.com/Supreeeme/xrizer), you can set the `RUST_LOG` environment variable to control the log level.

| Variable   | Description                                                                 | Default |
|------------|-----------------------------------------------------------------------------|---------|
| `RUST_LOG` | Configures the logging level for Rust applications. Accepts a comma-separated list of directives in the format `module::path=level`, where `level` can be `error`, `warn`, `info`, `debug`, or `trace`. For example, `RUST_LOG=info,my_crate=debug` sets the global level to `info` and the `my_crate` module to `debug`. | `warn`   |

In xrizer, `RUST_LOG` follows the standard [`env_logger` semantics](https://docs.rs/env_logger/latest/env_logger/#enabling-logging) described above. In addition, xrizer defines the following useful nonstandard logging targets:

| Logging Target | Description |
|-----------------|-------------|
| `openvr_calls` | Logs the name of each OpenVR function as they are called |
| `tracked_property` | Logs the name and device index of each requested tracked device property |

The log is located at `$XDG_STATE_HOME/xrizer/xrizer.txt`, or `$HOME/.local/state/xrizer/xrizer.txt` if `$XDG_STATE_HOME` is not set.

---

## Environment Variables Reference

### Compositor Settings

| Variable | Description | Default |
|----------|-------------|---------|
| `XRT_LOG` | Log level (`trace`, `debug`, `info`, `warn`, `error`) | `warn` |
| `XRT_COMPOSITOR_LOG` | Compositor log level (`trace`, `debug`, `info`, `warn`, `error`) | `info` |
| `OXR_VIEWPORT_SCALE_PERCENTAGE` | Per-application viewport scale (set on application, not server) | `100` |

### Debugging

| Variable | Description |
|----------|-------------|
| `WIVRN_DUMP_VIDEO` | Path to dump video frames (e.g., `/tmp/video-dump`) |
| `WIVRN_DUMP_TIMINGS` | Path to dump timing CSV (e.g., `/tmp/wivrn-timings.csv`) |
| `WIVRN_LOGLEVEL` | Log level for the native client |
| `WIVRN_AUTOCONNECT` | Auto-connect to the first discovered server |
| `XRT_TRACING` | Enable Perfetto tracing (`true`/`false`) |

### Vulkan

| Variable | Description |
|----------|-------------|
| `VK_INSTANCE_LAYERS` | Vulkan validation layers (e.g., `VK_LAYER_KHRONOS_validation`) |

### OpenXR

| Variable | Description |
|----------|-------------|
| `XR_RUNTIME_JSON` | Path to OpenXR runtime configuration |

See also the [Monado compositor documentation](https://monado.freedesktop.org/getting-started.html#compositor) for additional options.

---

## Configuration Files

### OpenXR Runtime Configuration

Active runtime symlink:

```bash
ls -la $HOME/.config/openxr/1/active_runtime.json
```

Should point to `/usr/share/openxr/1/openxr_wivrn.json` or similar.

Check runtime configuration:

```bash
cat /usr/share/openxr/1/openxr_wivrn.json
```

Example output:

```json
{
    "file_format_version": "1.0.0",
    "runtime": {
        "name": "Monado",
        "library_path": "../../../lib64/wivrn/libopenxr_wivrn.so",
        "MND_libmonado_path": "../../../lib64/wivrn/libmonado_wivrn.so"
    }
}
```

Verify library dependencies:

```bash
pushd /usr/share/openxr/1/
ldd ../../../lib64/wivrn/libopenxr_wivrn.so
popd
```

### WiVRn Server Configuration

See [docs/configuration.md](configuration.md).

---

## External Resources

### Tools and Documentation

- [ADB Documentation](https://developer.android.com/tools/adb) - Android Debug Bridge
- [ADB Logcat](https://developer.android.com/tools/logcat) - Log filtering and analysis
- [coredumpctl](https://www.freedesktop.org/software/systemd/man/latest/coredumpctl.html) - Core dump management
- [Perfetto](https://perfetto.dev/docs/) - System profiling and tracing
- [Perfetto UI](https://ui.perfetto.dev/) - Web-based trace viewer

### Monado/OpenXR

- [Monado Getting Started](https://monado.freedesktop.org/getting-started.html) - Compositor configuration
- [Monado Tracing with Perfetto](https://monado.pages.freedesktop.org/monado/tracing-perfetto.html) - Server-side tracing
- [OpenXR](https://www.khronos.org/openxr/) - Khronos OpenXR standard
- [xrgears](https://github.com/ila-embsys/monado_demo_xrgears) - OpenXR test application

### Gaming

- [OpenComposite](https://gitlab.com/znixian/OpenOVR) - OpenVR to OpenXR translation
- [xrizer](https://github.com/Supreeeme/xrizer) - OpenVR to OpenXR translation layer (Rust)

### Rust

- [env_logger](https://docs.rs/env_logger/latest/env_logger/#enabling-logging) - Rust logging configuration via RUST_LOG

### Android Debugging

- [Android NDK Debugging](https://developer.android.com/ndk/guides/ndk-gdb) - Native debugging with ndk-gdb
- [Android Tracing Quickstart](https://perfetto.dev/docs/quickstart/android-tracing) - Perfetto on Android

---

## Remote Native Debugging (Advanced)

This section covers attaching a native debugger to the HMD client remotely. This requires the Android NDK and familiarity with LLDB/GDB.

Start the client with debugging enabled:

```bash
WIVRN_PKG=$(adb shell pm list packages | grep wivrn | cut -d: -f2)
adb shell am start -D ${WIVRN_PKG}/org.meumeu.wivrn.MainActivity
```

Set up LLDB debugging:

1. Push the LLDB server to the device:

   ```bash
   adb shell "mkdir -p /data/local/tmp/tools"
   adb push "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/lib64/clang/*/lib/linux/aarch64/lldb-server" /data/local/tmp/tools/
   ```

2. Start the LLDB server on the device:

   ```bash
   adb shell /data/local/tmp/tools/lldb-server platform --server --listen '*:5039'
   ```

3. Forward the port:

   ```bash
   adb forward tcp:5039 tcp:5039
   ```

4. Connect from your host:

   ```bash
   lldb
   (lldb) platform select remote-android
   (lldb) platform connect connect://localhost:5039
   ```

Resume the app (required for `-D` flag):

```bash
adb forward tcp:12345 jdwp:$(adb shell pidof ${WIVRN_PKG})
jdb -attach localhost:12345
```

See the [Android NDK debugging guide](https://developer.android.com/ndk/guides/ndk-gdb) for more details.
