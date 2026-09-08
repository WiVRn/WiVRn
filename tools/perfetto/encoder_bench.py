#!/usr/bin/env python3
"""Benchmark WiVRn's video encoder one commit at a time, for `git bisect run`.

Builds the checked-out tree, streams a fixed headless workload through it a few times, and
reduces the result to one number: the median duration of the encode slice (by default the GPU
`encodeVideoKHR` span of the Vulkan encoder). Every measurement is recorded under the commit
sha, so a bisect never measures the same commit twice and the whole range can be tabulated
afterwards.

    tools/perfetto/encoder_bench.py bootstrap        # copy the harness out of the worktree
    ~/.cache/wivrn-encoder-bench/bin/encoder_bench.py run          # measure HEAD
    git bisect start <bad> <good>
    git bisect run ~/.cache/wivrn-encoder-bench/bin/encoder_bench.py bisect --good <good>

`bisect` speaks git's exit codes: 0 good, 1 bad, 125 skip (build or session failure), 128 abort
(misconfiguration — git stops the bisect rather than mislabelling commits).

See docs/benchmarking.md.
"""

import argparse
import hashlib
import json
import os
import platform
import shutil
import socket
import statistics
import subprocess
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
# Sibling scripts, importable only after the path insert above.
import pftrace_summary  # parse_full() reused
import wivrn_session
from wivrn_session import SessionError, SessionSpec

PROG = "encoder_bench"
SCHEMA = 1
DEFAULT_BUILD_DIR = "build-bench"  # per encoder: build-bench-vulkan, build-bench-x264, …

# git bisect run contract: 0 good, 1 bad, 125 skip, anything above 127 aborts the bisect.
EXIT_GOOD, EXIT_BAD, EXIT_SKIP, EXIT_ABORT = 0, 1, 125, 128

# The slice whose duration is that encoder's cost. Vulkan's comes from GPU timestamps, the rest
# are CPU spans with the GPU wait hoisted into "wait_gpu".
PRIMARY_SLICE = {
    "vulkan": "encodeVideoKHR",
    "nvenc": "nvEncEncodePicture+Lock",
    "vaapi": "avcodec_send+receive",
    "x264": "x264_encoder_encode",
}
FALLBACK_SLICE = "encode"
# Reported alongside the primary metric when present, to show where a change actually landed.
# The vk_copy_* slices are each encoder's GPU-timestamped frame preparation: whichever one this
# build emits is the peer of its encode slice, and a regression is usually in one or the other.
#
# SendData is deliberately absent: it is a frame-spanning cpu_begin/cpu_end pair, and under x264
# those calls arrive from libx264's nalu_process worker threads, so begin and end land on
# different thread sequences and never form a valid slice.
CONTEXT_SLICES = (
    "encode",
    "present_image",
    "x264_encoder_encode",
    "vk_copy_image_tmp",  # vulkan
    "vk_copy_to_host",  # vulkan
    "vk_copy_to_host_overflow",  # vulkan
    "vk_copy_image_to_buffer",  # nvenc
    "vk_copy_luma_chroma",  # vaapi, x264
    "wait_gpu",  # encoder thread blocked on the GPU, hoisted out of every metric slice
    "wait_bitstream_size",  # vulkan: the encode's size query, which also blocks
    "encoder_work iter",
)
STATS = ("mean", "p50", "p90", "p99", "max")

# Build settings for every bisected commit. Passed as -D rather than via the `bench` preset:
# an old commit's CMakePresets.json does not have that preset, and the point of a bisect is
# that the harness, not the tree, decides how the tree is built. Keep in sync with the
# `bench` preset in CMakePresets.json.
#
#   RelWithDebInfo         Debug would measure the wrong thing
#   IPO off                halves the link time of every bisect step
#   WERROR off             old commits must not fail to build under a newer compiler
#   one encoder only       everything else is dead weight in the build
CMAKE_ARGS = [
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
    "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF",
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    "-DWIVRN_BUILD_SERVER=ON",
    "-DWIVRN_BUILD_CLIENT=ON",
    "-DWIVRN_BUILD_SERVER_LIBRARY=ON",  # provides openxr_wivrn.json, which hello_xr needs
    "-DWIVRN_BUILD_BENCH_RUNTIME=ON",  # the client's OpenXR runtime; see cmake/BenchRuntime.cmake
    "-DWIVRN_BUILD_DASHBOARD=OFF",
    "-DWIVRN_BUILD_DISSECTOR=OFF",
    "-DWIVRN_BUILD_WIVRNCTL=OFF",
    "-DWIVRN_USE_PERFETTO=ON",
    "-DWIVRN_USE_SYSTEMD=ON",  # removed in 85a63506 (auto-detected); still needed before it
    "-DWIVRN_USE_SYSTEM_BOOST=ON",
    "-DWIVRN_USE_PIPEWIRE=OFF",
    "-DWIVRN_WERROR=OFF",
    "-DWIVRN_OPENXR_MANIFEST_TYPE=absolute",  # manifest is used from wherever it was installed
]
ENCODER_CMAKE_FLAG = {
    "vulkan": "WIVRN_USE_VULKAN_ENCODE",
    "nvenc": "WIVRN_USE_NVENC",
    "vaapi": "WIVRN_USE_VAAPI",
    "x264": "WIVRN_USE_X264",
}


