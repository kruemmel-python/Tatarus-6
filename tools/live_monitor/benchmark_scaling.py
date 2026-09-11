from __future__ import annotations

import argparse
import ctypes
import json
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from server import (
    AtmosphericEnvironment,
    MotorState,
    Observation,
    OrganismTelemetry,
    ProductState,
    RobotPhysicalLoad,
    RobotWorld,
)


PROFILES: dict[str, int] = {
    "compact": 96,
    "standard": 384,
    "large": 1_536,
    "research": 6_144,
}


@dataclass(slots=True, frozen=True)
class Result:
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
        return self.external_steps / self.elapsed_seconds

    @property
    def neural_steps_per_second(self) -> float:
        return self.neural_steps / self.elapsed_seconds

    @property
    def milliseconds_per_external_step(self) -> float:
        return 1000.0 / self.steps_per_second

    def to_dict(self) -> dict[str, Any]:
        data = asdict(self)
        data.update({
            "external_steps_per_second": self.steps_per_second,
            "neural_steps_per_second": self.neural_steps_per_second,
            "milliseconds_per_external_step": self.milliseconds_per_external_step,
            "synapse_delta": self.final_synapses - self.initial_synapses,
        })
        return data


def configure_library(library: ctypes.CDLL) -> None:
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


def json_snapshot(library: ctypes.CDLL, handle: ctypes.c_void_p, function_name: str) -> dict[str, Any]:
    function = getattr(library, function_name)
    required = int(function(handle, None, 0))
    if required <= 1:
        raise RuntimeError(f"{function_name}: {last_error(library)}")
    buffer = ctypes.create_string_buffer(required)
    returned = int(function(handle, buffer, required))
    if returned != required:
        raise RuntimeError(f"{function_name}: ungültige Snapshot-Größe")
    return json.loads(bytes(buffer.raw[: required - 1]))


def earth_atmosphere() -> AtmosphericEnvironment:
    return AtmosphericEnvironment(101.325, 0.2095, 0.0004, 0.7808, 22.0, 0.50, 10.0)


def execute_step(
    library: ctypes.CDLL,
    handle: ctypes.c_void_p,
    world: RobotWorld,
    motor_before: MotorState,
    atmosphere: AtmosphericEnvironment,
    tick: int,
    dt_seconds: float,
) -> MotorState:
    observation = world.advance(motor_before)
    observation.timestamp_ns = int(tick * dt_seconds * 1_000_000_000)
    action_id = world.motor_selected_direction + 1
    check(library, library.tatarus_organism_begin_action(
        handle, action_id, max(0.05, motor_before.confidence)
    ), "tatarus_organism_begin_action")

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
    check(library, library.tatarus_organism_step_with_load(
        handle,
        ctypes.byref(observation),
        ctypes.byref(atmosphere),
        ctypes.byref(load),
        dt_seconds,
        ctypes.byref(product),
    ), "tatarus_organism_step_with_load")

    motor_after = MotorState()
    check(library, library.tatarus_organism_get_motor(handle, ctypes.byref(motor_after)),
          "tatarus_organism_get_motor")
    check(library, library.tatarus_organism_end_action(
        handle,
        action_id,
        world.last_reward,
        1.0 if world.position == world.goal else 0.0,
        1.0 if world.last_new_cell else 0.0,
    ), "tatarus_organism_end_action")
    world.observe_neural(product, motor_after)
    return motor_after


