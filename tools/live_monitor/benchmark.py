from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import re
import shutil
import subprocess
import time
import urllib.error
import urllib.request
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Final
from urllib.parse import urlparse

try:
    from server import (
        AtmosphericEnvironment,
        MotorState,
        Observation,
        OrganismTelemetry,
        ProductState,
        RobotPhysicalLoad,
        RobotWorld,
    )
except ImportError:
    # --live-loaded only needs the HTTP API and can therefore still run when the
    # file is copied outside tools/live_monitor. The classic DLL benchmark cannot.
    AtmosphericEnvironment = MotorState = Observation = OrganismTelemetry = None  # type: ignore[assignment]
    ProductState = RobotPhysicalLoad = RobotWorld = None  # type: ignore[assignment]


PROFILES: Final[dict[str, int]] = {
    "compact": 96,
    "standard": 384,
    "large": 1_536,
    "research": 6_144,
}

# Endpoints that exist in the current TATARUS live server. The first five are
# internally refreshed by the training loop; the remaining endpoints are built
# on HTTP request by the browser/UI.
CAPTURE_ENDPOINTS: Final[tuple[str, ...]] = (
    "frame",
    "physiology",
    "organism",
    "robot",
    "geometry",
    "status",
    "timeline",
    "experiment",
    "snapshots",
    "throughput",
)

SOURCE_RELATIVE_PATHS: Final[tuple[str, ...]] = (
    "tools/live_monitor/server.py",
    "tools/live_monitor/index.html",
    "src/tatarus_mind.cpp",
    "modules/tatarus_neurobiology/tatarus_neural_network.cpp",
    "modules/tatarus_neurobiology/tatarus_tissue.cpp",
    "modules/tatarus_neurobiology/tatarus_physiology.cpp",
    "modules/tatarus_organism/tatarus_organism.cpp",
    "modules/tatarus_organism/tatarus_heart.cpp",
    "modules/tatarus_organism/tatarus_lung.cpp",
    "modules/tatarus_organism/tatarus_kidney.cpp",
    "modules/tatarus_organism/tatarus_circulation.cpp",
    "include/tatarus/version.hpp",
    "include/tatarus/types.hpp",
    "include/tatarus/throughput.hpp",
    "include/tatarus/c_api.h",
    "tests/tatarus_neurobiology_tests.cpp",
)


# ---------------------------------------------------------------------------
# Common helpers
# ---------------------------------------------------------------------------


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def count_scalar_values(value: Any) -> int:
    match value:
        case dict():
            return sum(count_scalar_values(item) for item in value.values())
        case list() | tuple():
            return sum(count_scalar_values(item) for item in value)
        case _:
            return 1


def nested(data: dict[str, Any], *keys: str, default: Any = 0) -> Any:
    current: Any = data
    for key in keys:
        if not isinstance(current, dict):
            return default
        current = current.get(key, default)
    return current


def as_int(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError, OverflowError):
        return default


def as_float(value: Any, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError, OverflowError):
        return default


def rate(delta: float | int, elapsed_seconds: float) -> float:
    return float(delta) / elapsed_seconds if elapsed_seconds > 0.0 else 0.0


def human_rate(value: float | None, suffix: str = "/s") -> str:
    if value is None:
        return "nicht verfügbar"
    absolute = abs(value)
    if absolute >= 1_000_000_000:
        return f"{value / 1_000_000_000:,.3f} Mrd.{suffix}"
    if absolute >= 1_000_000:
        return f"{value / 1_000_000:,.3f} Mio.{suffix}"
    if absolute >= 1_000:
        return f"{value / 1_000:,.3f} Tsd.{suffix}"
    return f"{value:,.3f}{suffix}"


def human_bytes(value: float | int | None, suffix: str = "") -> str:
    if value is None:
        return "nicht verfügbar"
    number = float(value)
    units = ("B", "KiB", "MiB", "GiB", "TiB")
    index = 0
    while abs(number) >= 1024.0 and index < len(units) - 1:
        number /= 1024.0
        index += 1
    return f"{number:,.3f} {units[index]}{suffix}"


# ---------------------------------------------------------------------------
# Source-aware profile of the exact project revision next to this script
# ---------------------------------------------------------------------------


@dataclass(slots=True, frozen=True)
class SourceProfile:
    project_root: str | None
    sdk_version: str | None
    source_hashes: dict[str, str]
    server_refresh_divisors: dict[str, int | None]
    browser_poll_hz: dict[str, float]
    warnings: tuple[str, ...]

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


def _find_project_root(script_path: Path) -> Path | None:
    candidates = [script_path.parent.parent.parent, Path.cwd()]
    candidates.extend(Path.cwd().parents)
    for candidate in candidates:
        if (
            (candidate / "tools" / "live_monitor" / "server.py").is_file()
            and (candidate / "src" / "tatarus_mind.cpp").is_file()
            and (candidate / "modules" / "tatarus_organism" / "tatarus_organism.cpp").is_file()
        ):
            return candidate.resolve()
    return None


def _read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def build_source_profile(script_path: Path) -> SourceProfile:
    project_root = _find_project_root(script_path)
    if project_root is None:
        return SourceProfile(
            project_root=None,
            sdk_version=None,
            source_hashes={},
            server_refresh_divisors={
                "frame": 1,
                "organism": 1,
                "robot": 1,
                "physiology": 5,
                "geometry": 20,
            },
            browser_poll_hz={
                "frame": 1000.0 / 90.0,
                "robot": 1000.0 / 90.0,
                "organism": 10.0,
                "status": 1.25,
                "experiment": 1.0,
                "timeline": 1.0,
            },
            warnings=("Projektquellen neben benchmark.py nicht gefunden; bekannte TATARUS-4-Cadences verwendet.",),
        )

    hashes: dict[str, str] = {}
    for relative in SOURCE_RELATIVE_PATHS:
        path = project_root / relative
        if path.is_file():
            hashes[relative] = sha256_file(path)

    warnings: list[str] = []
    sdk_version: str | None = None
    version_path = project_root / "include" / "tatarus" / "version.hpp"
    if version_path.is_file():
        version_match = re.search(
            r'#define\s+TATARUS_SDK_VERSION_STRING\s+"([^"]+)"',
            _read_text(version_path),
        )
        if version_match:
            sdk_version = version_match.group(1)

    server_path = project_root / "tools" / "live_monitor" / "server.py"
    server_text = _read_text(server_path)
    refresh: dict[str, int | None] = {
        "frame": 1,
        "organism": 1,
        "robot": 1,
        "physiology": 5,
        "geometry": 20,
    }
    for endpoint, attr in (("physiology", "latest_physiology"), ("geometry", "latest_geometry")):
        pattern = re.compile(
            rf"if\s+self\._sample\s*%\s*(\d+)\s*==\s*0\s*:\s*\n\s*self\.{attr}\s*=",
            re.MULTILINE,
        )
        match = pattern.search(server_text)
        if match:
            refresh[endpoint] = int(match.group(1))
        else:
            warnings.append(f"Refresh-Cadence für {endpoint} konnte nicht aus server.py gelesen werden.")

    index_path = project_root / "tools" / "live_monitor" / "index.html"
    index_text = _read_text(index_path)
    timeout_functions = {
        "frame": "frameLoop",
        "robot": "robotLoop",
        "organism": "organismLoop",
        "status": "statusLoop",
        "experiment": "laboratoryLoop",
        "timeline": "laboratoryLoop",
    }
    browser_poll_hz: dict[str, float] = {}
    for endpoint, function_name in timeout_functions.items():
        match = re.search(
            rf"setTimeout\(\s*{re.escape(function_name)}\s*,\s*([0-9.]+)\s*\)",
            index_text,
        )
        if match:
            milliseconds = float(match.group(1))
            if milliseconds > 0.0:
                browser_poll_hz[endpoint] = 1000.0 / milliseconds
        else:
            warnings.append(f"Browser-Polling für {endpoint} konnte nicht aus index.html gelesen werden.")

    # Verify the source patterns that make this benchmark a TATARUS-4 whole-organism run.
    organism_source = _read_text(
        project_root / "modules" / "tatarus_organism" / "tatarus_organism.cpp"
    )
    required_markers = (
        "mind_.observe(embodiedExperience)",
        "heart_.step(",
        "lung_.step(",
        "kidneys_.step(",
        "circulation_.step(",
        "updateAutonomicsAndInteroception(",
    )
    missing = [marker for marker in required_markers if marker not in organism_source]
    if missing:
        warnings.append(
            "Organismus-Pipeline weicht von der erwarteten Revision ab: " + ", ".join(missing)
        )

    return SourceProfile(
        project_root=str(project_root),
        sdk_version=sdk_version,
        source_hashes=hashes,
        server_refresh_divisors=refresh,
        browser_poll_hz=browser_poll_hz,
        warnings=tuple(warnings),
    )