def log(msg):
    wivrn_session.log(msg, PROG)


def err(msg):
    wivrn_session.err(msg, PROG)


def die(code, msg):
    err(msg)
    sys.exit(code)


# ---------------------------------------------------------------------------
# git
# ---------------------------------------------------------------------------


def git(repo, *args, check=True):
    r = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, check=False)
    if check and r.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)}: {r.stderr.strip()}")
    return r.stdout.strip()


def repo_root(start):
    try:
        return Path(git(start, "rev-parse", "--show-toplevel"))
    except (RuntimeError, FileNotFoundError):
        return None


def commit_info(repo, rev="HEAD"):
    sha = git(repo, "rev-parse", rev)
    return {
        "commit": sha,
        "short": sha[:12],
        "subject": git(repo, "log", "-1", "--format=%s", sha),
        "committed": git(repo, "log", "-1", "--format=%cI", sha),
    }


def worktree_dirty(repo):
    """True if tracked files differ from HEAD — the sha would not identify what was measured."""
    return bool(git(repo, "status", "--porcelain", "--untracked-files=no"))


# ---------------------------------------------------------------------------
# Result store
# ---------------------------------------------------------------------------


def default_state_dir():
    cache = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
    return cache / "wivrn-encoder-bench"


def signature(settings):
    """Short hash of everything that would make two measurements incomparable."""
    blob = json.dumps(settings, sort_keys=True).encode()
    return hashlib.sha256(blob).hexdigest()[:8]


def record_path(state, sha, sig):
    return state / "results" / f"{sha}-{sig}.json"


def load_record(state, sha, sig):
    p = record_path(state, sha, sig)
    if not p.is_file():
        return None
    try:
        return json.loads(p.read_text())
    except (OSError, json.JSONDecodeError):
        return None


def save_record(state, rec):
    p = record_path(state, rec["commit"], rec["signature"])
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(rec, indent=2, sort_keys=True))
    return p


def all_records(state, sig=None):
    out = []
    for p in sorted((state / "results").glob("*.json")):
        try:
            rec = json.loads(p.read_text())
        except (OSError, json.JSONDecodeError):
            continue
        if sig is None or rec.get("signature") == sig:
            out.append(rec)
    out.sort(key=lambda r: (r.get("committed", ""), r.get("created", 0)))
    return out


# ---------------------------------------------------------------------------
# Host environment
# ---------------------------------------------------------------------------


def cpu_governor():
    try:
        return Path("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor").read_text().strip()
    except OSError:
        return None


def gpus():
    found = []
    for dev in sorted(Path("/sys/class/drm").glob("card*/device")):
        try:
            vendor = (dev / "vendor").read_text().strip()
            device = (dev / "device").read_text().strip()
        except OSError:
            continue
        driver = None
        try:
            driver = (dev / "driver").resolve().name
        except OSError:
            pass
        entry = {"vendor": vendor, "device": device, "driver": driver}
        if entry not in found:
            found.append(entry)
    return found


def host_info():
    return {
        "hostname": socket.gethostname(),
        "kernel": platform.release(),
        "governor": cpu_governor(),
        "gpus": gpus(),
    }


def warn_unstable_host(info):
    gov = info.get("governor")
    if gov and gov != "performance":
        err(
            f"warning: CPU governor is '{gov}', not 'performance' — frequency scaling adds "
            "run-to-run noise that can swamp a small regression"
        )


# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------


def encoder_flags(encoder):
    return [
        f"-D{flag}={'ON' if enc == encoder else 'OFF'}" for enc, flag in ENCODER_CMAKE_FLAG.items()
    ]


def needs_configure(build_dir, encoder):
    """True unless build_dir is already configured for exactly this encoder.

    Reconfiguring matters when --encoder changes: cmake would otherwise keep the cached flags
    and cheerfully build a tree without the encoder we are about to ask the server for.
    """
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return True
    try:
        text = cache.read_text(errors="replace")
    except OSError:
        return True
    for enc, flag in ENCODER_CMAKE_FLAG.items():
        want = "ON" if enc == encoder else "OFF"
        if f"{flag}:BOOL={want}" not in text:
            return True
    return False


