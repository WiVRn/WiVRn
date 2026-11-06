#!/usr/bin/env python3
"""Headless per-encoder profiling of the WiVRn pipeline.

Per encoder, runs one streaming session (headless Monado as the client runtime +
wivrn-server auto-launching hello_xr as the frame producer + the wivrn client),
captures out/<enc>.pftrace, then prints a summary and a diff against a baseline.

Compares encoders inside *one* build. To compare one encoder across *commits* — bisecting a
performance regression — use encoder_bench.py instead.

The workload is pinned to the reference one (wivrn_session.add_workload_args), and the
negotiated stream is printed per encoder so a renegotiation is never silent.

    cmake --preset profiling && cmake --build build-profiling
    cmake --install build-profiling --prefix <prefix>
    tools/perfetto/encoder_profile.py -o out/
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
DEFAULT_BUILD_DIR = "build-profiling"


def log(msg):
    wivrn_session.log(msg, PROG)


def err(msg):
    wivrn_session.err(msg, PROG)


def run_encoder(enc, args, tools):
    """Run a single streaming session for one encoder; return the .pftrace path or None."""
    spec = SessionSpec.from_args(args, enc)
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

    # A frameless session still flushes a header-only trace; require encode slices after warmup.
    try:
        durations, begins, _span, _size = pftrace_summary.parse_full(trace)
    except (OSError, ValueError, IndexError):
        durations = {}
        begins = {}
    cutoff = pftrace_summary.warmup_cutoff(begins, args.warmup)
    durations = {
        n: pftrace_summary.trim_warmup(d, begins.get(n, []), cutoff) for n, d in durations.items()
    }
    if not durations.get("encode"):
        err(
            f"[{enc}] trace has no encode slices after {args.warmup:.0f}s warmup — encoder "
            f"produced no frames (not built with this encoder, unsupported by the GPU, or "
            f"--warmup exceeds --duration?); see {enc}-server.log"
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
        "--baseline", help="encoder to diff the others against (default: first successful)"
    )
    parser.add_argument(
        "--warmup",
        type=float,
        default=15.0,
        help="seconds discarded from the start of every run before summarizing "
        "(default: %(default)s)",
    )
    parser.add_argument(
        "-o", "--output", type=Path, default=Path("."), help="output directory (default: cwd)"
    )
    wivrn_session.add_workload_args(parser, default_duration=35.0)
    wivrn_session.add_toolchain_args(parser, DEFAULT_BUILD_DIR)
    args = parser.parse_args()
    args.build_dir = args.build_dir or Path(DEFAULT_BUILD_DIR)

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
        pftrace_summary.summarize(str(t), exclude=NETWORK_SLICES, warmup=args.warmup)

    # Pairwise diff against a baseline.
    baseline = args.baseline if args.baseline in traces else next(iter(traces))
    for enc, t in traces.items():
        if enc == baseline:
            continue
        pftrace_summary.compare(
            str(traces[baseline]), str(t), exclude=NETWORK_SLICES, warmup=args.warmup
        )

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
