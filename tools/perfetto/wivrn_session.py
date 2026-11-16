#!/usr/bin/env python3
"""One headless WiVRn streaming session — shared by the profiling and benchmark tools.

A session wires up three processes with no physical headset:

    hello_xr  --(WiVRn runtime)-->  wivrn-server  --(network)-->  wivrn client
                                                                  (Monado, null compositor)

hello_xr renders frames into the encoder under test, the server encodes and streams them, and
the client decodes and drops them. The server writes an in-process Perfetto trace, flushed when
the client disconnects.

Not executable on its own: see encoder_bench.py (per-commit benchmarking) and
encoder_profile.py (per-encoder comparison).
"""

import json
import os
import re
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from pathlib import Path

# Network slices are meaningless under a null-compositor client; drop from summaries.
NETWORK_SLICES = ("SendData",)
ALL_ENCODERS = ("nvenc", "vaapi", "vulkan", "x264")
WIVRN_PORT = 9757
# The unit wivrn-server launches the OpenXR application as.
APP_UNIT_PATTERN = "wivrn-application-*.service"

# PCI vendor ID -> the hardware encoder WiVRn can use on that GPU.
GPU_HW_ENCODER = {
    "0x10de": "nvenc",  # NVIDIA
    "0x1002": "vaapi",  # AMD
    "0x8086": "vaapi",  # Intel
}

# The reference workload, pinned by both harnesses: per-eye render resolution, set through the
# remote driver's display size, which the null compositor reports as the recommended view size.
REFERENCE_EYE_WIDTH = 1920
REFERENCE_EYE_HEIGHT = 1472
REFERENCE_RESOLUTION_SCALE = 1.0
REFERENCE_STREAM_SCALE = 1.0
REFERENCE_BITRATE_BPS = 50_000_000

# Set via XRT_COMPOSITOR_NULL_FPS; Monado's own default is 20.
REFERENCE_REFRESH_RATE_HZ = 90

# Below this the encode is per-frame fixed cost rather than codec work. Checked against what was
# negotiated, which depends on the runtime's recommended view.
MIN_USEFUL_ENCODE_WIDTH = 640

# GPU clock sampling, checked afterwards by check_gpu_clocks(). Encode time tracks the video
# clock, so a run spanning a downclock measures the clock instead of the encoder.
GPU_CLOCK_SAMPLE_INTERVAL = 2.0
GPU_CLOCK_DROP_RATIO = 0.85