def build(repo, build_dir, encoder, jobs, log_path, verbose, extra_args=()):
    """Configure (when needed) and build the checked-out tree. False if the build failed."""
    build_dir.mkdir(parents=True, exist_ok=True)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    steps = []
    if needs_configure(build_dir, encoder):
        steps.append(
            [
                "cmake",
                "-S",
                str(repo),
                "-B",
                str(build_dir),
                "-G",
                "Ninja",
                *CMAKE_ARGS,
                *encoder_flags(encoder),
                *extra_args,
            ]
        )
    cmd = ["cmake", "--build", str(build_dir)]
    if jobs:
        cmd += ["-j", str(jobs)]
    steps.append(cmd)

    with open(log_path, "wb") as f:
        for step in steps:
            f.write(f"\n$ {' '.join(step)}\n".encode())
            f.flush()
            r = subprocess.run(
                step,
                stdout=None if verbose else f,
                stderr=subprocess.STDOUT if not verbose else None,
                check=False,
            )
            if r.returncode != 0:
                err(f"build failed ({' '.join(step[:2])}); see {log_path}")
                return False
    return True


# ---------------------------------------------------------------------------
# Measurement
# ---------------------------------------------------------------------------


def percentile(sorted_vals, q):
    if not sorted_vals:
        return 0.0
    i = min(len(sorted_vals) - 1, round((len(sorted_vals) - 1) * q))
    return sorted_vals[i]


def slice_stats(samples_ns, begins_ns, warmup):
    """ms stats for one slice, discarding the first `warmup` samples.

    Durations arrive in completion order, so the head is the frames encoded while clocks and rate
    control were still settling. The rate covers the retained samples only.
    """
    samples = samples_ns[warmup:] if len(samples_ns) > warmup else []
    if not samples:
        return None
    ts = begins_ns[warmup:] if len(begins_ns) > warmup else []
    fps = 0.0
    if len(ts) > 1 and ts[-1] > ts[0]:
        fps = (len(ts) - 1) / ((ts[-1] - ts[0]) / 1e9)
    srt = sorted(samples)
    return {
        "n": len(samples),
        "mean": statistics.mean(samples) / 1e6,
        "p50": statistics.median(samples) / 1e6,
        "p90": percentile(srt, 0.90) / 1e6,
        "p99": percentile(srt, 0.99) / 1e6,
        "max": max(samples) / 1e6,
        "fps": fps,
    }


def measure_trace(trace, primary, warmup):
    """Reduce one .pftrace to per-slice stats. Raises SessionError if it holds no encode work."""
    durations, begins, span_ns, size = pftrace_summary.parse_full(trace)
    slices = {}
    for name in (primary, *CONTEXT_SLICES):
        st = slice_stats(durations.get(name, []), begins.get(name, []), warmup)
        if st:
            slices[name] = st
    if primary not in slices:
        have = ", ".join(sorted(n for n in durations if durations[n])) or "nothing"
        msg = (
            f"trace has no '{primary}' slices after {warmup} warmup frames (found: {have}) — "
            "the encoder produced no frames, or is not the one that ran"
        )
        if primary != FALLBACK_SLICE and FALLBACK_SLICE in slices:
            # The encoder ran, but this commit predates its own span. Say so: the whole bisect
            # would otherwise skip every commit before that one, for no visible reason.
            msg += (
                f"; the commit does have '{FALLBACK_SLICE}', so it likely predates the "
                f"'{primary}' span — re-run the whole range with --slice {FALLBACK_SLICE}"
            )
        raise SessionError("empty", msg)
    return {"span_s": span_ns / 1e9, "bytes": size, "slices": slices}


def aggregate(runs, primary, stat):
    """Median across repeats, per slice and per stat — one noisy session cannot move it."""
    names = sorted({n for r in runs for n in r["slices"]})
    agg = {}
    for name in names:
        vals = [r["slices"][name] for r in runs if name in r["slices"]]
        agg[name] = {k: statistics.median([v[k] for v in vals]) for k in (*STATS, "fps", "n")}
    per_run = [r["slices"][primary][stat] for r in runs if primary in r["slices"]]
    value = statistics.median(per_run)
    spread = (max(per_run) - min(per_run)) / value * 100 if value and len(per_run) > 1 else 0.0
    return agg, value, spread, per_run


