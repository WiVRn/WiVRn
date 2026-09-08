# Headless encoder profiling

`tools/perfetto/encoder_profile.py` runs one streaming session per encoder with no headset. Each
writes `out/<enc>.pftrace`; the run prints per-encoder stats and a diff against a baseline.

It compares encoders within one build; [benchmarking.md](benchmarking.md) compares one encoder
across commits. Both drive `tools/perfetto/wivrn_session.py`.

| Process | Runtime | Role |
| ------- | ------- | ---- |
| `hello_xr`, launched by the server | WiVRn (`openxr_wivrn.json`) | renders frames into the encoder |
| WiVRn client | Monado, null compositor + remote HMD, in-process (`openxr_monado.json`) | sends poses, swallows presents |

## Prerequisites

```bash
cmake --preset profiling
cmake --build build-profiling
```

`WIVRN_BUILD_BENCH_RUNTIME`, on in this preset, builds a second, unpatched Monado under
`build-profiling/bench-runtime` with the null compositor and remote HMD driver that
wivrn-server's own Monado omits, as an in-process runtime library — no separate service process,
no IPC (`cmake/BenchRuntime.cmake`). Both runtimes run from the build tree.

The build applies [monado!3006](https://gitlab.freedesktop.org/monado/monado/-/merge_requests/3006)
(null compositor FPS + device size, remote HMD resolution) at configure time, fetched fresh each
run — not carried as a repo patch. Drop once it merges upstream.

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
| `--eye-width` | `1920` | per-eye render width |
| `--eye-height` | `1472` | per-eye render height |
| `--resolution-scale` | `1.0` | extra scale on top of `--eye-width`/`--eye-height` |
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

`--eye-width`/`--eye-height` configure Monado's remote HMD driver directly (`monado/
config_v0.json`, `XRT_COMPOSITOR_NULL_USE_DEVICE_SIZE=1`) — the null compositor reports exactly
that as both the recommended and max view size, so the client renders at exactly that resolution
before `resolution_scale`. Needs the `bench-runtime` Monado (monado!3006 + the device-size
compositor patch); without them the null compositor falls back to a fixed 320x240 recommended /
1920x1080 max, and `resolution_scale` is the only way to reach a usable size. Anything under 640
wide is reported as too small to measure.

`XRT_COMPOSITOR_NULL_FPS` runs the null compositor at 90 Hz, which is also the refresh rate it
advertises. It needs the frame-rate-configurable `bench-runtime` Monado; a distribution one
ignores it and runs at 20.

The negotiated stream is printed per encoder; compare those lines before comparing numbers.