def detect_encoders():
    """Encoders worth trying here: vulkan + x264 everywhere, plus the GPU vendor's hardware
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


def log(msg, prog="wivrn"):
    print(f"{prog}: {msg}", flush=True)


def err(msg, prog="wivrn"):
    print(f"{prog}: {msg}", file=sys.stderr, flush=True)


class SessionError(Exception):
    """A session that did not produce a usable trace.

    `kind` is "startup" (server never came up), "crash" (a process died mid-run) or "empty"
    (no trace, or a trace with no encode slices).
    """

    def __init__(self, kind, msg):
        super().__init__(msg)
        self.kind = kind


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


def in_build(path):
    """A manifest generated into the build tree, when it is there."""
    return str(path) if path.is_file() else None


def find_manifest(explicit, filename):
    """Locate an OpenXR runtime manifest (explicit path wins, else common prefixes)."""
    if explicit:
        p = Path(explicit)
        return p if p.is_file() else None
    prefixes = [
        Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")),
        Path.home() / ".local/share",
        Path("/usr/local/share"),
        Path("/usr/share"),
    ]
    for pfx in prefixes:
        candidate = pfx / "openxr/1" / filename
        if candidate.is_file():
            return candidate
    return None


@dataclass
class Toolchain:
    """Everything a session needs to run, all resolved to absolute paths."""

    server: Path
    client: Path
    hello_xr: Path | None
    wivrn_manifest: Path
    monado_manifest: Path

    def describe(self):
        hello_xr = f"{self.hello_xr}" if self.hello_xr else "(overridden by --xr-app)"
        return [
            f"server:   {self.server}",
            f"client:   {self.client}  (runtime manifest {self.monado_manifest})",
            f"hello_xr: {hello_xr}  (WiVRn manifest {self.wivrn_manifest})",
        ]


def add_toolchain_args(parser, default_build_dir):
    """CLI flags for overriding anything resolve_toolchain() would auto-detect.

    --build-dir stays None when not given, so a caller can tell "unset" from a path that
    happens to equal its own default; fill it in before resolve_toolchain().
    """
    parser.add_argument(
        "--build-dir",
        type=Path,
        help=f"build tree holding wivrn-server + client (default: {default_build_dir})",
    )
    parser.add_argument("--server", help="path to wivrn-server (default: from --build-dir)")
    parser.add_argument("--client", help="path to the wivrn client (default: from --build-dir)")
    parser.add_argument("--hello-xr", help="path to hello_xr (default: from PATH)")
    parser.add_argument(
        "--xr-app",
        help='full command to launch instead of hello_xr, e.g. "hello_xr -g Vulkan2" or '
        '"xrgears" (default: hello_xr -g <--graphics>)',
    )
    parser.add_argument(
        "--wivrn-manifest",
        help="openxr_wivrn.json for hello_xr (default: auto-detect installed prefixes)",
    )
    parser.add_argument(
        "--monado-manifest",
        help="openxr_monado.json for the client (default: auto-detect installed prefixes)",
    )


def add_workload_args(parser, default_duration):
    """CLI flags for everything SessionSpec.from_args() reads.

    The workload is pinned here rather than left to the build, so a commit that changes a
    default does not read as a performance change.
    """
    parser.add_argument("--codec", help="video codec (default: the encoder's own default)")
    parser.add_argument(
        "--duration",
        type=float,
        default=default_duration,
        help="seconds streamed per run (default: %(default)s)",
    )
    parser.add_argument(
        "--graphics",
        default="Vulkan2",
        help="hello_xr graphics API: -g argument (default: %(default)s)",
    )
    parser.add_argument(
        "--bitrate",
        type=int,
        default=REFERENCE_BITRATE_BPS,
        help="stream bitrate in bps (default: %(default)s)",
    )
    parser.add_argument(
        "--eye-width",
        type=int,
        default=REFERENCE_EYE_WIDTH,
        help="per-eye render width; simulates a headset's panel resolution (default: %(default)s)",
    )
    parser.add_argument(
        "--eye-height",
        type=int,
        default=REFERENCE_EYE_HEIGHT,
        help="per-eye render height (default: %(default)s)",
    )
    parser.add_argument(
        "--resolution-scale",
        type=float,
        default=REFERENCE_RESOLUTION_SCALE,
        help="extra scale on top of --eye-width/--eye-height (default: %(default)s)",
    )
    parser.add_argument(
        "--stream-scale",
        type=float,
        default=REFERENCE_STREAM_SCALE,
        help="encoded resolution, as a fraction of the render resolution (default: %(default)s)",
    )
    parser.add_argument(
        "--refresh-rate", type=float, help="preferred refresh rate (default: the headset's)"
    )
    parser.add_argument(
        "--fps-divider", type=int, default=1, help="frame rate divider (default: %(default)s)"
    )
    parser.add_argument(
        "--compositor-fps",
        type=float,
        default=REFERENCE_REFRESH_RATE_HZ,
        help="null compositor's rate and advertised refresh, via XRT_COMPOSITOR_NULL_FPS "
        "(default: %(default)s)",
    )
    parser.add_argument(
        "--startup-timeout",
        type=float,
        default=20.0,
        help="server startup timeout (default: %(default)s)",
    )
    parser.add_argument(
        "--trace-buffer-kb",
        type=int,
        default=131072,
        help="Perfetto ring buffer KiB (default: %(default)s)",
    )


def resolve_toolchain(args):
    """Resolve every binary and manifest from parsed add_toolchain_args() flags.

    Returns (Toolchain, []) or (None, [human-readable missing pieces])."""
    build_dir = args.build_dir
    server = find_binary(args.server, build_dir / "server/wivrn-server", "wivrn-server")
    client = find_binary(args.client, build_dir / "bin/wivrn", "wivrn")
    hello_xr = None if args.xr_app else find_binary(args.hello_xr, None, "hello_xr")
    wivrn_manifest = find_manifest(
        args.wivrn_manifest or in_build(build_dir / "openxr_wivrn-dev.json"),
        "openxr_wivrn.json",
    )
    monado_manifest = find_manifest(
        args.monado_manifest or in_build(build_dir / "bench-runtime/openxr_monado-dev.json"),
        "openxr_monado.json",
    )

    missing = []
    if not server:
        missing.append(f"wivrn-server (not in {build_dir}; build it, or pass --server)")
    if not client:
        missing.append(f"wivrn client (not in {build_dir}; same build, or pass --client)")
    if not args.xr_app and not hello_xr:
        missing.append("hello_xr (github.com/KhronosGroup/OpenXR-SDK-Source; or pass --xr-app)")
    if not wivrn_manifest:
        missing.append("openxr_wivrn.json (cmake --install the build; or --wivrn-manifest)")
    if not monado_manifest:
        missing.append(
            "openxr_monado.json (build with WIVRN_BUILD_BENCH_RUNTIME; or --monado-manifest)"
        )
    if missing:
        return None, missing

    # The OpenXR loader (and the systemd unit hello_xr is launched from) resolves manifests
    # from its own cwd, so a relative path would not survive the launch.
    return (
        Toolchain(
            server=server.resolve(),
            client=client.resolve(),
            hello_xr=hello_xr.resolve() if hello_xr else None,
            wivrn_manifest=wivrn_manifest.resolve(),
            monado_manifest=monado_manifest.resolve(),
        ),
        [],
    )


# ---------------------------------------------------------------------------
# Process helpers
# ---------------------------------------------------------------------------


def spawn(cmd, env, logfile):
    """Start a process in its own session (process group) with output to logfile."""
    # Not a context manager: the child writes to this handle for its whole lifetime.
    f = open(logfile, "wb")  # noqa: SIM115
    proc = subprocess.Popen(
        cmd,
        env=env,
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
                ["systemctl", "--user", *args], capture_output=True, text=True, check=False
            ).returncode
            == 0
        )
    except FileNotFoundError:
        return False


def systemctl_user_env(name):
    """Current value of `name` in the systemd user manager environment, or None."""
    try:
        out = subprocess.run(
            ["systemctl", "--user", "show-environment"],
            capture_output=True,
            text=True,
            check=False,
        ).stdout
    except FileNotFoundError:
        return None
    for line in out.splitlines():
        key, sep, value = line.partition("=")
        if sep and key == name:
            return value
    return None


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
# Session parameters
# ---------------------------------------------------------------------------


@dataclass
class SessionSpec:
    """What to stream, and for how long.

    Video parameters default to None, meaning the build's own defaults. Both harnesses override
    them; see REFERENCE_EYE_WIDTH.
    """

    encoder: str = "vulkan"
    codec: str | None = None
    duration: float = 20.0
    graphics: str = "Vulkan2"
    xr_app: str | None = None
    bitrate_bps: int | None = None
    eye_width: int | None = None
    eye_height: int | None = None
    resolution_scale: float | None = None
    stream_scale: float | None = None
    refresh_rate: float | None = None
    fps_divider: int | None = None
    compositor_fps: float = REFERENCE_REFRESH_RATE_HZ
    bit_depth: int | None = None
    startup_timeout: float = 20.0
    trace_buffer_kb: int | None = None
    server_log_level: str | None = None  # XRT_LOG; "info" exposes the encoder configuration
    extra_server_config: dict = field(default_factory=dict)

    @classmethod
    def from_args(cls, args, encoder):
        """Build a spec for one encoder from add_workload_args() flags."""
        return cls(
            encoder=encoder,
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
            compositor_fps=args.compositor_fps,
            startup_timeout=args.startup_timeout,
            trace_buffer_kb=args.trace_buffer_kb,
            # parse_encoder_config() reads the block this log level exposes.
            server_log_level="info",
        )


def server_config(spec, hello_xr):
    """The wivrn/config.json the server reads (encoder choice + the app to auto-launch)."""
    encoder_cfg = {"encoder": spec.encoder}
    if spec.codec:
        encoder_cfg["codec"] = spec.codec
    # sleep infinity keeps stdin open so the app doesn't quit on EOF.
    argv = shlex.split(spec.xr_app) if spec.xr_app else [str(hello_xr), "-g", spec.graphics]
    app_cmd = "sleep infinity | exec " + " ".join(shlex.quote(a) for a in argv)
    cfg = {
        "encoder": encoder_cfg,
        "application": ["/bin/sh", "-c", app_cmd],
        "publish-service": "avahi",
    }
    if spec.bit_depth is not None:
        cfg["bit-depth"] = spec.bit_depth
    cfg.update(spec.extra_server_config)
    return cfg


def client_config(spec):
    """The wivrn/client.json the client reads, or None to leave the client's config alone.

    "servers" must be present: the client parses it before anything else and aborts the whole
    config load (silently keeping defaults) if it is missing.
    """
    cfg = {"servers": []}
    if spec.resolution_scale is not None:
        cfg["resolution_scale"] = spec.resolution_scale
    if spec.stream_scale is not None:
        cfg["stream_scale"] = spec.stream_scale
    if spec.bitrate_bps is not None:
        cfg["bitrate_bps"] = spec.bitrate_bps
    if spec.refresh_rate is not None:
        cfg["preferred_refresh_rate"] = spec.refresh_rate
    if spec.fps_divider is not None:
        cfg["fps_divider"] = spec.fps_divider
    return cfg if len(cfg) > 1 else None


def monado_config(spec):
    """The monado/config_v0.json the client's runtime reads to pick the remote HMD driver at
    spec.eye_width/eye_height, or None to leave Monado's own active-config selection alone."""
    if spec.eye_width is None or spec.eye_height is None:
        return None
    return {
        "active": "remote",
        "remote": {
            "version": 0,
            "port": 4242,
            "view_count": 2,
            "w_pixels": spec.eye_width * 2,
            "h_pixels": spec.eye_height,
            "w_meters": 0.13,
            "h_meters": 0.07,
        },
    }