def measure(args, tools, info, sig, settings):
    """Run args.repeat sessions and build the result record. Raises SessionError on failure."""
    spec = SessionSpec(
        encoder=args.encoder,
        codec=args.codec,
        duration=args.duration,
        graphics=args.graphics,
        xr_app=args.xr_app,
        bitrate_bps=args.bitrate,
        eye_width=args.eye_width,
        eye_height=args.eye_height,
        resolution_scale=args.resolution_scale,
        stream_scale=args.stream_scale,
        refresh_rate=args.refresh_rate,
        fps_divider=args.fps_divider,
        startup_timeout=args.startup_timeout,
        trace_buffer_kb=args.trace_buffer_kb,
        server_log_level="info",  # exposes the "Encoder configuration:" block we record below
    )
    trace_dir = args.state_dir / "traces"
    log_dir = args.state_dir / "logs"
    primary = args.slice or PRIMARY_SLICE[args.encoder]

    runs = []
    traces = []
    streams = []
    gpu_clocks = []
    for i in range(1, args.repeat + 1):
        tag = f"{info['short']}-{args.encoder}-{i}"
        trace = trace_dir / f"{tag}.pftrace"
        log(f"run {i}/{args.repeat}")
        samples = wivrn_session.run_session(spec, tools, trace, log_dir, tag=tag, progress=log)
        gpu_clocks += [mhz for _, mhz in samples if mhz]
        warning = wivrn_session.check_gpu_clocks(samples)
        if warning:
            err(warning)
        runs.append(measure_trace(trace, primary, args.warmup))
        traces.append(trace)
        if not streams:
            streams = wivrn_session.parse_encoder_config(log_dir / f"{tag}-server.log")
            warning = wivrn_session.check_encode_size(streams)
            if warning:
                err(warning)

    agg, value, spread, per_run = aggregate(runs, primary, args.stat)
    keep_traces(args.keep_traces, traces, per_run)

    return {
        "schema": SCHEMA,
        **info,
        "signature": sig,
        "settings": settings,
        "metric": {"slice": primary, "stat": args.stat},
        "value_ms": value,
        "run_values_ms": per_run,
        "spread_pct": spread,
        "slices": agg,
        "streams": streams,
        "gpu_video_clock_mhz": (
            {"min": min(gpu_clocks), "max": max(gpu_clocks)} if gpu_clocks else None
        ),
        "host": host_info(),
        "created": time.time(),
        "traces": [str(t) for t in traces if t.is_file()],
    }


def keep_traces(policy, traces, per_run):
    """Traces are the evidence behind a number; a bisect produces a lot of them."""
    if policy == "all":
        return
    keep = set()
    if policy == "median" and per_run:
        keep.add(traces[per_run.index(statistics.median_low(per_run))])
    for t in traces:
        if t not in keep:
            t.unlink(missing_ok=True)


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------


def print_record(rec, baseline=None):
    m = rec["metric"]
    print()
    print(f"  commit    {rec['short']}  {rec['subject'][:60]}")
    if rec.get("dirty"):
        print("            (dirty worktree — not comparable to a clean build of this commit)")
    for s in rec.get("streams", []):
        print(
            f"  stream    {s['encoder']} {s['codec']} {s['bit_depth']}-bit "
            f"{s['width']}x{s['height']} @ {s['mbit_s']}Mbit/s"
        )
    runs = "  ".join(f"{v:.3f}" for v in rec["run_values_ms"])
    print(f"  metric    {m['slice']} {m['stat']}")
    print(
        f"  value     {rec['value_ms']:.3f} ms   (runs: {runs};  spread {rec['spread_pct']:.1f}%)"
    )
    if baseline:
        d = rec["value_ms"] - baseline["value_ms"]
        pct = d / baseline["value_ms"] * 100 if baseline["value_ms"] else 0
        print(
            f"  vs {baseline['short']}  {d:+.3f} ms ({pct:+.1f}%)   [{baseline['value_ms']:.3f} ms]"
        )

    print()
    print(f"  {'slice':<28} {'n':>6} {'mean':>8} {'p50':>8} {'p90':>8} {'p99':>8} {'fps':>7}")
    for name, st in sorted(rec["slices"].items(), key=lambda kv: -kv[1]["mean"]):
        mark = "*" if name == m["slice"] else " "
        print(
            f" {mark}{name:<28} {st['n']:>6.0f} {st['mean']:>8.3f} {st['p50']:>8.3f} "
            f"{st['p90']:>8.3f} {st['p99']:>8.3f} {st['fps']:>7.1f}"
        )


def settings_line(rec):
    s = rec.get("settings", {})
    return (
        f"{s.get('encoder')}/{s.get('codec') or 'default'}  "
        f"{rec['metric']['slice']} {rec['metric']['stat']}  "
        f"{s.get('duration')}s x{s.get('repeat')}  "
        f"{(s.get('bitrate_bps') or 0) // 1_000_000}Mbit  "
        f"{s.get('eye_width')}x{s.get('eye_height')} "
        f"scale {s.get('resolution_scale')}x{s.get('stream_scale')}"
    )


