# Headless encoder profiling

`tools/perfetto/encoder_profile.py` runs one streaming session per encoder with no headset. Each
writes `out/<enc>.pftrace`; the run prints per-encoder stats and a diff against a baseline.

It compares encoders within one build; [benchmarking.md](benchmarking.md) compares one encoder
across commits. Both drive `tools/perfetto/wivrn_session.py`.

| Process | Runtime | Role |
| ------- | ------- | ---- |
| `hello_xr`, launched by the server | WiVRn (`openxr_wivrn.json`) | renders frames into the encoder |
| WiVRn client | Monado, null compositor, in-process (`openxr_monado.json`) | sends poses, swallows presents |

## Prerequisites

```bash
cmake --preset profiling
cmake --build build-profiling
```

`WIVRN_BUILD_BENCH_RUNTIME`, on in this preset, builds a second, unpatched Monado under
`build-profiling/bench-runtime` with the null compositor and simulated HMD that wivrn-server's own
Monado omits, as an in-process runtime library — no separate service process, no IPC
(`cmake/BenchRuntime.cmake`). Both runtimes run from the build tree.

Also required: `hello_xr` on `PATH`, a systemd user instance, a running `avahi-daemon`, a usable
Vulkan device.

## Usage

```bash
tools/perfetto/encoder_profile.py --duration 15 -o out/
```

| Flag | Default | |
| ---- | ------- | - |
| `--encoders` | what the GPUs support | comma-separated |
| `--codec` | encoder default | `h264` / `h265` / `av1` |
| `--bitrate` | `50000000` | bps |
| `--resolution-scale` | `6.0` | render resolution |
| `--stream-scale` | `1.0` | encoded resolution, as a fraction of the render resolution |
| `--duration` | `15` | seconds per encoder |
| `--graphics` | `Vulkan2` | `hello_xr` graphics API |
| `--xr-app` | `hello_xr -g <--graphics>` | full command to launch instead, e.g. `"xrgears"` |
| `--baseline` | first success | encoder the others are diffed against |
| `--build-dir` | `build-profiling` | |
| `-o` | `.` | output directory |

Paths come from `--build-dir`, then `PATH`, then the install prefixes; `--server`, `--client`,
`--hello-xr`, `--wivrn-manifest` and `--monado-manifest` override individually. An encoder that
cannot run is reported and skipped.

## Workload

The client runtime reports Monado's Simulated HMD at 320x240 per eye. The client multiplies that
by `resolution_scale` unclamped (`client/scenes/stream.cpp`), so the defaults encode 1920x1472 per
eye. Anything under 640 wide is reported as too small to measure.

`XRT_COMPOSITOR_NULL_FPS` runs the null compositor at 90 Hz, which is also the refresh rate it
advertises. It needs the frame-rate-configurable `bench-runtime` Monado; a distribution one
ignores it and runs at 20.

The negotiated stream is printed per encoder; compare those lines before comparing numbers.