# "Encoder configuration:" block, logged by the server at XRT_LOG=info.
ENCODER_LOG_RE = re.compile(
    r"^\s*\*\s+(?P<name>\S+)\s+\((?P<codec>\S+)\s+(?P<depth>\d+)-bit\)\s*\n"
    r"\s*size:\s*(?P<width>\d+)x(?P<height>\d+)\s*\n"
    r"\s*bitrate:\s*(?P<bitrate>[\d.]+)Mbit/s",
    re.MULTILINE,
)


def parse_encoder_config(server_log):
    """Extract the negotiated encoder streams from a server log (needs XRT_LOG=info).

    The benchmark records these so a run against a different resolution or bitrate — a
    renegotiation, not a performance change — is visible rather than silently compared.
    """
    try:
        text = Path(server_log).read_text(errors="replace")
    except OSError:
        return []
    return [
        {
            "encoder": m["name"],
            "codec": m["codec"],
            "bit_depth": int(m["depth"]),
            "width": int(m["width"]),
            "height": int(m["height"]),
            "mbit_s": float(m["bitrate"]),
        }
        for m in ENCODER_LOG_RE.finditer(text)
    ]


def gpu_clock_sample():
    """(pstate, video clock MHz) for the first NVIDIA GPU, or None when there is nothing to ask."""
    if not shutil.which("nvidia-smi"):
        return None
    try:
        out = subprocess.run(
            [
                "nvidia-smi",
                "--query-gpu=pstate,clocks.current.video",
                "--format=csv,noheader,nounits",
            ],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        ).stdout
        pstate, video = out.splitlines()[0].split(",")
        return pstate.strip(), int(video)
    except (OSError, ValueError, IndexError, subprocess.SubprocessError):
        return None