def history_order(repo, recs):
    """Sort records oldest-first by the repo's own commit order.

    Committer dates are not monotonic across rebases and cherry-picks; `git rev-list` is. Any
    commit the repo no longer knows about keeps its date ordering, after the ones it does.
    """
    if repo is None:
        return recs
    try:
        order = {
            sha: i
            for i, sha in enumerate(
                reversed(git(repo, "rev-list", "--all", "--topo-order").split())
            )
        }
    except (RuntimeError, FileNotFoundError):
        return recs
    return sorted(recs, key=lambda r: (order.get(r["commit"], len(order)), r.get("committed", "")))


def cmd_report(args):
    recs = all_records(args.state_dir)
    if args.signature:
        recs = [r for r in recs if r.get("signature") == args.signature]
    if not recs:
        die(EXIT_ABORT, f"no recorded measurements in {args.state_dir / 'results'}")
    recs = history_order(args.repo, recs)

    # Records are only comparable within one signature, so each gets its own table with its
    # own baseline — the oldest commit in the group.
    groups = {}
    for rec in recs:
        groups.setdefault(rec.get("signature"), []).append(rec)

    for sig, group in groups.items():
        print(f"\n  [{sig}]  {settings_line(group[0])}")
        print(f"  {'commit':<14} {'date':<11} {'value ms':>9} {'Δ base':>9} {'spread':>7}  subject")
        print("  " + "-" * 94)
        base = group[0]
        for rec in group:
            d = rec["value_ms"] - base["value_ms"]
            pct = d / base["value_ms"] * 100 if base["value_ms"] else 0
            delta = "baseline" if rec is base else f"{pct:+.1f}%"
            dirty = " *" if rec.get("dirty") else ""
            print(
                f"  {rec['short']:<14} {rec.get('committed', '')[:10]:<11} "
                f"{rec['value_ms']:>9.3f} {delta:>9} {rec['spread_pct']:>6.1f}%  "
                f"{rec['subject'][:38]}{dirty}"
            )
    print()
    return EXIT_GOOD


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------


HARNESS_FILES = ("encoder_bench.py", "wivrn_session.py", "pftrace_summary.py")


def cmd_bootstrap(args):
    """Copy the harness out of the worktree so a bisect cannot check it out from under us."""
    dest = args.state_dir / "bin"
    dest.mkdir(parents=True, exist_ok=True)
    for name in HARNESS_FILES:
        src = SCRIPT_DIR / name
        if not src.is_file():
            die(EXIT_ABORT, f"missing harness file {src}")
        if src != dest / name:  # re-bootstrapping the copy onto itself
            shutil.copy2(src, dest / name)
        (dest / name).chmod(0o755)
    tool = dest / "encoder_bench.py"
    log(f"harness copied to {dest}")
    print()
    print("  Measure the two ends of the range, then bisect between them:")
    print()
    print("    git checkout <good> && " + str(tool) + " run")
    print("    git checkout <bad>  && " + str(tool) + " run")
    print("    git bisect start <bad> <good>")
    print(f"    git bisect run {tool} bisect --good <good>")
    print()
    return EXIT_GOOD


def cmd_doctor(args):
    """Check everything a run needs, without running one."""
    ok = True

    def check(label, good, detail=""):
        nonlocal ok
        ok = ok and good
        print(f"  [{'ok' if good else 'FAIL'}] {label}{'  ' + detail if detail else ''}")

    print(f"\n{PROG}: environment check\n")
    repo = args.repo
    check("git repository", repo is not None, str(repo or "not found"))
    tools, missing = wivrn_session.resolve_toolchain(args)
    check("toolchain", not missing)
    for m in missing:
        print(f"         - {m}")
    if tools:
        for line in tools.describe():
            print(f"         {line}")
    check(
        "systemd user instance",
        wivrn_session.systemctl_user("is-system-running", "--quiet")
        or wivrn_session.systemctl_user("show", "--property=Version"),
    )
    avahi = subprocess.run(
        ["systemctl", "is-active", "--quiet", "avahi-daemon"], capture_output=True, check=False
    )
    check("avahi-daemon", avahi.returncode == 0, "(needed for service discovery)")

    info = host_info()
    gov = info["governor"]
    check("CPU governor", gov == "performance", f"({gov or 'unknown'}; 'performance' recommended)")
    for g in info["gpus"]:
        print(f"         gpu {g['vendor']}:{g['device']} driver={g['driver']}")
    if args.encoder == "vulkan":
        vi = shutil.which("vulkaninfo")
        if vi:
            out = subprocess.run(
                [vi, "--summary"], capture_output=True, text=True, check=False
            ).stdout
            print(
                f"         vulkaninfo: {len(out.splitlines())} lines "
                "(check for VK_KHR_video_encode_queue)"
            )
        else:
            print("         vulkaninfo not installed — cannot verify Vulkan video encode support")

    print()
    print(f"  state dir: {args.state_dir}")
    print(f"  build dir: {args.build_dir}")
    print(f"  metric:    {args.slice or PRIMARY_SLICE[args.encoder]} {args.stat}")
    print()
    return EXIT_GOOD if ok else EXIT_BAD


