#!/usr/bin/env python3
"""One-command Perfetto capture for wivrn-server.

Downloads tracebox on first run, starts traced (+ traced_probes if the config
uses ftrace) on a user-writable socket pair, captures a trace, then stops the
helpers it started. wivrn-server is launched separately with
WIVRN_TRACING=true — its trace init picks the same default socket path this
script uses, so no PERFETTO_PRODUCER_SOCK_NAME export is needed.

Usage:
  tools/perfetto/wivrn_capture.py                  # 15s, WiVRn-only (default)
  tools/perfetto/wivrn_capture.py --full           # full-system capture
  tools/perfetto/wivrn_capture.py -c custom.pbtx   # any perfetto config
  tools/perfetto/wivrn_capture.py --cpu-only       # strip ftrace from config
  tools/perfetto/wivrn_capture.py --duration-ms 30000 -o my.pftrace
"""

import argparse
import atexit
import json
import os
import re
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time
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


def detect_encoder():
    """Return the encoder tag from the WiVRn config, or 'auto' on any failure."""
    cfg_file = xdg("XDG_CONFIG_HOME", ".config") / "wivrn" / "config.json"
    if not cfg_file.is_file():
        return "auto"
    try:
        data = json.loads(cfg_file.read_text())
        enc = data.get("encoder")
        if enc is None:
            return "auto"
        if isinstance(enc, str):
            return enc
        if isinstance(enc, dict):
            return enc.get("encoder", "auto")
        if isinstance(enc, list):
            names = []
            for e in enc:
                if isinstance(e, dict):
                    names.append(e.get("encoder", ""))
                elif isinstance(e, str):
                    names.append(e)
            unique = list(dict.fromkeys(n for n in names if n))
            if len(unique) == 1:
                return unique[0]
            if unique:
                return ",".join(unique)
        return "auto"
    except Exception:
        return "auto"


def strip_ftrace_blocks(text):
    """Remove any data_sources { ... } block containing name: "linux.ftrace"."""
    lines = text.splitlines(keepends=True)
    result = []
    in_block = False
    depth = 0
    buf = []
    for line in lines:
        if not in_block:
            if re.match(r'^data_sources\s*\{', line):
                in_block = True
                depth = 1
                buf = [line]
            else:
                result.append(line)
        else:
            buf.append(line)
            depth += line.count('{') - line.count('}')
            if depth == 0:
                if not re.search(r'name:\s*"linux\.ftrace"', ''.join(buf)):
                    result.extend(buf)
                in_block = False
                buf = []
    return ''.join(result)


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
                        print(f"\r  {downloaded/1e6:.1f} / {total/1e6:.1f} MB ({pct}%)",
                              end="", flush=True)
                    else:
                        print(f"\r  {downloaded/1e6:.1f} MB", end="", flush=True)
        print()
    except Exception as e:
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
    r = subprocess.run(["pgrep", "-x", name], capture_output=True, text=True)
    return r.stdout.split() if r.returncode == 0 else []


