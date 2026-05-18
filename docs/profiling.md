# Profiling WiVRn

This guide covers performance analysis for WiVRn: timing CSV dumps and detailed system-wide profiling with [Perfetto](https://perfetto.dev/).

For general debugging — log collection, crash analysis, and troubleshooting — see [docs/debugging.md](debugging.md).

## Table of Contents

- [Quick Start](#quick-start)
- [Timing Analysis (CSV)](#timing-analysis-csv)
- [Perfetto Profiling](#perfetto-profiling)
  - [HMD Profiling](#hmd-profiling)
  - [Server Profiling](#server-profiling)
    - [Building](#building)
    - [What WiVRn tracing records](#what-wivrn-tracing-records)
    - [One-command capture](#one-command-capture)
    - [Manual capture](#manual-capture)
    - [Recipes](#recipes)
    - [Adding instrumentation](#adding-instrumentation)
    - [Full-system tracing](#full-system-tracing)
- [CSV ↔ Perfetto alignment](#csv--perfetto-alignment)

---

## Quick Start

Build with WiVRn tracing enabled, run with `WIVRN_TRACING=true`, connect a client and start streaming, then capture with the wrapper:

Build:
```bash
cmake -B build -DWIVRN_USE_PERFETTO=ON
cmake --build build
```

Run:
```bash
WIVRN_TRACING=true wivrn-server
```

Connect your HMD, start an OpenXR app, begin streaming.

Trace:
```bash
tools/perfetto/wivrn_capture.py
```

The wrapper drops a `wivrn-<encoder>-<timestamp>.pftrace` in the current directory (encoder name read from the WiVRn config, falls back to `auto`) and prints a warning if the file looks empty. Open it in <https://ui.perfetto.dev>.

To verify tracing is setup run the server at info level — Monado's default is `warn`, which suppresses the init log lines:

```bash
XRT_LOG=info WIVRN_TRACING=true wivrn-server
```

After the first client connects, you should see:

```
wivrn::trace: producer socket /run/user/<uid>/wivrn-perfetto-producer.sock
wivrn::trace: init complete
```

> **Socket path.** Perfetto's default socket (`/run/perfetto/traced-*.sock`) is root-only. WiVRn sets `PERFETTO_PRODUCER_SOCK_NAME` to `$XDG_RUNTIME_DIR/wivrn-perfetto-producer.sock` (fallback `/tmp/...`); the wrapper uses the same pair. Override by exporting `PERFETTO_PRODUCER_SOCK_NAME` before starting the server.

When you want system context (the rest of the OpenXR client, third-party drivers, kthreads, GPU power state, scheduling rows under the WiVRn slices), see [Full-system tracing](#full-system-tracing).

## Timing Analysis (CSV)

WiVRn can record per-frame timing as CSV without any Perfetto setup. Same events that land on the Perfetto `feedback` track, written one row per event:

```bash
WIVRN_DUMP_TIMINGS=/tmp/wivrn-timings.csv wivrn-server
```

CSV and Perfetto are independent — set either, neither, or both. See [CSV ↔ Perfetto alignment](#csv--perfetto-alignment) for how the two views line up.

## Perfetto Profiling

[Perfetto](https://perfetto.dev/) provides detailed system-wide profiling.

### HMD Profiling

Enable persistent tracing on Android:

```bash
adb shell setprop persist.traced.enable 1
```

Open the [Perfetto UI](https://ui.perfetto.dev/), connect to your device, and configure tracing as needed. See the [Android tracing quickstart](https://perfetto.dev/docs/quickstart/android-tracing) for detailed instructions.

### Server Profiling

WiVRn ships a single Perfetto producer (built on [Percetto](https://github.com/olvaffe/percetto)) covering the encoder pipeline, compositor, network, and headset feedback. It is built opt-in and gated at runtime by an env var, so it has zero cost when unused.

#### Building

```bash
cmake -B build -DWIVRN_USE_PERFETTO=ON
cmake --build build
```

`WIVRN_USE_PERFETTO` defaults to `OFF`. With it `ON`, the instrumentation stays compiled in but stays inert unless `WIVRN_TRACING` is also set at runtime: trace calls short-circuit on a process-wide flag and the GPU timestamp query pools are never created, so there is no measurable cost when disabled.

Percetto must be installed (via the distro or built from <https://github.com/olvaffe/percetto>) and discoverable through `CMAKE_PREFIX_PATH` / `PKG_CONFIG_PATH`. CMake errors out otherwise; there is no automatic fetch.

#### What WiVRn tracing records

| Category      | Spans / instants                                                                                                                                                      | Track            |
| ------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------- |
| `encoder`     | `present_image`, `encode`, `nvEncEncodePicture+Lock` (NVENC), `avcodec_send+receive` (VAAPI) — encoder-thread CPU spans                                               | WiVRn encoder    |
| `encoder_gpu` | `encodeVideoKHR`, `vk_copy_image_tmp`, `vk_copy_to_host`, `vk_copy_to_host_overflow` (Vulkan), `vk_copy_image_to_buffer` (NVENC pre-copy), `vk_copy_luma_chroma` (VAAPI pre-copy) — GPU-hardware-timestamped slices, anchored to host monotonic via `VK_EXT_calibrated_timestamps` | WiVRn encoder    |
| `compositor`  | `encoder_work iter`, encoder-thread wake instants                                                                                                                     | WiVRn compositor |
| `network`     | `SendData` — one frame-scoped span covering all NAL (Network Abstraction Layer) sends                                                                                  | WiVRn network    |
| `feedback`    | Every `dump_time` marker — headset feedback (`receive_*`, `decode_*`, `blit`, `display`) and server timing (`wake_up`, `begin`, `encode_*`, `send_*`)                 | WiVRn feedback   |

Each emit carries `frame` and `stream` as Perfetto debug args so you can pivot per-frame across the timeline.

All WiVRn spans land on four registered tracks (`WiVRn encoder`, `WiVRn compositor`, `WiVRn network`, `WiVRn feedback`), registered eagerly at init so descriptors precede events and every trace has an identical layout. Both `encoder` (CPU) and `encoder_gpu` (GPU-hardware-timestamped) spans share the `WiVRn encoder` track. Concurrent slices from multiple encoder streams stack on the shared lane; the `stream` debug arg lets you filter per-stream in SQL. NVENC's `nvEncEncodePicture+Lock` and VAAPI's `avcodec_send+receive` are CPU-timed (`encoder`), not GPU-timed (`encoder_gpu`). Trade-off: CPU spans don't align with `sched_switch` rows in `--full` captures.

#### One-command capture

`tools/perfetto/wivrn_capture.py` is a thin wrapper around `tracebox`, a static Perfetto bundle from upstream. It downloads `tracebox` on first run, starts the tracing services, captures with the Perfetto config of your choice, and stops everything cleanly:

Default config: `wivrn_trace_cfg.pbtx` — WiVRn only, no ftrace, no sudo
```bash 
tools/perfetto/wivrn_capture.py
```

Explicit default config:
```bash
tools/perfetto/wivrn_capture.py -c tools/perfetto/wivrn_trace_cfg.pbtx
```

`-c PATH` accepts any Perfetto config. The wrapper inspects the resolved config. If the config contains a `linux.ftrace` data source, it starts `traced_probes` and uses `sudo` (needed for `/sys/kernel/debug/tracing`)

Full-system capture — every category + broad ftrace
```bash
tools/perfetto/wivrn_capture.py --full
```

Strip ftrace from any config (e.g. --full but no scheduling rows)
```bash
tools/perfetto/wivrn_capture.py --full --cpu-only
```

`--cpu-only` is a generic ftrace-stripper that removes any `linux.ftrace` data_sources block from the chosen config before capture — equivalent to no-op on the default config, useful when paired with `--full`.

Custom duration / output
```bash
tools/perfetto/wivrn_capture.py --duration-ms 30000 -o my.pftrace
```

#### Manual capture

If you'd rather drive Perfetto yourself, point `traced` and the consumer at the same socket pair `wivrn-server` defaults to (`$XDG_RUNTIME_DIR/wivrn-perfetto-{producer,consumer}.sock`). The WiVRn-only config has no ftrace data source, so `traced_probes` and `sudo` aren't needed:

```bash
SOCK_DIR="${XDG_RUNTIME_DIR:-/tmp}"
PRODUCER="${SOCK_DIR}/wivrn-perfetto-producer.sock"
CONSUMER="${SOCK_DIR}/wivrn-perfetto-consumer.sock"

# 1. Start the daemon. traced reads PERFETTO_*_SOCK_NAME at startup (the
#    daemon has no --producer-socket / --consumer-socket CLI flags).
PERFETTO_PRODUCER_SOCK_NAME="$PRODUCER" \
PERFETTO_CONSUMER_SOCK_NAME="$CONSUMER" \
    traced --background

# 2. Start wivrn-server. Its trace init already defaults to the same producer
#    socket, so no env var is needed here.
WIVRN_TRACING=true wivrn-server

# 3. Capture. Reproduce the workload while this runs.
PERFETTO_PRODUCER_SOCK_NAME="$PRODUCER" \
PERFETTO_CONSUMER_SOCK_NAME="$CONSUMER" \
    perfetto -c tools/perfetto/wivrn_trace_cfg.pbtx --txt -o trace.pftrace
```

For full-system captures (or any config with `linux.ftrace`), you do need `traced_probes` and root for `/sys/kernel/debug/tracing`. Same socket setup, plus `sudo` for the daemons:

```bash
PERFETTO_PRODUCER_SOCK_NAME="$PRODUCER" \
PERFETTO_CONSUMER_SOCK_NAME="$CONSUMER" \
    sudo -E traced --background --set-socket-permissions "$USER:0660:$USER:0660"
PERFETTO_PRODUCER_SOCK_NAME="$PRODUCER" sudo -E traced_probes --background

WIVRN_TRACING=true wivrn-server

PERFETTO_PRODUCER_SOCK_NAME="$PRODUCER" \
PERFETTO_CONSUMER_SOCK_NAME="$CONSUMER" \
    perfetto -c tools/perfetto/wivrn_full_trace_cfg.pbtx --txt -o trace.pftrace
```

Open `trace.pftrace` in <https://ui.perfetto.dev>.

#### Recipes

**Encoder comparison (Vulkan vs NVENC vs VAAPI).** Capture one trace per encoder:

Start the server:
```bash
# In wivrn config: "encoder": "vulkan"
WIVRN_TRACING=true wivrn-server
```

Record a trace:
```bash
tools/perfetto/wivrn_capture.py -o vulkan.pftrace
```

Repeat with "nvenc" / "vaapi" → nvenc.pftrace / vaapi.pftrace.

Open the traces in <https://ui.perfetto.dev> and compare slice durations on the **WiVRn encoder** track — CPU spans (`encode`, `nvEncEncodePicture+Lock`, `avcodec_send+receive`) and GPU-timestamped spans (`encodeVideoKHR`, `vk_copy_image_to_buffer`, `vk_copy_luma_chroma`) all appear together on that track for direct comparison.

**Bulk summary / encoder diff.** `tools/perfetto/pftrace_summary.py` prints per-slice stats for one trace, or a side-by-side diff table with Δmean when two traces are given:

Single trace:
```bash
tools/perfetto/pftrace_summary.py nvenc.pftrace
```

Side-by-side diff (two traces):
```bash
tools/perfetto/pftrace_summary.py nvenc.pftrace vulkan.pftrace
```

#### Adding instrumentation

Include `utils/wivrn_trace.h`. No `#ifdef` needed at call sites.

**CPU span** — RAII, starts on construction, ends when the variable goes out of scope:
```cpp
wivrn::trace::scope my_span(wivrn::trace::cpu_track::encoder, stream_idx, frame_index, "my_span");
```

**CPU instant** — emitted at the call site:
```cpp
wivrn::trace::cpu_instant(wivrn::trace::cpu_track::compositor, "my_event", 0, 0);
```

**CPU begin/end pair** — for spans whose lifetime spans multiple calls (e.g. one network span covering all NAL units — pieces of the encoded H.264/H.265 bitstream — sent for a single frame):
```cpp
wivrn::trace::cpu_begin(wivrn::trace::cpu_track::network, stream_idx, frame_idx, "Send");
// ... later, possibly in another call ...
wivrn::trace::cpu_end  (wivrn::trace::cpu_track::network, stream_idx, frame_idx, "Send");
```

**GPU span** — bracket the work with a `wivrn::gpu_timestamp_pool`'s `cmd_begin` / `cmd_end`, then emit after the fence/wait:
```cpp
if (auto s = pool.collect(slot))
        wivrn::trace::gpu_slice(wivrn::trace::gpu_track::nvenc_copy,
                                "my_gpu_span", s->begin_ns, s->end_ns, s->frame_index, stream);
```
Begin/end come from the pool already in host monotonic ns (via `VK_EXT_calibrated_timestamps`, routed through monado's `vk_convert_timestamps_to_host_ns` so the calibration cache is shared with any monado-side producer). A disabled pool — queue family without timestamp bits, or `WIVRN_TRACING` not set at runtime — returns `nullopt`.

`name` must point to storage that outlives the emit (a string literal is fine). Place the scope/begin at the point that matches the semantic start of the operation, not at the top of the enclosing function, to keep Perfetto timings aligned with any corresponding `dump_time` timestamps.

| domain | category | track |
| --- | --- | --- |
| `encoder` | `encoder` | WiVRn encoder |
| `compositor` | `compositor` | WiVRn compositor |
| `network` | `network` | WiVRn network |

#### Full-system tracing

The default `wivrn_trace_cfg.pbtx` is **WiVRn-only**: only the five WiVRn `track_event` categories are recorded, and there is no `linux.ftrace` data source. That keeps captures small and focused, and means **anything outside WiVRn is invisible** — the rest of the OpenXR client, the GPU driver, third-party producers, kthreads stealing CPU, GPU frequency transitions, IRQ storms.

When the problem isn't necessarily inside WiVRn (a stutter that doesn't correlate with encoder spans, suspected interference from another process, a power-management glitch), reach for `wivrn_full_trace_cfg.pbtx` instead. It has no `track_event` filter (`enabled_categories: "*"`) and a broad ftrace set: scheduling + frequency + idle + IRQs + DRM vblank + `process_stats` for thread names + `system_info`. Buffer is also larger (256 MiB ring) to absorb the extra volume. It needs `traced_probes` + root because of ftrace; the wrapper handles that automatically.

Run a full trace:
```bash
tools/perfetto/wivrn_capture.py --full
```

Equivalent explicit form:
```bash
tools/perfetto/wivrn_capture.py -c tools/perfetto/wivrn_full_trace_cfg.pbtx
```

Full producers but no ftrace (every track_event category, no scheduling rows)
```bash
tools/perfetto/wivrn_capture.py --full --cpu-only
```

What changes in the trace:

| Aspect           | `wivrn_trace_cfg.pbtx` (default)         | `wivrn_full_trace_cfg.pbtx` (`--full`)  |
| ---------------- | ---------------------------------------- | --------------------------------------- |
| track_event      | WiVRn categories only                    | Every category, every producer          |
| ftrace events    | *(none)*                                 | sched + freq + idle + IRQs + vblank     |
| Process names    | from track_event thread descriptors only | full `process_stats` scan               |
| Buffer           | 64 MiB ring                              | 256 MiB ring                            |
| Typical trace size (15 s, idle) | 1–5 MB                    | 50–200 MB                               |
| Needs sudo?      | No                                       | Yes (ftrace)                            |
| When to reach for it | Encoder pipeline questions           | "Something outside WiVRn is doing X"    |

Tips:

- Capture for less time. A focused 5-second `--full` capture is usually more useful than 15 seconds of noise — pair `--full` with `--duration-ms 5000`.
- The Perfetto UI lets you hide tracks you don't care about; a full trace gives you the option to bring them back without re-capturing.
- If you want full producers but no scheduling noise (and no `sudo`), combine `--full --cpu-only` — the wrapper strips the `linux.ftrace` block from the config on the fly.
- If the trace gets too large to load in the UI, drop the IRQ ftrace events (highest-rate by far) by copying `wivrn_full_trace_cfg.pbtx` and removing the `irq/*` lines, then pass with `-c`.

---

## CSV ↔ Perfetto alignment

`WIVRN_DUMP_TIMINGS` (CSV) and `WIVRN_TRACING` (Perfetto) read the **same event stream** through the same `wivrn_session::dump_time` call. They are orthogonal: set either, neither, or both, and the CSV format is unchanged regardless of whether Perfetto is built or enabled.

| Aspect      | CSV (`WIVRN_DUMP_TIMINGS`)                                | Perfetto `feedback` track                                |
| ----------- | --------------------------------------------------------- | -------------------------------------------------------- |
| Event names | `"wake_up"`, `"begin"`, `"encode_begin"`, `"encode_end"`, `"send_begin"`, `"send_end"`, `"receive_begin"`, `"receive_end"`, `"decode_begin"`, `"decode_end"`, `"blit"`, `"display"` | Same literal names |
| Clock       | `os_monotonic_get_ns()` (CLOCK_MONOTONIC)                 | `PERCETTO_CLOCK_MONOTONIC` — same clock                  |
| Per-row data | `event,frame,time_ns,stream[,extra]`                     | `name` + `frame` / `stream` debug args                   |
| Where it lives | One row per call in the CSV                            | One instant per call on `feedback`                       |

Because both views share the same clock and event names, a `grep encode_begin` over the CSV and a filter on `name = "encode_begin"` in Perfetto select the same set of frames at the same timestamps. Use the CSV for offline scripting and the Perfetto trace for a visual timeline.

Perfetto adds data the CSV doesn't carry: CPU scope spans (`encode`, `encoder_work iter`, `SendData`, …) and GPU encode/copy slices from Vulkan timestamp queries, all visible on the `WiVRn encoder`, `WiVRn compositor`, and `WiVRn network` tracks.
