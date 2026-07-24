#!/usr/bin/env python3
"""One-command Perfetto capture for wivrn-server (run with WIVRN_TRACING=system).

wivrn_capture.py                 # WiVRn-only (default)
wivrn_capture.py --full          # full-system capture
wivrn_capture.py -c custom.pbtx  # any perfetto config
wivrn_capture.py --cpu-only      # strip ftrace from config
wivrn_capture.py --duration-ms 30000 -o my.pftrace
"""

import argparse
import atexit
import os
import re
import signal
import stat
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from datetime import datetime
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
TRACEBOX_URL = "https://get.perfetto.dev/tracebox"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def xdg(var, default_rel):
    """Resolve an XDG base directory."""
    val = os.environ.get(var)
    return Path(val) if val else Path.home() / default_rel


def strip_ftrace_blocks(text):
    """Remove any data_sources { ... } block containing name: "linux.ftrace"."""
    lines = text.splitlines(keepends=True)
    result = []
    in_block = False
    depth = 0
    buf = []
    for line in lines:
        if not in_block:
            if re.match(r"^data_sources\s*\{", line):
                in_block = True
                depth = 1
                buf = [line]
            else:
                result.append(line)
        else:
            buf.append(line)
            depth += line.count("{") - line.count("}")
            if depth == 0:
                if not re.search(r'name:\s*"linux\.ftrace"', "".join(buf)):
                    result.extend(buf)
                in_block = False
                buf = []
    return "".join(result)


def needs_ftrace(text):
    return bool(re.search(r'name:\s*"linux\.ftrace"', text))


def download_tracebox(url, dest):
    """Download url to dest with a simple progress indicator."""
    print(f"wivrn_capture: downloading tracebox to {dest}")
    try:
        with urllib.request.urlopen(url) as resp:
            total = int(resp.headers.get("Content-Length", 0))
            downloaded = 0
            with open(dest, "wb") as f:
                while True:
                    chunk = resp.read(65536)
                    if not chunk:
                        break
                    f.write(chunk)
                    downloaded += len(chunk)
                    if total:
                        pct = downloaded * 100 // total
                        print(
                            f"\r  {downloaded / 1e6:.1f} / {total / 1e6:.1f} MB ({pct}%)",
                            end="",
                            flush=True,
                        )
                    else:
                        print(f"\r  {downloaded / 1e6:.1f} MB", end="", flush=True)
        print()
    except (OSError, urllib.error.URLError) as e:
        Path(dest).unlink(missing_ok=True)
        sys.exit(f"wivrn_capture: download failed: {e}")


def ensure_tracebox(path):
    if path.is_file() and os.access(path, os.X_OK):
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    download_tracebox(TRACEBOX_URL, path)
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def pgrep(name):
    """Return list of matching PIDs (as strings), or []."""
    r = subprocess.run(["pgrep", "-x", name], capture_output=True, text=True, check=False)
    return r.stdout.split() if r.returncode == 0 else []


def socket_listening(sock_path):
    """Return True if a Unix socket at sock_path is bound and listening."""
    r = subprocess.run(
        ["ss", "-lxH", "src", str(sock_path)], capture_output=True, text=True, check=False
    )
    return bool(r.stdout.strip())


def wait_for_producer(tracebox, env, timeout_s=20):
    """True once a track_event data source registers (wivrn-server connected)."""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        r = subprocess.run(
            [str(tracebox), "perfetto", "--query"],
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )
        if "track_event" in r.stdout:
            return True
        time.sleep(0.5)
    return False


def log(msg):
    print(f"wivrn_capture: {msg}")