def socket_listening(sock_path):
    """Return True if a Unix socket at sock_path is bound and listening."""
    r = subprocess.run(["ss", "-lxH", "src", str(sock_path)],
                       capture_output=True, text=True)
    return bool(r.stdout.strip())


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
        epilog=(
            "wivrn-server picks the same default socket on init, so just:\n"
            "  WIVRN_TRACING=true wivrn-server\n\n"
            f"Override with PERFETTO_PRODUCER_SOCK_NAME if you want a custom path;\n"
            f"this script's default is {producer_sock}."
        ),
    )
    parser.add_argument("-c", "--config", metavar="PATH",
                        help="Perfetto config (default: wivrn_trace_cfg.pbtx — "
                             "WiVRn track_event categories only, no ftrace)")
    parser.add_argument("--full", action="store_true",
                        help="Shorthand for -c wivrn_full_trace_cfg.pbtx "
                             "(every category + broader ftrace)")
    parser.add_argument("-o", "--output", metavar="PATH",
                        help="Output .pftrace path (default: wivrn-<encoder>-<ts>.pftrace)")
    parser.add_argument("--cpu-only", action="store_true",
                        help="Strip any linux.ftrace data source from the config "
                             "(no traced_probes / no sudo needed)")
    parser.add_argument("--duration-ms", metavar="N", type=int,
                        help="Override duration_ms in the config")
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
        ts = datetime.now().strftime("%Y%m%d-%H%M%S")
        out = Path(f"wivrn-{detect_encoder()}-{ts}.pftrace")

    if not cfg.is_file():
        sys.exit(f"wivrn_capture: missing config: {cfg}")
    log(f"using config {cfg}")

    ensure_tracebox(tracebox)

    # Build the runtime config (strip ftrace / override duration as needed).
    cfg_text = cfg.read_text()
    if args.cpu_only:
        cfg_text = strip_ftrace_blocks(cfg_text)
    if args.duration_ms is not None:
        cfg_text = re.sub(r'^duration_ms: \d+', f'duration_ms: {args.duration_ms}',
                          cfg_text, flags=re.MULTILINE)

    run_cfg_fd, run_cfg_path = tempfile.mkstemp(suffix=".pbtx")
    run_cfg = Path(run_cfg_path)
    try:
        os.write(run_cfg_fd, cfg_text.encode())
    finally:
        os.close(run_cfg_fd)

    use_ftrace = needs_ftrace(cfg_text)
    sudo = ["sudo", "-E"] if use_ftrace else []
    user = os.environ.get("USER", os.getlogin())

    log(f"producer socket {producer_sock}")
    print("wivrn_capture: ----------------------------------------------------------------")
    print("wivrn_capture:   wivrn-server: XRT_LOG=info WIVRN_TRACING=true wivrn-server")
    print("wivrn_capture:")
    print("wivrn_capture:   IMPORTANT: percetto initializes inside the per-session FORK")
    print("wivrn_capture:   (xrt_instance_create), not in the daemon. You must have a")
    print("wivrn_capture:   client connected and streaming during the capture window —")
    print("wivrn_capture:   otherwise the trace is empty. Start streaming first, then")
    print("wivrn_capture:   run this wrapper, and stay in-session for the full duration.")
    print("wivrn_capture:")
    print("wivrn_capture:   XRT_LOG=info raises Monado's default (warn) so you can see")
    print('wivrn_capture:   the "wivrn::trace: init complete" line that confirms percetto')
    print("wivrn_capture:   actually connected.")
    print("wivrn_capture: ----------------------------------------------------------------")

    # Clean up stale sockets from prior crashed runs; traced won't reuse them.
    producer_sock.unlink(missing_ok=True)
    consumer_sock.unlink(missing_ok=True)

    started_traced = False
    started_probes = False

    def cleanup():
        run_cfg.unlink(missing_ok=True)
        if started_probes:
            subprocess.run([*sudo, "pkill", "-x", "traced_probes"], capture_output=True)
        if started_traced:
            subprocess.run([*sudo, "pkill", "-x", "traced"], capture_output=True)
            producer_sock.unlink(missing_ok=True)
            consumer_sock.unlink(missing_ok=True)

    atexit.register(cleanup)

    # Route SIGTERM / SIGINT through sys.exit so atexit cleanup runs.
    def _sig(signum, _frame):
        sys.exit(128 + signum)
    signal.signal(signal.SIGTERM, _sig)
    signal.signal(signal.SIGINT, _sig)

    # Find any existing traced and check whether it owns the right sockets.
    existing = pgrep("traced")
    if existing:
        if not socket_listening(consumer_sock):
            pids = ", ".join(existing)
            err(f"warning: an existing traced (pid {pids}) is")
            err(f"  running but not listening on {consumer_sock}.")
            err(f"  Kill it (kill {pids}) and re-run, or this capture will fail.")
            sys.exit(1)
    else:
        # traced takes socket paths via env vars, not CLI flags (only
        # --background / --set-socket-permissions / --version are accepted).
        env = {**os.environ,
               "PERFETTO_PRODUCER_SOCK_NAME": str(producer_sock),
               "PERFETTO_CONSUMER_SOCK_NAME": str(consumer_sock)}
        traced_cmd = [*sudo, str(tracebox), "traced", "--background"]
        if use_ftrace:
            traced_cmd += ["--set-socket-permissions", f"{user}:0660:{user}:0660"]
        subprocess.run(traced_cmd, env=env, check=True)
        started_traced = True
        time.sleep(0.3)

    if use_ftrace and not pgrep("traced_probes"):
        env = {**os.environ,
               "PERFETTO_PRODUCER_SOCK_NAME": str(producer_sock)}
        subprocess.run([*sudo, str(tracebox), "traced_probes", "--background"],
                       env=env, check=True)
        started_probes = True
        time.sleep(0.3)

    if not producer_sock.is_socket():
        err(f"producer socket {producer_sock} does not exist —")
        err("  traced did not bind to it. Check 'pgrep -a traced' and the env")
        err("  vars PERFETTO_PRODUCER_SOCK_NAME / PERFETTO_CONSUMER_SOCK_NAME.")
        sys.exit(1)

    log(f"capturing → {out}")
    env = {**os.environ,
           "PERFETTO_PRODUCER_SOCK_NAME": str(producer_sock),
           "PERFETTO_CONSUMER_SOCK_NAME": str(consumer_sock)}
    subprocess.run([str(tracebox), "perfetto", "-c", str(run_cfg), "--txt", "-o", str(out)],
                   env=env, check=True)

    print()
    log(f"wrote {out}")
    trace_bytes = out.stat().st_size if out.exists() else 0
    log(f"trace size: {trace_bytes} bytes")

    if trace_bytes < 4096:
        err("WARNING: the trace looks empty.")
        err("  Likely cause: no wivrn-server per-session child was alive during capture,")
        err("  so percetto never initialized. Check, with XRT_LOG=info (Monado's default")
        err("  is warn, which hides these lines):")
        err("    * wivrn-server was started: XRT_LOG=info WIVRN_TRACING=true wivrn-server")
        err("    * A client is connected AND streaming during the capture window")
        err('    * The wivrn-server log shows: "wivrn::trace: init complete"')
        err("    * The wivrn-server log shows the matching socket:")
        err(f'        "wivrn::trace: producer socket {producer_sock}"')
    else:
        log("open in https://ui.perfetto.dev")


if __name__ == "__main__":
    main()
