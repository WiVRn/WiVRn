# Benchmarking the encoder, commit by commit

`tools/perfetto/encoder_bench.py` answers one question: did this commit make encoding slower?

It builds the checked-out tree, streams a fixed headless workload through it a few times, and
reduces the result to one number — by default the median duration of `encodeVideoKHR`, the
GPU-timestamped span of the Vulkan encoder. Every measurement is filed under the commit sha, so
`git bisect` never measures a commit twice and the whole range tabulates afterwards.

To compare encoders within one build instead, see [headless.md](headless.md). Both drive
`tools/perfetto/wivrn_session.py`.

## Prerequisites

Those of [headless profiling](headless.md#prerequisites). The benchmark builds the tree itself.
`encoder_bench.py doctor` checks them and prints the metric it would measure.

## Quick start

```bash
# Copy the harness out of the worktree; a bisect checks out commits that would replace it.
tools/perfetto/encoder_bench.py bootstrap
BENCH=~/.cache/wivrn-encoder-bench/bin/encoder_bench.py

git checkout <good> && $BENCH run
git checkout <bad>  && $BENCH run --vs <good>

git bisect start <bad> <good>
git bisect run $BENCH bisect --good <good> --bad <bad>

$BENCH report
```

`bisect` prints a verdict per commit:

```
encoder_bench: threshold 2.205 ms (midpoint of good 4b1c9f2a1e05 1.800 ms and bad 21f9c1b4948b 2.700 ms)
encoder_bench: 94fffa0f5e57: 2.700 ms is above it -> BAD
```

## Exit codes

`bisect` speaks `git bisect run`'s contract, so a broken build is never labelled a slow one:

| Code | Meaning | When |
| ---- | ------- | ---- |
| `0` | good | the metric is at or below the threshold |
| `1` | bad | it is above, or below with `--bad-when faster` |
| `125` | skip | the commit does not build, the session crashed, or it produced no encode slices |
| `128` | abort | misconfiguration: missing prerequisites, no threshold, run from inside the repo under test |

## Threshold

| Flags | Threshold |
| ----- | --------- |
| `--threshold-ms 2.2` | exactly that |
| `--good <rev> --bad <rev>` | geometric midpoint of the two recorded values |
| `--good <rev>` | the recorded good value plus `--tolerance-pct` (default 10) |

`--good` and `--bad` read recorded measurements; a revision measured with different settings
aborts the run rather than supplying a number. `--bad-when faster` bisects a speedup.

## What is measured

| `--encoder` | metric slice | frame preparation | |
| ----------- | ------------ | ----------------- | - |
| `vulkan` | `encodeVideoKHR` | `vk_copy_image_tmp`, `vk_copy_to_host` | GPU timestamps |
| `nvenc` | `nvEncEncodePicture+Lock` | `vk_copy_image_to_buffer` | CPU span, GPU-timestamped copy |
| `vaapi` | `avcodec_send+receive` | `vk_copy_luma_chroma` | CPU span, GPU-timestamped copy |
| `x264` | `x264_encoder_encode` | `vk_copy_luma_chroma` | CPU span, GPU-timestamped copy |

The metric slice holds encoder work only: each encoder's wait on the GPU is a separate `wait_gpu`
span, and Vulkan's blocking size query a separate `wait_bitstream_size`. Frame preparation carries
its own GPU timestamps. `encode`, the base-class span, wraps all of it, and the parts sum to it.
`SendData` is reported alongside because x264 sends from inside its own encode call, through
`param.nalu_process`, while libx264 runs. x264 is H.264 only.

`--slice` selects another slice, `--stat p50|mean|p90|p99|max` another statistic. An encoder's own
span exists only on commits that have it; older commits report that, and name `--slice encode` as
the fallback. It spans the whole tracing era for every encoder, folding the GPU wait and the send
back in, so it applies to a whole range or none of it.

## What is pinned

The harness sets the workload rather than taking the build's defaults, so a commit that changes a
default does not read as a performance change:

| Flag | Default | |
| ---- | ------- | - |
| `--bitrate` | `50000000` | bps |
| `--eye-width` | `1920` | per-eye render width |
| `--eye-height` | `1472` | per-eye render height |
| `--resolution-scale` | `1.0` | extra scale on top of `--eye-width`/`--eye-height` |
| `--stream-scale` | `1.0` | encoded resolution, as a fraction of the render resolution |
| `--fps-divider` | `1` | |
| `--codec` | encoder default | `h264` / `h265` / `av1` |
| `--duration` | `20` | seconds per run |
| `--repeat` | `3` | runs per commit; the median is the result |
| `--warmup` | `120` | leading frames discarded from each run |

Those defaults encode 1920x1472 per eye; see [the workload](headless.md#workload).

Builds are pinned too. The harness passes its own `-D` flags rather than a CMake preset, which an
old commit may not have: `RelWithDebInfo`, IPO off, `WIVRN_WERROR=OFF`, and only the encoder under
test. Each encoder gets its own tree (`build-bench-vulkan`, `build-bench-x264`, …); `--build-dir`
is used as given and reconfigured when its cache names a different encoder. `cmake --preset bench`
builds the same thing by hand.

Changing a pinned setting changes the signature a measurement is filed under. `report` groups by
signature, and `--good`/`--bad` only read records matching the current one.

## Stability

Each measurement records the negotiated stream (`vulkan h264 8-bit 1920x1472 @ 24.6Mbit/s`), the
CPU governor and the GPU video clock range, and each run reports the spread between repeats.
`--max-spread-pct 5` turns an unstable measurement into a skip. A governor other than
`performance` warns, as does a falling GPU video clock:

```bash
sudo cpupower frequency-set -g performance
```

On NVIDIA hardware, check your avaialble clocks with `nvidia-smi -q -d SUPPORTED_CLOCKS` and set values:

```bash
sudo nvidia-smi -pm 1
sudo nvidia-smi -lgc 1755,1755
```

Run the testing. Then, reset:

```bash
sudo nvidia-smi -rgc
```