def settings_for(args):
    """Everything that makes two measurements comparable, plus args.signature naming it.

    Two records may only be compared when this matches: a different duration, bitrate or
    metric is a different experiment, not a different result.
    """
    settings = {
        "encoder": args.encoder,
        "codec": args.codec,
        "duration": args.duration,
        "repeat": args.repeat,
        "warmup": args.warmup,
        "bitrate_bps": args.bitrate,
        "eye_width": args.eye_width,
        "eye_height": args.eye_height,
        "resolution_scale": args.resolution_scale,
        "stream_scale": args.stream_scale,
        "refresh_rate": args.refresh_rate,
        "fps_divider": args.fps_divider,
        "graphics": args.graphics,
        "xr_app": args.xr_app,
        "slice": args.slice or PRIMARY_SLICE[args.encoder],
        "stat": args.stat,
        "cmake_arg": sorted(args.cmake_arg),
    }
    args.signature = signature(settings)
    return settings


def prepare(args, for_bisect):
    """Shared front half of run/bisect: guard, build, and either reuse or measure.

    Returns (record, exit_code); exit_code is None when the record is usable.
    """
    if args.repo is None:
        return None, (EXIT_ABORT, "not inside a git repository (pass --repo)")

    # A bisect checks out the tree this script lives in; the copy git leaves behind is the
    # commit's version, not the harness the range is being measured with.
    if SCRIPT_DIR.is_relative_to(args.repo) and not args.allow_in_tree:
        msg = (
            f"running from inside the repository under test ({SCRIPT_DIR}); "
            f"run '{PROG} bootstrap' and use the copy it makes (or pass --allow-in-tree)"
        )
        if for_bisect:
            return None, (EXIT_ABORT, msg)
        err(f"warning: {msg}")

    info = commit_info(args.repo)
    info["dirty"] = worktree_dirty(args.repo)
    if info["dirty"] and not args.allow_dirty:
        return None, (EXIT_ABORT, "worktree has uncommitted changes (pass --allow-dirty)")

    settings = settings_for(args)

    if args.reuse and not info["dirty"]:
        cached = load_record(args.state_dir, info["commit"], args.signature)
        if cached:
            log(f"reusing recorded measurement for {info['short']} ({args.signature})")
            return cached, None

    if args.build:
        log(f"building {info['short']} in {args.build_dir}")
        t0 = time.time()
        if not build(
            args.repo,
            args.build_dir,
            args.encoder,
            args.jobs,
            args.state_dir / "logs" / "build.log",
            args.verbose,
            args.cmake_arg,
        ):
            return None, (EXIT_SKIP, f"cannot build {info['short']}")
        log(f"built in {time.time() - t0:.0f}s")

    tools, missing = wivrn_session.resolve_toolchain(args)
    if missing:
        # Missing prerequisites are the operator's problem, not the commit's: aborting is
        # right, skipping would quietly declare the whole range untestable.
        return None, (EXIT_ABORT, "missing prerequisites: " + "; ".join(missing))

    warn_unstable_host(host_info())
    try:
        rec = measure(args, tools, info, args.signature, settings)
    except SessionError as e:
        return None, (EXIT_SKIP, f"{info['short']}: {e.kind}: {e}")

    if not info["dirty"]:
        save_record(args.state_dir, rec)
    if args.max_spread_pct and rec["spread_pct"] > args.max_spread_pct:
        return rec, (
            EXIT_SKIP,
            (
                f"run-to-run spread {rec['spread_pct']:.1f}% exceeds "
                f"--max-spread-pct {args.max_spread_pct:g}; measurement not trustworthy"
            ),
        )
    return rec, None


def cmd_run(args):
    if args.repo is None:
        die(EXIT_ABORT, "not inside a git repository (pass --repo)")
    settings_for(args)
    rec, failure = prepare(args, for_bisect=False)
    if failure:
        code, msg = failure
        err(msg)
        return code
    baseline = None
    if args.vs:
        baseline = load_record(args.state_dir, git(args.repo, "rev-parse", args.vs), args.signature)
        if not baseline:
            err(f"no recorded measurement for {args.vs} with these settings; skipping comparison")
    if args.json:
        print(json.dumps(rec, indent=2, sort_keys=True))
    else:
        print_record(rec, baseline)
    return EXIT_GOOD