# ---------------------------------------------------------------------------
# Classic fresh-organism scaling benchmark
# ---------------------------------------------------------------------------


@dataclass(slots=True, frozen=True)
class ScalingResult:
    profile: str
    neurons: int
    mode: str
    init_seconds: float
    external_steps: int
    neural_steps: int
    simulated_seconds: float
    elapsed_seconds: float
    realtime_factor: float
    initial_synapses: int
    final_synapses: int
    spike_delta: int
    initial_myelin: float
    final_myelin: float
    final_brain_atp: float
    final_map_mm_hg: float
    final_sao2: float
    final_cardiac_output_l_min: float
    final_gfr_ml_min: float
    final_left_gfr_ml_min: float
    final_right_gfr_ml_min: float

    @property
    def steps_per_second(self) -> float:
        return rate(self.external_steps, self.elapsed_seconds)

    @property
    def neural_steps_per_second(self) -> float:
        return rate(self.neural_steps, self.elapsed_seconds)

    @property
    def milliseconds_per_external_step(self) -> float:
        return 1000.0 / self.steps_per_second if self.steps_per_second > 0.0 else float("inf")

    def to_dict(self) -> dict[str, Any]:
        result = asdict(self)
        result.update(
            {
                "external_steps_per_second": self.steps_per_second,
                "neural_steps_per_second": self.neural_steps_per_second,
                "milliseconds_per_external_step": self.milliseconds_per_external_step,
                "synapse_delta": self.final_synapses - self.initial_synapses,
            }
        )
        return result


def configure_library(library: ctypes.CDLL) -> None:
    if OrganismTelemetry is None:
        raise RuntimeError("server.py konnte nicht importiert werden; DLL-Benchmark ist nicht verfügbar")

    library.tatarus_organism_create_sized.argtypes = [ctypes.c_uint64, ctypes.c_uint32]
    library.tatarus_organism_create_sized.restype = ctypes.c_void_p
    library.tatarus_organism_destroy.argtypes = [ctypes.c_void_p]
    library.tatarus_organism_destroy.restype = None
    library.tatarus_organism_step_with_load.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(Observation),
        ctypes.POINTER(AtmosphericEnvironment),
        ctypes.POINTER(RobotPhysicalLoad),
        ctypes.c_double,
        ctypes.POINTER(ProductState),
    ]
    library.tatarus_organism_step_with_load.restype = ctypes.c_int
    library.tatarus_organism_get_telemetry.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(OrganismTelemetry),
    ]
    library.tatarus_organism_get_telemetry.restype = ctypes.c_int
    library.tatarus_organism_get_motor.argtypes = [ctypes.c_void_p, ctypes.POINTER(MotorState)]
    library.tatarus_organism_get_motor.restype = ctypes.c_int
    library.tatarus_organism_begin_action.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_double]
    library.tatarus_organism_begin_action.restype = ctypes.c_int
    library.tatarus_organism_end_action.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint64,
        ctypes.c_double,
        ctypes.c_double,
        ctypes.c_double,
    ]
    library.tatarus_organism_end_action.restype = ctypes.c_int
    library.tatarus_organism_end_episode.argtypes = [ctypes.c_void_p, ctypes.c_int32]
    library.tatarus_organism_end_episode.restype = ctypes.c_int
    for name in (
        "tatarus_organism_get_json",
        "tatarus_organism_get_live_json",
        "tatarus_organism_get_physiology_json",
        "tatarus_organism_get_spatial_json",
    ):
        function = getattr(library, name)
        function.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint64]
        function.restype = ctypes.c_uint64
    library.tatarus_last_error.argtypes = []
    library.tatarus_last_error.restype = ctypes.c_char_p


def last_error(library: ctypes.CDLL) -> str:
    value = library.tatarus_last_error()
    return value.decode("utf-8", errors="replace") if value else "Unbekannter TATARUS-Fehler"


def check(library: ctypes.CDLL, result: int, operation: str) -> None:
    if not result:
        raise RuntimeError(f"{operation} fehlgeschlagen: {last_error(library)}")


def dll_json_snapshot(library: ctypes.CDLL, handle: ctypes.c_void_p, function_name: str) -> dict[str, Any]:
    function = getattr(library, function_name)
    required = int(function(handle, None, 0))
    if required <= 1:
        raise RuntimeError(f"{function_name}: {last_error(library)}")
    buffer = ctypes.create_string_buffer(required)
    returned = int(function(handle, buffer, required))
    if returned != required:
        raise RuntimeError(f"{function_name}: ungültige Snapshot-Größe")
    return json.loads(bytes(buffer.raw[: required - 1]))


def earth_atmosphere() -> Any:
    if AtmosphericEnvironment is None:
        raise RuntimeError("server.py konnte nicht importiert werden")
    return AtmosphericEnvironment(101.325, 0.2095, 0.0004, 0.7808, 22.0, 0.50, 10.0)


def execute_step(
    library: ctypes.CDLL,
    handle: ctypes.c_void_p,
    world: Any,
    motor_before: Any,
    atmosphere: Any,
    tick: int,
    dt_seconds: float,
) -> Any:
    observation = world.advance(motor_before)
    observation.timestamp_ns = int(tick * dt_seconds * 1_000_000_000)
    action_id = world.motor_selected_direction + 1
    check(
        library,
        library.tatarus_organism_begin_action(handle, action_id, max(0.05, motor_before.confidence)),
        "tatarus_organism_begin_action",
    )

    movement = max(0.0, min(1.0, float(motor_before.movement)))
    mechanical_power = 4.0 + 18.0 * movement
    load = RobotPhysicalLoad(
        mechanical_power / 24.0,
        24.0,
        1.0 + 4.0 * movement,
        0.5 + 2.5 * movement,
        mechanical_power,
        8.0,
        float(world.battery),
        25.0 + 6.0 * movement,
    )
    product = ProductState()
    check(
        library,
        library.tatarus_organism_step_with_load(
            handle,
            ctypes.byref(observation),
            ctypes.byref(atmosphere),
            ctypes.byref(load),
            dt_seconds,
            ctypes.byref(product),
        ),
        "tatarus_organism_step_with_load",
    )

    motor_after = MotorState()
    check(
        library,
        library.tatarus_organism_get_motor(handle, ctypes.byref(motor_after)),
        "tatarus_organism_get_motor",
    )
    check(
        library,
        library.tatarus_organism_end_action(
            handle,
            action_id,
            world.last_reward,
            1.0 if world.position == world.goal else 0.0,
            1.0 if world.last_new_cell else 0.0,
        ),
        "tatarus_organism_end_action",
    )
    if world._reset_pending:
        check(
            library,
            library.tatarus_organism_end_episode(
                handle, 1 if world.position == world.goal else 0
            ),
            "tatarus_organism_end_episode",
        )
    world.observe_neural(product, motor_after)
    return motor_after


