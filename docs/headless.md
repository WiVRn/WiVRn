# Headless encoder profiling

`tools/perfetto/encoder_profile.py` profiles the WiVRn encode → stream → decode pipeline with no
physical headset. Per encoder it runs one streaming session, writes `out/<enc>.pftrace`, and prints
per-encoder stats plus a pairwise diff against a baseline.

It drives two OpenXR runtimes:

| Process | Runtime | Role |
| ------- | ------- | ---- |
| `hello_xr` (auto-launched by the server) | WiVRn (`openxr_wivrn.json`) | renders frames into the encoder |
| WiVRn client | standalone Monado, null compositor (`openxr_monado.json`) | sends poses, swallows presents |

## Prerequisites

Build and install the `profiling` preset (server + client + tracing + every supported encoder):

```bash
cmake --preset profiling
cmake --build build-profiling
cmake --install build-profiling --prefix ~/.local   # generates openxr_wivrn.json
```

On `PATH` (not built by WiVRn):

- `monado-service` — standalone Monado (provides `openxr_monado.json`).
- `hello_xr` — Khronos OpenXR sample (<https://github.com/KhronosGroup/OpenXR-SDK-Source>), or pass `--hello-xr`.

Also required: a systemd user instance (`systemctl --user`), a running `avahi-daemon`, and a usable
Vulkan device (real GPU, or `lavapipe`).

## Usage

```bash
tools/perfetto/encoder_profile.py --duration 15 -o out/
```

Encoders default to the set the GPUs support: Vulkan and software everywhere, plus NVENC (NVIDIA) or
VAAPI (AMD/Intel) when detected. Override with `--encoders a,b,…`. An encoder that cannot run is
reported and skipped; the rest still run.

Binaries are found under `build-profiling` and manifests under standard install prefixes; override
with `--server`, `--client`, `--monado-service`, `--hello-xr`, `--wivrn-manifest`,
`--monado-manifest`.

| Flag | Default | Meaning |
| ---- | ------- | ------- |
| `--encoders` | auto-detected from GPUs | comma-separated encoders to profile |
| `--codec` | encoder default | codec for every run (`h264`/`h265`/`av1`) |
| `--duration` | `15` | seconds streamed per encoder |
| `--graphics` | `Vulkan2` | `hello_xr` graphics API |
| `--baseline` | first successful | encoder the others are diffed against |
| `--build-dir` | `build-profiling` | tree holding `wivrn-server` + client |
| `-o` / `--output` | `.` | output directory |

## Output

Each encoder writes `out/<enc>.pftrace` plus `-server.log` / `-client.log` / `-monado.log`. Open a
trace at <https://ui.perfetto.dev>, or read the printed summaries: encoder cost is the `encode` /
`wivrn_encoder_gpu` slices; `wivrn_feedback` events (`decode_begin`, `display`) confirm the full
round-trip.

## Troubleshooting

- **`server not listening on 9757`** — see `out/<enc>-server.log`; add `XRT_LOG=info`.
- **Empty trace / no encode slices** — `hello_xr` produced no frames. Check
  `journalctl --user -t hello_xr -e`. `XR_ERROR_RUNTIME_UNAVAILABLE` means the `XR_RUNTIME_JSON`
  systemd-env injection failed (check `systemctl --user`); an exit right after *"Creating swapchain"*
  means stdin hit EOF.
- **Encoder reported as failed** — that encoder is not compiled into the build or is unsupported by
  the GPU. Rebuild with it enabled, or drop it from `--encoders`.
