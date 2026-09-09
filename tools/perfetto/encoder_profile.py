#!/usr/bin/env python3
"""Headless per-encoder profiling of the WiVRn pipeline.

Per encoder, runs one streaming session (headless Monado as the client runtime +
wivrn-server auto-launching hello_xr as the frame producer + the wivrn client),
captures out/<enc>.pftrace, then prints a summary and a diff against a baseline.

Compares encoders inside *one* build. To compare one encoder across *commits* — bisecting a
performance regression — use encoder_bench.py instead.

The workload is pinned to the reference one (wivrn_session.REFERENCE_EYE_WIDTH/HEIGHT): left
alone, the headless setup negotiates a 192x128 encode that measures fixed cost rather than the
codec. The negotiated stream is printed per encoder so a renegotiation is never silent.

    cmake --preset profiling && cmake --build build-profiling
    cmake --install build-profiling --prefix <prefix>
    tools/perfetto/encoder_profile.py --duration 15 -o out/
"""

import argparse
import signal
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
# Sibling scripts, importable only after the path insert above.
import pftrace_summary  # summarize()/compare() reused
import wivrn_session
from wivrn_session import NETWORK_SLICES, SessionError, SessionSpec

PROG = "encoder_profile"


def log(msg):
    wivrn_session.log(msg, PROG)


def err(msg):
    wivrn_session.err(msg, PROG)


def run_encoder(enc, args, tools):
    """Run a single streaming session for one encoder; return the .pftrace path or None."""
    spec = SessionSpec(
        encoder=enc,
        codec=args.codec,
        duration=args.duration,
        graphics=args.graphics,
        xr_app=args.xr_app,
        bitrate_bps=args.bitrate,
        eye_width=args.eye_width,
        eye_height=args.eye_height,
        resolution_scale=args.resolution_scale,
        stream_scale=args.stream_scale,
        startup_timeout=args.startup_timeout,
        server_log_level="info",  # exposes the "Encoder configuration:" block reported below
    )
    trace = args.output / f"{enc}.pftrace"
    try:
        gpu_samples = wivrn_session.run_session(
            spec, tools, trace, args.output, tag=enc, progress=log
        )
    except SessionError as e:
        err(f"[{enc}] {e}")
        return None

    warning = wivrn_session.check_gpu_clocks(gpu_samples)
    if warning:
        err(f"[{enc}] {warning}")

    # A frameless session still flushes a header-only trace; require encode slices.
    try:
        durations, _span, _size = pftrace_summary.parse(trace)
    except (OSError, ValueError, IndexError):
        durations = {}
    if not durations.get("encode"):
        err(
            f"[{enc}] trace has no encode slices — encoder produced no frames "
            f"(not built with this encoder, or unsupported by the GPU?); see {enc}-server.log"
        )
        return None

    # What was actually encoded. Two encoders that negotiated different streams are not
    # comparable, and too small a stream is not worth comparing at all.
    streams = wivrn_session.parse_encoder_config(args.output / f"{enc}-server.log")
    if streams:
        log(f"[{enc}] stream: {wivrn_session.describe_streams(streams)}")
    warning = wivrn_session.check_encode_size(streams)
    if warning:
        err(f"[{enc}] {warning}")

    log(f"[{enc}] wrote {trace} ({trace.stat().st_size:,} bytes)")
    return trace


def main():
    parser = argparse.ArgumentParser(
        prog=PROG,
        description="Headless per-encoder profiling of the full WiVRn pipeline.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--encoders",
        default=",".join(wivrn_session.detect_encoders()),
        help="comma-separated encoders to profile "
        "(default: auto-detected from the GPUs present: %(default)s)",
    )
    parser.add_argument(
        "--codec", help="video codec for every run (default: each encoder's default)"
    )
    parser.add_argument(
        "--duration", type=float, default=15.0, help="seconds to stream per encoder (default: 15)"
    )
    parser.add_argument(
        "--graphics",
        default="Vulkan2",
        help="hello_xr graphics API: -g argument (default: Vulkan2)",
    )
    parser.add_argument(
        "--bitrate",
        type=int,
        default=50_000_000,
        help="stream bitrate in bps (default: %(default)s)",
    )
    parser.add_argument(
        "--eye-width",
        type=int,
        default=wivrn_session.REFERENCE_EYE_WIDTH,
        help="per-eye render width; simulates a headset's panel resolution (default: %(default)s)",
    )
    parser.add_argument(
        "--eye-height",
        type=int,
        default=wivrn_session.REFERENCE_EYE_HEIGHT,
        help="per-eye render height (default: %(default)s)",
    )
    parser.add_argument(
        "--resolution-scale",
        type=float,
        default=wivrn_session.REFERENCE_RESOLUTION_SCALE,
        help="extra scale on top of --eye-width/--eye-height (default: %(default)s)",
    )
    parser.add_argument(
        "--stream-scale",
        type=float,
        default=wivrn_session.REFERENCE_STREAM_SCALE,
        help="encoded resolution, as a fraction of the render resolution (default: %(default)s)",
    )
    parser.add_argument(
        "--baseline", help="encoder to diff the others against (default: first successful)"
    )
    parser.add_argument(
        "--startup-timeout", type=float, default=20.0, help="server startup timeout (default: 20)"
    )
    parser.add_argument(
        "-o", "--output", type=Path, default=Path("."), help="output directory (default: cwd)"
    )
    wivrn_session.add_toolchain_args(parser, "build-profiling")
    args = parser.parse_args()

    encoders = [e.strip() for e in args.encoders.split(",") if e.strip()]
    unknown = [e for e in encoders if e not in wivrn_session.ALL_ENCODERS]
    if unknown:
        sys.exit(f"{PROG}: unknown encoder(s): {', '.join(unknown)}")

    tools, missing = wivrn_session.resolve_toolchain(args)
    if missing:
        err("missing prerequisites:")
        for m in missing:
            err(f"  - {m}")
        sys.exit(1)

    args.output.mkdir(parents=True, exist_ok=True)
    for line in tools.describe():
        log(line)

    traces = {}
    failed = []
    for enc in encoders:
        # One encoder blowing up must not abort the rest of the matrix, whatever it raised.
        try:
            t = run_encoder(enc, args, tools)
        except Exception as e:  # noqa: BLE001
            err(f"[{enc}] unexpected error: {e}")
            t = None
        if t:
            traces[enc] = t
        else:
            failed.append(enc)

    if failed:
        err(f"failed encoders (skipped): {', '.join(failed)}")
    if not traces:
        sys.exit(f"{PROG}: no successful runs")

    # Per-encoder summary (network slices excluded — see NETWORK_SLICES).
    for enc, t in traces.items():
        pftrace_summary.summarize(str(t), exclude=NETWORK_SLICES)

    # Pairwise diff against a baseline.
    baseline = args.baseline if args.baseline in traces else next(iter(traces))
    for enc, t in traces.items():
        if enc == baseline:
            continue
        pftrace_summary.compare(str(traces[baseline]), str(t), exclude=NETWORK_SLICES)

    summary = f"done: {', '.join(traces)}  (baseline: {baseline})"
    if failed:
        summary += f";  failed: {', '.join(failed)}"
    log(summary)


if __name__ == "__main__":
    # Route signals through sys.exit so per-encoder session cleanup runs.
    def _sig(signum, _frame):
        sys.exit(128 + signum)

    signal.signal(signal.SIGTERM, _sig)
    signal.signal(signal.SIGINT, _sig)
    main()