def run_scaling_benchmark(
    library_path: Path,
    profile: str,
    neurons: int,
    mode: str,
    seconds: float,
    seed: int,
    dt_seconds: float,
    warmup_steps: int,
) -> ScalingResult:
    library = ctypes.CDLL(str(library_path))
    configure_library(library)
    init_start = time.perf_counter()
    raw_handle = library.tatarus_organism_create_sized(seed, neurons)
    if not raw_handle:
        raise RuntimeError(f"Erzeugung mit {neurons} Neuronen fehlgeschlagen: {last_error(library)}")
    handle = ctypes.c_void_p(raw_handle)
    init_seconds = time.perf_counter() - init_start

    try:
        world = RobotWorld(seed=seed, controller_mode="neural_direct")
        atmosphere = earth_atmosphere()
        motor = MotorState()
        check(
            library,
            library.tatarus_organism_get_motor(handle, ctypes.byref(motor)),
            "tatarus_organism_get_motor",
        )
        tick = 0
        for _ in range(warmup_steps):
            tick += 1
            motor = execute_step(library, handle, world, motor, atmosphere, tick, dt_seconds)

        initial_live = dll_json_snapshot(library, handle, "tatarus_organism_get_live_json")
        initial_body = OrganismTelemetry()
        check(
            library,
            library.tatarus_organism_get_telemetry(handle, ctypes.byref(initial_body)),
            "tatarus_organism_get_telemetry",
        )

        measured_steps = 0
        started = time.perf_counter()
        while time.perf_counter() - started < seconds:
            tick += 1
            motor = execute_step(library, handle, world, motor, atmosphere, tick, dt_seconds)
            measured_steps += 1
            if mode == "telemetry":
                world.snapshot()
                dll_json_snapshot(library, handle, "tatarus_organism_get_live_json")
                dll_json_snapshot(library, handle, "tatarus_organism_get_json")
                if measured_steps % 5 == 0:
                    dll_json_snapshot(library, handle, "tatarus_organism_get_physiology_json")
                if measured_steps % 20 == 0:
                    dll_json_snapshot(library, handle, "tatarus_organism_get_spatial_json")
        elapsed = time.perf_counter() - started

        final_live = dll_json_snapshot(library, handle, "tatarus_organism_get_live_json")
        final_body = OrganismTelemetry()
        check(
            library,
            library.tatarus_organism_get_telemetry(handle, ctypes.byref(final_body)),
            "tatarus_organism_get_telemetry",
        )
        metrics0 = initial_live.get("metrics", {})
        metrics1 = final_live.get("metrics", {})
        biology0 = initial_live.get("biology", {})
        biology1 = final_live.get("biology", {})
        physiology1 = final_live.get("physiology", {})
        simulated_seconds = final_body.simulated_time_seconds - initial_body.simulated_time_seconds
        neural_steps = as_int(final_live.get("step")) - as_int(initial_live.get("step"))

        return ScalingResult(
            profile=profile,
            neurons=neurons,
            mode=mode,
            init_seconds=init_seconds,
            external_steps=measured_steps,
            neural_steps=neural_steps,
            simulated_seconds=simulated_seconds,
            elapsed_seconds=elapsed,
            realtime_factor=simulated_seconds / elapsed,
            initial_synapses=as_int(metrics0.get("active_synapses")),
            final_synapses=as_int(metrics1.get("active_synapses")),
            spike_delta=as_int(metrics1.get("total_spikes")) - as_int(metrics0.get("total_spikes")),
            initial_myelin=as_float(biology0.get("myelin_coverage")),
            final_myelin=as_float(biology1.get("myelin_coverage")),
            final_brain_atp=as_float(physiology1.get("atp")),
            final_map_mm_hg=float(final_body.mean_arterial_pressure_mm_hg),
            final_sao2=float(final_body.arterial_oxygen_saturation),
            final_cardiac_output_l_min=float(final_body.cardiac_output_l_per_min),
            final_gfr_ml_min=float(final_body.glomerular_filtration_rate_ml_per_min),
            final_left_gfr_ml_min=float(final_body.left_kidney_gfr_ml_per_min),
            final_right_gfr_ml_min=float(final_body.right_kidney_gfr_ml_per_min),
        )
    finally:
        library.tatarus_organism_destroy(handle)


def print_scaling_results(results: list[ScalingResult]) -> None:
    print()
    print(
        f"{'Profil':<11}{'N':>7}{'Modus':>11}{'Init[s]':>10}{'Org/s':>11}"
        f"{'Neural/s':>12}{'ms/Org':>11}{'RT-Faktor':>11}{'GFR L/R':>17}"
    )
    print("-" * 101)
    for result in results:
        renal = f"{result.final_left_gfr_ml_min:.1f}/{result.final_right_gfr_ml_min:.1f}"
        print(
            f"{result.profile:<11}{result.neurons:>7,}{result.mode:>11}"
            f"{result.init_seconds:>10.3f}{result.steps_per_second:>11.2f}"
            f"{result.neural_steps_per_second:>12.1f}"
            f"{result.milliseconds_per_external_step:>11.2f}"
            f"{result.realtime_factor:>11.2f}{renal:>17}"
        )


# ---------------------------------------------------------------------------
# Live-loaded whole-system benchmark
# ---------------------------------------------------------------------------


@dataclass(slots=True, frozen=True)
class PayloadSample:
    raw: bytes
    data: dict[str, Any]
    scalar_values: int

    @property
    def size_bytes(self) -> int:
        return len(self.raw)


def http_get(base_url: str, endpoint: str, timeout: float = 30.0) -> PayloadSample:
    url = f"{base_url.rstrip('/')}/api/{endpoint}"
    request = urllib.request.Request(url, headers={"Cache-Control": "no-cache"})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read()
    except urllib.error.URLError as exc:
        raise RuntimeError(f"GET {url} fehlgeschlagen: {exc}") from exc
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"{url} liefert kein gültiges JSON") from exc
    if not isinstance(data, dict):
        raise RuntimeError(f"{url} liefert kein JSON-Objekt")
    return PayloadSample(raw=raw, data=data, scalar_values=count_scalar_values(data))


def http_control(base_url: str, action: str, timeout: float = 30.0) -> dict[str, Any]:
    url = f"{base_url.rstrip('/')}/api/control"
    body = json.dumps({"action": action}, separators=(",", ":")).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=body,
        method="POST",
        headers={"Content-Type": "application/json", "Content-Length": str(len(body))},
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read()
    except urllib.error.URLError as exc:
        raise RuntimeError(f"POST {url} ({action}) fehlgeschlagen: {exc}") from exc
    if not raw:
        return {}
    decoded = json.loads(raw)
    return decoded if isinstance(decoded, dict) else {}


def capture_live_payloads(base_url: str) -> dict[str, PayloadSample]:
    captured: dict[str, PayloadSample] = {}
    for endpoint in CAPTURE_ENDPOINTS:
        try:
            captured[endpoint] = http_get(base_url, endpoint)
        except RuntimeError:
            # The core five endpoints are mandatory. Ancillary UI endpoints may
            # be absent in older snapshots and are therefore optional.
            if endpoint in {"frame", "physiology", "organism", "robot", "geometry", "status"}:
                raise
    return captured


def _endpoint_port(endpoint: str) -> int | None:
    text = endpoint.strip()
    if not text:
        return None
    if text.startswith("["):
        match = re.search(r"\]:(\d+)$", text)
        return int(match.group(1)) if match else None
    if ":" not in text:
        return None
    try:
        return int(text.rsplit(":", 1)[1])
    except ValueError:
        return None


def _powershell_executable() -> str | None:
    return shutil.which("powershell.exe") or shutil.which("powershell") or shutil.which("pwsh.exe") or shutil.which("pwsh")


def discover_windows_pid_for_port(port: int) -> int | None:
    """Resolve the owning PID without depending on localized netstat state text."""
    if os.name != "nt":
        return None

    powershell = _powershell_executable()
    if powershell:
        command = (
            "$ErrorActionPreference='SilentlyContinue'; "
            f"Get-NetTCPConnection -LocalPort {port} -State Listen | "
            "Select-Object -ExpandProperty OwningProcess -Unique"
        )
        try:
            completed = subprocess.run(
                [powershell, "-NoProfile", "-NonInteractive", "-Command", command],
                check=False,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=10.0,
            )
            for token in re.findall(r"\b\d+\b", completed.stdout):
                pid = int(token)
                if pid > 0:
                    return pid
        except (OSError, subprocess.SubprocessError):
            pass

    try:
        completed = subprocess.run(
            ["netstat", "-ano", "-p", "tcp"],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10.0,
        )
    except (OSError, subprocess.SubprocessError):
        return None

    # Windows localizes LISTENING, so only TCP, local endpoint and final PID are
    # parsed. This works for German and English Windows and for IPv4/IPv6.
    for line in completed.stdout.splitlines():
        parts = line.split()
        if len(parts) < 4 or parts[0].upper() != "TCP":
            continue
        local_port = _endpoint_port(parts[1])
        if local_port != port:
            continue
        try:
            pid = int(parts[-1])
        except ValueError:
            continue
        if pid > 0:
            return pid
    return None