def err(msg):
    print(f"wivrn_capture: {msg}", file=sys.stderr)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main():
    cache_dir = xdg("XDG_CACHE_HOME", ".cache") / "wivrn"
    tracebox = cache_dir / "tracebox"
    sock_dir = Path(os.environ.get("XDG_RUNTIME_DIR", "/tmp"))
    producer_sock = sock_dir / "wivrn-perfetto-producer.sock"
    consumer_sock = sock_dir / "wivrn-perfetto-consumer.sock"

    parser = argparse.ArgumentParser(
        prog=Path(sys.argv[0]).name,
        description="One-command Perfetto capture for wivrn-server.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"Run: WIVRN_TRACING=system wivrn-server  (socket: {producer_sock})",
    )
    parser.add_argument(
        "-c",
        "--config",
        metavar="PATH",
        help="Perfetto config (default: wivrn_trace_cfg.pbtx — "
        "WiVRn track_event categories only, no ftrace)",
    )
    parser.add_argument(
        "--full",
        action="store_true",
        help="Shorthand for -c wivrn_full_trace_cfg.pbtx (every category + broader ftrace)",
    )
    parser.add_argument(
        "-o", "--output", metavar="PATH", help="Output .pftrace path (default: wivrn-<ts>.pftrace)"
    )
    parser.add_argument(
        "--cpu-only",
        action="store_true",
        help="Strip any linux.ftrace data source from the config "
        "(no traced_probes / no sudo needed)",
    )
    parser.add_argument(
        "--duration-ms", metavar="N", type=int, help="Override duration_ms in the config"
    )
    args = parser.parse_args()

    # Resolve config.
    if args.full:
        cfg = SCRIPT_DIR / "wivrn_full_trace_cfg.pbtx"
    elif args.config:
        cfg = Path(args.config)
    else:
        cfg = SCRIPT_DIR / "wivrn_trace_cfg.pbtx"

    # Resolve output filename.
    if args.output:
        out = Path(args.output)
    else:
        ts = datetime.now().astimezone().strftime("%Y%m%d-%H%M%S")
        out = Path(f"wivrn-{ts}.pftrace")

    if not cfg.is_file():
        sys.exit(f"wivrn_capture: missing config: {cfg}")
    log(f"using config {cfg}")

    ensure_tracebox(tracebox)

    # Build the runtime config (strip ftrace / override duration as needed).
    cfg_text = cfg.read_text()
    if args.cpu_only:
        cfg_text = strip_ftrace_blocks(cfg_text)
    if args.duration_ms is not None:
        cfg_text = re.sub(
            r"^duration_ms: \d+", f"duration_ms: {args.duration_ms}", cfg_text, flags=re.MULTILINE
        )

    run_cfg_fd, run_cfg_path = tempfile.mkstemp(suffix=".pbtx")
    run_cfg = Path(run_cfg_path)
    try:
        os.write(run_cfg_fd, cfg_text.encode())
    finally:
        os.close(run_cfg_fd)

    use_ftrace = needs_ftrace(cfg_text)
    sudo = ["sudo", "-E"] if use_ftrace else []
    user = os.environ.get("USER", os.getlogin())

    log("run a client and stream during capture, or the trace is empty")

    # Clean up stale sockets from crashed runs; traced won't reuse them.
    producer_sock.unlink(missing_ok=True)
    consumer_sock.unlink(missing_ok=True)

    started_traced = False
    started_probes = False

    def cleanup():
        run_cfg.unlink(missing_ok=True)
        if started_probes:
            subprocess.run(
                [*sudo, "pkill", "-x", "traced_probes"], capture_output=True, check=False
            )
        if started_traced:
            subprocess.run([*sudo, "pkill", "-x", "traced"], capture_output=True, check=False)
            producer_sock.unlink(missing_ok=True)
            consumer_sock.unlink(missing_ok=True)

    atexit.register(cleanup)

    # Route SIGTERM / SIGINT through sys.exit so atexit cleanup runs.
    def _sig(signum, _frame):
        sys.exit(128 + signum)

    signal.signal(signal.SIGTERM, _sig)
    signal.signal(signal.SIGINT, _sig)

    # Reuse an existing traced only if it owns our sockets.
    existing = pgrep("traced")
    if existing:
        if not socket_listening(consumer_sock):
            pids = ", ".join(existing)
            err(f"traced (pid {pids}) not on {consumer_sock}; kill it and re-run")
            sys.exit(1)
    else:
        # traced reads socket paths from env vars, not CLI flags.
        env = {
            **os.environ,
            "PERFETTO_PRODUCER_SOCK_NAME": str(producer_sock),
            "PERFETTO_CONSUMER_SOCK_NAME": str(consumer_sock),
        }
        traced_cmd = [*sudo, str(tracebox), "traced", "--background"]
        if use_ftrace:
            traced_cmd += ["--set-socket-permissions", f"{user}:0660:{user}:0660"]
        subprocess.run(traced_cmd, env=env, check=True)
        started_traced = True
        time.sleep(0.3)

    if use_ftrace and not pgrep("traced_probes"):
        env = {**os.environ, "PERFETTO_PRODUCER_SOCK_NAME": str(producer_sock)}
        subprocess.run([*sudo, str(tracebox), "traced_probes", "--background"], env=env, check=True)
        started_probes = True
        time.sleep(0.3)

    if not producer_sock.is_socket():
        err(f"traced did not bind {producer_sock}")
        sys.exit(1)

    env = {
        **os.environ,
        "PERFETTO_PRODUCER_SOCK_NAME": str(producer_sock),
        "PERFETTO_CONSUMER_SOCK_NAME": str(consumer_sock),
    }

    # Wait for the producer; abort rather than capture an empty trace.
    log("waiting for producer…")
    if not wait_for_producer(tracebox, env):
        err(
            "no track_event producer after 20s — is wivrn-server running with "
            "WIVRN_TRACING and a client streaming?"
        )
        err("(if it just worked, restart wivrn-server — the producer doesn't reconnect)")
        sys.exit(1)

    log(f"capturing → {out}")
    subprocess.run(
        [str(tracebox), "perfetto", "-c", str(run_cfg), "--txt", "-o", str(out)],
        env=env,
        check=True,
    )

    print()
    trace_bytes = out.stat().st_size if out.exists() else 0
    log(f"wrote {out} ({trace_bytes} bytes)")

    if trace_bytes < 4096:
        err("trace looks empty — was a client streaming during capture?")
    else:
        log("open in https://ui.perfetto.dev")


if __name__ == "__main__":
    main()