def check_gpu_clocks(samples):
    """Warn when the GPU downclocked mid-run. Takes run_session()'s samples; returns a message,
    or None.

    Chronological on purpose: the first sample lands before the workload has spun the GPU up, so
    a plain min/max would read every run's ramp from idle as a drop.
    """
    clocks = [mhz for _, mhz in samples if mhz]
    if len(clocks) < 2:
        return None
    peak = clocks[0]
    for mhz in clocks[1:]:
        if mhz < peak * GPU_CLOCK_DROP_RATIO:
            states = " -> ".join(dict.fromkeys(p for p, _ in samples))
            return (
                f"GPU video clock fell from {peak} to {mhz} MHz during the run ({states}); "
                f"lock it with nvidia-smi -lgc before trusting this number"
            )
        peak = max(peak, mhz)
    return None


def check_encode_size(streams):
    """Warn when the negotiated encode is too small to measure an encoder by. Takes
    parse_encoder_config()'s streams; returns a message, or None."""
    if not streams:
        return None
    if max(s["width"] for s in streams) >= MIN_USEFUL_ENCODE_WIDTH:
        return None
    sizes = ", ".join(f"{s['width']}x{s['height']}" for s in streams)
    return (
        f"negotiated encode is only {sizes} — that measures per-frame fixed cost, not the "
        f"encoder; raise --eye-width/--eye-height "
        f"(the reference workload uses {REFERENCE_EYE_WIDTH}x{REFERENCE_EYE_HEIGHT})"
    )