if os.name == "nt":
    from ctypes import wintypes

    PROCESS_QUERY_INFORMATION = 0x0400
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    PROCESS_VM_READ = 0x0010

    class FILETIME(ctypes.Structure):
        _fields_ = [("dwLowDateTime", wintypes.DWORD), ("dwHighDateTime", wintypes.DWORD)]

    class IO_COUNTERS(ctypes.Structure):
        _fields_ = [
            ("ReadOperationCount", ctypes.c_ulonglong),
            ("WriteOperationCount", ctypes.c_ulonglong),
            ("OtherOperationCount", ctypes.c_ulonglong),
            ("ReadTransferCount", ctypes.c_ulonglong),
            ("WriteTransferCount", ctypes.c_ulonglong),
            ("OtherTransferCount", ctypes.c_ulonglong),
        ]

    class PROCESS_MEMORY_COUNTERS_EX(ctypes.Structure):
        _fields_ = [
            ("cb", wintypes.DWORD),
            ("PageFaultCount", wintypes.DWORD),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
            ("PrivateUsage", ctypes.c_size_t),
        ]


@dataclass(slots=True, frozen=True)
class ProcessSample:
    pid: int | None
    process_cycles: int | None
    user_seconds: float | None
    kernel_seconds: float | None
    read_bytes: int | None
    write_bytes: int | None
    other_bytes: int | None
    read_operations: int | None
    write_operations: int | None
    other_operations: int | None
    page_faults: int | None
    working_set_bytes: int | None
    private_bytes: int | None
    peak_working_set_bytes: int | None
    handle_count: int | None


def _empty_process_sample(pid: int | None) -> ProcessSample:
    return ProcessSample(
        pid=pid,
        process_cycles=None,
        user_seconds=None,
        kernel_seconds=None,
        read_bytes=None,
        write_bytes=None,
        other_bytes=None,
        read_operations=None,
        write_operations=None,
        other_operations=None,
        page_faults=None,
        working_set_bytes=None,
        private_bytes=None,
        peak_working_set_bytes=None,
        handle_count=None,
    )


def _filetime_seconds(value: Any) -> float:
    ticks_100ns = (int(value.dwHighDateTime) << 32) | int(value.dwLowDateTime)
    return ticks_100ns / 10_000_000.0


def sample_process(pid: int | None) -> ProcessSample:
    if pid is None or os.name != "nt":
        return _empty_process_sample(pid)

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)

    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.QueryProcessCycleTime.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_ulonglong)]
    kernel32.QueryProcessCycleTime.restype = wintypes.BOOL
    kernel32.GetProcessTimes.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(FILETIME),
        ctypes.POINTER(FILETIME),
        ctypes.POINTER(FILETIME),
        ctypes.POINTER(FILETIME),
    ]
    kernel32.GetProcessTimes.restype = wintypes.BOOL
    kernel32.GetProcessIoCounters.argtypes = [wintypes.HANDLE, ctypes.POINTER(IO_COUNTERS)]
    kernel32.GetProcessIoCounters.restype = wintypes.BOOL
    kernel32.GetProcessHandleCount.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    kernel32.GetProcessHandleCount.restype = wintypes.BOOL
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOL
    psapi.GetProcessMemoryInfo.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(PROCESS_MEMORY_COUNTERS_EX),
        wintypes.DWORD,
    ]
    psapi.GetProcessMemoryInfo.restype = wintypes.BOOL

    handle = None
    for access in (
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
        PROCESS_QUERY_LIMITED_INFORMATION,
    ):
        handle = kernel32.OpenProcess(access, False, pid)
        if handle:
            break
    if not handle:
        return _empty_process_sample(pid)

    try:
        cycles = ctypes.c_ulonglong()
        process_cycles = int(cycles.value) if kernel32.QueryProcessCycleTime(handle, ctypes.byref(cycles)) else None

        creation = FILETIME()
        exit_time = FILETIME()
        kernel = FILETIME()
        user = FILETIME()
        user_seconds: float | None = None
        kernel_seconds: float | None = None
        if kernel32.GetProcessTimes(
            handle,
            ctypes.byref(creation),
            ctypes.byref(exit_time),
            ctypes.byref(kernel),
            ctypes.byref(user),
        ):
            user_seconds = _filetime_seconds(user)
            kernel_seconds = _filetime_seconds(kernel)

        io = IO_COUNTERS()
        read_bytes = write_bytes = other_bytes = None
        read_operations = write_operations = other_operations = None
        if kernel32.GetProcessIoCounters(handle, ctypes.byref(io)):
            read_bytes = int(io.ReadTransferCount)
            write_bytes = int(io.WriteTransferCount)
            other_bytes = int(io.OtherTransferCount)
            read_operations = int(io.ReadOperationCount)
            write_operations = int(io.WriteOperationCount)
            other_operations = int(io.OtherOperationCount)

        memory = PROCESS_MEMORY_COUNTERS_EX()
        memory.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS_EX)
        page_faults = working_set = private_bytes = peak_working_set = None
        if psapi.GetProcessMemoryInfo(handle, ctypes.byref(memory), memory.cb):
            page_faults = int(memory.PageFaultCount)
            working_set = int(memory.WorkingSetSize)
            private_bytes = int(memory.PrivateUsage)
            peak_working_set = int(memory.PeakWorkingSetSize)

        handle_count_value = wintypes.DWORD()
        handle_count = (
            int(handle_count_value.value)
            if kernel32.GetProcessHandleCount(handle, ctypes.byref(handle_count_value))
            else None
        )

        return ProcessSample(
            pid=pid,
            process_cycles=process_cycles,
            user_seconds=user_seconds,
            kernel_seconds=kernel_seconds,
            read_bytes=read_bytes,
            write_bytes=write_bytes,
            other_bytes=other_bytes,
            read_operations=read_operations,
            write_operations=write_operations,
            other_operations=other_operations,
            page_faults=page_faults,
            working_set_bytes=working_set,
            private_bytes=private_bytes,
            peak_working_set_bytes=peak_working_set,
            handle_count=handle_count,
        )
    finally:
        kernel32.CloseHandle(handle)


