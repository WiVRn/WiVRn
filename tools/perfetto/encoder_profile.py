#!/usr/bin/env python3
"""Headless per-encoder profiling of the WiVRn pipeline.

Per encoder, runs one streaming session (headless Monado as the client runtime +
wivrn-server auto-launching hello_xr as the frame producer + the wivrn client),
captures out/<enc>.pftrace, then prints a summary and a diff against a baseline.

    cmake --preset profiling && cmake --build build-profiling
    cmake --install build-profiling --prefix <prefix>
    tools/perfetto/encoder_profile.py --duration 15 -o out/
"""

import argparse
import json
import os
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
import pftrace_summary  # noqa: E402  (sibling script; parse()/summarize()/compare() reused)

# Network slices are meaningless under a null-compositor client; drop from summaries.
NETWORK_SLICES = ("SendData",)
ALL_ENCODERS = ("nvenc", "vaapi", "vulkan", "x264")
WIVRN_PORT = 9757

# PCI vendor ID -> the hardware encoder WiVRn can use on that GPU.
GPU_HW_ENCODER = {
    "0x10de": "nvenc",  # NVIDIA
    "0x1002": "vaapi",  # AMD
    "0x8086": "vaapi",  # Intel
}


def detect_encoders():
    """Default encoders: vulkan + x264 everywhere, plus the GPU vendor's hardware
    encoder (from the PCI vendor id under /sys/class/drm) when present."""
    hw = []
    for vendor in sorted(Path("/sys/class/drm").glob("card*/device/vendor")):
        try:
            enc = GPU_HW_ENCODER.get(vendor.read_text().strip().lower())
        except OSError:
            continue
        if enc and enc not in hw:
            hw.append(enc)
    return hw + ["vulkan", "x264"]


def log(msg):
    print(f"encoder_profile: {msg}")


def err(msg):
    print(f"encoder_profile: {msg}", file=sys.stderr)


# ---------------------------------------------------------------------------
# Discovery of binaries and OpenXR manifests
# ---------------------------------------------------------------------------


def find_binary(explicit, build_rel, name):
    """Resolve a binary from an explicit path, the build dir, or PATH."""
    if explicit:
        p = Path(explicit)
        return p if p.is_file() else None
    if build_rel and build_rel.is_file():
        return build_rel
    found = shutil.which(name)
    return Path(found) if found else None


def find_manifest(explicit, filename):
    """Locate an OpenXR runtime manifest (explicit path wins, else common prefixes)."""
    if explicit:
        p = Path(explicit)
        return p if p.is_file() else None
    prefixes = [
        Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")),
        Path.home() / ".local",
        Path("/usr/local/share"),
        Path("/usr/share"),
        Path("/usr/local"),
        Path("/usr"),
    ]
    candidates = []
    for pfx in prefixes:
        candidates.append(pfx / "share/openxr/1" / filename)
        candidates.append(pfx / "openxr/1" / filename)
    for c in candidates:
        if c.is_file():
            return c
    return None


# ---------------------------------------------------------------------------
# Process helpers
# ---------------------------------------------------------------------------