def resolve_threshold(args):
    """Work out the ms value above which a commit counts as bad.

    Explicit --threshold-ms wins. Otherwise the recorded value for --good, either widened by
    --tolerance-pct or, when --bad is also recorded, split geometrically between the two — the
    midpoint on a ratio scale, so it sits the same relative distance from each end.
    """
    if args.threshold_ms:
        return args.threshold_ms, "--threshold-ms"
    if not args.good:
        return None, "pass --threshold-ms, or --good <rev> (measured with 'run' first)"
    good = load_record(args.state_dir, git(args.repo, "rev-parse", args.good), args.signature)
    if not good:
        return None, (
            f"no recorded measurement for --good {args.good} with these settings "
            f"(signature {args.signature}); measure it first: {PROG} run"
        )
    if args.bad:
        bad = load_record(args.state_dir, git(args.repo, "rev-parse", args.bad), args.signature)
        if bad:
            t = (good["value_ms"] * bad["value_ms"]) ** 0.5
            return t, (
                f"midpoint of good {good['short']} {good['value_ms']:.3f} ms and "
                f"bad {bad['short']} {bad['value_ms']:.3f} ms"
            )
        err(f"no recorded measurement for --bad {args.bad}; falling back to --tolerance-pct")
    t = good["value_ms"] * (1 + args.tolerance_pct / 100)
    return t, (
        f"good {good['short']} {good['value_ms']:.3f} ms + {args.tolerance_pct:g}% tolerance"
    )


def cmd_bisect(args):
    if args.repo is None:
        die(EXIT_ABORT, "not inside a git repository (pass --repo)")
    settings_for(args)
    threshold, how = resolve_threshold(args)
    if threshold is None:
        die(EXIT_ABORT, how)

    rec, failure = prepare(args, for_bisect=True)
    if failure:
        code, msg = failure
        err(msg)
        err({EXIT_SKIP: "-> skip", EXIT_ABORT: "-> abort"}.get(code, "-> bad"))
        return code

    print_record(rec)
    value = rec["value_ms"]
    slower = value > threshold
    bad = slower if args.bad_when == "slower" else not slower
    print()
    log(f"threshold {threshold:.3f} ms ({how})")
    log(
        f"{rec['short']}: {value:.3f} ms is "
        f"{'above' if slower else 'at or below'} it -> "
        f"{'BAD' if bad else 'GOOD'}"
    )
    return EXIT_BAD if bad else EXIT_GOOD


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def add_measure_args(p):
    p.add_argument(
        "--encoder",
        default="vulkan",
        choices=wivrn_session.ALL_ENCODERS,
        help="encoder to benchmark (default: %(default)s)",
    )
    p.add_argument("--codec", help="video codec (default: the encoder's own default)")
    p.add_argument(
        "--duration", type=float, default=20.0, help="seconds streamed per run (default: 20)"
    )
    p.add_argument(
        "--repeat", type=int, default=3, help="sessions per commit, median wins (default: 3)"
    )
    p.add_argument(
        "--warmup",
        type=int,
        default=120,
        help="leading frames discarded from every run (default: 120)",
    )
    p.add_argument("--slice", help="slice to measure (default: the encoder's own encode slice)")
    p.add_argument(
        "--stat", default="p50", choices=STATS, help="statistic over that slice (default: p50)"
    )
    # Pinned, not left to the build: a commit that changes a default must not read as a
    # performance change.
    p.add_argument(
        "--bitrate", type=int, default=50_000_000, help="stream bitrate in bps (default: 50000000)"
    )
    p.add_argument(
        "--eye-width",
        type=int,
        default=wivrn_session.REFERENCE_EYE_WIDTH,
        help="per-eye render width; simulates a headset's panel resolution (default: %(default)s)",
    )
    p.add_argument(
        "--eye-height",
        type=int,
        default=wivrn_session.REFERENCE_EYE_HEIGHT,
        help="per-eye render height (default: %(default)s)",
    )
    p.add_argument(
        "--resolution-scale",
        type=float,
        default=wivrn_session.REFERENCE_RESOLUTION_SCALE,
        help="extra scale on top of --eye-width/--eye-height (default: %(default)s)",
    )
    p.add_argument(
        "--stream-scale",
        type=float,
        default=wivrn_session.REFERENCE_STREAM_SCALE,
        help="encoded resolution, as a fraction of the render resolution (default: %(default)s)",
    )
    p.add_argument("--refresh-rate", type=float, help="preferred refresh rate (default: headset's)")
    p.add_argument("--fps-divider", type=int, default=1, help="frame rate divider (default: 1)")
    p.add_argument(
        "--graphics", default="Vulkan2", help="hello_xr graphics API (default: %(default)s)"
    )
    p.add_argument(
        "--max-spread-pct",
        type=float,
        default=0.0,
        help="skip the commit if the runs disagree by more than this (default: 0 = never)",
    )
    p.add_argument(
        "--keep-traces",
        default="median",
        choices=("median", "all", "none"),
        help="which .pftrace files to keep per commit (default: median)",
    )
    p.add_argument("--startup-timeout", type=float, default=20.0, help="server startup timeout")
    p.add_argument("--trace-buffer-kb", type=int, default=131072, help="Perfetto ring buffer KiB")
    p.add_argument("--no-build", dest="build", action="store_false", help="use the existing build")
    p.add_argument(
        "--cmake-arg",
        action="append",
        default=[],
        metavar="-DX=Y",
        help="extra flag for the configure step, repeatable "
        "(for per-machine choices such as -DWIVRN_USE_SYSTEM_LIBKTX=OFF)",
    )
    p.add_argument("-j", "--jobs", type=int, help="build parallelism")
    p.add_argument("-v", "--verbose", action="store_true", help="stream build output")
    p.add_argument("--allow-dirty", action="store_true", help="measure an uncommitted worktree")
    p.add_argument(
        "--allow-in-tree", action="store_true", help="run from inside the repository under test"
    )
    wivrn_session.add_toolchain_args(p, DEFAULT_BUILD_DIR)


