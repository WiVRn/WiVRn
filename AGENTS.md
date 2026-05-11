# WiVRn - Agent Guide

WiVRn is a wireless VR streaming solution that connects standalone VR headsets to Linux computers. This document provides essential information for agents working with this codebase.

## Project Overview

WiVRn consists of three main components:
- **Client**: Android application running on VR headsets
- **Server**: Linux service handling the OpenXR runtime and video encoding
- **Dashboard**: QML-based GUI for configuration and control

The project uses a client-server architecture with real-time streaming of video, audio, and tracking data over the network.

## Build System & Commands

### Build Commands
The project uses CMake with presets for different targets:

```bash
# Build server (PC) only
cmake -B build-server . -GNinja -DWIVRN_BUILD_CLIENT=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-server

# Build dashboard
cmake -B build-dashboard . -GNinja -DWIVRN_BUILD_CLIENT=OFF -DWIVRN_BUILD_SERVER=ON -DWIVRN_BUILD_DASHBOARD=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-dashboard

# Build client (headset) - requires Android environment
export ANDROID_HOME=~/Android
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk/
./gradlew assembleRelease
```

### CMake Presets
Use CMake presets from `CMakePresets.json`:
- `client` - Headset client only
- `server` - Server and wivrnctl
- `dashboard` - Dashboard GUI only
- `everything` - All components

### Build Options
Key build options:
- `WIVRN_USE_NVENC`, `WIVRN_USE_VAAPI`, `WIVRN_USE_VULKAN_ENCODE`, `WIVRN_USE_X264` - Encoder backends
- `WIVRN_USE_PIPEWIRE`, `WIVRN_USE_PULSEAUDIO` - Audio backends
- `WIVRN_USE_SYSTEMD` - Systemd integration
- `WIVRN_FEATURE_STEAMVR_LIGHTHOUSE` - Lighthouse driver support

## Code Organization

### Directory Structure
```
client/          - Headset client application
├── xr/         - OpenXR integration and extensions
├── scenes/     - Application scenes (lobby, stream, etc.)
├── vk/         - Vulkan rendering
├── shaders/    - GLSL shaders
├── audio/      - Audio handling
└── decoder/    - Video decoding

server/          - PC server application
├── driver/     - Monado/OpenXR driver
├── encoder/    - Video encoding
├── audio/      - Audio handling
└── utils/      - Utilities

common/          - Shared code
├── utils/      - Common utilities
├── vk/         - Common Vulkan code
└── xr/         - Common XR code

dashboard/       - QML-based GUI
└── qml/        - QML files
```

### Key Communication
Client-server communication is defined in `common/wivrn_packets.h` using a tagged packet system with `operator()` overloads for handling different packet types.

## Code Conventions

### Formatting
- `.clang-format` defines project formatting rules
- Uses tabs for indentation (TabWidth: 8, UseTab: ForIndentation)
- Custom brace wrapping (Allman style)
- Column limit: 0 (no line limit)

### Naming
- Lowercase snake_case for functions and variables
- Upper snake_case for constants and macros
- Namespaces: `lowercase` (`wivrn::`, `scenes::`, `xr::`)
- Classes: `lowercase` for consistency with project style

### Code Style
- RAII patterns extensively used for resource management
- Smart pointers (`std::unique_ptr`, `std::shared_ptr`) for memory management
- Template metaprogramming for packet handling
- Vulkan RAII wrappers (`vk::raii::`)

## Architecture & Flow

### Client Architecture
- Scene-based architecture with `scene_impl<T>` base class
- Main scene: `scenes::stream` handles video rendering and tracking
- Network isolation: `wivrn_session` manages all network communication
- Multi-threaded: network thread, tracking thread, render thread

### Server Architecture
- Built on Monado OpenXR runtime
- Driver-based architecture in `server/driver/`
- Video encoding pipeline with multiple encoder backends
- D-Bus integration for IPC

### Data Flow
1. Client sends tracking data to server
2. Server renders XR application, encodes video frames
3. Server streams video shards to client
4. Client reassembles and displays video
5. Client sends audio and input back to server

## Testing

### Debugging
Extensive debugging documentation in `docs/debugging.md`:
- Client logs via `adb logcat`
- Server logs via `XRT_LOG=debug`
- Video dumps with `WIVRN_DUMP_VIDEO`
- Timing analysis with `WIVRN_DUMP_TIMINGS`

### Testing Commands
```bash
# Start server with debug logging
XRT_LOG=debug XRT_COMPOSITOR_LOG=debug wivrn-server

# Collect WiVRn logs only
adb logcat '*:S' WiVRn:V

# Performance profiling with perfetto
XRT_TRACING=true wivrn-server
```

## Important Implementation Details

### Protocol
- Based on packet protocol with automatic serialization
- Packet types in `common/wivrn_packets.h`
- Protocol versioning support

### Video Streaming
- Shard-based streaming for low latency
- Multiple codec support (h.264, h.265, AV1)
- Adaptive bitrate and foveated encoding

### Audio
- Bidirectional audio streaming
- Low-latency audio handling
- Support for PipeWire and PulseAudio

### Input Handling
- Extensive input device support in `common/wivrn_packets.h`
- HID forwarding for gamepad input
- haptics feedback support

## Project-Specific Gotchas

### Android Client
- Requires signing keys for installation
- Uses Android NDK for native code
- ADB commands for debugging and installation

### Server Dependencies
- Requires Vulkan, OpenXR, and various encoder libraries
- Systemd integration optional but recommended
- Network configuration (firewall ports 9757/UDP+TCP)

### Build System
- Client and server have completely separate build systems
- Client uses Gradle/Android build, server uses CMake
- External dependencies managed with FetchContent

### Memory Management
- VkMemoryAllocator used for Vulkan memory management
- Custom reference counting for some resources
- Careful with threading and resource lifetimes

## Component Integration

### OpenXR Integration
- Custom OpenXR runtime in server component
- Client uses standard OpenXR with extensions
- Protocol abstracts OpenXR concepts

### Vulkan Usage
- Used for rendering on client
- Custom buffer management for video frames
- RAII wrappers throughout (`vk::raii::`)

### Threading Model
- Server: Monado threading model
- Client: Network thread, render thread, tracking thread
- Careful synchronization with `std::mutex` and atomics

## External Dependencies

Key external dependencies:
- Monado (OpenXR runtime)
- Vulkan/OpenGL ES for rendering
- Various video encoders (x264, NVENC, VAAPI)
- Qt6 for dashboard
- OpenSSL for security

## Development Workflow

### Adding Features
1. Add packet types to `common/wivrn_packets.h`
2. Implement handler with `operator()` in appropriate class
3. Add any necessary serialization support
4. Test with debug logging enabled

### Debug Connection Issues
1. Check firewall settings
2. Use USB tunneling for debugging: `adb reverse tcp:9757 tcp:9757`
3. Verify avahi/zeroconf is working
4. Check log output on both client and server

### Performance Optimization
1. Use video dumps to verify quality
2. Monitor timing with `WIVRN_DUMP_TIMINGS`
3. Use perfetto for detailed profiling
4. Check encoder latency in logs

## Documentation
- `docs/building.md` - Detailed build instructions
- `docs/debugging.md` - Debugging guide
- `docs/configuration.md` - Configuration reference
- `docs/steamvr.md` - SteamVR integration