def describe_streams(streams):
    """One line naming what was actually encoded, so a renegotiation is visible."""
    return ", ".join(
        f"{s['encoder']} {s['codec']} {s['bit_depth']}-bit "
        f"{s['width']}x{s['height']} @ {s['mbit_s']:.1f}Mbit/s"
        for s in streams
    )


# ---------------------------------------------------------------------------
# The session itself
# ---------------------------------------------------------------------------


def run_session(spec, tools, trace, log_dir, tag=None, progress=None):
    """Stream for spec.duration seconds, writing the .pftrace to `trace`.

    Returns the GPU clock samples taken while streaming, for check_gpu_clocks(). Raises
    SessionError if the pipeline never produced a trace. Logs land in
    log_dir/<tag>-{server,client}.log.
    """
    tag = tag or spec.encoder
    trace = Path(trace)
    log_dir = Path(log_dir)
    trace.parent.mkdir(parents=True, exist_ok=True)
    log_dir.mkdir(parents=True, exist_ok=True)
    trace.unlink(missing_ok=True)

    def say(msg):
        if progress:
            progress(f"[{tag}] {msg}")

    server_log = log_dir / f"{tag}-server.log"
    client_log = log_dir / f"{tag}-client.log"

    # A private XDG_CONFIG_HOME for both ends: the server's encoder choice and the client's
    # video settings, isolated from whatever the user has configured.
    scratch = Path(tempfile.mkdtemp(prefix=f"wivrn-bench-{tag}-"))
    cfg_dir = scratch / "wivrn"
    cfg_dir.mkdir(parents=True)
    (cfg_dir / "config.json").write_text(json.dumps(server_config(spec, tools.hello_xr), indent=2))
    client_cfg = client_config(spec)
    if client_cfg:
        (cfg_dir / "client.json").write_text(json.dumps(client_cfg, indent=2))
    monado_cfg = monado_config(spec)
    if monado_cfg:
        monado_dir = scratch / "monado"
        monado_dir.mkdir(parents=True)
        (monado_dir / "config_v0.json").write_text(json.dumps(monado_cfg, indent=2))

    server = client = None
    gpu_samples = []
    # Restored by cleanup(); the run overwrites it below.
    prev_runtime_json = systemctl_user_env("XR_RUNTIME_JSON")

    def cleanup():
        # Client first: disconnecting flushes the .pftrace (trace::flush_session).
        stop(client, signal.SIGINT)
        # The app is the server's systemd unit, so stop it by unit rather than by name.
        systemctl_user("stop", APP_UNIT_PATTERN)
        stop(server)
        if prev_runtime_json is None:
            systemctl_user("unset-environment", "XR_RUNTIME_JSON")
        else:
            systemctl_user("set-environment", f"XR_RUNTIME_JSON={prev_runtime_json}")
        systemctl_user("reset-failed", APP_UNIT_PATTERN)
        shutil.rmtree(scratch, ignore_errors=True)

    try:
        # The server launches hello_xr as a systemd unit, which inherits the user manager's
        # environment, not the server's — so set XR_RUNTIME_JSON there too.
        systemctl_user("set-environment", f"XR_RUNTIME_JSON={tools.wivrn_manifest}")

        say("starting wivrn-server (auto-launches hello_xr)")
        server_env = {
            **os.environ,
            "XDG_CONFIG_HOME": str(scratch),
            "XR_RUNTIME_JSON": str(tools.wivrn_manifest),
            "WIVRN_TRACING": "inprocess",
            "WIVRN_TRACING_FILE": str(trace),
        }
        if spec.trace_buffer_kb:
            server_env["WIVRN_TRACING_BUFFER_KB"] = str(spec.trace_buffer_kb)
        if spec.server_log_level:
            server_env["XRT_LOG"] = spec.server_log_level
        server = spawn(
            [str(tools.server), "--no-encrypt", "--no-manage-active-runtime"],
            server_env,
            server_log,
        )
        if not wait_port(WIVRN_PORT, timeout=spec.startup_timeout):
            raise SessionError("startup", f"server not listening on {WIVRN_PORT}; see {server_log}")

        say("starting client (autoconnect)")
        client_env = {
            **os.environ,
            "XDG_CONFIG_HOME": str(scratch),
            "XR_RUNTIME_JSON": str(tools.monado_manifest),
            "WIVRN_AUTOCONNECT": "1",
            "XRT_COMPOSITOR_NULL": "1",
            "XRT_COMPOSITOR_NULL_FPS": str(spec.compositor_fps),
        }
        client = spawn([str(tools.client)], client_env, client_log)

        say(f"streaming for {spec.duration:g}s")
        deadline = time.time() + spec.duration
        # First sample one interval in, not at once: at t=0 the GPU is still idle, and that
        # reading would be reported as the clock the encoder ran at.
        next_sample = time.time() + GPU_CLOCK_SAMPLE_INTERVAL
        while time.time() < deadline:
            if server.poll() is not None:
                raise SessionError("crash", f"server exited during run; see {server_log}")
            if client.poll() is not None:
                raise SessionError("crash", f"client exited during run; see {client_log}")
            if time.time() >= next_sample:
                sample = gpu_clock_sample()
                if sample:
                    gpu_samples.append(sample)
                next_sample = time.time() + GPU_CLOCK_SAMPLE_INTERVAL
            time.sleep(0.5)
    finally:
        cleanup()

    # The trace is flushed asynchronously on disconnect; give it a moment to land.
    for _ in range(20):
        if trace.is_file() and trace.stat().st_size > 0:
            break
        time.sleep(0.25)
    if not trace.is_file() or trace.stat().st_size == 0:
        raise SessionError("empty", f"no trace written (empty pipeline?); see {server_log}")

    # The server flushes once more at shutdown; rather than clobber, it lands that second copy
    # as <stem>-2.pftrace (unique_file_path in wivrn_trace.cpp). It covers the same session.
    for extra in trace.parent.glob(f"{trace.stem}-*{trace.suffix}"):
        extra.unlink(missing_ok=True)

    return gpu_samples