def spawn(cmd, env, logfile, stdin=None):
    """Start a process in its own session (process group) with output to logfile.

    Pass stdin=subprocess.PIPE for monado-service: it epoll_ctl()s stdin, which
    fails on a /dev/null stdin (nohup/CI) but works on a pipe.
    """
    f = open(logfile, "wb")
    proc = subprocess.Popen(
        cmd,
        env=env,
        stdin=stdin,
        stdout=f,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    proc._logfile = f  # keep the handle alive for the process lifetime
    return proc


def stop(proc, sig=signal.SIGTERM, timeout=5.0):
    """Signal a process group and wait for it to exit."""
    if proc is None or proc.poll() is not None:
        return
    try:
        os.killpg(os.getpgid(proc.pid), sig)
    except (ProcessLookupError, PermissionError):
        return
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        except ProcessLookupError:
            pass


def systemctl_user(*args):
    """Best-effort `systemctl --user …`; True on success, False if systemd is absent."""
    try:
        return (
            subprocess.run(
                ["systemctl", "--user", *args], capture_output=True, text=True
            ).returncode
            == 0
        )
    except FileNotFoundError:
        return False


def wait_port(port, timeout=15.0):
    """True once something accepts TCP connections on localhost:port."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(0.5)
            if s.connect_ex(("127.0.0.1", port)) == 0:
                return True
        time.sleep(0.3)
    return False


# ---------------------------------------------------------------------------
# One session per encoder
# ---------------------------------------------------------------------------


def run_encoder(enc, codec, args, wivrn_manifest, monado_manifest):
    """Run a single streaming session for one encoder; return the .pftrace path or None."""
    out = args.output / f"{enc}.pftrace"
    out.unlink(missing_ok=True)

    scratch = Path(tempfile.mkdtemp(prefix=f"wivrn-prof-{enc}-"))
    encoder_cfg = {"encoder": enc}
    if codec:
        encoder_cfg["codec"] = codec
    # -g: hello_xr needs a graphics API. sleep infinity: hello_xr quits on stdin EOF,
    # and the systemd unit's stdin is /dev/null, so keep it open with a never-ending pipe.
    hello_xr_cmd = (
        f"sleep infinity | exec {shlex.quote(str(args.hello_xr))} -g {shlex.quote(args.graphics)}"
    )
    config = {
        "encoder": encoder_cfg,
        "application": ["/bin/sh", "-c", hello_xr_cmd],
        "publish-service": "avahi",
    }
    cfg_dir = scratch / "wivrn"
    cfg_dir.mkdir(parents=True)
    (cfg_dir / "config.json").write_text(json.dumps(config, indent=2))

    # Absolute manifest: the loader (and the systemd unit below) resolves it from
    # its own cwd, so a relative path would not survive the launch.
    wivrn_manifest = Path(wivrn_manifest).resolve()

    monado = server = client = None

    def cleanup():
        # Client first: disconnecting flushes the .pftrace (trace::flush_session).
        stop(client, signal.SIGINT)
        # Backstop for hello_xr, which the server forks into its own process group.
        subprocess.run(["pkill", "-x", "hello_xr"], capture_output=True)
        stop(server)
        stop(monado)
        # Undo the systemd user-manager env injection and clear failed app units.
        systemctl_user("unset-environment", "XR_RUNTIME_JSON")
        systemctl_user("reset-failed", "wivrn-application-*.service")
        shutil.rmtree(scratch, ignore_errors=True)

    try:
        log(f"[{enc}] starting headless Monado (client runtime)")
        monado_env = {**os.environ, "XRT_COMPOSITOR_NULL": "1"}
        monado = spawn(
            [str(args.monado_service)],
            monado_env,
            args.output / f"{enc}-monado.log",
            stdin=subprocess.PIPE,  # epoll-able stdin; see spawn() docstring
        )
        time.sleep(2.0)
        if monado.poll() is not None:
            err(f"[{enc}] monado-service exited early; see {enc}-monado.log")
            return None

        # The server launches hello_xr as a systemd unit, which inherits the user
        # manager's environment, not the server's — so set XR_RUNTIME_JSON there.
        systemctl_user("set-environment", f"XR_RUNTIME_JSON={wivrn_manifest}")

        log(f"[{enc}] starting wivrn-server (auto-launches hello_xr)")
        server_env = {
            **os.environ,
            "XDG_CONFIG_HOME": str(scratch),
            "XR_RUNTIME_JSON": str(wivrn_manifest),
            "WIVRN_TRACING": "inprocess",
            "WIVRN_TRACING_FILE": str(out),
        }
        server = spawn(
            [str(args.server), "--no-encrypt", "--no-manage-active-runtime"],
            server_env,
            args.output / f"{enc}-server.log",
        )
        if not wait_port(WIVRN_PORT, timeout=args.startup_timeout):
            err(f"[{enc}] server not listening on {WIVRN_PORT}; see {enc}-server.log")
            return None

        log(f"[{enc}] starting client (autoconnect)")
        client_env = {
            **os.environ,
            "XR_RUNTIME_JSON": str(monado_manifest),
            "WIVRN_AUTOCONNECT": "1",
        }
        client = spawn([str(args.client)], client_env, args.output / f"{enc}-client.log")

        log(f"[{enc}] streaming for {args.duration}s")
        deadline = time.time() + args.duration
        while time.time() < deadline:
            if server.poll() is not None:
                err(f"[{enc}] server exited during run; see {enc}-server.log")
                return None
            if client.poll() is not None:
                err(f"[{enc}] client exited during run; see {enc}-client.log")
                return None
            time.sleep(0.5)
    finally:
        cleanup()

    # The trace is flushed asynchronously on disconnect; give it a moment to land.
    for _ in range(20):
        if out.is_file() and out.stat().st_size > 0:
            break
        time.sleep(0.25)

    if not out.is_file() or out.stat().st_size == 0:
        err(f"[{enc}] no trace written (empty pipeline?); see {enc}-server.log")
        return None

    # A frameless session still flushes a header-only trace; require encode slices.
    try:
        durations, _span, _size = pftrace_summary.parse(out)
    except Exception:
        durations = {}
    if not durations.get("encode"):
        err(
            f"[{enc}] trace has no encode slices — encoder produced no frames "
            f"(not built with this encoder, or unsupported by the GPU?); see {enc}-server.log"
        )
        return None

    log(f"[{enc}] wrote {out} ({out.stat().st_size:,} bytes)")
    return out


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(
        prog=Path(sys.argv[0]).name,
        description="Headless per-encoder profiling of the full WiVRn pipeline.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--encoders",
        default=",".join(detect_encoders()),
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
        "--build-dir",
        type=Path,
        default=Path("build-profiling"),
        help="build tree holding wivrn-server + client (default: build-profiling)",
    )
    parser.add_argument("--server", help="path to wivrn-server (default: from --build-dir)")
    parser.add_argument("--client", help="path to the wivrn client (default: from --build-dir)")
    parser.add_argument(
        "--monado-service", help="path to standalone monado-service (default: from PATH)"
    )
    parser.add_argument("--hello-xr", help="path to hello_xr (default: from PATH)")
    parser.add_argument(
        "--graphics",
        default="Vulkan2",
        help="hello_xr graphics API: -g argument (default: Vulkan2)",
    )
    parser.add_argument(
        "--wivrn-manifest",
        help="openxr_wivrn.json for hello_xr (default: auto-detect installed prefixes)",
    )
    parser.add_argument(
        "--monado-manifest",
        help="openxr_monado.json for the client (default: auto-detect installed prefixes)",
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
    args = parser.parse_args()

    encoders = [e.strip() for e in args.encoders.split(",") if e.strip()]
    unknown = [e for e in encoders if e not in ALL_ENCODERS]
    if unknown:
        sys.exit(f"encoder_profile: unknown encoder(s): {', '.join(unknown)}")

    # Resolve binaries.
    args.server = find_binary(args.server, args.build_dir / "server/wivrn-server", "wivrn-server")
    args.client = find_binary(args.client, args.build_dir / "bin/wivrn", "wivrn")
    args.monado_service = find_binary(args.monado_service, None, "monado-service")
    args.hello_xr = find_binary(args.hello_xr, None, "hello_xr")

    wivrn_manifest = find_manifest(args.wivrn_manifest, "openxr_wivrn.json")
    monado_manifest = find_manifest(args.monado_manifest, "openxr_monado.json")

    missing = []
    if not args.server:
        missing.append("wivrn-server (build with: cmake --preset profiling && cmake --build ...)")
    if not args.client:
        missing.append("wivrn client (same profiling build)")
    if not args.monado_service:
        missing.append("monado-service (install standalone Monado)")
    if not args.hello_xr:
        missing.append("hello_xr (github.com/KhronosGroup/OpenXR-SDK-Source)")
    if not wivrn_manifest:
        missing.append(
            "openxr_wivrn.json (cmake --install the profiling build; or --wivrn-manifest)"
        )
    if not monado_manifest:
        missing.append("openxr_monado.json (install standalone Monado; or --monado-manifest)")
    if missing:
        err("missing prerequisites:")
        for m in missing:
            err(f"  - {m}")
        sys.exit(1)

    args.output.mkdir(parents=True, exist_ok=True)
    log(f"server:  {args.server}")
    log(f"client:  {args.client}")
    log(f"monado:  {args.monado_service}  (manifest {monado_manifest})")
    log(f"hello_xr: {args.hello_xr} -g {args.graphics}  (WiVRn manifest {wivrn_manifest})")

    traces = {}
    failed = []
    for enc in encoders:
        try:
            t = run_encoder(enc, args.codec, args, wivrn_manifest, monado_manifest)
        except Exception as e:
            # One encoder blowing up must not abort the rest of the matrix.
            err(f"[{enc}] unexpected error: {e}")
            t = None
        if t:
            traces[enc] = t
        else:
            failed.append(enc)

    if failed:
        err(f"failed encoders (skipped): {', '.join(failed)}")
    if not traces:
        sys.exit("encoder_profile: no successful runs")

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
    # Route signals through sys.exit so per-encoder finally: cleanup runs.
    def _sig(signum, _frame):
        sys.exit(128 + signum)

    signal.signal(signal.SIGTERM, _sig)
    signal.signal(signal.SIGINT, _sig)
    main()