@dataclass(slots=True, frozen=True)
class LiveResult:
    url: str
    pid: int | None
    elapsed_seconds: float
    source_profile: SourceProfile
    start_payloads: dict[str, PayloadSample]
    end_payloads: dict[str, PayloadSample]
    process_start: ProcessSample
    process_end: ProcessSample

    @property
    def start_frame(self) -> dict[str, Any]:
        return self.start_payloads["frame"].data

    @property
    def end_frame(self) -> dict[str, Any]:
        return self.end_payloads["frame"].data

    @property
    def start_robot(self) -> dict[str, Any]:
        return self.start_payloads["robot"].data

    @property
    def end_robot(self) -> dict[str, Any]:
        return self.end_payloads["robot"].data

    @property
    def start_status(self) -> dict[str, Any]:
        return self.start_payloads["status"].data

    @property
    def end_status(self) -> dict[str, Any]:
        return self.end_payloads["status"].data

    @property
    def start_physiology(self) -> dict[str, Any]:
        return self.start_payloads["physiology"].data

    @property
    def end_physiology(self) -> dict[str, Any]:
        return self.end_payloads["physiology"].data

    @property
    def start_throughput(self) -> dict[str, Any]:
        return self.start_payloads.get("throughput", PayloadSample({}, 0, 0)).data

    @property
    def end_throughput(self) -> dict[str, Any]:
        return self.end_payloads.get("throughput", PayloadSample({}, 0, 0)).data

    @property
    def experiences_delta(self) -> int:
        return as_int(self.end_frame.get("experiences")) - as_int(self.start_frame.get("experiences"))

    @property
    def neural_steps_delta(self) -> int:
        return as_int(self.end_frame.get("step")) - as_int(self.start_frame.get("step"))

    @property
    def sample_delta(self) -> int:
        return as_int(self.end_status.get("sample")) - as_int(self.start_status.get("sample"))

    @property
    def experiences_per_second(self) -> float:
        return rate(self.experiences_delta, self.elapsed_seconds)

    @property
    def neural_steps_per_second(self) -> float:
        return rate(self.neural_steps_delta, self.elapsed_seconds)

    @property
    def milliseconds_per_experience(self) -> float:
        value = self.experiences_per_second
        return 1000.0 / value if value > 0.0 else float("inf")

    @property
    def start_synapses(self) -> int:
        return as_int(nested(self.start_frame, "metrics", "active_synapses"))

    @property
    def end_synapses(self) -> int:
        return as_int(nested(self.end_frame, "metrics", "active_synapses"))

    @property
    def mean_synapses(self) -> float:
        return (self.start_synapses + self.end_synapses) / 2.0

    def frame_counter_delta(self, section: str, key: str) -> float:
        return as_float(nested(self.end_frame, section, key)) - as_float(
            nested(self.start_frame, section, key)
        )

    def physiology_counter_delta(self, section: str, key: str) -> float:
        return as_float(nested(self.end_physiology, section, key)) - as_float(
            nested(self.start_physiology, section, key)
        )

    def robot_counter_delta(self, section: str, key: str) -> float:
        return as_float(nested(self.end_robot, section, key)) - as_float(
            nested(self.start_robot, section, key)
        )

    def exact_event_rates(self) -> dict[str, float]:
        counters: dict[str, float] = {
            "spikes": self.frame_counter_delta("metrics", "total_spikes"),
            "synaptic_transmissions": self.frame_counter_delta("metrics", "total_transmissions"),
            "dendritic_spikes": self.frame_counter_delta("biology", "dendritic_spikes"),
            "microglial_pruning": self.frame_counter_delta("biology", "pruning_events"),
            "microglial_repairs": self.frame_counter_delta("biology", "repair_events"),
            "growth_limited_events": self.frame_counter_delta("biology", "growth_limited_events"),
            "material_synthesized_um3": self.frame_counter_delta("biology", "material_synthesized_um3"),
            "material_recycled_um3": self.frame_counter_delta("biology", "material_recycled_um3"),
            "ltp_events": self.frame_counter_delta("physiology", "l_ltp_events"),
            "ltd_events": self.frame_counter_delta("physiology", "l_ltd_events"),
            "atp_consumed": self.physiology_counter_delta("energy", "atp_consumed"),
            "heat_pj": self.physiology_counter_delta("energy", "heat_pj"),
            "entropy_pj_k": self.physiology_counter_delta("energy", "entropy_pj_k"),
            "robot_steps": self.robot_counter_delta("training", "total_steps"),
            "motor_decisions": self.robot_counter_delta("training", "motor_decisions"),
            "policy_updates": self.robot_counter_delta("training", "policy_updates"),
            "successes": self.robot_counter_delta("training", "successes"),
            "collisions": self.robot_counter_delta("training", "collisions"),
            "safety_interventions": self.robot_counter_delta("safety", "interventions"),
            "blocked_actions": self.robot_counter_delta("safety", "blocked_actions"),
            "net_synapse_growth": self.end_synapses - self.start_synapses,
        }
        return {name: rate(value, self.elapsed_seconds) for name, value in counters.items()}

    def population_counts(self) -> dict[str, float]:
        start_frame = self.start_frame
        end_frame = self.end_frame
        def mean_len(key: str) -> float:
            start_value = start_frame.get(key, [])
            end_value = end_frame.get(key, [])
            start_count = len(start_value) if isinstance(start_value, list) else 0
            end_count = len(end_value) if isinstance(end_value, list) else 0
            return (start_count + end_count) / 2.0

        # axons in live-v3 are LOD-capped at 25k, therefore total active_synapses
        # is used for the real synaptic population rather than len(frame['axons']).
        return {
            "neurons": mean_len("neurons"),
            "dendritic_segments": mean_len("dendrites"),
            "synapses": self.mean_synapses,
            "astrocytes": mean_len("astrocytes"),
            "capillaries": mean_len("capillaries"),
            "oligodendrocytes": mean_len("oligodendrocytes"),
            "microglia": mean_len("microglia"),
        }

    def entity_tick_equivalents(self) -> dict[str, float]:
        counts = self.population_counts()
        neural_rate = self.neural_steps_per_second
        result = {name: count * neural_rate for name, count in counts.items()}
        result["total_one_sweep_equivalent"] = sum(result.values())
        return result

    def organism_pipeline_rates(self) -> dict[str, float]:
        # Source-verified one-call-per SyntheticOrganism::stepWithLoad pipeline.
        organism_rate = self.experiences_per_second
        return {
            "brain_observe_calls": organism_rate,
            "heart_steps": organism_rate,
            "lung_steps": organism_rate,
            "kidney_steps": organism_rate * 2.0,  # bilateral kidneys
            "circulation_steps": organism_rate,
            "circulation_exchange_calls": organism_rate * 5.0,
            "conservation_ledger_flux_records": organism_rate * 14.0,
            "endocrine_updates": organism_rate,
            "autonomic_interoception_updates": organism_rate,
        }

    def internal_snapshot_generation(self) -> dict[str, Any]:
        start_sample = as_int(self.start_status.get("sample"))
        end_sample = as_int(self.end_status.get("sample"))
        generated_counts: dict[str, int] = {}
        bytes_total = 0.0
        scalars_total = 0.0

        for endpoint, divisor in self.source_profile.server_refresh_divisors.items():
            if divisor is None or endpoint not in self.start_payloads or endpoint not in self.end_payloads:
                continue
            if divisor <= 1:
                count = max(0, end_sample - start_sample)
            else:
                count = max(0, end_sample // divisor - start_sample // divisor)
            generated_counts[endpoint] = count
            avg_bytes = (
                self.start_payloads[endpoint].size_bytes + self.end_payloads[endpoint].size_bytes
            ) / 2.0
            avg_scalars = (
                self.start_payloads[endpoint].scalar_values + self.end_payloads[endpoint].scalar_values
            ) / 2.0
            bytes_total += count * avg_bytes
            scalars_total += count * avg_scalars

        return {
            "generation_counts": generated_counts,
            "bytes_per_second": rate(bytes_total, self.elapsed_seconds),
            "mib_per_second": rate(bytes_total, self.elapsed_seconds) / (1024.0 * 1024.0),
            "scalar_values_serialized_per_second": rate(scalars_total, self.elapsed_seconds),
        }

    def instrumented_cpp_throughput(self) -> dict[str, Any]:
        start = self.start_throughput
        end = self.end_throughput
        if not start or not end or end.get("schema") != "tatarus-throughput-v1":
            return {"available": False}

        totals = {}
        for key in (
            "neural_ticks", "organism_steps", "entity_visits", "logical_reads",
            "logical_writes", "logical_state_ops", "logical_bytes_read",
            "logical_bytes_written", "logical_bytes_touched",
        ):
            delta = as_int(end.get(key)) - as_int(start.get(key))
            totals[key + "_delta"] = delta
            totals[key + "_per_second"] = rate(delta, self.elapsed_seconds)

        domains: dict[str, Any] = {}
        start_domains = start.get("domains", {}) if isinstance(start.get("domains"), dict) else {}
        end_domains = end.get("domains", {}) if isinstance(end.get("domains"), dict) else {}
        for name, final in end_domains.items():
            if not isinstance(final, dict):
                continue
            initial = start_domains.get(name, {}) if isinstance(start_domains.get(name), dict) else {}
            item = {}
            for key in (
                "entity_visits", "logical_reads", "logical_writes",
                "logical_bytes_read", "logical_bytes_written",
            ):
                delta = as_int(final.get(key)) - as_int(initial.get(key))
                item[key + "_delta"] = delta
                item[key + "_per_second"] = rate(delta, self.elapsed_seconds)
            item["logical_state_ops_per_second"] = (
                item["logical_reads_per_second"] + item["logical_writes_per_second"]
            )
            item["logical_bytes_touched_per_second"] = (
                item["logical_bytes_read_per_second"] + item["logical_bytes_written_per_second"]
            )
            domains[name] = item

        return {"available": True, "totals": totals, "domains": domains,
                "semantics": "Direct C++ counters of instrumented persistent-state record visits; bytes are logical record footprint, not hardware DRAM traffic."}

    def configured_browser_http_payload(self) -> dict[str, Any]:
        # This is the steady-state configured UI poll target, not a packet capture.
        # geometry is conditional in index.html and is intentionally not guessed.
        bytes_per_second = 0.0
        scalars_per_second = 0.0
        endpoint_rates: dict[str, dict[str, float]] = {}
        for endpoint, hz in self.source_profile.browser_poll_hz.items():
            if endpoint not in self.start_payloads or endpoint not in self.end_payloads:
                continue
            avg_bytes = (
                self.start_payloads[endpoint].size_bytes + self.end_payloads[endpoint].size_bytes
            ) / 2.0
            avg_scalars = (
                self.start_payloads[endpoint].scalar_values + self.end_payloads[endpoint].scalar_values
            ) / 2.0
            bps = avg_bytes * hz
            sps = avg_scalars * hz
            endpoint_rates[endpoint] = {
                "configured_hz": hz,
                "payload_bytes_per_second": bps,
                "scalar_values_per_second": sps,
            }
            bytes_per_second += bps
            scalars_per_second += sps
        return {
            "bytes_per_second": bytes_per_second,
            "mib_per_second": bytes_per_second / (1024.0 * 1024.0),
            "scalar_values_per_second": scalars_per_second,
            "per_endpoint": endpoint_rates,
            "note": "Geometrie-Requests sind zustandsabhängig und hier nicht geschätzt.",
        }

    @property
    def cpu_cycles_per_second(self) -> float | None:
        start = self.process_start.process_cycles
        end = self.process_end.process_cycles
        if start is None or end is None:
            return None
        return rate(max(0, end - start), self.elapsed_seconds)

    @property
    def cpu_core_equivalents(self) -> float | None:
        values = (
            self.process_start.user_seconds,
            self.process_start.kernel_seconds,
            self.process_end.user_seconds,
            self.process_end.kernel_seconds,
        )
        if any(value is None for value in values):
            return None
        start_cpu = float(self.process_start.user_seconds) + float(self.process_start.kernel_seconds)
        end_cpu = float(self.process_end.user_seconds) + float(self.process_end.kernel_seconds)
        return rate(max(0.0, end_cpu - start_cpu), self.elapsed_seconds)

    def process_io_rates(self) -> dict[str, float | None]:
        def delta_per_second(start: int | None, end: int | None) -> float | None:
            if start is None or end is None:
                return None
            return rate(max(0, end - start), self.elapsed_seconds)

        return {
            "read_bytes_per_second": delta_per_second(self.process_start.read_bytes, self.process_end.read_bytes),
            "write_bytes_per_second": delta_per_second(self.process_start.write_bytes, self.process_end.write_bytes),
            "other_bytes_per_second": delta_per_second(self.process_start.other_bytes, self.process_end.other_bytes),
            "read_operations_per_second": delta_per_second(self.process_start.read_operations, self.process_end.read_operations),
            "write_operations_per_second": delta_per_second(self.process_start.write_operations, self.process_end.write_operations),
            "other_operations_per_second": delta_per_second(self.process_start.other_operations, self.process_end.other_operations),
            "page_faults_per_second": delta_per_second(self.process_start.page_faults, self.process_end.page_faults),
        }

    def process_metrics(self) -> dict[str, Any]:
        cycles = self.cpu_cycles_per_second
        cycle_delta = None
        if self.process_start.process_cycles is not None and self.process_end.process_cycles is not None:
            cycle_delta = max(0, self.process_end.process_cycles - self.process_start.process_cycles)
        return {
            "pid": self.pid,
            "cpu_cycles_per_second": cycles,
            "cpu_cycles_per_experience": (
                cycle_delta / self.experiences_delta
                if cycle_delta is not None and self.experiences_delta > 0
                else None
            ),
            "cpu_cycles_per_neural_step": (
                cycle_delta / self.neural_steps_delta
                if cycle_delta is not None and self.neural_steps_delta > 0
                else None
            ),
            "cpu_core_equivalents": self.cpu_core_equivalents,
            "logical_cpu_utilization_fraction": (
                self.cpu_core_equivalents / max(1, os.cpu_count() or 1)
                if self.cpu_core_equivalents is not None
                else None
            ),
            "io": self.process_io_rates(),
            "working_set_start_bytes": self.process_start.working_set_bytes,
            "working_set_end_bytes": self.process_end.working_set_bytes,
            "private_start_bytes": self.process_start.private_bytes,
            "private_end_bytes": self.process_end.private_bytes,
            "peak_working_set_end_bytes": self.process_end.peak_working_set_bytes,
            "handle_count_start": self.process_start.handle_count,
            "handle_count_end": self.process_end.handle_count,
        }

    def to_dict(self) -> dict[str, Any]:
        steps_per_experience = (
            self.neural_steps_delta / self.experiences_delta if self.experiences_delta else None
        )
        events = self.exact_event_rates()
        internal_serialization = self.internal_snapshot_generation()
        browser_payload = self.configured_browser_http_payload()
        process = self.process_metrics()
        instrumented = self.instrumented_cpp_throughput()
        population_counts = self.population_counts()
        entity_equivalents = self.entity_tick_equivalents()
        pipeline = self.organism_pipeline_rates()

        return {
            "schema": "tatarus-4-live-full-system-throughput-v3",
            "url": self.url,
            "pid": self.pid,
            "elapsed_seconds": self.elapsed_seconds,
            "source_profile": self.source_profile.to_dict(),
            "simulation": {
                "experiences_start": as_int(self.start_frame.get("experiences")),
                "experiences_end": as_int(self.end_frame.get("experiences")),
                "experiences_delta": self.experiences_delta,
                "experiences_per_second": self.experiences_per_second,
                "milliseconds_per_experience": self.milliseconds_per_experience,
                "neural_steps_start": as_int(self.start_frame.get("step")),
                "neural_steps_end": as_int(self.end_frame.get("step")),
                "neural_steps_delta": self.neural_steps_delta,
                "neural_steps_per_second": self.neural_steps_per_second,
                "neural_steps_per_experience": steps_per_experience,
                "runtime_samples_delta": self.sample_delta,
                "synapses_start": self.start_synapses,
                "synapses_end": self.end_synapses,
                "synapses_mean": self.mean_synapses,
                "sdk_version_reported": self.end_frame.get("sdk_version"),
            },
            "exact_event_rates_per_second": events,
            "population_counts_mean": population_counts,
            "entity_tick_equivalents_per_second": entity_equivalents,
            "organism_pipeline_calls_per_second": pipeline,
            "internal_snapshot_serialization": internal_serialization,
            "configured_browser_http_payload": browser_payload,
            "whole_process": process,
            "instrumented_cpp_throughput": instrumented,
            "measurement_semantics": {
                "exact_full_process_compute_metric": "whole_process.cpu_cycles_per_second",
                "exact_external_simulation_metric": "simulation.experiences_per_second",
                "exact_internal_event_metrics": "exact_event_rates_per_second",
                "direct_cpp_state_metric": "instrumented_cpp_throughput.totals.logical_state_ops_per_second",
                "source_derived_entity_metric": "entity_tick_equivalents_per_second",
                "important": (
                    "CPU cycles/s covers the entire server process. Entity-tick equivalents count one population "
                    "sweep per neural tick and are not a claim about every C++ load/store. Exact internal RAM "
                    "C++ counters now expose instrumented logical state operations; hardware PMU tracing is still required for physical cache/DRAM traffic."
                ),
            },
        }


def run_live_loaded_benchmark(
    base_url: str,
    seconds: float,
    pid: int | None,
    settle_seconds: float,
    control_runtime: bool,
    source_profile: SourceProfile,
) -> LiveResult:
    parsed = urlparse(base_url)
    port = parsed.port or (443 if parsed.scheme == "https" else 80)
    resolved_pid = pid if pid is not None else discover_windows_pid_for_port(port)

    if control_runtime:
        http_control(base_url, "pause")
        time.sleep(settle_seconds)

    start_payloads = capture_live_payloads(base_url)
    start_process = sample_process(resolved_pid)

    started = time.perf_counter()
    if control_runtime:
        http_control(base_url, "resume")
    try:
        time.sleep(seconds)
    finally:
        if control_runtime:
            http_control(base_url, "pause")
    elapsed = time.perf_counter() - started

    time.sleep(settle_seconds)
    end_process = sample_process(resolved_pid)
    end_payloads = capture_live_payloads(base_url)

    return LiveResult(
        url=base_url,
        pid=resolved_pid,
        elapsed_seconds=elapsed,
        source_profile=source_profile,
        start_payloads=start_payloads,
        end_payloads=end_payloads,
        process_start=start_process,
        process_end=end_process,
    )


def print_live_result(result: LiveResult) -> None:
    payload = result.to_dict()
    simulation = payload["simulation"]
    events = payload["exact_event_rates_per_second"]
    populations = payload["population_counts_mean"]
    entity = payload["entity_tick_equivalents_per_second"]
    pipeline = payload["organism_pipeline_calls_per_second"]
    internal = payload["internal_snapshot_serialization"]
    browser = payload["configured_browser_http_payload"]
    process = payload["whole_process"]
    instrumented = payload["instrumented_cpp_throughput"]
    io = process["io"]

    print()
    print("=" * 86)
    print("TATARUS 4 - LIVE-LOADED FULL-SYSTEM THROUGHPUT · SOURCE-AWARE v3 · C++ INSTRUMENTED")
    print("=" * 86)
    print(f"URL                              : {result.url}")
    print(f"Server-PID                       : {result.pid if result.pid is not None else 'nicht erkannt'}")
    print(f"Reale Messzeit                   : {result.elapsed_seconds:,.3f} s")
    print(f"SDK laut Live-State              : {simulation['sdk_version_reported'] or 'unbekannt'}")
    if result.source_profile.project_root:
        print(f"Projektquelle                    : {result.source_profile.project_root}")
    if result.source_profile.warnings:
        print("Quellcode-Hinweise               : " + " | ".join(result.source_profile.warnings))

    print()
    print("A) EXAKT GEMESSENER GESAMTORGANISMUS-DURCHSATZ")
    print(f"Erfahrungen                      : {simulation['experiences_delta']:,}")
    print(f"Erfahrungen / s                  : {simulation['experiences_per_second']:,.6f}")
    print(f"ms / Erfahrung                   : {simulation['milliseconds_per_experience']:,.3f}")
    print(f"Neuronale Schritte               : {simulation['neural_steps_delta']:,}")
    print(f"Neuronale Schritte / s           : {simulation['neural_steps_per_second']:,.3f}")
    if simulation["neural_steps_per_experience"] is not None:
        print(f"Steps / Erfahrung                : {simulation['neural_steps_per_experience']:,.6f}")
    print(
        f"Synapsen                         : {simulation['synapses_start']:,} -> "
        f"{simulation['synapses_end']:,} (Ø {simulation['synapses_mean']:,.1f})"
    )
    print(f"Netto-Synapsenwachstum / s       : {events['net_synapse_growth']:,.6f}")

    print()
    print("B) EXAKT GEMESSENE INTERNE EREIGNISRATEN")
    labels = (
        ("Neuronale Spikes / s", "spikes"),
        ("Synaptische Transmissionen / s", "synaptic_transmissions"),
        ("Dendritische Spikes / s", "dendritic_spikes"),
        ("LTP-Ereignisse / s", "ltp_events"),
        ("LTD-Ereignisse / s", "ltd_events"),
        ("Mikroglia-Pruning / s", "microglial_pruning"),
        ("Mikroglia-Reparaturen / s", "microglial_repairs"),
        ("Wachstum limitiert / s", "growth_limited_events"),
        ("Material synth. µm³ / s", "material_synthesized_um3"),
        ("Material recycelt µm³ / s", "material_recycled_um3"),
        ("Motorentscheidungen / s", "motor_decisions"),
        ("Policy-Updates / s", "policy_updates"),
        ("Safety-Eingriffe / s", "safety_interventions"),
    )
    for label, key in labels:
        print(f"{label:<34}: {events[key]:,.3f}")

    print()
    print("C) QUELLCODE-BASIERTE POPULATIONSARBEIT · EIN VOLLER SWEEP JE NEURAL-TICK")
    print("   Diese Werte sind Entity-Tick-Äquivalente, keine erfundenen CPU-Load/Store-Zähler.")
    for key, label in (
        ("neurons", "Neuronen"),
        ("dendritic_segments", "Dendritensegmente"),
        ("synapses", "Synapsen"),
        ("astrocytes", "Astrozyten"),
        ("capillaries", "Kapillaren"),
        ("oligodendrocytes", "Oligodendrozyten"),
        ("microglia", "Mikroglia"),
    ):
        print(f"{label:<34}: Ø {populations[key]:,.1f} · {human_rate(entity[key])}")
    print(f"{'SUMME 1-Sweep-Äquivalent':<34}: {human_rate(entity['total_one_sweep_equivalent'])}")

    print()
    print("D) AKTUELLE SYNTHETIC-ORGANISM-PIPELINE · CALL-RATEN")
    for key, label in (
        ("brain_observe_calls", "Brain observe"),
        ("heart_steps", "Herz-Step"),
        ("lung_steps", "Lungen-Step"),
        ("kidney_steps", "Nieren-Steps (links+rechts)"),
        ("circulation_steps", "Kreislauf-Step"),
        ("circulation_exchange_calls", "Stoffaustausch-Calls"),
        ("conservation_ledger_flux_records", "Conservation-Ledger-Fluxes"),
        ("endocrine_updates", "Endokrine Updates"),
        ("autonomic_interoception_updates", "Autonomik/Interozeption"),
    ):
        print(f"{label:<34}: {pipeline[key]:,.3f} /s")

    print()
    print("E) DIREKT IN C++ GEZÄHLTER LOGISCHER GESAMT-STATE-DURCHSATZ")
    if instrumented.get("available"):
        totals = instrumented["totals"]
        print(f"Entity-Record-Besuche / s         : {totals['entity_visits_per_second']:,.0f}")
        print(f"Logische State-Reads / s          : {totals['logical_reads_per_second']:,.0f}")
        print(f"Logische State-Writes / s         : {totals['logical_writes_per_second']:,.0f}")
        print(f"TOTAL LOGICAL STATE OPS / s       : {totals['logical_state_ops_per_second']:,.0f}")
        print(f"Logische State-Bytes gelesen / s  : {human_bytes(totals['logical_bytes_read_per_second'], '/s')}")
        print(f"Logische State-Bytes geschrieben/s: {human_bytes(totals['logical_bytes_written_per_second'], '/s')}")
        print(f"TOTAL LOGICAL STATE BYTES / s     : {human_bytes(totals['logical_bytes_touched_per_second'], '/s')}")
        print("  Nach Subsystem:")
        for name, domain in instrumented["domains"].items():
            ops = domain["logical_state_ops_per_second"]
            bytes_s = domain["logical_bytes_touched_per_second"]
            if ops > 0.0 or bytes_s > 0.0:
                print(f"    {name:<18}: {ops:>14,.0f} ops/s · {human_bytes(bytes_s, '/s')}")
    else:
        print("nicht verfügbar – neue DLL mit Throughput-Instrumentierung bauen/starten")

    print()
    print("F) DATENBEWEGUNG DES LIVE-SYSTEMS")
    print("Interne JSON-Snapshot-Erzeugung (echte server.py-Cadence):")
    print(f"  serialisierte Bytes / s        : {internal['bytes_per_second']:,.0f}")
    print(f"  serialisierte MiB / s          : {internal['mib_per_second']:,.3f}")
    print(f"  skalare Werte serialisiert / s : {internal['scalar_values_serialized_per_second']:,.0f}")
    print(f"  erzeugte Snapshots             : {internal['generation_counts']}")
    print("Konfigurierter Browser-HTTP-Payload (index.html Polling, ohne konditionale Geometrie):")
    print(f"  Payload Bytes / s              : {browser['bytes_per_second']:,.0f}")
    print(f"  Payload MiB / s                : {browser['mib_per_second']:,.3f}")
    print(f"  skalare JSON-Werte / s         : {browser['scalar_values_per_second']:,.0f}")

    print()
    print("G) VOLLSTÄNDIGE PROZESSWEITE RECHENARBEIT · WINDOWS")
    print(f"CPU-Zyklen / s                   : {human_rate(process['cpu_cycles_per_second'])}")
    if process["cpu_cycles_per_experience"] is not None:
        print(f"CPU-Zyklen / Erfahrung           : {process['cpu_cycles_per_experience']:,.0f}")
    else:
        print("CPU-Zyklen / Erfahrung           : nicht verfügbar")
    if process["cpu_cycles_per_neural_step"] is not None:
        print(f"CPU-Zyklen / Neural-Step         : {process['cpu_cycles_per_neural_step']:,.0f}")
    else:
        print("CPU-Zyklen / Neural-Step         : nicht verfügbar")
    if process["cpu_core_equivalents"] is not None:
        print(f"CPU-Kernäquivalente              : {process['cpu_core_equivalents']:,.3f}")
        print(f"Anteil aller logischen CPUs      : {100.0 * process['logical_cpu_utilization_fraction']:,.2f} %")
    else:
        print("CPU-Kernäquivalente              : nicht verfügbar")
    print(f"Working Set Ende                 : {human_bytes(process['working_set_end_bytes'])}")
    print(f"Private Bytes Ende               : {human_bytes(process['private_end_bytes'])}")
    print(f"Peak Working Set                 : {human_bytes(process['peak_working_set_end_bytes'])}")
    print(f"OS Read Bytes / s                : {human_bytes(io['read_bytes_per_second'], '/s')}")
    print(f"OS Write Bytes / s               : {human_bytes(io['write_bytes_per_second'], '/s')}")
    print(f"OS Other Bytes / s               : {human_bytes(io['other_bytes_per_second'], '/s')}")
    print(f"Page Faults / s                  : {io['page_faults_per_second'] if io['page_faults_per_second'] is not None else 'nicht verfügbar'}")

    print()
    print("H) WAS IST JETZT 'VOLLSTÄNDIG'?")
    if process["cpu_cycles_per_second"] is not None:
        print("  ✓ Vollständige Prozess-Rechenarbeit: CPU-Zyklen/s umfasst den gesamten Serverprozess")
        print("    inklusive Nervensystem, Plastizität, Gewebe, Herz, Lunge, beide Nieren,")
        print("    Kreislauf, Roboterwelt, Python-Orchestrierung und JSON-Erzeugung.")
    else:
        print("  ! Server-PID/Windows-Prozesscounter fehlen; --pid <PID> kann manuell gesetzt werden.")
    print("  ✓ Simulations-, Ereignis-, Organpipeline- und Serialisierungsraten werden separat gemessen.")
    print("  ✓ Direkte C++-Counter liefern jetzt den instrumentierten logischen State-Durchsatz.")
    print("  ! Die gezählten State-Bytes sind logische Record-Footprints, keine Hardware-DRAM-Bytes.")
    print("    Hardware-PMU/ETW wäre zusätzlich nötig, um physische Cache/DRAM-Transfers zu messen.")
    print("    bezeichnet deshalb keine heuristische Synapsenformel als 'Gesamtdatendurchsatz'.")
    print("=" * 86)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="TATARUS 4 Skalierungs- und Live-Gesamtsystem-Benchmark"
    )
    parser.add_argument("--seconds", type=float, default=10.0, help="Messdauer in Sekunden")
    parser.add_argument("--seed", type=int, default=7411)
    parser.add_argument("--dt", type=float, default=0.05, help="Organismus-Zeitschritt in Sekunden")
    parser.add_argument("--warmup-steps", type=int, default=5)
    parser.add_argument("--library", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--live-loaded",
        action="store_true",
        help="geladenen Zustand des laufenden Live-Monitors messen statt neuen Organismus zu erzeugen",
    )
    parser.add_argument(
        "--url",
        default="http://127.0.0.1:8765",
        help="Basis-URL des TATARUS Live-Monitors",
    )
    parser.add_argument(
        "--pid",
        type=int,
        help="optional: PID des Live-Monitor-Prozesses; Windows erkennt sie sonst über den Listen-Port",
    )
    parser.add_argument(
        "--settle-ms",
        type=float,
        default=300.0,
        help="Wartezeit nach Pause vor Snapshot-Aufnahme",
    )
    parser.add_argument(
        "--no-control",
        action="store_true",
        help="Live-Monitor nicht automatisch pausieren/fortsetzen; nur passiv messen",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_arguments()
    if args.seconds <= 0.0:
        raise ValueError("--seconds muss > 0 sein")

    script_path = Path(__file__).resolve()
    source_profile = build_source_profile(script_path)

    if args.live_loaded:
        result = run_live_loaded_benchmark(
            base_url=args.url,
            seconds=args.seconds,
            pid=args.pid,
            settle_seconds=max(0.0, args.settle_ms / 1000.0),
            control_runtime=not args.no_control,
            source_profile=source_profile,
        )
        print_live_result(result)
        output_path = (
            args.output
            or Path.cwd() / "output" / "benchmarks" / "tatarus4_live_full_system_throughput_v3.json"
        ).resolve()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(
                {"created_utc": datetime.now(timezone.utc).isoformat(), **result.to_dict()},
                indent=2,
                ensure_ascii=False,
            ),
            encoding="utf-8",
        )
        print(f"JSON-Ergebnis                    : {output_path}")
        return 0

    if OrganismTelemetry is None:
        raise RuntimeError(
            "Für den klassischen DLL-Benchmark muss benchmark.py im tools/live_monitor-Verzeichnis "
            "neben server.py liegen."
        )

    project_root = _find_project_root(script_path)
    if project_root is None:
        raise RuntimeError("TATARUS-Projektwurzel nicht gefunden")
    candidates = [
        project_root / "build" / "Release" / "tatarus4_c.dll",
        project_root / "build-live-monitor" / "Release" / "tatarus4_c.dll",
    ]
    library_path = (
        args.library.resolve()
        if args.library
        else next((path for path in candidates if path.is_file()), candidates[0])
    )
    if not library_path.is_file():
        raise FileNotFoundError(f"DLL nicht gefunden: {library_path}\nRelease-Build zuerst erstellen.")

    output_path = (
        args.output or project_root / "output" / "benchmarks" / "tatarus4_scaling.json"
    ).resolve()

    print("TATARUS 4 Synthetic-Organism Skalierungsbenchmark")
    print(f"DLL: {library_path}")
    print(f"Messzeit: {args.seconds:.1f} s pro Profil/Modus; dt={args.dt:.3f} s")
    results: list[ScalingResult] = []
    for profile, neurons in PROFILES.items():
        for mode in ("core", "telemetry"):
            print(f"Teste {profile}: {neurons:,} Neuronen / {mode} ...", flush=True)
            results.append(
                run_scaling_benchmark(
                    library_path,
                    profile,
                    neurons,
                    mode,
                    args.seconds,
                    args.seed,
                    args.dt,
                    args.warmup_steps,
                )
            )
    print_scaling_results(results)

    payload = {
        "schema": "tatarus-4-synthetic-organism-scaling-v2",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "seed": args.seed,
        "seconds_per_run": args.seconds,
        "dt_seconds": args.dt,
        "warmup_steps": args.warmup_steps,
        "library": str(library_path),
        "source_profile": source_profile.to_dict(),
        "results": [result.to_dict() for result in results],
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\nJSON-Ergebnis: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