def run_benchmark(
    library_path: Path,
    profile: str,
    neurons: int,
    mode: str,
    seconds: float,
    seed: int,
    dt_seconds: float,
    warmup_steps: int,
) -> Result:
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
        check(library, library.tatarus_organism_get_motor(handle, ctypes.byref(motor)),
              "tatarus_organism_get_motor")
        tick = 0
        for _ in range(warmup_steps):
            tick += 1
            motor = execute_step(library, handle, world, motor, atmosphere, tick, dt_seconds)

        initial_live = json_snapshot(library, handle, "tatarus_organism_get_live_json")
        initial_body = OrganismTelemetry()
        check(library, library.tatarus_organism_get_telemetry(handle, ctypes.byref(initial_body)),
              "tatarus_organism_get_telemetry")

        measured_steps = 0
        start = time.perf_counter()
        while time.perf_counter() - start < seconds:
            tick += 1
            motor = execute_step(library, handle, world, motor, atmosphere, tick, dt_seconds)
            measured_steps += 1
            if mode == "telemetry":
                world.snapshot()
                json_snapshot(library, handle, "tatarus_organism_get_live_json")
                if measured_steps % 5 == 0:
                    json_snapshot(library, handle, "tatarus_organism_get_physiology_json")
                    json_snapshot(library, handle, "tatarus_organism_get_json")
                if measured_steps % 20 == 0:
                    json_snapshot(library, handle, "tatarus_organism_get_spatial_json")
        elapsed = time.perf_counter() - start

        final_live = json_snapshot(library, handle, "tatarus_organism_get_live_json")
        final_body = OrganismTelemetry()
        check(library, library.tatarus_organism_get_telemetry(handle, ctypes.byref(final_body)),
              "tatarus_organism_get_telemetry")
        metrics0 = initial_live.get("metrics", {})
        metrics1 = final_live.get("metrics", {})
        biology0 = initial_live.get("biology", {})
        biology1 = final_live.get("biology", {})
        physiology1 = final_live.get("physiology", {})
        simulated_seconds = final_body.simulated_time_seconds - initial_body.simulated_time_seconds
        neural_steps = int(final_live.get("step", 0)) - int(initial_live.get("step", 0))

        return Result(
            profile=profile,
            neurons=neurons,
            mode=mode,
            init_seconds=init_seconds,
            external_steps=measured_steps,
            neural_steps=neural_steps,
            simulated_seconds=simulated_seconds,
            elapsed_seconds=elapsed,
            realtime_factor=simulated_seconds / elapsed,
            initial_synapses=int(metrics0.get("active_synapses", 0)),
            final_synapses=int(metrics1.get("active_synapses", 0)),
            spike_delta=int(metrics1.get("total_spikes", 0)) - int(metrics0.get("total_spikes", 0)),
            initial_myelin=float(biology0.get("myelin_coverage", 0.0)),
            final_myelin=float(biology1.get("myelin_coverage", 0.0)),
            final_brain_atp=float(physiology1.get("atp", 0.0)),
            final_map_mm_hg=float(final_body.mean_arterial_pressure_mm_hg),
            final_sao2=float(final_body.arterial_oxygen_saturation),
            final_cardiac_output_l_min=float(final_body.cardiac_output_l_per_min),
            final_gfr_ml_min=float(final_body.glomerular_filtration_rate_ml_per_min),
            final_left_gfr_ml_min=float(final_body.left_kidney_gfr_ml_per_min),
            final_right_gfr_ml_min=float(final_body.right_kidney_gfr_ml_per_min),
        )
    finally:
        library.tatarus_organism_destroy(handle)


def print_results(results: list[Result]) -> None:
    print()
    print(f"{'Profil':<11}{'N':>7}{'Modus':>11}{'Init[s]':>10}{'Org/s':>11}{'Neural/s':>12}{'ms/Org':>11}{'RT-Faktor':>11}{'GFR L/R':>17}")
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


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="TATARUS 4 Synthetic-Organism Skalierungsbenchmark")
    parser.add_argument("--seconds", type=float, default=10.0, help="Messdauer pro Profil und Modus")
    parser.add_argument("--seed", type=int, default=7411)
    parser.add_argument("--dt", type=float, default=0.05, help="Organismus-Zeitschritt in Sekunden")
    parser.add_argument("--warmup-steps", type=int, default=5)
    parser.add_argument("--library", type=Path)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_arguments()
    script_directory = Path(__file__).resolve().parent
    project_root = script_directory.parent.parent
    candidates = [
        project_root / "build" / "Release" / "tatarus4_c.dll",
        project_root / "build-live-monitor" / "Release" / "tatarus4_c.dll",
    ]
    library_path = args.library.resolve() if args.library else next((p for p in candidates if p.is_file()), candidates[0])
    if not library_path.is_file():
        raise FileNotFoundError(f"DLL nicht gefunden: {library_path}\nRelease-Build zuerst erstellen.")
    output_path = (args.output or project_root / "output" / "benchmarks" / "tatarus4_scaling.json").resolve()

    print("TATARUS 4 Synthetic-Organism Skalierungsbenchmark")
    print(f"DLL: {library_path}")
    print(f"Messzeit: {args.seconds:.1f} s pro Profil/Modus; dt={args.dt:.3f} s")
    results: list[Result] = []
    for profile, neurons in PROFILES.items():
        for mode in ("core", "telemetry"):
            print(f"Teste {profile}: {neurons:,} Neuronen / {mode} ...", flush=True)
            results.append(run_benchmark(
                library_path, profile, neurons, mode, args.seconds, args.seed,
                args.dt, args.warmup_steps,
            ))
    print_results(results)

    payload = {
        "schema": "tatarus-4-synthetic-organism-scaling-v1",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "seed": args.seed,
        "seconds_per_run": args.seconds,
        "dt_seconds": args.dt,
        "warmup_steps": args.warmup_steps,
        "library": str(library_path),
        "results": [result.to_dict() for result in results],
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(f"\nJSON-Ergebnis: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