def main():
    parser = argparse.ArgumentParser(
        prog=PROG,
        description="Per-commit benchmark of the WiVRn video encoder, for git bisect.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--repo", type=Path, help="repository under test (default: from cwd)")
    parser.add_argument(
        "--state-dir",
        type=Path,
        default=default_state_dir(),
        help="results, traces and logs (default: %(default)s)",
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("bootstrap", help="copy the harness somewhere a bisect cannot touch it")
    p.set_defaults(func=cmd_bootstrap)

    p = sub.add_parser("doctor", help="check prerequisites and machine stability")
    add_measure_args(p)
    p.set_defaults(func=cmd_doctor, reuse=False)

    p = sub.add_parser("run", help="build and measure the checked-out commit")
    add_measure_args(p)
    p.add_argument("--vs", metavar="REV", help="also print the delta against a recorded commit")
    p.add_argument("--reuse", action="store_true", help="reuse a recorded measurement if present")
    p.add_argument("--json", action="store_true", help="print the raw record")
    p.set_defaults(func=cmd_run)

    p = sub.add_parser("bisect", help="measure and emit git bisect exit codes")
    add_measure_args(p)
    p.add_argument("--threshold-ms", type=float, help="above this value the commit is bad")
    p.add_argument("--good", metavar="REV", help="recorded commit to derive the threshold from")
    p.add_argument(
        "--bad", metavar="REV", help="recorded regressed commit, for a midpoint threshold"
    )
    p.add_argument(
        "--tolerance-pct",
        type=float,
        default=10.0,
        help="how much slower than --good still counts as good (default: 10)",
    )
    p.add_argument(
        "--bad-when",
        default="slower",
        choices=("slower", "faster"),
        help="which direction is the regression (default: slower)",
    )
    p.add_argument("--no-reuse", dest="reuse", action="store_false", help="always re-measure")
    p.set_defaults(func=cmd_bisect, reuse=True)

    p = sub.add_parser("report", help="tabulate recorded measurements, grouped by settings")
    p.add_argument("--signature", help="show only this measurement-settings group")
    p.set_defaults(func=cmd_report)

    args = parser.parse_args()
    args.repo = args.repo or repo_root(Path.cwd())
    if args.repo:
        args.repo = Path(args.repo).resolve()
    if hasattr(args, "build_dir"):
        if args.build_dir == Path(DEFAULT_BUILD_DIR):
            # Untouched default: give each encoder its own tree, so switching between them
            # is a rebuild of nothing rather than of every encoder source.
            args.build_dir = Path(f"{args.build_dir}-{args.encoder}")
        if not args.build_dir.is_absolute() and args.repo:
            args.build_dir = args.repo / args.build_dir
    args.state_dir = args.state_dir.expanduser().resolve()

    try:
        return args.func(args)
    except RuntimeError as e:
        die(EXIT_ABORT, str(e))


if __name__ == "__main__":
    # Route signals through sys.exit so the session cleanup in wivrn_session runs.
    import signal

    def _sig(signum, _frame):
        sys.exit(128 + signum)

    signal.signal(signal.SIGTERM, _sig)
    signal.signal(signal.SIGINT, _sig)
    sys.exit(main())
