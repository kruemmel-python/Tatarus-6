#!/usr/bin/env python3
"""Local TATARUS organism loop and HTTP bridge for the live 3D monitor."""

from __future__ import annotations

import argparse
import base64
import ctypes
import hashlib
import json
import math
import mimetypes
import random
import re
import shutil
import struct
import tempfile
import threading
import time
import uuid
import webbrowser
import zlib
from collections import deque
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


TISSUE_PROFILES = {
    "compact": 96,
    "standard": 384,
    "large": 1536,
    "research": 6144,
}

MAP_SLOT_LIMIT = 10


class TatarusSceneObject(ctypes.Structure):
    _fields_ = [
        ("instance", ctypes.c_char_p), ("category", ctypes.c_char_p),
        ("pose", ctypes.c_char_p), ("present_mask", ctypes.c_uint32),
        ("x", ctypes.c_double), ("y", ctypes.c_double),
        ("scale", ctypes.c_double), ("rotation", ctypes.c_double),
        ("depth", ctypes.c_double),
    ]


class TatarusSceneRelation(ctypes.Structure):
    _fields_ = [
        ("subject", ctypes.c_char_p), ("relation_kind", ctypes.c_uint32),
        ("object", ctypes.c_char_p), ("confidence", ctypes.c_double),
    ]


class TatarusSceneDescription(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char_p),
        ("objects", ctypes.POINTER(TatarusSceneObject)),
        ("object_count", ctypes.c_uint64),
        ("relations", ctypes.POINTER(TatarusSceneRelation)),
        ("relation_count", ctypes.c_uint64),
        ("background_red", ctypes.c_double),
        ("background_green", ctypes.c_double),
        ("background_blue", ctypes.c_double),
    ]


class TatarusSceneAction(ctypes.Structure):
    _fields_ = [
        ("action_kind", ctypes.c_uint32), ("label", ctypes.c_char_p),
        ("actor", ctypes.c_char_p), ("target", ctypes.c_char_p),
        ("direction_x", ctypes.c_double), ("direction_y", ctypes.c_double),
        ("magnitude", ctypes.c_double), ("duration", ctypes.c_double),
    ]


SCENE_RELATIONS = {
    "left_of": 0, "right_of": 1, "above": 2, "below": 3,
    "in_front_of": 4, "behind": 5, "inside": 6, "contains": 7,
    "touching": 8, "overlapping": 9, "near": 10, "far": 11,
    "looking_at": 12, "connected_to": 13,
}
SCENE_ACTIONS = {
    "move": 0, "kick": 1, "push": 2, "pull": 3, "fall": 4,
    "rise": 5, "approach": 6, "depart": 7, "custom": 8,
}

CORTEX_COMMANDS = {
    "analyze": 1,
    "imagine": 2,
    "hybrid": 3,
    "plan": 4,
    "autonomous_tick": 5,
    "poll": 6,
    "execute_imagination": 7,
}


def default_map_catalog() -> list[dict[str, Any]]:
    """Return ten deterministic, editable navigation maps."""
    width, depth = 20, 15
    start, goal = (1, 1), (18, 13)

    def vertical_walls(columns: tuple[int, ...], gaps: tuple[tuple[int, ...], ...]) -> set[tuple[int, int]]:
        result: set[tuple[int, int]] = set()
        for column, openings in zip(columns, gaps):
            result.update((column, z) for z in range(1, depth - 1) if z not in openings)
        return result

    maps: list[tuple[str, set[tuple[int, int]]]] = []

    classic = vertical_walls((5, 10, 15), ((3, 10), (5, 12), (2, 8, 13)))
    classic.update((x, 8) for x in range(6, 10) if x != 8)
    classic.update((x, 4) for x in range(11, 15) if x != 13)
    maps.append(("Klassischer Parcours", classic))

    maps.append(("Wechselnde Tore", vertical_walls(
        (4, 8, 12, 16), ((3, 11), (6, 12), (2, 9), (5, 12))
    )))

    horizontal: set[tuple[int, int]] = set()
    for row, openings in ((3, (3, 16)), (6, (6, 13)), (9, (3, 10, 17)), (12, (7, 15))):
        horizontal.update((x, row) for x in range(1, width - 1) if x not in openings)
    maps.append(("Horizontale Schleusen", horizontal))

    rooms: set[tuple[int, int]] = set()
    rooms.update((x, 5) for x in range(2, 18) if x not in (4, 10, 16))
    rooms.update((x, 10) for x in range(2, 18) if x not in (3, 8, 14))
    rooms.update((7, z) for z in range(1, 14) if z not in (3, 8, 12))
    rooms.update((13, z) for z in range(1, 14) if z not in (2, 7, 11))
    maps.append(("Räume und Türen", rooms))

    islands: set[tuple[int, int]] = set()
    for center_x, center_z in ((5, 4), (10, 8), (15, 4), (5, 11), (15, 11)):
        islands.update(
            (center_x + dx, center_z + dz)
            for dx in (-1, 0, 1)
            for dz in (-1, 0, 1)
            if abs(dx) + abs(dz) <= 1
        )
    maps.append(("Hindernis-Inseln", islands))

    zigzag: set[tuple[int, int]] = set()
    for column, gap in ((4, 12), (7, 3), (10, 11), (13, 4), (16, 10)):
        zigzag.update((column, z) for z in range(1, depth - 1) if z not in (gap, gap + 1))
    maps.append(("Zickzack-Korridor", zigzag))

    pillars = {
        (x, z)
        for x in range(3, 18, 3)
        for z in range(3, 14, 3)
        if (x + z) % 4 != 0
    }
    maps.append(("Säulenfeld", pillars))

    spiral: set[tuple[int, int]] = set()
    spiral.update((x, 3) for x in range(3, 17) if x != 4)
    spiral.update((16, z) for z in range(3, 12) if z != 10)
    spiral.update((x, 11) for x in range(5, 17) if x != 14)
    spiral.update((5, z) for z in range(6, 12) if z != 7)
    spiral.update((x, 6) for x in range(5, 13) if x != 11)
    maps.append(("Offene Spirale", spiral))

    gates: set[tuple[int, int]] = set()
    gates.update((x, z) for x in (5, 10, 15) for z in range(2, 13))
    for opening in ((5, 2), (5, 7), (10, 5), (10, 11), (15, 3), (15, 9)):
        gates.discard(opening)
    maps.append(("Sechs Tore", gates))

    transfer = {(4, 4), (4, 5), (8, 9), (9, 9), (13, 5), (14, 5), (16, 10)}
    maps.append(("Transfer-Test", transfer))

    catalog: list[dict[str, Any]] = []
    for index, (name, obstacles) in enumerate(maps, start=1):
        obstacles.discard(start)
        obstacles.discard(goal)
        catalog.append(
            {
                "id": f"map-{index}",
                "name": name,
                "width": width,
                "depth": depth,
                "cell_size": 1.0,
                "start": list(start),
                "goal": list(goal),
                "obstacles": [list(value) for value in sorted(obstacles)],
                "revision": 1,
            }
        )
    return catalog


class Observation(ctypes.Structure):
    _fields_ = [
        ("timestamp_ns", ctypes.c_uint64),
        ("acceleration", ctypes.c_double * 3),
        ("rotation", ctypes.c_double * 3),
        ("light", ctypes.c_double),
        ("sound_level", ctypes.c_double),
        ("temperature", ctypes.c_double),
        ("battery", ctypes.c_double),
        ("novelty", ctypes.c_double),
        ("reward", ctypes.c_double),
        ("goal_direction", ctypes.c_double * 2),
        ("proximity", ctypes.c_double * 3),
    ]


class ProductState(ctypes.Structure):
    _fields_ = [
        ("assembly_id", ctypes.c_uint64),
        ("predicted_assembly_id", ctypes.c_uint64),
        ("prediction_confidence", ctypes.c_double),
        ("prediction_error", ctypes.c_double),
        ("learned_transitions", ctypes.c_uint64),
        ("experiences", ctypes.c_uint64),
        ("predictions", ctypes.c_uint64),
    ]


class MotorState(ctypes.Structure):
    _fields_ = [
        ("available", ctypes.c_int32),
        ("selected_direction", ctypes.c_uint32),
        ("directional_activity", ctypes.c_double * 4),
        ("movement", ctypes.c_double),
        ("attention", ctypes.c_double),
        ("vocalization", ctypes.c_double),
        ("confidence", ctypes.c_double),
    ]


class Pose3D(ctypes.Structure):
    _fields_ = [
        ("position_m", ctypes.c_double * 3),
        ("orientation_xyzw", ctypes.c_double * 4),
    ]


class RangeReading(ctypes.Structure):
    _fields_ = [
        ("direction", ctypes.c_double * 3),
        ("distance_m", ctypes.c_double),
        ("max_range_m", ctypes.c_double),
        ("confidence", ctypes.c_double),
        ("hit", ctypes.c_int32),
        ("semantic_label", ctypes.c_char_p),
    ]


class CartographyUpdate(ctypes.Structure):
    _fields_ = [
        ("environment_id", ctypes.c_uint64),
        ("accepted_rays", ctypes.c_uint64),
        ("rejected_rays", ctypes.c_uint64),
        ("touched_voxels", ctypes.c_uint64),
        ("newly_mapped_voxels", ctypes.c_uint64),
        ("occupied_endpoints", ctypes.c_uint64),
        ("mapped_voxels", ctypes.c_uint64),
        ("free_voxels", ctypes.c_uint64),
        ("occupied_voxels", ctypes.c_uint64),
        ("uncertain_voxels", ctypes.c_uint64),
        ("frontier_voxels", ctypes.c_uint64),
        ("local_novelty", ctypes.c_double),
        ("frontier_ratio", ctypes.c_double),
        ("obstacle_proximity", ctypes.c_double * 6),
    ]


class InterventionState(ctypes.Structure):
    _fields_ = [
        ("astrocyte_function", ctypes.c_double),
        ("oxygen_supply", ctypes.c_double),
        ("glucose_supply", ctypes.c_double),
        ("pump_efficiency", ctypes.c_double),
        ("myelin_integrity", ctypes.c_double),
        ("microglia_function", ctypes.c_double),
        ("neuromodulator_gain", ctypes.c_double),
        ("sleep_enabled", ctypes.c_int32),
    ]


class AtmosphericEnvironment(ctypes.Structure):
    _fields_ = [
        ("barometric_pressure_kpa", ctypes.c_double),
        ("o2_fraction", ctypes.c_double),
        ("co2_fraction", ctypes.c_double),
        ("n2_fraction", ctypes.c_double),
        ("ambient_temperature_c", ctypes.c_double),
        ("relative_humidity", ctypes.c_double),
        ("dust_ppm", ctypes.c_double),
    ]


class RobotPhysicalLoad(ctypes.Structure):
    _fields_ = [
        ("motor_current_a", ctypes.c_double),
        ("motor_voltage_v", ctypes.c_double),
        ("joint_torque_nm", ctypes.c_double),
        ("angular_velocity_rad_per_s", ctypes.c_double),
        ("mechanical_power_w", ctypes.c_double),
        ("cpu_gpu_power_w", ctypes.c_double),
        ("battery_charge_fraction", ctypes.c_double),
        ("chassis_temperature_c", ctypes.c_double),
    ]


class OrganismTelemetry(ctypes.Structure):
    _fields_ = [
        ("available", ctypes.c_int32),
        ("step_count", ctypes.c_uint64),
        ("simulated_time_seconds", ctypes.c_double),
        ("total_blood_volume_l", ctypes.c_double),
        ("mean_arterial_pressure_mm_hg", ctypes.c_double),
        ("systolic_pressure_mm_hg", ctypes.c_double),
        ("diastolic_pressure_mm_hg", ctypes.c_double),
        ("central_venous_pressure_mm_hg", ctypes.c_double),
        ("arterial_po2_mm_hg", ctypes.c_double),
        ("arterial_pco2_mm_hg", ctypes.c_double),
        ("arterial_oxygen_saturation", ctypes.c_double),
        ("arterial_ph", ctypes.c_double),
        ("plasma_glucose_mm", ctypes.c_double),
        ("plasma_na_mm", ctypes.c_double),
        ("plasma_k_mm", ctypes.c_double),
        ("plasma_ca_mm", ctypes.c_double),
        ("blood_temperature_c", ctypes.c_double),
        ("heart_rate_bpm", ctypes.c_double),
        ("stroke_volume_ml", ctypes.c_double),
        ("cardiac_output_l_per_min", ctypes.c_double),
        ("ejection_fraction_lv", ctypes.c_double),
        ("left_ventricle_pressure_mm_hg", ctypes.c_double),
        ("myocardial_o2_consumption_ml_per_min", ctypes.c_double),
        ("ischemic_stress_index", ctypes.c_double),
        ("respiration_rate_bpm", ctypes.c_double),
        ("tidal_volume_l", ctypes.c_double),
        ("minute_ventilation_l_per_min", ctypes.c_double),
        ("alveolar_po2_mm_hg", ctypes.c_double),
        ("alveolar_pco2_mm_hg", ctypes.c_double),
        ("o2_uptake_rate_ml_per_min", ctypes.c_double),
        ("glomerular_filtration_rate_ml_per_min", ctypes.c_double),
        ("urine_output_rate_ml_per_min", ctypes.c_double),
        ("urine_osmolarity_m_osm_per_kg", ctypes.c_double),
        ("renin_secretion_rate", ctypes.c_double),
        ("left_kidney_gfr_ml_per_min", ctypes.c_double),
        ("right_kidney_gfr_ml_per_min", ctypes.c_double),
        ("left_kidney_urine_output_ml_per_min", ctypes.c_double),
        ("right_kidney_urine_output_ml_per_min", ctypes.c_double),
        ("left_kidney_renal_blood_flow_ml_per_min", ctypes.c_double),
        ("right_kidney_renal_blood_flow_ml_per_min", ctypes.c_double),
        ("visceral_distress", ctypes.c_double),
        ("cardiovascular_load", ctypes.c_double),
        ("metabolic_depletion", ctypes.c_double),
        ("respiratory_hypoxia", ctypes.c_double),
        ("fluid_electrolyte_imbalance", ctypes.c_double),
        ("sympathetic_tone", ctypes.c_double),
        ("parasympathetic_tone", ctypes.c_double),
    ]


ATMOSPHERE_PRESETS = {
    "earth": {
        "label": "Erde (101.3 kPa, 21% O2, 22°C)",
        "barometric_pressure_kpa": 101.325,
        "o2_fraction": 0.2095,
        "co2_fraction": 0.0004,
        "n2_fraction": 0.7808,
        "ambient_temperature_c": 22.0,
        "relative_humidity": 0.50,
        "dust_ppm": 10.0,
    },
    "mars": {
        "label": "Mars (0.64 kPa, 95% CO2, 0.13% O2, -60°C)",
        "barometric_pressure_kpa": 0.636,
        "o2_fraction": 0.0013,
        "co2_fraction": 0.9532,
        "n2_fraction": 0.027,
        "ambient_temperature_c": -60.0,
        "relative_humidity": 0.01,
        "dust_ppm": 250.0,
    },
    "altitude_4000m": {
        "label": "Hochgebirge (4000m, 61.6 kPa, hypobar)",
        "barometric_pressure_kpa": 61.6,
        "o2_fraction": 0.2095,
        "co2_fraction": 0.0004,
        "n2_fraction": 0.7808,
        "ambient_temperature_c": -5.0,
        "relative_humidity": 0.30,
        "dust_ppm": 5.0,
    },
    "hypoxic_chamber": {
        "label": "Hypoxie-Kammer (101.3 kPa, 10% O2)",
        "barometric_pressure_kpa": 101.325,
        "o2_fraction": 0.1000,
        "co2_fraction": 0.0004,
        "n2_fraction": 0.8900,
        "ambient_temperature_c": 22.0,
        "relative_humidity": 0.50,
        "dust_ppm": 5.0,
    },
    "hypercapnia": {
        "label": "Hyperkapnie (101.3 kPa, 5% CO2, Azidose)",
        "barometric_pressure_kpa": 101.325,
        "o2_fraction": 0.2000,
        "co2_fraction": 0.0500,
        "n2_fraction": 0.7400,
        "ambient_temperature_c": 22.0,
        "relative_humidity": 0.50,
        "dust_ppm": 10.0,
    },
}


class SafetyMonitor:
    """Independent deterministic motion guard outside the learned controller."""

    def __init__(self) -> None:
        self.enabled = True
        self.interventions = 0
        self.blocked_actions = 0
        self.last_reason = "none"
        self.safe_state = True

    def validate(
        self, world: "RobotWorld", action: int, motor: MotorState
    ) -> tuple[bool, str]:
        reason = "none"
        finite_motor = all(
            math.isfinite(float(value)) for value in motor.directional_activity
        ) and math.isfinite(float(motor.confidence))
        if action < 0 or action >= len(world.ACTIONS):
            reason = "invalid_action"
        elif not finite_motor:
            reason = "non_finite_motor_output"
        elif world.battery <= 0.12:
            reason = "energy_safe_hold"
        else:
            dx, dz = world.ACTIONS[action]
            candidate = (world.position[0] + dx, world.position[1] + dz)
            if not world._valid(candidate):
                reason = "collision_prevented"
        allowed = reason == "none" or not self.enabled
        self.safe_state = allowed
        self.last_reason = reason
        if not allowed:
            self.interventions += 1
            self.blocked_actions += 1
        return allowed, reason

    def export_state(self) -> dict[str, Any]:
        return {
            "enabled": self.enabled,
            "interventions": self.interventions,
            "blocked_actions": self.blocked_actions,
            "last_reason": self.last_reason,
            "safe_state": self.safe_state,
        }

    def import_state(self, state: dict[str, Any]) -> None:
        self.enabled = bool(state.get("enabled", True))
        self.interventions = int(state.get("interventions", 0))
        self.blocked_actions = int(state.get("blocked_actions", 0))
        self.last_reason = str(state.get("last_reason", "none"))
        self.safe_state = bool(state.get("safe_state", True))


class RobotWorld:
    """Deterministic embodied world with explicit controller provenance."""

    ACTIONS = ((0, -1), (1, 0), (0, 1), (-1, 0))
    ACTION_NAMES = ("north", "east", "south", "west")
    CONTROLLER_MODES = ("neural_direct", "hybrid_reward", "classical_baseline")
    STEP_COST = -0.018
    DISTANCE_POTENTIAL_SCALE = 0.070
    NOVEL_CELL_BONUS = 0.10
    IMMEDIATE_BACKTRACK_PENALTY = 0.030
    RECENT_LOOP_WINDOW = 12

    def __init__(
        self,
        seed: int,
        controller_mode: str = "neural_direct",
        map_definition: dict[str, Any] | None = None,
    ) -> None:
        if controller_mode not in self.CONTROLLER_MODES:
            raise ValueError(f"unsupported controller mode: {controller_mode}")
        self.seed = seed
        self.controller_mode = controller_mode
        self.safety = SafetyMonitor()
        self.random = random.Random(seed ^ 0xA7A7_3000)
        self.map_id = "map-1"
        self.map_name = "Klassischer Parcours"
        self.map_revision = 1
        self.width = 20
        self.depth = 15
        self.cell_size = 1.0
        self.start = (1, 1)
        self.goal = (18, 13)
        self.obstacles: set[tuple[int, int]] = set()
        self.shortest_path: list[tuple[int, int]] = []
        self.navigation_context_id = 0
        self._configure_map(map_definition or default_map_catalog()[0])
        # The actor has no map coordinates, obstacle model or reference path.
        # It receives only the live motor pools and current neural assembly.
        self.actor_preferences: dict[tuple[int, int, int], list[float]] = {}
        self.global_motor_preferences = [0.0, 0.0, 0.0, 0.0]
        self.reward_baseline = 0.0
        self.policy_updates = 0
        self.motor_decisions = 0
        self.learning_steps = 0
        self.learning_enabled = True
        self.pending_actor_state: tuple[int, int, int] | None = None
        self.pending_action: int | None = None
        self.pending_probabilities = [0.25, 0.25, 0.25, 0.25]
        self.pending_reward = 0.0
        self.visited: set[tuple[int, int]] = {self.start}
        self.visit_counts: dict[tuple[int, int], int] = {self.start: 1}
        self.best_path: list[tuple[int, int]] = []
        self.best_steps: int | None = None
        self.episode = 1
        self.successes = 0
        self.collisions = 0
        self.total_steps = 0
        self.total_reward = 0.0
        self.epsilon = 0.34
        self.position = self.start
        self.heading = 1
        self.last_action = "start"
        self.last_reward = 0.0
        self.last_collision = False
        self.last_new_cell = True
        self.battery = 1.0
        self.episode_steps = 0
        self.episode_reward = 0.0
        self.trail: list[tuple[int, int]] = [self.start]
        self.episode_visit_counts: dict[tuple[int, int], int] = {self.start: 1}
        self.episode_revisits = 0
        self.episode_backtracks = 0
        self.episode_loops = 0
        self.episode_cycle_preventions = 0
        self.revisit_events = 0
        self.backtrack_events = 0
        self.loop_events = 0
        self.cycle_preventions = 0
        self.last_loop_length = 0
        self._reset_pending = False
        self.neural_assembly = 0
        self.predicted_assembly = 0
        self.prediction_confidence = 0.0
        self.prediction_error = 0.0
        self.motor_activity = [0.0, 0.0, 0.0, 0.0]
        self.motor_raw_direction = 0
        self.motor_selected_direction = 0
        self.motor_confidence = 0.0
        self.motor_attention = 0.0
        self.last_safety_intervention = False
        self.last_safety_reason = "none"

    def _make_obstacles(self) -> set[tuple[int, int]]:
        obstacles: set[tuple[int, int]] = set()
        for z in range(1, self.depth - 1):
            if z not in (3, 10):
                obstacles.add((5, z))
            if z not in (5, 12):
                obstacles.add((10, z))
            if z not in (2, 8, 13):
                obstacles.add((15, z))
        for x in range(6, 10):
            if x != 8:
                obstacles.add((x, 8))
        for x in range(11, 15):
            if x != 13:
                obstacles.add((x, 4))
        obstacles.discard(self.start)
        obstacles.discard(self.goal)
        return obstacles

    @staticmethod
    def _path_for(
        width: int,
        depth: int,
        obstacles: set[tuple[int, int]],
        start: tuple[int, int],
        goal: tuple[int, int],
    ) -> list[tuple[int, int]]:
        queue = deque([start])
        previous: dict[tuple[int, int], tuple[int, int] | None] = {start: None}
        while queue:
            current = queue.popleft()
            if current == goal:
                path: list[tuple[int, int]] = []
                while current is not None:
                    path.append(current)
                    current = previous[current]  # type: ignore[assignment]
                return list(reversed(path))
            for dx, dz in RobotWorld.ACTIONS:
                candidate = (current[0] + dx, current[1] + dz)
                inside = 0 <= candidate[0] < width and 0 <= candidate[1] < depth
                if inside and candidate not in obstacles and candidate not in previous:
                    previous[candidate] = current
                    queue.append(candidate)
        return []

    @classmethod
    def normalize_map_definition(cls, value: dict[str, Any]) -> dict[str, Any]:
        width = int(value.get("width", 20))
        depth = int(value.get("depth", 15))
        if not 6 <= width <= 48 or not 6 <= depth <= 48:
            raise ValueError("Kartengröße muss zwischen 6 und 48 Zellen liegen")
        start = tuple(int(item) for item in value.get("start", (1, 1)))
        goal = tuple(int(item) for item in value.get("goal", (width - 2, depth - 2)))
        if len(start) != 2 or len(goal) != 2:
            raise ValueError("Start und Ziel benötigen jeweils zwei Koordinaten")
        if start == goal:
            raise ValueError("Start und Ziel müssen verschieden sein")
        if not all((0 <= point[0] < width and 0 <= point[1] < depth) for point in (start, goal)):
            raise ValueError("Start oder Ziel liegt außerhalb der Karte")
        obstacles = {
            tuple(int(item) for item in position)
            for position in value.get("obstacles", [])
        }

        if any(
            len(position) != 2
            or position[0] < 0
            or position[0] >= width
            or position[1] < 0
            or position[1] >= depth
            for position in obstacles
        ):
            raise ValueError("Ein Hindernis liegt außerhalb der Karte")
        obstacles.discard(start)
        obstacles.discard(goal)
        shortest_path = cls._path_for(width, depth, obstacles, start, goal)
        if not shortest_path:
            raise ValueError("Die Änderung würde Start und Ziel vollständig trennen")
        map_id = str(value.get("id", "map-1"))
        if not map_id.startswith("map-") or not map_id[4:].isdigit():
            raise ValueError("Ungültiger Kartenplatz")
        slot_number = int(map_id[4:])
        if slot_number < 1 or slot_number > MAP_SLOT_LIMIT:
            raise ValueError("Kartenplatz muss zwischen 1 und 10 liegen")
        name = str(value.get("name", "Unbenannte Karte")).strip()[:60] or "Unbenannte Karte"
        return {
            "id": map_id,
            "name": name,
            "width": width,
            "depth": depth,
            "cell_size": max(0.1, min(10.0, float(value.get("cell_size", 1.0)))),
            "start": list(start),
            "goal": list(goal),
            "obstacles": [list(position) for position in sorted(obstacles)],
            "revision": max(1, int(value.get("revision", 1))),
            "shortest_path": [list(position) for position in shortest_path],
        }

    @staticmethod
    def _navigation_context_for_definition(definition: dict[str, Any]) -> int:
        """Stable map-layout fingerprint for episodic spatial-memory scoping."""
        layout = {
            "id": definition["id"],
            "width": definition["width"],
            "depth": definition["depth"],
            "start": definition["start"],
            "goal": definition["goal"],
            "obstacles": definition["obstacles"],
        }
        encoded = json.dumps(
            layout, sort_keys=True, separators=(",", ":"), ensure_ascii=True
        ).encode("utf-8")
        context_id = int.from_bytes(
            hashlib.blake2b(encoded, digest_size=8, person=b"TATARUS4-MAP").digest(),
            "little",
        )
        return context_id or 1

    def _configure_map(self, value: dict[str, Any]) -> None:
        definition = self.normalize_map_definition(value)
        self.map_id = definition["id"]
        self.map_name = definition["name"]
        self.map_revision = definition["revision"]
        self.width = definition["width"]
        self.depth = definition["depth"]
        self.cell_size = definition["cell_size"]
        self.start = tuple(definition["start"])
        self.goal = tuple(definition["goal"])
        self.obstacles = {tuple(position) for position in definition["obstacles"]}
        self.shortest_path = [tuple(position) for position in definition["shortest_path"]]
        self.navigation_context_id = self._navigation_context_for_definition(definition)

    def map_definition(self) -> dict[str, Any]:
        return {
            "id": self.map_id,
            "name": self.map_name,
            "width": self.width,
            "depth": self.depth,
            "cell_size": self.cell_size,
            "start": list(self.start),
            "goal": list(self.goal),
            "obstacles": [list(position) for position in sorted(self.obstacles)],
            "revision": self.map_revision,
        }

    def _reset_map_run(self) -> None:
        self.pending_actor_state = None
        self.pending_action = None
        self.pending_probabilities = [0.25, 0.25, 0.25, 0.25]
        self.pending_reward = 0.0
        self.visited = {self.start}
        self.visit_counts = {self.start: 1}
        self.best_path = []
        self.best_steps = None
        self.episode = 1
        self.successes = 0
        self.collisions = 0
        self.total_steps = 0
        self.total_reward = 0.0
        self.position = self.start
        self.heading = 1
        self.last_action = "map_reset"
        self.last_reward = 0.0
        self.last_collision = False
        self.last_new_cell = True
        self.battery = 1.0
        self.episode_steps = 0
        self.episode_reward = 0.0
        self.trail = [self.start]
        self.episode_visit_counts = {self.start: 1}
        self.episode_revisits = 0
        self.episode_backtracks = 0
        self.episode_loops = 0
        self.episode_cycle_preventions = 0
        self.revisit_events = 0
        self.backtrack_events = 0
        self.loop_events = 0
        self.cycle_preventions = 0
        self.last_loop_length = 0
        self._reset_pending = False
        enabled = self.safety.enabled
        self.safety = SafetyMonitor()
        self.safety.enabled = enabled
        self.last_safety_intervention = False
        self.last_safety_reason = "none"

    def switch_map(self, value: dict[str, Any]) -> None:
        """Change the environment while retaining neural/controller learning."""
        self._configure_map(value)
        self._reset_map_run()

    def edit_map_cell(self, x: int, z: int, tool: str) -> dict[str, Any]:
        position = (int(x), int(z))
        if not self._inside(position):
            raise ValueError("Die gewählte Zelle liegt außerhalb der Karte")
        definition = self.map_definition()
        obstacles = {tuple(item) for item in definition["obstacles"]}
        start = tuple(definition["start"])
        goal = tuple(definition["goal"])
        if tool == "obstacle":
            if position in (start, goal):
                raise ValueError("Auf Start oder Ziel kann kein Hindernis stehen")
            obstacles.add(position)
        elif tool == "erase":
            obstacles.discard(position)
        elif tool == "start":
            if position == goal:
                raise ValueError("Start und Ziel müssen verschieden sein")
            start = position
            obstacles.discard(position)
        elif tool == "goal":
            if position == start:
                raise ValueError("Start und Ziel müssen verschieden sein")
            goal = position
            obstacles.discard(position)
        else:
            raise ValueError("Unbekanntes Kartenwerkzeug")
        definition.update(
            {
                "start": list(start),
                "goal": list(goal),
                "obstacles": [list(item) for item in sorted(obstacles)],
                "revision": self.map_revision + 1,
            }
        )
        self.switch_map(definition)
        return self.map_definition()

    def rename_map(self, name: str) -> dict[str, Any]:
        cleaned = str(name).strip()[:60]
        if not cleaned:
            raise ValueError("Bitte einen Kartennamen eingeben")
        self.map_name = cleaned
        self.map_revision += 1
        return self.map_definition()

    def _inside(self, position: tuple[int, int]) -> bool:
        x, z = position
        return 0 <= x < self.width and 0 <= z < self.depth

    def _valid(self, position: tuple[int, int]) -> bool:
        return self._inside(position) and position not in self.obstacles

    def _breadth_first_path(
        self, start: tuple[int, int], goal: tuple[int, int]
    ) -> list[tuple[int, int]]:
        return self._path_for(self.width, self.depth, self.obstacles, start, goal)

    def _actor_state(self, motor: MotorState) -> tuple[int, int, int]:
        activity = [max(0.0, min(1.0, float(value))) for value in motor.directional_activity]
        dominant = max(range(4), key=lambda index: activity[index])
        contrast = max(activity) - min(activity)
        contrast_bin = min(3, int(contrast * 8.0))
        return (self.neural_assembly % 16, dominant, contrast_bin)

    @staticmethod
    def _softmax(values: list[float]) -> list[float]:
        peak = max(values)
        exponentials = [math.exp(max(-30.0, min(30.0, value - peak))) for value in values]
        total = sum(exponentials)
        return [value / total for value in exponentials]

    def _choose_action(self, motor: MotorState) -> int:
        state = self._actor_state(motor)
        activity = [max(0.0, min(1.0, float(value))) for value in motor.directional_activity]
        if self.controller_mode == "neural_direct":
            raw_action = int(motor.selected_direction) % 4
            action = self._inhibit_repeated_backtrack(raw_action, activity)
            self.pending_actor_state = None
            self.pending_action = action
            self.pending_probabilities = [1.0 if index == action else 0.0 for index in range(4)]
            self.motor_activity = activity
            self.motor_raw_direction = raw_action
            self.motor_selected_direction = action
            self.motor_confidence = float(motor.confidence)
            self.motor_attention = float(motor.attention)
            self.motor_decisions += 1
            return action
        if self.controller_mode == "classical_baseline":
            path = self._breadth_first_path(self.position, self.goal)
            action = int(motor.selected_direction) % 4
            if len(path) >= 2:
                delta = (path[1][0] - self.position[0], path[1][1] - self.position[1])
                action = self.ACTIONS.index(delta)
            self.pending_actor_state = None
            self.pending_action = action
            self.pending_probabilities = [1.0 if index == action else 0.0 for index in range(4)]
            self.motor_activity = activity
            self.motor_raw_direction = int(motor.selected_direction) % 4
            self.motor_selected_direction = action
            self.motor_confidence = float(motor.confidence)
            self.motor_attention = float(motor.attention)
            self.motor_decisions += 1
            return action
        preferences = self.actor_preferences.setdefault(state, [0.0, 0.0, 0.0, 0.0])
        logits = [
            4.0 * activity[index]
            + preferences[index]
            + self.global_motor_preferences[index]
            for index in range(4)
        ]
        probabilities = self._softmax(logits)
        if not self.learning_enabled:
            action = max(range(4), key=lambda index: probabilities[index])
        elif self.random.random() < self.epsilon:
            action = self.random.randrange(4)
        else:
            draw = self.random.random()
            action = 3
            cumulative = 0.0
            for index, probability in enumerate(probabilities):
                cumulative += probability
                if draw <= cumulative:
                    action = index
                    break
        self.pending_actor_state = state
        self.pending_action = action
        self.pending_probabilities = probabilities
        self.motor_activity = activity
        self.motor_raw_direction = int(motor.selected_direction) % 4
        self.motor_selected_direction = action
        self.motor_confidence = float(motor.confidence)
        self.motor_attention = float(motor.attention)
        self.motor_decisions += 1
        return action

    def _inhibit_repeated_backtrack(self, action: int, activity: list[float]) -> int:
        """Break only an established A-B-A-B oscillation.

        One ordinary reversal remains untouched so the robot can leave a dead
        end. Once the same edge is traversed repeatedly, short-term motor
        inhibition selects the strongest valid neural alternative. This is not
        a route planner: it has no goal or shortest-path access.
        """
        if len(self.trail) < 4:
            return action
        dx, dz = self.ACTIONS[action]
        candidate = (self.position[0] + dx, self.position[1] + dz)
        if candidate != self.trail[-2] or candidate != self.trail[-4]:
            return action

        recent = set(self.trail[-6:])
        alternatives: list[tuple[int, float, int]] = []
        for alternative, (alt_dx, alt_dz) in enumerate(self.ACTIONS):
            if alternative == action:
                continue
            next_position = (self.position[0] + alt_dx, self.position[1] + alt_dz)
            if not self._valid(next_position):
                continue
            alternatives.append(
                (1 if next_position not in recent else 0, activity[alternative], alternative)
            )
        if not alternatives:
            return action

        selected = max(alternatives, key=lambda item: (item[0], item[1], -item[2]))[2]
        self.episode_cycle_preventions += 1
        self.cycle_preventions += 1
        return selected

    def _learn_actor(self, motor: MotorState) -> None:
        if self.controller_mode != "hybrid_reward" or not self.learning_enabled:
            self.motor_activity = [
                max(0.0, min(1.0, float(value))) for value in motor.directional_activity
            ]
            return
        if self.pending_actor_state is None or self.pending_action is None:
            return
        preferences = self.actor_preferences.setdefault(
            self.pending_actor_state, [0.0, 0.0, 0.0, 0.0]
        )
        advantage = max(-1.5, min(5.0, self.pending_reward - self.reward_baseline))
        self.reward_baseline = 0.985 * self.reward_baseline + 0.015 * self.pending_reward
        learning_rate = 0.16
        for action in range(4):
            gradient = (1.0 if action == self.pending_action else 0.0) - self.pending_probabilities[action]
            preferences[action] = max(
                -6.0,
                min(6.0, preferences[action] + learning_rate * advantage * gradient),
            )
            self.global_motor_preferences[action] = max(
                -3.0,
                min(
                    3.0,
                    self.global_motor_preferences[action]
                    + 0.035 * advantage * gradient,
                ),
            )
        # The next motor state is intentionally consumed here even though the
        # update is REINFORCE-style: it proves policy updates follow a completed
        # neural sensor/action/reward cycle rather than map look-ahead.
        self.motor_activity = [
            max(0.0, min(1.0, float(value))) for value in motor.directional_activity
        ]
        self.policy_updates += 1

    @staticmethod
    def _loop_erased(path: list[tuple[int, int]]) -> list[tuple[int, int]]:
        result: list[tuple[int, int]] = []
        indexes: dict[tuple[int, int], int] = {}
        for position in path:
            if position in indexes:
                keep = indexes[position] + 1
                for removed in result[keep:]:
                    indexes.pop(removed, None)
                result = result[:keep]
                continue
            indexes[position] = len(result)
            result.append(position)
        return result

    def _begin_next_episode(self) -> None:
        self.episode += 1
        self.position = self.start
        self.heading = 1
        self.last_action = "reset"
        self.last_reward = 0.0
        self.last_collision = False
        self.last_new_cell = False
        self.battery = 1.0
        self.episode_steps = 0
        self.episode_reward = 0.0
        self.trail = [self.start]
        self.episode_visit_counts = {self.start: 1}
        self.episode_revisits = 0
        self.episode_backtracks = 0
        self.episode_loops = 0
        self.episode_cycle_preventions = 0
        self.last_loop_length = 0
        self._reset_pending = False

    @staticmethod
    def _revisit_penalty(previous_visits: int) -> float:
        if previous_visits <= 0:
            return 0.0
        if previous_visits == 1:
            return 0.005
        if previous_visits == 2:
            return 0.010
        if previous_visits == 3:
            return 0.020
        return 0.030

    def _recent_cycle_length(self, next_state: tuple[int, int]) -> int:
        maximum = min(self.RECENT_LOOP_WINDOW, len(self.trail))
        for length in range(2, maximum + 1):
            if self.trail[-length] == next_state:
                return length
        return 0

    @classmethod
    def _navigation_reward(
        cls,
        old_distance: int,
        new_distance: int,
        *,
        new_cell: bool,
        previous_episode_visits: int,
        cycle_length: int,
    ) -> float:
        # Potential-based shaping: Phi(s)=-distance. The potential term
        # telescopes to zero around every closed path; the negative step cost
        # therefore makes every repeated loop strictly loss-making.
        reward = cls.STEP_COST + cls.DISTANCE_POTENTIAL_SCALE * (
            old_distance - new_distance
        )
        if new_cell:
            reward += cls.NOVEL_CELL_BONUS
        reward -= cls._revisit_penalty(previous_episode_visits)
        if cycle_length == 2:
            reward -= cls.IMMEDIATE_BACKTRACK_PENALTY
        return reward

    def advance(self, motor: MotorState) -> Observation:
        if self._reset_pending:
            self._begin_next_episode()

        state = self.position
        action = self._choose_action(motor)
        self.heading = action
        dx, dz = self.ACTIONS[action]
        candidate = (state[0] + dx, state[1] + dz)
        old_distance = abs(state[0] - self.goal[0]) + abs(state[1] - self.goal[1])
        allowed, safety_reason = self.safety.validate(self, action, motor)
        self.last_safety_intervention = not allowed
        self.last_safety_reason = safety_reason
        collision = not self._valid(candidate) if allowed else False
        next_state = state if collision or not allowed else candidate
        new_cell = next_state not in self.visited
        previous_episode_visits = self.episode_visit_counts.get(next_state, 0)
        cycle_length = (
            self._recent_cycle_length(next_state)
            if allowed and not collision and next_state != state
            else 0
        )
        reward = self.STEP_COST
        if not allowed:
            reward -= 0.22
        elif collision:
            reward -= 0.62
            self.collisions += 1
        else:
            new_distance = abs(next_state[0] - self.goal[0]) + abs(next_state[1] - self.goal[1])
            reward = self._navigation_reward(
                old_distance,
                new_distance,
                new_cell=new_cell,
                previous_episode_visits=previous_episode_visits,
                cycle_length=cycle_length,
            )

        terminal = next_state == self.goal
        self.position = next_state
        self.trail.append(next_state)
        if len(self.trail) > 420:
            self.trail = self.trail[-420:]
        self.visited.add(next_state)
        self.visit_counts[next_state] = self.visit_counts.get(next_state, 0) + 1
        self.episode_visit_counts[next_state] = previous_episode_visits + 1
        self.last_loop_length = cycle_length
        if previous_episode_visits > 0 and allowed and not collision:
            self.episode_revisits += 1
            self.revisit_events += 1
        if cycle_length >= 2:
            self.episode_loops += 1
            self.loop_events += 1
        if cycle_length == 2:
            self.episode_backtracks += 1
            self.backtrack_events += 1
        self.episode_steps += 1
        self.total_steps += 1
        if self.learning_enabled:
            self.learning_steps += 1
        if terminal:
            reward += 5.0
            self.successes += 1
            simplified = self._loop_erased(self.trail)
            route_steps = len(simplified) - 1
            if self.best_steps is None or route_steps < self.best_steps:
                self.best_steps = route_steps
                self.best_path = simplified

        self.pending_reward = reward
        if self.learning_enabled:
            self.epsilon = max(0.025, 0.34 * math.exp(-self.learning_steps / 1800.0))
        self.last_action = "safe_hold" if not allowed else self.ACTION_NAMES[action]
        self.last_reward = reward
        self.last_collision = collision
        self.last_new_cell = new_cell
        self.episode_reward += reward
        self.total_reward += reward
        self.battery = max(0.18, self.battery - 0.0011 - (0.003 if collision else 0.0))
        if terminal or self.episode_steps >= 460:
            self._reset_pending = True

        return self._make_observation(next_state, action, collision, new_cell)

    def _make_observation(
        self,
        position: tuple[int, int],
        action: int,
        collision: bool,
        new_cell: bool,
    ) -> Observation:
        observation = Observation()
        observation.timestamp_ns = time.time_ns()
        front = self._ray_distance(0)
        left = self._ray_distance(-1)
        right = self._ray_distance(1)
        goal_dx = (self.goal[0] - position[0]) / max(1, self.width - 1)
        goal_dz = (self.goal[1] - position[1]) / max(1, self.depth - 1)
        observation.acceleration[:] = (
            goal_dx * 10.0,
            goal_dz * 10.0,
            (front / 7.0 * 2.0 - 1.0) * 10.0,
        )
        observation.rotation[:] = (
            (left / 7.0 * 2.0 - 1.0) * 4.5,
            (action / 3.0 * 2.0 - 1.0) * 4.5,
            (right / 7.0 * 2.0 - 1.0) * 4.5,
        )
        goal_distance = math.hypot(self.goal[0] - position[0], self.goal[1] - position[1])
        observation.light = max(0.05, 1.0 - goal_distance / math.hypot(self.width, self.depth))
        observation.sound_level = 0.92 if collision else 0.08
        observation.temperature = 20.0 + 2.0 * math.sin((position[0] + position[1]) * 0.31)
        observation.battery = self.battery
        observation.novelty = 0.92 if new_cell else 0.04
        # Reward is delivered through tatarus_end_action after the sensory
        # consequence has been observed, preserving the action/reward cycle.
        observation.reward = 0.0
        observation.goal_direction[:] = (goal_dx, goal_dz)
        observation.proximity[:] = (front / 7.0, left / 7.0, right / 7.0)
        return observation

    def observe_neural(self, product: ProductState, motor: MotorState) -> None:
        self.neural_assembly = int(product.assembly_id)
        self.predicted_assembly = int(product.predicted_assembly_id)
        self.prediction_confidence = float(product.prediction_confidence)
        self.prediction_error = float(product.prediction_error)
        self._learn_actor(motor)
        self.motor_raw_direction = int(motor.selected_direction) % 4
        self.motor_confidence = float(motor.confidence)
        self.motor_attention = float(motor.attention)

    def _ray_distance(self, relative: int) -> int:
        action = (self.heading + relative) % 4
        dx, dz = self.ACTIONS[action]
        cursor = self.position
        distance = 0
        while distance < 7:
            cursor = (cursor[0] + dx, cursor[1] + dz)
            if not self._valid(cursor):
                break
            distance += 1
        return distance

    def scanner_frame(self) -> tuple[Pose3D, ctypes.Array[RangeReading]]:
        """Build the live robot's front/left/right range-scanner frame."""
        pose = Pose3D()
        pose.position_m[:] = (
            (self.position[0] + 0.5) * self.cell_size,
            0.5 * self.cell_size,
            (self.position[1] + 0.5) * self.cell_size,
        )
        pose.orientation_xyzw[:] = (0.0, 0.0, 0.0, 1.0)
        result = (RangeReading * 3)()
        max_cells = 7
        max_range = max_cells * self.cell_size
        for index, relative in enumerate((0, -1, 1)):
            direction_index = (self.heading + relative) % 4
            dx, dz = self.ACTIONS[direction_index]
            free_cells = self._ray_distance(relative)
            hit = free_cells < max_cells
            result[index].direction[:] = (float(dx), 0.0, float(dz))
            result[index].distance_m = (
                (free_cells + 1) * self.cell_size if hit else max_range
            )
            result[index].max_range_m = max_range
            result[index].confidence = 1.0
            result[index].hit = 1 if hit else 0
            result[index].semantic_label = b"obstacle" if hit else None
        return pose, result

    def _learned_route(self) -> tuple[list[tuple[int, int]], bool]:
        if self.best_path:
            return self.best_path, True
        return self._loop_erased(self.trail), False

    @staticmethod
    def _points(path: list[tuple[int, int]]) -> list[list[float]]:
        return [[float(x), 0.0, float(z)] for x, z in path]

    @staticmethod
    def _nested_tuple(value: Any) -> Any:
        if isinstance(value, list):
            return tuple(RobotWorld._nested_tuple(item) for item in value)
        return value

    def export_state(self) -> dict[str, Any]:
        """Serialize every mutable embodied-world and controller value."""
        return {
            "schema": "tatarus-embodied-world-state-v1",
            "seed": self.seed,
            "controller_mode": self.controller_mode,
            "random_state": self.random.getstate(),
            "world": {
                "map_id": self.map_id,
                "map_name": self.map_name,
                "map_revision": self.map_revision,
                "navigation_context_id": self.navigation_context_id,
                "width": self.width,
                "depth": self.depth,
                "cell_size": self.cell_size,
                "start": list(self.start),
                "goal": list(self.goal),
                "obstacles": [list(value) for value in sorted(self.obstacles)],
                "shortest_path": [list(value) for value in self.shortest_path],
            },
            "actor_preferences": [
                {"state": list(key), "values": list(values)}
                for key, values in sorted(self.actor_preferences.items())
            ],
            "global_motor_preferences": list(self.global_motor_preferences),
            "reward_baseline": self.reward_baseline,
            "policy_updates": self.policy_updates,
            "motor_decisions": self.motor_decisions,
            "learning_steps": self.learning_steps,
            "learning_enabled": self.learning_enabled,
            "pending_actor_state": (
                list(self.pending_actor_state) if self.pending_actor_state is not None else None
            ),
            "pending_action": self.pending_action,
            "pending_probabilities": list(self.pending_probabilities),
            "pending_reward": self.pending_reward,
            "visited": [list(value) for value in sorted(self.visited)],
            "visit_counts": [
                {"position": list(key), "count": value}
                for key, value in sorted(self.visit_counts.items())
            ],
            "best_path": [list(value) for value in self.best_path],
            "best_steps": self.best_steps,
            "episode": self.episode,
            "successes": self.successes,
            "collisions": self.collisions,
            "total_steps": self.total_steps,
            "total_reward": self.total_reward,
            "epsilon": self.epsilon,
            "position": list(self.position),
            "heading": self.heading,
            "last_action": self.last_action,
            "last_reward": self.last_reward,
            "last_collision": self.last_collision,
            "last_new_cell": self.last_new_cell,
            "battery": self.battery,
            "episode_steps": self.episode_steps,
            "episode_reward": self.episode_reward,
            "trail": [list(value) for value in self.trail],
            "episode_visit_counts": [
                {"position": list(key), "count": value}
                for key, value in sorted(self.episode_visit_counts.items())
            ],
            "episode_revisits": self.episode_revisits,
            "episode_backtracks": self.episode_backtracks,
            "episode_loops": self.episode_loops,
            "episode_cycle_preventions": self.episode_cycle_preventions,
            "revisit_events": self.revisit_events,
            "backtrack_events": self.backtrack_events,
            "loop_events": self.loop_events,
            "cycle_preventions": self.cycle_preventions,
            "last_loop_length": self.last_loop_length,
            "reset_pending": self._reset_pending,
            "neural": {
                "assembly": self.neural_assembly,
                "predicted_assembly": self.predicted_assembly,
                "prediction_confidence": self.prediction_confidence,
                "prediction_error": self.prediction_error,
                "motor_activity": list(self.motor_activity),
                "motor_raw_direction": self.motor_raw_direction,
                "motor_selected_direction": self.motor_selected_direction,
                "motor_confidence": self.motor_confidence,
                "motor_attention": self.motor_attention,
            },
            "safety": self.safety.export_state(),
            "last_safety_intervention": self.last_safety_intervention,
            "last_safety_reason": self.last_safety_reason,
        }

    @classmethod
    def from_state(cls, state: dict[str, Any]) -> "RobotWorld":
        if state.get("schema") != "tatarus-embodied-world-state-v1":
            raise ValueError("unsupported embodied world snapshot schema")
        result = cls(int(state["seed"]), str(state.get("controller_mode", "neural_direct")))
        world = state["world"]
        result.width = int(world["width"])
        result.depth = int(world["depth"])
        result.cell_size = float(world["cell_size"])
        result.start = tuple(int(value) for value in world["start"])
        result.goal = tuple(int(value) for value in world["goal"])
        result.obstacles = {tuple(int(value) for value in item) for item in world["obstacles"]}
        result.shortest_path = [tuple(int(value) for value in item) for item in world["shortest_path"]]
        result.map_id = str(world.get("map_id", "map-1"))
        result.map_name = str(world.get("map_name", "Klassischer Parcours"))
        result.map_revision = int(world.get("map_revision", 1))
        # A legacy snapshot used unscoped place keys. Keeping context zero lets
        # its original map remain usable; the next map switch creates a scoped
        # context and prevents further cross-map interference.
        result.navigation_context_id = int(world.get("navigation_context_id", 0))
        result.random.setstate(cls._nested_tuple(state["random_state"]))
        result.actor_preferences = {
            tuple(int(value) for value in item["state"]): [float(value) for value in item["values"]]
            for item in state.get("actor_preferences", [])
        }
        result.global_motor_preferences = [float(value) for value in state["global_motor_preferences"]]
        result.reward_baseline = float(state["reward_baseline"])
        result.policy_updates = int(state["policy_updates"])
        result.motor_decisions = int(state["motor_decisions"])
        result.learning_steps = int(state.get("learning_steps", state.get("total_steps", 0)))
        result.learning_enabled = bool(state.get("learning_enabled", True))
        pending = state.get("pending_actor_state")
        result.pending_actor_state = tuple(int(value) for value in pending) if pending else None
        result.pending_action = state.get("pending_action")
        result.pending_probabilities = [float(value) for value in state["pending_probabilities"]]
        result.pending_reward = float(state["pending_reward"])
        result.visited = {tuple(int(value) for value in item) for item in state["visited"]}
        result.visit_counts = {
            tuple(int(value) for value in item["position"]): int(item["count"])
            for item in state["visit_counts"]
        }
        result.best_path = [tuple(int(value) for value in item) for item in state["best_path"]]
        result.best_steps = state.get("best_steps")
        for name in ("episode", "successes", "collisions", "total_steps", "heading", "episode_steps"):
            setattr(result, name, int(state[name]))
        for name in ("total_reward", "epsilon", "last_reward", "battery", "episode_reward"):
            setattr(result, name, float(state[name]))
        result.position = tuple(int(value) for value in state["position"])
        result.last_action = str(state["last_action"])
        result.last_collision = bool(state["last_collision"])
        result.last_new_cell = bool(state["last_new_cell"])
        result.trail = [tuple(int(value) for value in item) for item in state["trail"]]
        episode_counts = state.get("episode_visit_counts")
        if episode_counts is None:
            result.episode_visit_counts = {}
            for position in result.trail:
                result.episode_visit_counts[position] = (
                    result.episode_visit_counts.get(position, 0) + 1
                )
        else:
            result.episode_visit_counts = {
                tuple(int(value) for value in item["position"]): int(item["count"])
                for item in episode_counts
            }
        result.episode_revisits = int(state.get(
            "episode_revisits",
            max(0, len(result.trail) - len(result.episode_visit_counts)),
        ))
        result.episode_backtracks = int(state.get("episode_backtracks", 0))
        result.episode_loops = int(state.get("episode_loops", 0))
        result.episode_cycle_preventions = int(state.get("episode_cycle_preventions", 0))
        result.revisit_events = int(state.get("revisit_events", result.episode_revisits))
        result.backtrack_events = int(state.get("backtrack_events", result.episode_backtracks))
        result.loop_events = int(state.get("loop_events", result.episode_loops))
        result.cycle_preventions = int(
            state.get("cycle_preventions", result.episode_cycle_preventions)
        )
        result.last_loop_length = int(state.get("last_loop_length", 0))
        result._reset_pending = bool(state["reset_pending"])
        neural = state["neural"]
        result.neural_assembly = int(neural["assembly"])
        result.predicted_assembly = int(neural["predicted_assembly"])
        result.prediction_confidence = float(neural["prediction_confidence"])
        result.prediction_error = float(neural["prediction_error"])
        result.motor_activity = [float(value) for value in neural["motor_activity"]]
        result.motor_raw_direction = int(neural["motor_raw_direction"])
        result.motor_selected_direction = int(neural["motor_selected_direction"])
        result.motor_confidence = float(neural["motor_confidence"])
        result.motor_attention = float(neural["motor_attention"])
        result.safety.import_state(state.get("safety", {}))
        result.last_safety_intervention = bool(state.get("last_safety_intervention", False))
        result.last_safety_reason = str(state.get("last_safety_reason", "none"))
        return result

    def snapshot(self) -> bytes:
        learned_route, route_complete = self._learned_route()
        explored_ratio = len(self.visited) / max(1, self.width * self.depth - len(self.obstacles))
        current_distance = abs(self.position[0] - self.goal[0]) + abs(self.position[1] - self.goal[1])
        initial_distance = abs(self.start[0] - self.goal[0]) + abs(self.start[1] - self.goal[1])
        payload = {
            "schema": "tatarus-robot-training-v2",
            "seed": self.seed,
            "world": {
                "map_id": self.map_id,
                "map_name": self.map_name,
                "map_revision": self.map_revision,
                "navigation_context_id": self.navigation_context_id,
                "width": self.width,
                "depth": self.depth,
                "cell_size": self.cell_size,
                "start": list(self.start),
                "goal": list(self.goal),
                "obstacles": [
                    {"x": x, "z": z, "height": 0.8 + ((x * 17 + z * 11) % 6) * 0.16}
                    for x, z in sorted(self.obstacles)
                ],
                "shortest_steps": len(self.shortest_path) - 1,
            },
            "robot": {
                "x": self.position[0],
                "y": 0.0,
                "z": self.position[1],
                "heading": self.heading,
                "action": self.last_action,
                "battery": self.battery,
                "collision": self.last_collision,
                "reward": self.last_reward,
            },
            "training": {
                "controller": self.controller_mode,
                "episode": self.episode,
                "episode_steps": self.episode_steps,
                "total_steps": self.total_steps,
                "successes": self.successes,
                "collisions": self.collisions,
                "epsilon": self.epsilon,
                "states_explored": len(self.visited),
                "explored_ratio": explored_ratio,
                "goal_progress": max(0.0, min(1.0, 1.0 - current_distance / max(1, initial_distance))),
                "episode_reward": self.episode_reward,
                "total_reward": self.total_reward,
                "best_steps": self.best_steps,
                "route_complete": route_complete,
                "route_efficiency": (
                    (len(self.shortest_path) - 1) / self.best_steps
                    if self.best_steps else 0.0
                ),
                "policy_states": len(self.actor_preferences),
                "policy_updates": self.policy_updates,
                "motor_decisions": self.motor_decisions,
                "learning_steps": self.learning_steps,
                "learning_enabled": self.learning_enabled,
                "mode": "training" if self.learning_enabled else "frozen_evaluation",
                "episode_unique_cells": len(self.episode_visit_counts),
                "episode_revisits": self.episode_revisits,
                "episode_backtracks": self.episode_backtracks,
                "episode_loops": self.episode_loops,
                "episode_cycle_preventions": self.episode_cycle_preventions,
                "revisit_ratio": self.episode_revisits / max(1, self.episode_steps),
                "backtrack_events": self.backtrack_events,
                "loop_events": self.loop_events,
                "cycle_preventions": self.cycle_preventions,
            },
            "sensors": {
                "front": self._ray_distance(0),
                "left": self._ray_distance(-1),
                "right": self._ray_distance(1),
                "goal_light": max(0.05, 1.0 - current_distance / max(1, initial_distance)),
                "novelty": 1.0 if self.last_new_cell else 0.0,
            },
            "neural": {
                "control_source": self.controller_mode,
                "assembly": self.neural_assembly,
                "predicted_assembly": self.predicted_assembly,
                "confidence": self.prediction_confidence,
                "prediction_error": self.prediction_error,
                "motor_activity": self.motor_activity,
                "raw_direction": self.motor_raw_direction,
                "selected_direction": self.motor_selected_direction,
                "selected_name": self.ACTION_NAMES[self.motor_selected_direction],
                "motor_confidence": self.motor_confidence,
                "attention": self.motor_attention,
                "reward_baseline": self.reward_baseline,
            },
            "safety": {
                **self.safety.export_state(),
                "intervened": self.last_safety_intervention,
                "reason": self.last_safety_reason,
            },
            "trail": self._points(self.trail),
            "visited": self._points(sorted(self.visited)),
            "learned_route": self._points(learned_route),
        }
        return json.dumps(payload, separators=(",", ":"), allow_nan=False).encode()


DEFAULT_INTERVENTION: dict[str, Any] = {
    "astrocyte_function": 1.0,
    "oxygen_supply": 1.0,
    "glucose_supply": 1.0,
    "pump_efficiency": 1.0,
    "myelin_integrity": 1.0,
    "microglia_function": 1.0,
    "neuromodulator_gain": 1.0,
    "sleep_enabled": True,
    "neuron_damage": 0.0,
    "synapse_damage": 0.0,
}

INTERVENTION_PRESETS: dict[str, dict[str, Any]] = {
    "control": {},
    "astrocyte_failure": {"astrocyte_function": 0.28},
    "demyelination": {"myelin_integrity": 0.24},
    "hypoxia": {"oxygen_supply": 0.35},
    "hypoglycemia": {"glucose_supply": 0.35},
    "pump_failure": {"pump_efficiency": 0.30},
    "sleep_deprivation": {"sleep_enabled": False},
    "microglia_suppression": {"microglia_function": 0.22},
    "neuromodulator_suppression": {"neuromodulator_gain": 0.25},
    "tissue_damage": {"neuron_damage": 0.08, "synapse_damage": 0.18},
}


def normalized_intervention(value: dict[str, Any] | None) -> dict[str, Any]:
    result = dict(DEFAULT_INTERVENTION)
    if value:
        for key in result:
            if key in value:
                result[key] = value[key]
    for key in result:
        if key == "sleep_enabled":
            result[key] = bool(result[key])
        else:
            result[key] = max(0.0, min(1.5, float(result[key])))
    return result


class TatarusRuntime:
    def __init__(
        self,
        library_path: Path,
        seed: int,
        interval: float,
        tissue_profile: str = "standard",
        neuron_count: int | None = None,
        imaginatio_only: bool = False,
    ) -> None:
        self.library_path = library_path
        self.seed = seed
        self.interval = interval
        if tissue_profile not in TISSUE_PROFILES:
            raise ValueError("unknown tissue profile")
        self.tissue_profile = tissue_profile
        self.neuron_count = int(neuron_count or TISSUE_PROFILES[tissue_profile])
        if self.neuron_count < 96 or self.neuron_count > 65_536:
            raise ValueError("neuron count must be between 96 and 65536")
        self.speed = 1.0
        self.imaginatio_only = bool(imaginatio_only)
        self.paused = self.imaginatio_only
        self.stopping = False
        self.error = ""
        self._sample = 0
        self._lock = threading.RLock()
        self._wake = threading.Event()
        self._thread: threading.Thread | None = None
        self._cortex_thread: threading.Thread | None = None
        self.cortex_autonomous = False
        self.cortex_configured = False
        self.cortex_available = False
        self.latest_cortex = b'{"schema":"tatarus-cortex-ui-v1","available":false,"configured":false}'
        self.project_root = Path(__file__).resolve().parents[2]
        self.snapshot_root = self.project_root / "output" / "embodiment_snapshots"
        self.snapshot_root.mkdir(parents=True, exist_ok=True)
        self.imaginatio_gallery_root = self.project_root / "imaginatio_output" / "gallery"
        self.imaginatio_gallery_root.mkdir(parents=True, exist_ok=True)
        self.last_imaginatio_context: dict[str, Any] = {
            "operation": "idle",
            "text": "",
        }
        self.map_library_path = self.project_root / "output" / "embodiment_maps.json"
        self.map_catalog = self._load_map_catalog()
        self.active_map_id = self.map_catalog[0]["id"]
        self.controller_mode = "neural_direct"
        self.evaluation_mode = False
        self.intervention = normalized_intervention(None)
        self.timeline: deque[dict[str, Any]] = deque(maxlen=900)
        self.timeline_markers: deque[dict[str, Any]] = deque(maxlen=120)
        self.experiment: dict[str, Any] = {
            "status": "idle",
            "progress": 0.0,
            "result": None,
            "presets": INTERVENTION_PRESETS,
        }
        self._experiment_thread: threading.Thread | None = None
        self.latest_geometry = b"{}"
        self.latest_frame = b"{}"
        self.latest_physiology = b"{}"
        self.latest_environment_map = b"{}"
        self.latest_cartography_update: dict[str, Any] = {}
        self.robot = RobotWorld(seed, self.controller_mode, self.map_catalog[0])
        self.latest_robot = self.robot.snapshot()
        self.library = ctypes.CDLL(str(library_path))
        self._configure_library()
        self.handle = self._create_handle(seed)
        self._apply_intervention(self.handle, self.intervention)
        self.organism_handle = None
        if hasattr(self.library, "tatarus_organism_create"):
            self.organism_handle = self.library.tatarus_organism_create_sized(
                seed, self.neuron_count
            )
            self._apply_organism_intervention(self.intervention)
            self._configure_cortex_runtime()
        self._synchronize_native_navigation_state()
        self.atmosphere_preset = "earth"
        self.atmosphere = AtmosphericEnvironment(
            101.325, 0.2095, 0.0004, 0.7808, 22.0, 0.50, 10.0
        )
        self.latest_organism = b"{}"
        self.latest_imaginatio = b'{"schema":"tatarus-imaginatio-v14","available":false}'
        self.latest_imaginatio_trace = b'{"schema":"tatarus-imaginatio-causal-trace-v1","events":[]}'
        self.next_motor = self._motor_state()
        self._refresh_snapshots()

    @staticmethod
    def _profile_for_count(neuron_count: int) -> str:
        for name, count in TISSUE_PROFILES.items():
            if count == neuron_count:
                return name
        return "custom"

    def _load_map_catalog(self) -> list[dict[str, Any]]:
        defaults = default_map_catalog()
        loaded_by_id: dict[str, dict[str, Any]] = {}
        try:
            payload = json.loads(self.map_library_path.read_text(encoding="utf-8"))
            if payload.get("schema") != "tatarus-map-library-v1":
                raise ValueError("unsupported map library")
            for value in payload.get("maps", [])[:MAP_SLOT_LIMIT]:
                normalized = RobotWorld.normalize_map_definition(value)
                loaded_by_id[normalized["id"]] = normalized
        except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError):
            loaded_by_id = {}
        result: list[dict[str, Any]] = []
        for fallback in defaults:
            normalized = loaded_by_id.get(fallback["id"], fallback)
            normalized.pop("shortest_path", None)
            result.append(normalized)
        return result

    def _save_map_catalog(self) -> None:
        self.map_library_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.map_library_path.with_suffix(".json.pending")
        payload = {
            "schema": "tatarus-map-library-v1",
            "maps": self.map_catalog,
        }
        temporary.write_text(
            json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False),
            encoding="utf-8",
        )
        temporary.replace(self.map_library_path)

    def _map_by_id(self, map_id: str) -> dict[str, Any]:
        for definition in self.map_catalog:
            if definition["id"] == map_id:
                return definition
        raise ValueError("Kartenplatz nicht gefunden")

    def _active_map_definition(self) -> dict[str, Any]:
        return self._map_by_id(self.active_map_id)

    def _store_robot_map(self) -> None:
        definition = self.robot.map_definition()
        for index, current in enumerate(self.map_catalog):
            if current["id"] == definition["id"]:
                self.map_catalog[index] = definition
                self.active_map_id = definition["id"]
                self._save_map_catalog()
                return
        raise ValueError("Snapshot enthält keinen gültigen Kartenplatz")

    def map_library_payload(self) -> dict[str, Any]:
        return {
            "schema": "tatarus-map-library-v1",
            "active_map_id": self.active_map_id,
            "maps": [
                {
                    "id": value["id"],
                    "name": value["name"],
                    "revision": value.get("revision", 1),
                    "obstacle_count": len(value.get("obstacles", [])),
                }
                for value in self.map_catalog
            ],
        }

    def select_map(self, map_id: str) -> dict[str, Any]:
        with self._lock:
            definition = self._map_by_id(map_id)
            self.robot.switch_map(definition)
            self.active_map_id = map_id
            self._synchronize_native_navigation_state()
            self.latest_robot = self.robot.snapshot()
            self.timeline_markers.append(
                {
                    "sample": self._sample,
                    "type": "map_selected",
                    "map_id": map_id,
                    "label": definition["name"],
                }
            )
            return self.robot.map_definition()

    def edit_active_map(self, x: int, z: int, tool: str) -> dict[str, Any]:
        with self._lock:
            self.paused = True
            definition = self.robot.edit_map_cell(x, z, tool)
            self._store_robot_map()
            self._synchronize_native_navigation_state()
            self.latest_robot = self.robot.snapshot()
            self.timeline_markers.append(
                {
                    "sample": self._sample,
                    "type": "map_edited",
                    "map_id": self.active_map_id,
                    "label": f"{definition['name']}: Zelle {x}/{z}",
                }
            )
            return definition

    def rename_active_map(self, name: str) -> dict[str, Any]:
        with self._lock:
            definition = self.robot.rename_map(name)
            self._store_robot_map()
            self.latest_robot = self.robot.snapshot()
            return definition

    def reset_active_map(self) -> dict[str, Any]:
        with self._lock:
            fallback = next(
                value for value in default_map_catalog() if value["id"] == self.active_map_id
            )
            self.robot.switch_map(fallback)
            self.paused = True
            self._store_robot_map()
            self._synchronize_native_navigation_state()
            self.latest_robot = self.robot.snapshot()
            return self.robot.map_definition()

    def _configure_library(self) -> None:
        lib = self.library
        lib.tatarus_create.argtypes = [ctypes.c_uint64]
        lib.tatarus_create.restype = ctypes.c_void_p
        lib.tatarus_create_sized.argtypes = [ctypes.c_uint64, ctypes.c_uint32]
        lib.tatarus_create_sized.restype = ctypes.c_void_p
        lib.tatarus_destroy.argtypes = [ctypes.c_void_p]
        lib.tatarus_destroy.restype = None
        lib.tatarus_observe.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(Observation),
            ctypes.POINTER(ProductState),
        ]
        lib.tatarus_observe.restype = ctypes.c_int
        if hasattr(lib, "tatarus_explore"):
            lib.tatarus_explore.argtypes = [
                ctypes.c_void_p,
                ctypes.POINTER(Observation),
                ctypes.c_uint64,
                ctypes.POINTER(Pose3D),
                ctypes.POINTER(RangeReading),
                ctypes.c_uint64,
                ctypes.POINTER(ProductState),
                ctypes.POINTER(CartographyUpdate),
            ]
            lib.tatarus_explore.restype = ctypes.c_int
        lib.tatarus_get_motor.argtypes = [ctypes.c_void_p, ctypes.POINTER(MotorState)]
        lib.tatarus_get_motor.restype = ctypes.c_int
        lib.tatarus_begin_action.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_double]
        lib.tatarus_begin_action.restype = ctypes.c_int
        lib.tatarus_end_action.argtypes = [
            ctypes.c_void_p,
            ctypes.c_uint64,
            ctypes.c_double,
            ctypes.c_double,
            ctypes.c_double,
        ]
        lib.tatarus_end_action.restype = ctypes.c_int
        lib.tatarus_end_episode.argtypes = [ctypes.c_void_p, ctypes.c_int32]
        lib.tatarus_end_episode.restype = ctypes.c_int
        if hasattr(lib, "tatarus_set_learning_enabled"):
            lib.tatarus_set_learning_enabled.argtypes = [ctypes.c_void_p, ctypes.c_int32]
            lib.tatarus_set_learning_enabled.restype = ctypes.c_int
        if hasattr(lib, "tatarus_set_navigation_context"):
            lib.tatarus_set_navigation_context.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
            lib.tatarus_set_navigation_context.restype = ctypes.c_int
        lib.tatarus_set_intervention.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(InterventionState),
        ]
        lib.tatarus_set_intervention.restype = ctypes.c_int
        lib.tatarus_apply_damage.argtypes = [
            ctypes.c_void_p,
            ctypes.c_double,
            ctypes.c_double,
            ctypes.c_uint64,
        ]
        lib.tatarus_apply_damage.restype = ctypes.c_int
        lib.tatarus_save.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        lib.tatarus_save.restype = ctypes.c_int
        lib.tatarus_load.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        lib.tatarus_load.restype = ctypes.c_int
        for name in (
            "tatarus_get_spatial_json",
            "tatarus_get_live_json",
            "tatarus_get_physiology_json",
        ):
            function = getattr(lib, name)
            function.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint64]
            function.restype = ctypes.c_uint64
        if hasattr(lib, "tatarus_get_environment_map_json"):
            lib.tatarus_get_environment_map_json.argtypes = [
                ctypes.c_void_p, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_uint64
            ]
            lib.tatarus_get_environment_map_json.restype = ctypes.c_uint64
        lib.tatarus_last_error.argtypes = []
        lib.tatarus_last_error.restype = ctypes.c_char_p

        # Organism C ABI bindings
        if hasattr(lib, "tatarus_organism_create"):
            lib.tatarus_organism_create.argtypes = [ctypes.c_uint64]
            lib.tatarus_organism_create.restype = ctypes.c_void_p
            lib.tatarus_organism_create_sized.argtypes = [ctypes.c_uint64, ctypes.c_uint32]
            lib.tatarus_organism_create_sized.restype = ctypes.c_void_p
            lib.tatarus_organism_destroy.argtypes = [ctypes.c_void_p]
            lib.tatarus_organism_destroy.restype = None
            lib.tatarus_organism_step.argtypes = [
                ctypes.c_void_p,
                ctypes.POINTER(Observation),
                ctypes.POINTER(AtmosphericEnvironment),
                ctypes.c_double,
                ctypes.POINTER(ProductState),
            ]
            lib.tatarus_organism_step.restype = ctypes.c_int
            lib.tatarus_organism_step_with_load.argtypes = [
                ctypes.c_void_p,
                ctypes.POINTER(Observation),
                ctypes.POINTER(AtmosphericEnvironment),
                ctypes.POINTER(RobotPhysicalLoad),
                ctypes.c_double,
                ctypes.POINTER(ProductState),
            ]
            lib.tatarus_organism_step_with_load.restype = ctypes.c_int
            if hasattr(lib, "tatarus_organism_explore_step_with_load"):
                lib.tatarus_organism_explore_step_with_load.argtypes = [
                    ctypes.c_void_p,
                    ctypes.POINTER(Observation),
                    ctypes.c_uint64,
                    ctypes.POINTER(Pose3D),
                    ctypes.POINTER(RangeReading),
                    ctypes.c_uint64,
                    ctypes.POINTER(AtmosphericEnvironment),
                    ctypes.POINTER(RobotPhysicalLoad),
                    ctypes.c_double,
                    ctypes.POINTER(ProductState),
                    ctypes.POINTER(CartographyUpdate),
                ]
                lib.tatarus_organism_explore_step_with_load.restype = ctypes.c_int
            lib.tatarus_organism_get_telemetry.argtypes = [
                ctypes.c_void_p,
                ctypes.POINTER(OrganismTelemetry),
            ]
            lib.tatarus_organism_get_telemetry.restype = ctypes.c_int
            lib.tatarus_organism_get_json.argtypes = [
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_uint64,
            ]
            lib.tatarus_organism_get_json.restype = ctypes.c_uint64
            lib.tatarus_organism_infuse.argtypes = [
                ctypes.c_void_p,
                ctypes.c_double,
                ctypes.c_double,
                ctypes.c_double,
                ctypes.c_double,
            ]
            lib.tatarus_organism_infuse.restype = ctypes.c_int
            lib.tatarus_organism_bleed.argtypes = [
                ctypes.c_void_p,
                ctypes.c_double,
            ]
            lib.tatarus_organism_bleed.restype = ctypes.c_int
            lib.tatarus_organism_set_renal_function.argtypes = [
                ctypes.c_void_p,
                ctypes.c_double,
                ctypes.c_double,
            ]
            lib.tatarus_organism_set_renal_function.restype = ctypes.c_int
            lib.tatarus_organism_get_motor.argtypes = [
                ctypes.c_void_p,
                ctypes.POINTER(MotorState),
            ]
            lib.tatarus_organism_get_motor.restype = ctypes.c_int
            lib.tatarus_organism_begin_action.argtypes = [
                ctypes.c_void_p, ctypes.c_uint64, ctypes.c_double
            ]
            lib.tatarus_organism_begin_action.restype = ctypes.c_int
            lib.tatarus_organism_end_action.argtypes = [
                ctypes.c_void_p,
                ctypes.c_uint64,
                ctypes.c_double,
                ctypes.c_double,
                ctypes.c_double,
            ]
            lib.tatarus_organism_end_action.restype = ctypes.c_int
            lib.tatarus_organism_end_episode.argtypes = [ctypes.c_void_p, ctypes.c_int32]
            lib.tatarus_organism_end_episode.restype = ctypes.c_int
            if hasattr(lib, "tatarus_organism_set_learning_enabled"):
                lib.tatarus_organism_set_learning_enabled.argtypes = [
                    ctypes.c_void_p, ctypes.c_int32
                ]
                lib.tatarus_organism_set_learning_enabled.restype = ctypes.c_int
            if hasattr(lib, "tatarus_organism_set_navigation_context"):
                lib.tatarus_organism_set_navigation_context.argtypes = [
                    ctypes.c_void_p, ctypes.c_uint64
                ]
                lib.tatarus_organism_set_navigation_context.restype = ctypes.c_int
            lib.tatarus_organism_set_intervention.argtypes = [
                ctypes.c_void_p,
                ctypes.POINTER(InterventionState),
            ]
            lib.tatarus_organism_set_intervention.restype = ctypes.c_int
            lib.tatarus_organism_apply_damage.argtypes = [
                ctypes.c_void_p,
                ctypes.c_double,
                ctypes.c_double,
                ctypes.c_uint64,
            ]
            lib.tatarus_organism_apply_damage.restype = ctypes.c_int
            for name in (
                "tatarus_organism_get_spatial_json",
                "tatarus_organism_get_live_json",
                "tatarus_organism_get_physiology_json",
                "tatarus_organism_get_throughput_json",
            ):
                function = getattr(lib, name)
                function.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint64]
                function.restype = ctypes.c_uint64
            if hasattr(lib, "tatarus_organism_get_environment_map_json"):
                lib.tatarus_organism_get_environment_map_json.argtypes = [
                    ctypes.c_void_p, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_uint64
                ]
                lib.tatarus_organism_get_environment_map_json.restype = ctypes.c_uint64
            if hasattr(lib, "tatarus_organism_reset_throughput"):
                lib.tatarus_organism_reset_throughput.argtypes = [ctypes.c_void_p]
                lib.tatarus_organism_reset_throughput.restype = ctypes.c_int
            lib.tatarus_organism_save.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
            lib.tatarus_organism_save.restype = ctypes.c_int
            lib.tatarus_organism_load.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
            lib.tatarus_organism_load.restype = ctypes.c_int
            if hasattr(lib, "tatarus_organism_imaginatio_get_json"):
                pixel_pointer = ctypes.POINTER(ctypes.c_double)
                lib.tatarus_organism_imaginatio_learn.argtypes = [
                    ctypes.c_void_p, pixel_pointer, ctypes.c_uint64, ctypes.c_char_p
                ]
                lib.tatarus_organism_imaginatio_learn.restype = ctypes.c_int
                lib.tatarus_organism_imaginatio_recall.argtypes = [
                    ctypes.c_void_p, pixel_pointer, ctypes.c_uint64
                ]
                lib.tatarus_organism_imaginatio_recall.restype = ctypes.c_int
                lib.tatarus_organism_imaginatio_associate_symbol.argtypes = [
                    ctypes.c_void_p,
                    pixel_pointer,
                    ctypes.c_uint64,
                    ctypes.c_char_p,
                ]
                lib.tatarus_organism_imaginatio_associate_symbol.restype = ctypes.c_int
                for name in (
                    "tatarus_organism_imaginatio_learn_rgb",
                    "tatarus_organism_imaginatio_recall_rgb",
                    "tatarus_organism_imaginatio_associate_symbol_rgb",
                ):
                    if not hasattr(lib, name):
                        continue
                    function = getattr(lib, name)
                    function.argtypes = [
                        ctypes.c_void_p, pixel_pointer, ctypes.c_uint64
                    ] + ([ctypes.c_char_p] if name != "tatarus_organism_imaginatio_recall_rgb" else [])
                    function.restype = ctypes.c_int
                rgb8_pointer = ctypes.POINTER(ctypes.c_uint8)
                if hasattr(lib, "tatarus_organism_imaginatio_learn_rgb8"):
                    lib.tatarus_organism_imaginatio_learn_rgb8.argtypes = [
                        ctypes.c_void_p, rgb8_pointer, ctypes.c_uint64,
                        ctypes.c_uint64, ctypes.c_uint64, ctypes.c_uint64,
                        ctypes.c_char_p,
                    ]
                    lib.tatarus_organism_imaginatio_learn_rgb8.restype = ctypes.c_int
                if hasattr(lib, "tatarus_organism_imaginatio_learn_category_rgb8"):
                    lib.tatarus_organism_imaginatio_learn_category_rgb8.argtypes = [
                        ctypes.c_void_p, rgb8_pointer, ctypes.c_uint64,
                        ctypes.c_uint64, ctypes.c_uint64, ctypes.c_uint64,
                        ctypes.c_char_p, ctypes.c_char_p,
                    ]
                    lib.tatarus_organism_imaginatio_learn_category_rgb8.restype = ctypes.c_int
                if hasattr(lib, "tatarus_organism_imaginatio_recognize_categories_rgb8"):
                    lib.tatarus_organism_imaginatio_recognize_categories_rgb8.argtypes = [
                        ctypes.c_void_p, rgb8_pointer, ctypes.c_uint64,
                        ctypes.c_uint64, ctypes.c_uint64, ctypes.c_char_p,
                        ctypes.c_uint64, ctypes.c_void_p, ctypes.c_uint64,
                    ]
                    lib.tatarus_organism_imaginatio_recognize_categories_rgb8.restype = ctypes.c_uint64
                if hasattr(lib, "tatarus_organism_imaginatio_perceive_scene_rgb8"):
                    lib.tatarus_organism_imaginatio_perceive_scene_rgb8.argtypes = [
                        ctypes.c_void_p, rgb8_pointer, ctypes.c_uint64,
                        ctypes.c_uint64, ctypes.c_uint64, ctypes.c_char_p,
                        ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_uint64,
                    ]
                    lib.tatarus_organism_imaginatio_perceive_scene_rgb8.restype = ctypes.c_uint64
                if hasattr(lib, "tatarus_organism_imaginatio_render_native_rgb8"):
                    lib.tatarus_organism_imaginatio_render_native_rgb8.argtypes = [
                        ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint64,
                        rgb8_pointer, ctypes.c_uint64,
                    ]
                    lib.tatarus_organism_imaginatio_render_native_rgb8.restype = ctypes.c_uint64
                if hasattr(lib, "tatarus_organism_imaginatio_remember_source_resolution"):
                    lib.tatarus_organism_imaginatio_remember_source_resolution.argtypes = [
                        ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint64,
                    ]
                    lib.tatarus_organism_imaginatio_remember_source_resolution.restype = ctypes.c_int
                if hasattr(lib, "tatarus_organism_imaginatio_recall_rgb8"):
                    lib.tatarus_organism_imaginatio_recall_rgb8.argtypes = [
                        ctypes.c_void_p, rgb8_pointer, ctypes.c_uint64,
                        ctypes.c_uint64, ctypes.c_uint64,
                    ]
                    lib.tatarus_organism_imaginatio_recall_rgb8.restype = ctypes.c_int
                if hasattr(lib, "tatarus_organism_imaginatio_associate_symbol_rgb8"):
                    lib.tatarus_organism_imaginatio_associate_symbol_rgb8.argtypes = [
                        ctypes.c_void_p, rgb8_pointer, ctypes.c_uint64,
                        ctypes.c_uint64, ctypes.c_uint64, ctypes.c_char_p,
                    ]
                    lib.tatarus_organism_imaginatio_associate_symbol_rgb8.restype = ctypes.c_int
                for name in (
                    "tatarus_organism_imaginatio_draw_symbol",
                    "tatarus_organism_imaginatio_draw_free",
                    "tatarus_organism_imaginatio_compose",
                ):
                    function = getattr(lib, name)
                    function.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
                    function.restype = ctypes.c_int
                for name in (
                    "tatarus_organism_imaginatio_draw_category",
                    "tatarus_organism_imaginatio_fuse_categories",
                ):
                    if not hasattr(lib, name):
                        continue
                    function = getattr(lib, name)
                    function.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64]
                    function.restype = ctypes.c_int
                if hasattr(lib, "tatarus_organism_imaginatio_learn_pose_rgb8"):
                    lib.tatarus_organism_imaginatio_learn_pose_rgb8.argtypes = [
                        ctypes.c_void_p, rgb8_pointer, ctypes.c_uint64,
                        ctypes.c_uint64, ctypes.c_uint64, ctypes.c_uint64,
                        ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
                    ]
                    lib.tatarus_organism_imaginatio_learn_pose_rgb8.restype = ctypes.c_int
                scene_pointer = ctypes.POINTER(TatarusSceneDescription)
                action_pointer = ctypes.POINTER(TatarusSceneAction)
                if hasattr(lib, "tatarus_organism_imaginatio_observe_scene"):
                    lib.tatarus_organism_imaginatio_observe_scene.argtypes = [
                        ctypes.c_void_p, scene_pointer,
                    ]
                    lib.tatarus_organism_imaginatio_observe_scene.restype = ctypes.c_int
                    lib.tatarus_organism_imaginatio_draw_scene.argtypes = [
                        ctypes.c_void_p, scene_pointer, ctypes.c_uint64,
                    ]
                    lib.tatarus_organism_imaginatio_draw_scene.restype = ctypes.c_int
                    lib.tatarus_organism_imaginatio_learn_transition.argtypes = [
                        ctypes.c_void_p, scene_pointer, action_pointer, scene_pointer,
                    ]
                    lib.tatarus_organism_imaginatio_learn_transition.restype = ctypes.c_int
                    lib.tatarus_organism_imaginatio_imagine_future.argtypes = [
                        ctypes.c_void_p, scene_pointer, action_pointer,
                        ctypes.c_uint64, ctypes.c_uint64,
                    ]
                    lib.tatarus_organism_imaginatio_imagine_future.restype = ctypes.c_int
                lib.tatarus_organism_imaginatio_reset_canvas.argtypes = [ctypes.c_void_p]
                lib.tatarus_organism_imaginatio_reset_canvas.restype = ctypes.c_int
                for name in (
                    "tatarus_organism_imaginatio_get_json",
                    "tatarus_organism_imaginatio_get_trace_json",
                ):
                    function = getattr(lib, name)
                    function.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint64]
                    function.restype = ctypes.c_uint64

            if hasattr(lib, "tatarus_organism_cortex_get_json"):
                lib.tatarus_organism_cortex_configure.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
                lib.tatarus_organism_cortex_configure.restype = ctypes.c_int
                lib.tatarus_organism_cortex_disable.argtypes = [ctypes.c_void_p]
                lib.tatarus_organism_cortex_disable.restype = ctypes.c_int
                lib.tatarus_organism_cortex_probe.argtypes = [ctypes.c_void_p]
                lib.tatarus_organism_cortex_probe.restype = ctypes.c_int
                lib.tatarus_organism_cortex_push_goal.argtypes = [
                    ctypes.c_void_p, ctypes.c_char_p, ctypes.c_double,
                ]
                lib.tatarus_organism_cortex_push_goal.restype = ctypes.c_uint64
                lib.tatarus_organism_cortex_complete_goal.argtypes = [
                    ctypes.c_void_p, ctypes.c_uint64, ctypes.c_int32,
                ]
                lib.tatarus_organism_cortex_complete_goal.restype = ctypes.c_int
                lib.tatarus_organism_cortex_cancel_goal.argtypes = [
                    ctypes.c_void_p, ctypes.c_uint64,
                ]
                lib.tatarus_organism_cortex_cancel_goal.restype = ctypes.c_int
                lib.tatarus_organism_cortex_remember.argtypes = [
                    ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_double,
                ]
                lib.tatarus_organism_cortex_remember.restype = ctypes.c_int
                lib.tatarus_organism_cortex_command.argtypes = [
                    ctypes.c_void_p, ctypes.c_uint32, ctypes.c_char_p,
                ]
                lib.tatarus_organism_cortex_command.restype = ctypes.c_int
                lib.tatarus_organism_cortex_get_json.argtypes = [
                    ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint64,
                ]
                lib.tatarus_organism_cortex_get_json.restype = ctypes.c_uint64

    def _last_error(self) -> str:
        raw = self.library.tatarus_last_error()
        return raw.decode("utf-8", errors="replace") if raw else "unknown SDK error"

    def _refresh_cortex_status(self) -> dict[str, Any]:
        if not self.organism_handle or not hasattr(
            self.library, "tatarus_organism_cortex_get_json"
        ):
            self.cortex_available = False
            self.cortex_configured = False
            payload = {
                "schema": "tatarus-cortex-ui-v1",
                "available": False,
                "configured": False,
                "autonomous": self.cortex_autonomous,
                "last_error": "Die geladene C-ABI enthält den Hybrid Cortex nicht.",
            }
            self.latest_cortex = json.dumps(payload, separators=(",", ":")).encode()
            return payload
        self.cortex_available = True
        try:
            self.latest_cortex = self._json_snapshot(
                "tatarus_organism_cortex_get_json", self.organism_handle
            )
            payload = json.loads(self.latest_cortex)
        except Exception as exc:
            payload = {
                "schema": "tatarus-cortex-ui-v1",
                "available": True,
                "configured": False,
                "last_error": str(exc),
            }
            self.latest_cortex = json.dumps(payload, separators=(",", ":")).encode()
        self.cortex_configured = bool(payload.get("configured", False))
        payload["autonomous"] = self.cortex_autonomous
        return payload

    def _configure_cortex_runtime(self) -> None:
        self.cortex_available = bool(
            self.organism_handle
            and hasattr(self.library, "tatarus_organism_cortex_get_json")
        )
        self.cortex_configured = False
        if not self.cortex_available:
            self._refresh_cortex_status()
            return
        config_path = self.project_root / "config" / "cortex_hybrid.json"
        if not config_path.is_file():
            self.latest_cortex = json.dumps({
                "schema": "tatarus-cortex-ui-v1",
                "available": True,
                "configured": False,
                "last_error": f"Cortex-Konfiguration fehlt: {config_path}",
            }, separators=(",", ":")).encode()
            return
        ok = self.library.tatarus_organism_cortex_configure(
            self.organism_handle, str(config_path).encode("utf-8")
        )
        if not ok:
            self.latest_cortex = json.dumps({
                "schema": "tatarus-cortex-ui-v1",
                "available": True,
                "configured": False,
                "last_error": self._last_error(),
            }, separators=(",", ":")).encode()
            return
        self.cortex_configured = True
        self.cortex_autonomous = False
        self._refresh_cortex_status()

    def _native_cortex_command(self, name: str, goal: str = "") -> bool:
        if not self.organism_handle or not self.cortex_configured:
            raise RuntimeError("Der Hybrid Cortex ist nicht konfiguriert")
        if name not in CORTEX_COMMANDS:
            raise ValueError("Unbekannte Cortex-Aktion")
        ok = self.library.tatarus_organism_cortex_command(
            self.organism_handle,
            CORTEX_COMMANDS[name],
            goal.encode("utf-8"),
        )
        if not ok:
            raise RuntimeError(self._last_error())
        return True

    def _cortex_loop(self) -> None:
        while not self.stopping:
            if not self.cortex_autonomous or not self.cortex_configured:
                time.sleep(0.15)
                continue
            try:
                with self._lock:
                    self._native_cortex_command("autonomous_tick")
                    self._native_cortex_command("poll")
                    state = self._refresh_cortex_status()
                    if state.get("imagination_executable"):
                        self._native_cortex_command("execute_imagination")
                        self.latest_imaginatio = self._json_snapshot(
                            "tatarus_organism_imaginatio_get_json", self.organism_handle
                        )
                        self.latest_imaginatio_trace = self._json_snapshot(
                            "tatarus_organism_imaginatio_get_trace_json", self.organism_handle
                        )
                        self._refresh_cortex_status()
                time.sleep(0.35)
            except Exception as exc:
                self.error = f"Cortex autonomous cycle: {exc}"
                time.sleep(0.75)

    def _create_handle(
        self, seed: int, neuron_count: int | None = None
    ) -> ctypes.c_void_p:
        handle = self.library.tatarus_create_sized(
            seed, int(neuron_count or self.neuron_count)
        )
        if not handle:
            raise RuntimeError(self._last_error())
        return ctypes.c_void_p(handle)

    def _synchronize_native_navigation_state(
        self,
        *,
        handle: ctypes.c_void_p | None = None,
        organism_handle: ctypes.c_void_p | None = None,
        world: RobotWorld | None = None,
    ) -> None:
        target_world = world or self.robot
        target_handle = self.handle if handle is None else handle
        target_organism = self.organism_handle if organism_handle is None else organism_handle
        learning_enabled = 1 if target_world.learning_enabled else 0
        context_id = int(target_world.navigation_context_id)
        if target_handle and hasattr(self.library, "tatarus_set_navigation_context"):
            if not self.library.tatarus_set_navigation_context(target_handle, context_id):
                raise RuntimeError(self._last_error())
        if target_handle and hasattr(self.library, "tatarus_set_learning_enabled"):
            if not self.library.tatarus_set_learning_enabled(target_handle, learning_enabled):
                raise RuntimeError(self._last_error())
        if target_organism and hasattr(
            self.library, "tatarus_organism_set_navigation_context"
        ):
            if not self.library.tatarus_organism_set_navigation_context(
                target_organism, context_id
            ):
                raise RuntimeError(self._last_error())
        if target_organism and hasattr(
            self.library, "tatarus_organism_set_learning_enabled"
        ):
            if not self.library.tatarus_organism_set_learning_enabled(
                target_organism, learning_enabled
            ):
                raise RuntimeError(self._last_error())

    def _apply_intervention(
        self, handle: ctypes.c_void_p, intervention: dict[str, Any]
    ) -> None:
        value = InterventionState(
            float(intervention["astrocyte_function"]),
            float(intervention["oxygen_supply"]),
            float(intervention["glucose_supply"]),
            float(intervention["pump_efficiency"]),
            float(intervention["myelin_integrity"]),
            float(intervention["microglia_function"]),
            float(intervention["neuromodulator_gain"]),
            1 if intervention["sleep_enabled"] else 0,
        )
        if not self.library.tatarus_set_intervention(handle, ctypes.byref(value)):
            raise RuntimeError(self._last_error())

    def _apply_damage(
        self, handle: ctypes.c_void_p, intervention: dict[str, Any], seed: int
    ) -> None:
        neurons = float(intervention.get("neuron_damage", 0.0))
        synapses = float(intervention.get("synapse_damage", 0.0))
        if neurons <= 0.0 and synapses <= 0.0:
            return
        if not self.library.tatarus_apply_damage(handle, neurons, synapses, seed):
            raise RuntimeError(self._last_error())

    def _apply_organism_intervention(self, intervention: dict[str, Any]) -> None:
        if not self.organism_handle:
            return
        value = InterventionState(
            float(intervention["astrocyte_function"]),
            float(intervention["oxygen_supply"]),
            float(intervention["glucose_supply"]),
            float(intervention["pump_efficiency"]),
            float(intervention["myelin_integrity"]),
            float(intervention["microglia_function"]),
            float(intervention["neuromodulator_gain"]),
            1 if intervention["sleep_enabled"] else 0,
        )
        if not self.library.tatarus_organism_set_intervention(
            self.organism_handle, ctypes.byref(value)
        ):
            raise RuntimeError(self._last_error())

        neurons = float(intervention.get("neuron_damage", 0.0))
        synapses = float(intervention.get("synapse_damage", 0.0))
        if neurons > 0.0 or synapses > 0.0:
            if not self.library.tatarus_organism_apply_damage(
                self.organism_handle,
                neurons,
                synapses,
                self.seed ^ self._sample ^ 0xDA6A6E,
            ):
                raise RuntimeError(self._last_error())

    def _json_snapshot(
        self, function_name: str, handle: ctypes.c_void_p | None = None
    ) -> bytes:
        if handle is None and self.organism_handle:
            organism_functions = {
                "tatarus_get_spatial_json": "tatarus_organism_get_spatial_json",
                "tatarus_get_live_json": "tatarus_organism_get_live_json",
                "tatarus_get_physiology_json": "tatarus_organism_get_physiology_json",
            }
            function_name = organism_functions.get(function_name, function_name)
            target = self.organism_handle if function_name.startswith("tatarus_organism_") else self.handle
        else:
            target = handle or self.handle
        function = getattr(self.library, function_name)
        required = int(function(target, None, 0))
        if required <= 1 or required > 128 * 1024 * 1024:
            raise RuntimeError(self._last_error())
        buffer = ctypes.create_string_buffer(required)
        returned = int(function(target, buffer, required))
        if returned != required:
            raise RuntimeError(self._last_error())
        payload = bytes(buffer.raw[: required - 1])
        json.loads(payload)
        return payload

    def _motor_state(self, handle: ctypes.c_void_p | None = None) -> MotorState:
        motor = MotorState()
        if handle is None and self.organism_handle:
            ok = self.library.tatarus_organism_get_motor(
                self.organism_handle, ctypes.byref(motor)
            )
        else:
            ok = self.library.tatarus_get_motor(handle or self.handle, ctypes.byref(motor))
        if not ok:
            raise RuntimeError(self._last_error())
        return motor

    def _environment_map_snapshot(self) -> bytes:
        environment_id = int(self.robot.navigation_context_id)
        if self.organism_handle and hasattr(
            self.library, "tatarus_organism_get_environment_map_json"
        ):
            function = self.library.tatarus_organism_get_environment_map_json
            target = self.organism_handle
        elif hasattr(self.library, "tatarus_get_environment_map_json"):
            function = self.library.tatarus_get_environment_map_json
            target = self.handle
        else:
            return b'{"schema":"tatarus-environment-map-v1","available":false}'
        required = int(function(target, environment_id, None, 0))
        if required <= 1 or required > 128 * 1024 * 1024:
            raise RuntimeError(self._last_error())
        buffer = ctypes.create_string_buffer(required)
        returned = int(function(target, environment_id, buffer, required))
        if returned != required:
            raise RuntimeError(self._last_error())
        payload = bytes(buffer.raw[: required - 1])
        json.loads(payload)
        return payload

    def _refresh_snapshots(self) -> None:
        self.latest_geometry = self._json_snapshot("tatarus_get_spatial_json")
        self.latest_frame = self._json_snapshot("tatarus_get_live_json")
        self.latest_physiology = self._json_snapshot("tatarus_get_physiology_json")
        if self.organism_handle and hasattr(self.library, "tatarus_organism_get_json"):
            self.latest_organism = self._json_snapshot("tatarus_organism_get_json", self.organism_handle)
        if self.organism_handle and hasattr(
            self.library, "tatarus_organism_imaginatio_get_json"
        ):
            self.latest_imaginatio = self._json_snapshot(
                "tatarus_organism_imaginatio_get_json", self.organism_handle
            )
            self.latest_imaginatio_trace = self._json_snapshot(
                "tatarus_organism_imaginatio_get_trace_json", self.organism_handle
            )
        self.latest_environment_map = self._environment_map_snapshot()

    @staticmethod
    def _motor_payload(motor: MotorState) -> dict[str, Any]:
        return {
            "available": int(motor.available),
            "selected_direction": int(motor.selected_direction),
            "directional_activity": [float(value) for value in motor.directional_activity],
            "movement": float(motor.movement),
            "attention": float(motor.attention),
            "vocalization": float(motor.vocalization),
            "confidence": float(motor.confidence),
        }

    @staticmethod
    def _motor_from_payload(payload: dict[str, Any]) -> MotorState:
        motor = MotorState()
        motor.available = int(payload["available"])
        motor.selected_direction = int(payload["selected_direction"])
        motor.directional_activity[:] = tuple(float(value) for value in payload["directional_activity"])
        motor.movement = float(payload["movement"])
        motor.attention = float(payload["attention"])
        motor.vocalization = float(payload["vocalization"])
        motor.confidence = float(payload["confidence"])
        return motor

    @staticmethod
    def _sha256(path: Path) -> str:
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        return digest.hexdigest()

    def _record_timeline(self) -> None:
        live = json.loads(self.latest_frame)
        robot = json.loads(self.latest_robot)
        physiology = live.get("physiology", {})
        biology = live.get("biology", {})
        metrics = live.get("metrics", {})
        training = robot.get("training", {})
        organism = {}
        try:
            if self.latest_organism and self.latest_organism != b"{}":
                organism = json.loads(self.latest_organism)
        except Exception:
            pass
        circ = organism.get("circulation", {})
        heart = organism.get("heart", {})
        lung = organism.get("lung", {})
        kidney = organism.get("kidney", {})
        intero = organism.get("interoception", {})
        self.timeline.append(
            {
                "sample": self._sample,
                "wall_time": datetime.now(timezone.utc).isoformat(),
                "episode": training.get("episode", 0),
                "successes": training.get("successes", 0),
                "collisions": training.get("collisions", 0),
                "total_reward": training.get("total_reward", 0.0),
                "goal_progress": training.get("goal_progress", 0.0),
                "controller": self.controller_mode,
                "atp": physiology.get("atp", 0.0),
                "pump": physiology.get("pump", 0.0),
                "sleep_stage": physiology.get("sleep_stage", "WAKE"),
                "oxygen": biology.get("oxygen", 0.0),
                "glucose": biology.get("glucose", 0.0),
                "myelin": biology.get("myelin_coverage", 0.0),
                "microglia": biology.get("microglia_activation", 0.0),
                "tissue_volume_ratio": biology.get("tissue_volume_ratio", 1.0),
                "linear_expansion": biology.get("linear_expansion", 1.0),
                "effective_tissue_mass_ng": biology.get("effective_tissue_mass_ng", 0.0),
                "net_biomass_change_ng": biology.get("net_biomass_change_ng", 0.0),
                "tissue_pressure_kpa": biology.get("tissue_pressure_kpa", 0.0),
                "solid_packing_fraction": biology.get("solid_packing_fraction", 0.8),
                "ecs_fraction": biology.get("ecs_fraction", 0.2),
                "material_reserve": biology.get("material_reserve", 1.0),
                "mean_energy": metrics.get("mean_energy", 0.0),
                "safety_interventions": self.robot.safety.interventions,
                "heart_rate": heart.get("heart_rate_bpm", 0.0),
                "mean_arterial_pressure": circ.get("mean_arterial_pressure_mm_hg", 0.0),
                "sao2": circ.get("arterial_oxygen_saturation", 0.0),
                "gfr": kidney.get("glomerular_filtration_rate_ml_per_min", 0.0),
                "visceral_distress": intero.get("visceral_distress", 0.0),
            }
        )

    def start(self) -> None:
        if self.cortex_available and self._cortex_thread is None:
            self._cortex_thread = threading.Thread(
                target=self._cortex_loop,
                name="tatarus-cortex-executive",
                daemon=True,
            )
            self._cortex_thread.start()
        if self.imaginatio_only:
            return
        self._thread = threading.Thread(
            target=self._training_loop,
            name="tatarus-live-training",
            daemon=True,
        )
        self._thread.start()

    def _training_loop(self) -> None:
        while not self.stopping:
            if self.paused:
                self._wake.wait(0.1)
                self._wake.clear()
                continue
            started = time.monotonic()
            try:
                with self._lock:
                    # A snapshot load can pause the runtime after this loop has
                    # already passed the fast check above. Re-check while
                    # holding the same lock that protects handle replacement;
                    # otherwise one stale iteration may enter the newly loaded
                    # native organism before the user resumes it.
                    if self.paused:
                        continue
                    motor_before = self.next_motor
                    observation = self.robot.advance(motor_before)
                    scanner_pose, scanner_readings = self.robot.scanner_frame()
                    map_update = CartographyUpdate()
                    action_id = self.robot.motor_selected_direction + 1
                    product = ProductState()
                    if self.organism_handle:
                        if not self.library.tatarus_organism_begin_action(
                            self.organism_handle,
                            action_id,
                            max(0.05, motor_before.confidence),
                        ):
                            raise RuntimeError(self._last_error())
                        movement = max(0.0, min(1.0, float(motor_before.movement)))
                        mechanical_power = 4.0 + 18.0 * movement
                        physical_load = RobotPhysicalLoad(
                            mechanical_power / 24.0,
                            24.0,
                            1.0 + 4.0 * movement,
                            0.5 + 2.5 * movement,
                            mechanical_power,
                            8.0,
                            float(self.robot.battery),
                            25.0 + 6.0 * movement,
                        )
                        if hasattr(self.library, "tatarus_organism_explore_step_with_load"):
                            ok = self.library.tatarus_organism_explore_step_with_load(
                                self.organism_handle,
                                ctypes.byref(observation),
                                int(self.robot.navigation_context_id),
                                ctypes.byref(scanner_pose),
                                scanner_readings,
                                len(scanner_readings),
                                ctypes.byref(self.atmosphere),
                                ctypes.byref(physical_load),
                                float(self.interval),
                                ctypes.byref(product),
                                ctypes.byref(map_update),
                            )
                        else:
                            ok = self.library.tatarus_organism_step_with_load(
                                self.organism_handle,
                                ctypes.byref(observation),
                                ctypes.byref(self.atmosphere),
                                ctypes.byref(physical_load),
                                float(self.interval),
                                ctypes.byref(product),
                            )
                        if not ok:
                            raise RuntimeError(self._last_error())
                    else:
                        if not self.library.tatarus_begin_action(
                            self.handle,
                            action_id,
                            max(0.05, motor_before.confidence),
                        ):
                            raise RuntimeError(self._last_error())
                        if hasattr(self.library, "tatarus_explore"):
                            ok = self.library.tatarus_explore(
                                self.handle,
                                ctypes.byref(observation),
                                int(self.robot.navigation_context_id),
                                ctypes.byref(scanner_pose),
                                scanner_readings,
                                len(scanner_readings),
                                ctypes.byref(product),
                                ctypes.byref(map_update),
                            )
                        else:
                            ok = self.library.tatarus_observe(
                                self.handle,
                                ctypes.byref(observation),
                                ctypes.byref(product),
                            )
                        if not ok:
                            raise RuntimeError(self._last_error())
                    self.latest_cartography_update = {
                        "environment_id": int(map_update.environment_id),
                        "mapped_voxels": int(map_update.mapped_voxels),
                        "free_voxels": int(map_update.free_voxels),
                        "occupied_voxels": int(map_update.occupied_voxels),
                        "frontier_voxels": int(map_update.frontier_voxels),
                        "local_novelty": float(map_update.local_novelty),
                    }
                    motor_after_sense = self._motor_state()
                    end_action = (
                        self.library.tatarus_organism_end_action
                        if self.organism_handle
                        else self.library.tatarus_end_action
                    )
                    end_handle = self.organism_handle or self.handle
                    if not end_action(
                        end_handle, action_id, self.robot.last_reward,
                        1.0 if self.robot.position == self.robot.goal else 0.0,
                        1.0 if self.robot.last_new_cell else 0.0,
                    ):
                        raise RuntimeError(self._last_error())
                    if self.robot._reset_pending:
                        end_episode = (
                            self.library.tatarus_organism_end_episode
                            if self.organism_handle
                            else self.library.tatarus_end_episode
                        )
                        if not end_episode(
                            end_handle,
                            1 if self.robot.position == self.robot.goal else 0,
                        ):
                            raise RuntimeError(self._last_error())
                    self.next_motor = motor_after_sense
                    self.robot.observe_neural(product, motor_after_sense)
                    self.latest_robot = self.robot.snapshot()
                    self._sample += 1
                    self.latest_frame = self._json_snapshot("tatarus_get_live_json")
                    if self.organism_handle and hasattr(self.library, "tatarus_organism_get_json"):
                        self.latest_organism = self._json_snapshot(
                            "tatarus_organism_get_json", self.organism_handle
                        )
                    if self._sample % 5 == 0:
                        self.latest_physiology = self._json_snapshot(
                            "tatarus_get_physiology_json"
                        )
                        self._record_timeline()
                        self.latest_environment_map = self._environment_map_snapshot()
                    if self._sample % 20 == 0:
                        self.latest_geometry = self._json_snapshot(
                            "tatarus_get_spatial_json"
                        )
                    self.error = ""
            except Exception as exc:  # keep the status endpoint alive for diagnosis
                self.error = str(exc)
                self.paused = True
            delay = max(0.004, self.interval / self.speed - (time.monotonic() - started))
            self._wake.wait(delay)
            self._wake.clear()

    def snapshot(self, kind: str) -> bytes:
        with self._lock:
            if kind == "geometry":
                return self.latest_geometry
            if kind == "physiology":
                return self.latest_physiology
            if kind == "robot":
                return self.latest_robot
            if kind == "organism":
                return self.latest_organism
            if kind == "imaginatio":
                return self.latest_imaginatio
            if kind == "imaginatio_trace":
                return self.latest_imaginatio_trace
            if kind == "cortex":
                if self.cortex_configured:
                    try:
                        self._native_cortex_command("poll")
                    except (ValueError, RuntimeError, OSError):
                        pass
                state = self._refresh_cortex_status()
                state["autonomous"] = self.cortex_autonomous
                self.latest_cortex = json.dumps(
                    state, ensure_ascii=False, separators=(",", ":")
                ).encode("utf-8")
                return self.latest_cortex
            if kind == "throughput":
                if self.organism_handle and hasattr(self.library, "tatarus_organism_get_throughput_json"):
                    return self._json_snapshot("tatarus_organism_get_throughput_json", self.organism_handle)
                return b'{"schema":"tatarus-throughput-v1","available":false}'
            if kind == "environment_map":
                return self.latest_environment_map
            return self.latest_frame

    @staticmethod
    def _imaginatio_pixels(command: dict[str, Any]) -> tuple[Any, int, str]:
        packed = command.get("rgb8_base64")
        if isinstance(packed, str):
            width = int(command.get("width", 0))
            height = int(command.get("height", 0))
            if width != 512 or height != 512:
                raise ValueError("Das Farblabor erwartet ein echtes 512 × 512 RGB-Bild")
            try:
                values = base64.b64decode(packed, validate=True)
            except (ValueError, TypeError) as exc:
                raise ValueError("Die kompakten RGB24-Bilddaten sind ungültig") from exc
            expected = width * height * 3
            if len(values) != expected:
                raise ValueError("Das Farblabor erwartet 512 × 512 × 3 RGB8-Kanäle")
            return (ctypes.c_uint8 * len(values)).from_buffer_copy(values), len(values), "rgb8"
        raw_rgb = command.get("rgb_pixels")
        is_rgb = isinstance(raw_rgb, list)
        raw = raw_rgb if is_rgb else command.get("pixels")
        expected = 32 * 32 * (3 if is_rgb else 1)
        if not isinstance(raw, list) or len(raw) != expected:
            raise ValueError(
                "Das Farblabor erwartet 32 × 32 RGB-Bildpunkte"
                if is_rgb else "Das Zeichenlabor erwartet genau 32 × 32 Bildpunkte"
            )
        values: list[float] = []
        for value in raw:
            number = float(value)
            if not math.isfinite(number):
                raise ValueError("Bildpunkte müssen endliche Zahlen sein")
            values.append(min(1.0, max(0.0, number)))
        return (ctypes.c_double * len(values))(*values), len(values), "rgb" if is_rgb else "gray"

    @staticmethod
    def _imaginatio_scene(raw: Any) -> tuple[TatarusSceneDescription, list[Any]]:
        if not isinstance(raw, dict):
            raise ValueError("Eine Szene muss als Objektbeschreibung übergeben werden")
        raw_objects = raw.get("objects")
        raw_relations = raw.get("relations", [])
        if not isinstance(raw_objects, list) or not 1 <= len(raw_objects) <= 64:
            raise ValueError("Eine Szene braucht 1 bis 64 Objekte")
        if not isinstance(raw_relations, list):
            raise ValueError("Szenenrelationen müssen eine Liste sein")
        references: list[Any] = []

        def encoded(value: Any, fallback: str = "") -> bytes:
            result = str(value if value is not None else fallback).strip().encode("utf-8")
            references.append(result)
            return result

        objects: list[TatarusSceneObject] = []
        fields = (("x", 1), ("y", 2), ("scale", 4), ("rotation", 8), ("depth", 16))
        for raw_object in raw_objects:
            if not isinstance(raw_object, dict):
                raise ValueError("Jedes Szenenobjekt muss strukturiert beschrieben sein")
            instance = str(raw_object.get("instance", "")).strip()
            category = str(raw_object.get("category", "")).strip()
            if not instance or not category:
                raise ValueError("Jedes Szenenobjekt braucht Instanzname und Kategorie")
            values: dict[str, float] = {}
            present = 0
            for field, bit in fields:
                if raw_object.get(field) is None:
                    values[field] = 0.0
                    continue
                values[field] = float(raw_object[field])
                if not math.isfinite(values[field]):
                    raise ValueError("Szenenzustände müssen endliche Zahlen sein")
                present |= bit
            objects.append(TatarusSceneObject(
                encoded(instance), encoded(category),
                encoded(raw_object.get("pose", "canonical"), "canonical"), present,
                values["x"], values["y"], values["scale"],
                values["rotation"], values["depth"],
            ))
        object_array = (TatarusSceneObject * len(objects))(*objects)
        references.append(object_array)

        relations: list[TatarusSceneRelation] = []
        for raw_relation in raw_relations:
            if not isinstance(raw_relation, dict):
                raise ValueError("Jede Szenenrelation muss strukturiert beschrieben sein")
            kind = str(raw_relation.get("relation", "")).strip().lower().replace("-", "_")
            if kind not in SCENE_RELATIONS:
                raise ValueError(f"Unbekannte Szenenrelation: {kind or 'leer'}")
            relations.append(TatarusSceneRelation(
                encoded(raw_relation.get("subject", "")), SCENE_RELATIONS[kind],
                encoded(raw_relation.get("object", "")),
                float(raw_relation.get("confidence", 1.0)),
            ))
        relation_array = (TatarusSceneRelation * len(relations))(*relations)
        references.append(relation_array)
        background = raw.get("background", [0.0, 0.0, 0.0])
        if not isinstance(background, list) or len(background) != 3:
            raise ValueError("Der Szenenhintergrund braucht drei RGB-Werte")
        color = [float(value) for value in background]
        name = encoded(raw.get("name", "scene"), "scene")
        description = TatarusSceneDescription(
            name, object_array, len(objects),
            relation_array if relations else None, len(relations),
            color[0], color[1], color[2],
        )
        references.append(description)
        return description, references

    @staticmethod
    def _imaginatio_action(raw: Any) -> tuple[TatarusSceneAction, list[Any]]:
        if not isinstance(raw, dict):
            raise ValueError("Eine prospektive Handlung muss strukturiert beschrieben sein")
        kind = str(raw.get("kind", "")).strip().lower().replace("-", "_")
        if kind not in SCENE_ACTIONS:
            raise ValueError(f"Unbekannte prospektive Handlung: {kind or 'leer'}")
        actor = str(raw.get("actor", "")).strip()
        if not actor:
            raise ValueError("Eine prospektive Handlung braucht einen Akteur")
        direction = raw.get("direction", [0.0, 0.0])
        if not isinstance(direction, list) or len(direction) != 2:
            raise ValueError("Die Handlungsrichtung braucht zwei Werte")
        references: list[Any] = []

        def encoded(value: Any) -> bytes:
            result = str(value or "").strip().encode("utf-8")
            references.append(result)
            return result

        action = TatarusSceneAction(
            SCENE_ACTIONS[kind], encoded(raw.get("label", "")), encoded(actor),
            encoded(raw.get("target", "")), float(direction[0]), float(direction[1]),
            float(raw.get("magnitude", 1.0)), float(raw.get("duration", 1.0)),
        )
        references.append(action)
        return action, references

    def imaginatio_control(self, command: dict[str, Any]) -> dict[str, Any]:
        if not self.organism_handle or not hasattr(
            self.library, "tatarus_organism_imaginatio_get_json"
        ):
            raise RuntimeError("Diese SDK-Bibliothek enthält das KI-Zeichenlabor nicht")
        action = str(command.get("action", "")).strip().lower()
        allowed = {
            "learn",
            "recognize",
            "learn_pose",
            "recall",
            "associate_symbol",
            "draw_symbol",
            "draw_free",
            "compose",
            "draw_category",
            "fuse_categories",
            "observe_scene",
            "draw_scene",
            "learn_transition",
            "imagine_future",
            "reset_canvas",
        }
        if action not in allowed:
            raise ValueError("Unbekannte Zeichenlabor-Aktion")

        with self._lock:
            self.paused = True
            text_value = str(command.get("text", "")).strip()[:120]
            category_value = str(command.get("category", "")).strip()[:240]
            if action in {
                "learn", "learn_pose", "associate_symbol",
                "observe_scene", "learn_transition",
            } and self.evaluation_mode:
                raise ValueError(
                    "Das Nervensystem ist eingefroren. Für neue Engramme zuerst Lernmodus aktivieren."
                )
            if action in {"learn", "learn_pose", "recognize", "recall", "associate_symbol"}:
                pixels, pixel_count, pixel_format = self._imaginatio_pixels(command)
            else:
                pixels = None
                pixel_count = 0
                pixel_format = "none"

            recognition = None
            if action == "recognize":
                if pixel_format != "rgb8":
                    raise ValueError("Biologische Bilderkennung benötigt ein RGB8-Bild")
                expected = category_value.encode("utf-8") if category_value else None
                if hasattr(self.library, "tatarus_organism_imaginatio_perceive_scene_rgb8"):
                    # Whole-scene perception: retina/V1 first segments the image,
                    # then every candidate is foveated and recognized independently.
                    function = self.library.tatarus_organism_imaginatio_perceive_scene_rgb8
                    maximum_objects = min(32, max(1, int(command.get("maximum_objects", 16))))
                    required = int(function(
                        self.organism_handle, pixels, pixel_count, 512, 512,
                        expected, 0, maximum_objects, None, 0,
                    ))
                    if required <= 1 or required > 4 * 1024 * 1024:
                        raise RuntimeError(self._last_error())
                    buffer = ctypes.create_string_buffer(required)
                    returned = int(function(
                        self.organism_handle, pixels, pixel_count, 512, 512,
                        expected, 0, maximum_objects, buffer, required,
                    ))
                    if returned != required:
                        raise RuntimeError(self._last_error())
                    recognition = json.loads(bytes(buffer.raw[: required - 1]))
                    recognition["mode"] = "multi_object_scene"
                    ok = True
                    names = [
                        obj.get("category") or obj.get("instance") or "unbekannt"
                        for obj in recognition.get("objects", [])
                    ]
                    label = f"Visuell geprüft: {len(names)} Objekt(e)"
                    if names:
                        label += " · " + ", ".join(names[:6])
                else:
                    if not hasattr(
                        self.library,
                        "tatarus_organism_imaginatio_recognize_categories_rgb8",
                    ):
                        raise RuntimeError("Die SDK-Bibliothek unterstützt noch keine visuelle Kategorienerkennung")
                    function = self.library.tatarus_organism_imaginatio_recognize_categories_rgb8
                    maximum_matches = min(10, max(1, int(command.get("maximum_matches", 3))))
                    required = int(function(
                        self.organism_handle, pixels, pixel_count, 512, 512,
                        expected, maximum_matches, None, 0,
                    ))
                    if required <= 1 or required > 1024 * 1024:
                        raise RuntimeError(self._last_error())
                    buffer = ctypes.create_string_buffer(required)
                    returned = int(function(
                        self.organism_handle, pixels, pixel_count, 512, 512,
                        expected, maximum_matches, buffer, required,
                    ))
                    if returned != required:
                        raise RuntimeError(self._last_error())
                    recognition = json.loads(bytes(buffer.raw[: required - 1]))
                    recognition["mode"] = "whole_frame_legacy"
                    ok = True
                    best = (recognition.get("matches") or [{}])[0].get("category", "unbekannt")
                    label = f"Visuell geprüft: {best}"
            elif action == "learn":
                source_width = max(1, int(command.get("source_width", 512) or 512))
                source_height = max(1, int(command.get("source_height", 512) or 512))
                if (source_width > 512 or source_height > 512) \
                        and hasattr(self.library, "tatarus_organism_imaginatio_remember_source_resolution"):
                    if not self.library.tatarus_organism_imaginatio_remember_source_resolution(
                        self.organism_handle, source_width, source_height
                    ):
                        raise RuntimeError(self._last_error())
                if pixel_format == "rgb8":
                    if category_value:
                        if not hasattr(self.library, "tatarus_organism_imaginatio_learn_category_rgb8"):
                            raise RuntimeError("Die SDK-Bibliothek unterstützt noch kein Kategoriegedächtnis")
                        patch_side = min(8, max(1, int(command.get("patch_side", 8))))
                        ok = self.library.tatarus_organism_imaginatio_learn_category_rgb8(
                            self.organism_handle, pixels, pixel_count, 512, 512,
                            patch_side, text_value.encode("utf-8"),
                            category_value.encode("utf-8"),
                        )
                    elif not hasattr(self.library, "tatarus_organism_imaginatio_learn_rgb8"):
                        raise RuntimeError("Die geladene SDK-Bibliothek unterstützt noch kein RGB24-Training")
                    else:
                        patch_side = min(8, max(1, int(command.get("patch_side", 8))))
                        ok = self.library.tatarus_organism_imaginatio_learn_rgb8(
                            self.organism_handle, pixels, pixel_count, 512, 512,
                            patch_side, text_value.encode("utf-8"),
                        )
                else:
                    is_rgb = pixel_format == "rgb"
                    if is_rgb and not hasattr(self.library, "tatarus_organism_imaginatio_learn_rgb"):
                        raise RuntimeError("Die geladene SDK-Bibliothek unterstützt noch kein RGB-Training")
                    function = self.library.tatarus_organism_imaginatio_learn_rgb \
                        if is_rgb else self.library.tatarus_organism_imaginatio_learn
                    ok = function(
                        self.organism_handle,
                        pixels,
                        pixel_count,
                        text_value.encode("utf-8"),
                    )
                is_rgb = pixel_format in {"rgb", "rgb8"}
                label = (f"Kategorie {category_value} erweitert: {text_value or 'Beispiel'}"
                    if category_value else
                    f"Farbbild gelernt: {text_value or 'ohne Namen'}" if is_rgb else
                    f"Nachzeichnen gelernt: {text_value or 'ohne Namen'}")
            elif action == "learn_pose":
                pose_value = str(command.get("pose", "")).strip()[:120]
                if not category_value or not pose_value:
                    raise ValueError("Posenlernen braucht Kategorie und Posenname")
                if pixel_format != "rgb8":
                    raise ValueError("Posenlernen erwartet ein 512 × 512 RGB24-Bild")
                if not hasattr(self.library, "tatarus_organism_imaginatio_learn_pose_rgb8"):
                    raise RuntimeError("Die SDK-Bibliothek unterstützt Stage 6 noch nicht")
                patch_side = min(8, max(1, int(command.get("patch_side", 8))))
                ok = self.library.tatarus_organism_imaginatio_learn_pose_rgb8(
                    self.organism_handle, pixels, pixel_count, 512, 512,
                    patch_side, text_value.encode("utf-8"),
                    category_value.encode("utf-8"), pose_value.encode("utf-8"),
                )
                label = f"Pose {category_value} / {pose_value} gelernt"
            elif action == "recall":
                if pixel_format == "rgb8":
                    if not hasattr(self.library, "tatarus_organism_imaginatio_recall_rgb8"):
                        raise RuntimeError("Die geladene SDK-Bibliothek unterstützt noch keinen RGB24-Abruf")
                    ok = self.library.tatarus_organism_imaginatio_recall_rgb8(
                        self.organism_handle, pixels, pixel_count, 512, 512
                    )
                else:
                    is_rgb = pixel_format == "rgb"
                    if is_rgb and not hasattr(self.library, "tatarus_organism_imaginatio_recall_rgb"):
                        raise RuntimeError("Die geladene SDK-Bibliothek unterstützt noch keinen RGB-Abruf")
                    function = self.library.tatarus_organism_imaginatio_recall_rgb \
                        if is_rgb else self.library.tatarus_organism_imaginatio_recall
                    ok = function(self.organism_handle, pixels, pixel_count)
                label = "Gedächtniszeichnung aus visuellem Hinweis"
            elif action == "associate_symbol":
                if not text_value:
                    raise ValueError("Für die Assoziation wird ein Symbol benötigt")
                if pixel_format == "rgb8":
                    if not hasattr(self.library, "tatarus_organism_imaginatio_associate_symbol_rgb8"):
                        raise RuntimeError("Die geladene SDK-Bibliothek unterstützt noch keine RGB24-Symbolbindung")
                    ok = self.library.tatarus_organism_imaginatio_associate_symbol_rgb8(
                        self.organism_handle, pixels, pixel_count, 512, 512,
                        text_value.encode("utf-8"),
                    )
                else:
                    is_rgb = pixel_format == "rgb"
                    if is_rgb and not hasattr(self.library, "tatarus_organism_imaginatio_associate_symbol_rgb"):
                        raise RuntimeError("Die geladene SDK-Bibliothek unterstützt noch keine RGB-Symbolbindung")
                    function = self.library.tatarus_organism_imaginatio_associate_symbol_rgb \
                        if is_rgb else self.library.tatarus_organism_imaginatio_associate_symbol
                    ok = function(
                        self.organism_handle,
                        pixels,
                        pixel_count,
                        text_value.encode("utf-8"),
                    )
                label = f"Symbol verknüpft: {text_value}"
            elif action == "draw_symbol":
                if not text_value:
                    raise ValueError("Für den Abruf wird ein Symbol benötigt")
                ok = self.library.tatarus_organism_imaginatio_draw_symbol(
                    self.organism_handle, text_value.encode("utf-8")
                )
                label = f"Symbolabruf: {text_value}"
            elif action == "draw_free":
                ok = self.library.tatarus_organism_imaginatio_draw_free(
                    self.organism_handle, text_value.encode("utf-8")
                )
                label = f"Freie Rekonstruktion: {text_value or 'stärkstes Engramm'}"
            elif action == "compose":
                if len([part for part in text_value.split(",") if part.strip()]) < 2:
                    raise ValueError("Eine Komposition braucht mindestens zwei Symbole")
                ok = self.library.tatarus_organism_imaginatio_compose(
                    self.organism_handle, text_value.encode("utf-8")
                )
                label = f"Freie Komposition: {text_value}"
            elif action in {"draw_category", "fuse_categories"}:
                categories = category_value or text_value
                parts = [part.strip() for part in categories.split(",") if part.strip()]
                if not parts:
                    raise ValueError("Mindestens eine gelernte Kategorie wird benötigt")
                if len(parts) > 5:
                    raise ValueError("Die 5-Punkt-Verschmelzung akzeptiert höchstens fünf Kategorien")
                function_name = ("tatarus_organism_imaginatio_draw_category"
                    if len(parts) == 1 else "tatarus_organism_imaginatio_fuse_categories")
                if not hasattr(self.library, function_name):
                    raise RuntimeError("Die geladene SDK-Bibliothek unterstützt Stufe 5 noch nicht")
                variation_seed = int(command.get("variation_seed", 0)) & 0xFFFFFFFFFFFFFFFF
                ok = getattr(self.library, function_name)(
                    self.organism_handle, ",".join(parts).encode("utf-8"), variation_seed
                )
                label = (f"Neue Variante aus Kategorie: {parts[0]}" if len(parts) == 1
                    else f"5-Punkt-Verschmelzung: {' + '.join(parts)}")
            elif action == "observe_scene":
                if not hasattr(self.library, "tatarus_organism_imaginatio_observe_scene"):
                    raise RuntimeError("Die SDK-Bibliothek unterstützt Stage 6 noch nicht")
                scene, scene_refs = self._imaginatio_scene(command.get("scene"))
                ok = self.library.tatarus_organism_imaginatio_observe_scene(
                    self.organism_handle, ctypes.byref(scene)
                )
                label = "Relationsszene beobachtet"
            elif action == "draw_scene":
                if not hasattr(self.library, "tatarus_organism_imaginatio_draw_scene"):
                    raise RuntimeError("Die SDK-Bibliothek unterstützt Stage 6 noch nicht")
                scene, scene_refs = self._imaginatio_scene(command.get("scene"))
                variation_seed = int(command.get("variation_seed", 0)) & 0xFFFFFFFFFFFFFFFF
                ok = self.library.tatarus_organism_imaginatio_draw_scene(
                    self.organism_handle, ctypes.byref(scene), variation_seed
                )
                label = "Neue relationale Szene imaginiert"
            elif action == "learn_transition":
                if not hasattr(self.library, "tatarus_organism_imaginatio_learn_transition"):
                    raise RuntimeError("Die SDK-Bibliothek unterstützt Stage 7 noch nicht")
                before, before_refs = self._imaginatio_scene(command.get("before"))
                after, after_refs = self._imaginatio_scene(command.get("after"))
                scene_action, action_refs = self._imaginatio_action(command.get("scene_action"))
                ok = self.library.tatarus_organism_imaginatio_learn_transition(
                    self.organism_handle, ctypes.byref(before),
                    ctypes.byref(scene_action), ctypes.byref(after),
                )
                label = "Handlungswirkung für Prospektion gelernt"
            elif action == "imagine_future":
                if not hasattr(self.library, "tatarus_organism_imaginatio_imagine_future"):
                    raise RuntimeError("Die SDK-Bibliothek unterstützt Stage 7 noch nicht")
                scene, scene_refs = self._imaginatio_scene(
                    command.get("initial", command.get("scene"))
                )
                raw_actions = command.get("actions")
                if not isinstance(raw_actions, list) or not 1 <= len(raw_actions) <= 256:
                    raise ValueError("Stage 7 braucht 1 bis 256 Handlungen")
                parsed_actions = [self._imaginatio_action(value) for value in raw_actions]
                action_array = (TatarusSceneAction * len(parsed_actions))(
                    *(value[0] for value in parsed_actions)
                )
                action_refs = [item for value in parsed_actions for item in value[1]]
                variation_seed = int(command.get("variation_seed", 0)) & 0xFFFFFFFFFFFFFFFF
                ok = self.library.tatarus_organism_imaginatio_imagine_future(
                    self.organism_handle, ctypes.byref(scene), action_array,
                    len(parsed_actions), variation_seed,
                )
                label = f"Zukunft über {len(parsed_actions)} Zustände imaginiert"
            else:
                ok = self.library.tatarus_organism_imaginatio_reset_canvas(
                    self.organism_handle
                )
                label = "Zeichenleinwand geleert"

            if not ok:
                raise RuntimeError(self._last_error())
            self.latest_imaginatio = self._json_snapshot(
                "tatarus_organism_imaginatio_get_json", self.organism_handle
            )
            self.latest_imaginatio_trace = self._json_snapshot(
                "tatarus_organism_imaginatio_get_trace_json", self.organism_handle
            )
            self.latest_frame = self._json_snapshot("tatarus_get_live_json")
            self.latest_physiology = self._json_snapshot("tatarus_get_physiology_json")
            self.latest_organism = self._json_snapshot(
                "tatarus_organism_get_json", self.organism_handle
            )
            marker = {
                "sample": self._sample,
                "type": f"imaginatio_{action}",
                "label": label,
            }
            self.timeline_markers.append(marker)
            self.last_imaginatio_context = {
                "operation": action,
                "text": text_value,
                "category": category_value,
                "reference_visible": bool(
                    json.loads(self.latest_imaginatio)
                    .get("last_result", {})
                    .get("reference_visible_during_drawing", False)
                ),
            }
            self._wake.set()
            return {
                "ok": True,
                "operation": action,
                "label": label,
                "paused": True,
                "imaginatio": json.loads(self.latest_imaginatio),
                "trace": json.loads(self.latest_imaginatio_trace),
                "recognition": recognition,
            }

    def cortex_control(self, command: dict[str, Any]) -> dict[str, Any]:
        action = str(command.get("action", "status")).strip().lower()
        if action == "status":
            return self._refresh_cortex_status()
        if not self.cortex_available:
            raise RuntimeError("Die geladene SDK-Bibliothek enthält den Hybrid Cortex nicht")

        with self._lock:
            if action == "configure":
                self._configure_cortex_runtime()
            elif action == "probe":
                if not self.cortex_configured:
                    self._configure_cortex_runtime()
                ok = self.library.tatarus_organism_cortex_probe(self.organism_handle)
                state = self._refresh_cortex_status()
                state["ok"] = bool(ok)
                if not ok:
                    state["probe_error"] = self._last_error()
                return state
            elif action == "add_goal":
                text = str(command.get("text", "")).strip()[:800]
                if not text:
                    raise ValueError("Ein Executive-Ziel darf nicht leer sein")
                priority = max(0.0, min(1.0, float(command.get("priority", 0.9))))
                goal_id = self.library.tatarus_organism_cortex_push_goal(
                    self.organism_handle, text.encode("utf-8"), priority
                )
                if not goal_id:
                    raise RuntimeError(self._last_error())
                self.timeline_markers.append({
                    "sample": self._sample, "type": "cortex_goal",
                    "label": f"Cortex-Ziel #{goal_id}: {text[:80]}",
                })
            elif action in {"complete_goal", "fail_goal", "cancel_goal"}:
                goal_id = int(command.get("goal_id", 0))
                if goal_id <= 0:
                    raise ValueError("Ungültige Cortex-Ziel-ID")
                if action == "cancel_goal":
                    ok = self.library.tatarus_organism_cortex_cancel_goal(
                        self.organism_handle, goal_id
                    )
                else:
                    ok = self.library.tatarus_organism_cortex_complete_goal(
                        self.organism_handle, goal_id, 1 if action == "complete_goal" else 0
                    )
                if not ok:
                    raise RuntimeError(self._last_error())
            elif action == "remember":
                key = str(command.get("key", "")).strip()[:120]
                value = str(command.get("value", "")).strip()[:800]
                if not key or not value:
                    raise ValueError("Working Memory braucht Schlüssel und Inhalt")
                salience = max(0.0, min(1.0, float(command.get("salience", 0.8))))
                if not self.library.tatarus_organism_cortex_remember(
                    self.organism_handle, key.encode("utf-8"), value.encode("utf-8"), salience
                ):
                    raise RuntimeError(self._last_error())
            elif action in {"analyze", "imagine", "hybrid", "plan", "autonomous_tick", "poll"}:
                goal = str(command.get("goal", "")).strip()[:1200]
                self._native_cortex_command(action, goal)
            elif action == "execute_imagination":
                self._native_cortex_command("execute_imagination")
                self.latest_imaginatio = self._json_snapshot(
                    "tatarus_organism_imaginatio_get_json", self.organism_handle
                )
                self.latest_imaginatio_trace = self._json_snapshot(
                    "tatarus_organism_imaginatio_get_trace_json", self.organism_handle
                )
                self.latest_organism = self._json_snapshot(
                    "tatarus_organism_get_json", self.organism_handle
                )
                self.timeline_markers.append({
                    "sample": self._sample, "type": "cortex_imagination",
                    "label": "Cortex-Direktive durch TATARUS IMAGINATIO ausgeführt",
                })
            elif action == "autonomous_start":
                self.cortex_autonomous = True
            elif action == "autonomous_stop":
                self.cortex_autonomous = False
            elif action == "disable":
                if not self.library.tatarus_organism_cortex_disable(self.organism_handle):
                    raise RuntimeError(self._last_error())
                self.cortex_autonomous = False
                self.cortex_configured = False
            else:
                raise ValueError("Unbekannte Cortex-UI-Aktion")

            state = self._refresh_cortex_status()
            state["ok"] = True
            state["autonomous"] = self.cortex_autonomous
            if action == "execute_imagination":
                state["imaginatio"] = json.loads(self.latest_imaginatio)
                state["trace"] = json.loads(self.latest_imaginatio_trace)
            return state

    @staticmethod
    def _png_chunk(kind: bytes, payload: bytes) -> bytes:
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

    @classmethod
    def _grayscale_png(cls, pixels: list[float], width: int, height: int) -> bytes:
        rows = bytearray()
        for y in range(height):
            rows.append(0)
            for x in range(width):
                value = min(1.0, max(0.0, float(pixels[y * width + x])))
                rows.append(int(round(value * 255.0)))
        return (
            b"\x89PNG\r\n\x1a\n"
            + cls._png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
            + cls._png_chunk(b"IDAT", zlib.compress(bytes(rows), level=9))
            + cls._png_chunk(b"IEND", b"")
        )

    @classmethod
    def _rgb_png(cls, pixels: list[float] | bytes, width: int, height: int) -> bytes:
        rows = bytearray()
        for y in range(height):
            rows.append(0)
            begin = y * width * 3
            end = begin + width * 3
            if isinstance(pixels, bytes):
                rows.extend(pixels[begin:end])
            else:
                for channel in pixels[begin:end]:
                    value = min(1.0, max(0.0, float(channel)))
                    rows.append(int(round(value * 255.0)))
        return (
            b"\x89PNG\r\n\x1a\n"
            + cls._png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + cls._png_chunk(b"IDAT", zlib.compress(bytes(rows), level=9))
            + cls._png_chunk(b"IEND", b"")
        )

    @staticmethod
    def _pgm_bytes(pixels: list[float] | bytes, width: int, height: int) -> bytes:
        values = pixels if isinstance(pixels, bytes) else bytes(
            int(round(min(1.0, max(0.0, float(value))) * 255.0)) for value in pixels
        )
        return f"P5\n{width} {height}\n255\n".encode("ascii") + values

    @staticmethod
    def _ppm_bytes(pixels: list[float] | bytes, width: int, height: int) -> bytes:
        values = pixels if isinstance(pixels, bytes) else bytes(
            int(round(min(1.0, max(0.0, float(value))) * 255.0)) for value in pixels
        )
        return f"P6\n{width} {height}\n255\n".encode("ascii") + values

    def _gallery_directory(self, item_id: str) -> Path:
        if not item_id or Path(item_id).name != item_id or item_id.startswith("."):
            raise ValueError("Ungültige Galerie-ID")
        directory = (self.imaginatio_gallery_root / item_id).resolve()
        if directory.parent != self.imaginatio_gallery_root.resolve() or not directory.is_dir():
            raise ValueError("Galerieeintrag nicht gefunden")
        return directory

    def export_imaginatio(self, command: dict[str, Any]) -> dict[str, Any]:
        archive = bool(command.get("archive", False))
        raw_title = str(command.get("title", "")).strip()[:120]
        with self._lock:
            state = json.loads(self.latest_imaginatio)
            trace = json.loads(self.latest_imaginatio_trace)
            canvas = state.get("canvas", {})
            width = int(canvas.get("width", 0))
            height = int(canvas.get("height", 0))
            pixels = canvas.get("pixels", [])
            rgb_pixels = canvas.get("rgb_pixels", [])
            has_rgb = isinstance(rgb_pixels, list) and len(rgb_pixels) == width * height * 3
            packed_rgb = canvas.get("rgb8_base64")
            if not has_rgb and isinstance(packed_rgb, str):
                try:
                    rgb_pixels = base64.b64decode(packed_rgb, validate=True)
                except (ValueError, TypeError) as exc:
                    raise RuntimeError("Die kompakten Leinwanddaten sind beschädigt") from exc
                has_rgb = len(rgb_pixels) == width * height * 3
            if has_rgb and (not isinstance(pixels, list) or len(pixels) != width * height):
                if isinstance(rgb_pixels, bytes):
                    pixels = bytes(
                        int(round(0.2126 * rgb_pixels[index]
                            + 0.7152 * rgb_pixels[index + 1]
                            + 0.0722 * rgb_pixels[index + 2]))
                        for index in range(0, len(rgb_pixels), 3)
                    )
            if width <= 0 or height <= 0 or len(pixels) != width * height:
                raise RuntimeError("Die aktuelle IMAGINATIO-Leinwand ist nicht exportierbar")

            working_width, working_height = width, height
            target_space_motor_render = False
            requested_width = int(command.get("output_width", width) or width)
            requested_height = int(command.get("output_height", height) or height)
            use_target_space_render = bool(command.get(
                "target_space_render", command.get("native_render", False)
            ))
            if use_target_space_render:
                if not hasattr(self.library, "tatarus_organism_imaginatio_render_native_rgb8"):
                    raise RuntimeError("Diese TATARUS-Bibliothek besitzt noch keine direkte Ziel-Leinwand-Ausgabe")
                if requested_width < 8 or requested_height < 8 or requested_width > 4096 or requested_height > 4096:
                    raise ValueError("Die IMAGINATIO-Zielleinwand muss zwischen 8 und 4096 Pixel je Achse liegen")
                required = int(self.library.tatarus_organism_imaginatio_render_native_rgb8(
                    self.organism_handle, requested_width, requested_height, None, 0
                ))
                if required != requested_width * requested_height * 3:
                    raise RuntimeError(self._last_error() or "IMAGINATIO-Zielleinwandgröße ist ungültig")
                native_buffer = (ctypes.c_uint8 * required)()
                written = int(self.library.tatarus_organism_imaginatio_render_native_rgb8(
                    self.organism_handle, requested_width, requested_height, native_buffer, required
                ))
                if written != required:
                    raise RuntimeError(self._last_error() or "TATARUS konnte die Zielleinwand nicht vollständig bemalen")
                rgb_pixels = bytes(native_buffer)
                pixels = bytes(
                    int(round(0.2126 * rgb_pixels[index]
                        + 0.7152 * rgb_pixels[index + 1]
                        + 0.0722 * rgb_pixels[index + 2]))
                    for index in range(0, len(rgb_pixels), 3)
                )
                width, height = requested_width, requested_height
                has_rgb = True
                target_space_motor_render = True

            result = state.get("last_result", {})
            context = dict(self.last_imaginatio_context)
            title = raw_title or context.get("text") or f"IMAGINATIO {result.get('stage', 'Werk')}"
            slug = re.sub(r"[^a-z0-9]+", "-", title.casefold()).strip("-")[:48]
            slug = slug or "werk"
            created = datetime.now(timezone.utc)
            item_id = f"{created.strftime('%Y%m%dT%H%M%SZ')}-{slug}-{uuid.uuid4().hex[:6]}"
            temporary = Path(tempfile.mkdtemp(prefix=".pending-", dir=self.imaginatio_gallery_root))
            try:
                (temporary / "image.png").write_bytes(
                    self._rgb_png(rgb_pixels, width, height)
                    if has_rgb else self._grayscale_png(pixels, width, height)
                )
                (temporary / "image.pgm").write_bytes(
                    self._pgm_bytes(pixels, width, height)
                )
                if has_rgb:
                    (temporary / "image.ppm").write_bytes(
                        self._ppm_bytes(rgb_pixels, width, height)
                    )
                if archive:
                    snapshot = temporary / "snapshot"
                    if not self.library.tatarus_organism_save(
                        self.organism_handle, str(snapshot).encode("utf-8")
                    ):
                        raise RuntimeError(self._last_error())
                    trace_lines = "".join(
                        json.dumps(event, ensure_ascii=False, separators=(",", ":")) + "\n"
                        for event in trace.get("events", [])
                    )
                    (temporary / "action_trace.jsonl").write_text(
                        trace_lines, encoding="utf-8"
                    )

                events = trace.get("events", [])
                last_event = events[-1] if events else {}
                live = json.loads(self.latest_frame)
                live_metrics = live.get("metrics", {})
                live_biology = live.get("biology", {})
                neural = last_event.get("neural", {})
                plasticity = last_event.get("plasticity", {})
                metabolism = last_event.get("metabolism", {})
                text_value = str(context.get("text", ""))
                symbols = [part.strip() for part in text_value.split(",") if part.strip()]
                metadata = {
                    "schema": "tatarus-imaginatio-artifact-v9",
                    "id": item_id,
                    "title": title,
                    "created": created.isoformat(),
                    "stage": result.get("stage", "unknown"),
                    "operation": context.get("operation", "unknown"),
                    "canvas": {
                        "width": width,
                        "height": height,
                        "color_space": "sRGB" if has_rgb else "grayscale",
                        "channels": 3 if has_rgb else 1,
                        "working_width": working_width,
                        "working_height": working_height,
                        "motor_trace_replayed": target_space_motor_render,
                        "executed_directly_on_target_canvas": target_space_motor_render,
                        "raster_interpolation_used": False,
                        "information_width": working_width,
                        "information_height": working_height,
                        "synthesized_high_frequency_detail": bool(
                            target_space_motor_render
                            and (width > working_width or height > working_height)
                        ),
                        "detail_truth": (
                            "inferred_from_learned_pigment_and_edge_variation_not_recovered_source_truth"
                            if target_space_motor_render else "none"
                        ),
                        "render_method": (
                            "tatarus_normalized_motor_target_space_pigment_synthesis_v1"
                            if target_space_motor_render else "working_canvas"
                        ),
                    },
                    "symbols": symbols if context.get("operation") in {
                        "associate_symbol", "draw_symbol", "compose"
                    } else [],
                    "categories": result.get("categories", []) or [
                        part.strip() for part in str(context.get("category", "")).split(",")
                        if part.strip()
                    ],
                    "reference_visible": bool(
                        result.get("reference_visible_during_drawing", False)
                    ),
                    "result": {
                        "success": bool(result.get("success", False)),
                        "similarity": float(result.get("similarity", 0.0)),
                        "novelty": float(result.get("novelty", 0.0)),
                        "actions": int(result.get("actions", 0)),
                        "strokes": int(trace.get("summary", {}).get("strokes", 0)),
                    },
                    "memory": {
                        "recalled_engrams": result.get("recalled_engrams", []),
                        "active_assembly": int(result.get("active_assembly", 0)),
                    },
                    "organism": {
                        "experiences": int(live_metrics.get("experiences", 0)),
                        "somatic_spikes": int(live_metrics.get("total_spikes", 0)),
                        "dendritic_spikes": int(
                            neural.get("dendritic_spikes", live_biology.get("dendritic_spikes", 0))
                        ),
                        "active_synapses": int(live_metrics.get("active_synapses", 0)),
                        "atp": float(metabolism.get("atp", state.get("organism", {}).get("atp", 0.0))),
                        "creb": float(plasticity.get("creb", state.get("organism", {}).get("creb", 0.0))),
                        "protein": float(plasticity.get("protein", state.get("organism", {}).get("protein", 0.0))),
                        "sleep_pressure": float(metabolism.get("sleep_pressure", state.get("organism", {}).get("sleep_pressure", 0.0))),
                    },
                    "snapshot_attached": archive,
                    "snapshot_scope": "synthetic_organism+imaginatio" if archive else None,
                    "files": {
                        "png": f"/api/imaginatio/gallery/{item_id}/image.png",
                        "pgm": f"/api/imaginatio/gallery/{item_id}/image.pgm",
                        "ppm": f"/api/imaginatio/gallery/{item_id}/image.ppm"
                        if has_rgb else None,
                        "metadata": f"/api/imaginatio/gallery/{item_id}/metadata.json",
                        "trace": f"/api/imaginatio/gallery/{item_id}/action_trace.jsonl"
                        if archive else None,
                    },
                }
                (temporary / "metadata.json").write_text(
                    json.dumps(metadata, ensure_ascii=False, indent=2, allow_nan=False),
                    encoding="utf-8",
                )
                hashes = {
                    path.relative_to(temporary).as_posix(): self._sha256(path)
                    for path in sorted(temporary.rglob("*"))
                    if path.is_file()
                }
                (temporary / "archive_manifest.json").write_text(
                    json.dumps(
                        {
                            "schema": "tatarus-imaginatio-artifact-manifest-v1",
                            "artifact_id": item_id,
                            "files": hashes,
                        },
                        indent=2,
                    ),
                    encoding="utf-8",
                )
                final = self.imaginatio_gallery_root / item_id
                temporary.replace(final)
                return {"ok": True, "artifact": metadata}
            except Exception:
                shutil.rmtree(temporary, ignore_errors=True)
                raise

    def gallery_payload(self) -> dict[str, Any]:
        artifacts: list[dict[str, Any]] = []
        for directory in sorted(self.imaginatio_gallery_root.iterdir(), reverse=True):
            if not directory.is_dir() or directory.name.startswith("."):
                continue
            try:
                metadata = json.loads(
                    (directory / "metadata.json").read_text(encoding="utf-8")
                )
                if metadata.get("schema") not in {
                    "tatarus-imaginatio-artifact-v1",
                    "tatarus-imaginatio-artifact-v2",
                    "tatarus-imaginatio-artifact-v3",
                    "tatarus-imaginatio-artifact-v4",
                    "tatarus-imaginatio-artifact-v5",
                    "tatarus-imaginatio-artifact-v7",
                    "tatarus-imaginatio-artifact-v8",
                    "tatarus-imaginatio-artifact-v9",
                }:
                    continue
                artifacts.append(metadata)
            except (OSError, ValueError, TypeError, json.JSONDecodeError):
                continue
        return {"schema": "tatarus-imaginatio-gallery-v1", "artifacts": artifacts}

    def gallery_file(self, item_id: str, filename: str) -> tuple[bytes, str]:
        allowed = {
            "image.png": "image/png",
            "image.pgm": "image/x-portable-graymap",
            "image.ppm": "image/x-portable-pixmap",
            "metadata.json": "application/json; charset=utf-8",
            "action_trace.jsonl": "application/x-ndjson; charset=utf-8",
            "archive_manifest.json": "application/json; charset=utf-8",
        }
        if filename not in allowed:
            raise ValueError("Galeriedatei ist nicht freigegeben")
        path = self._gallery_directory(item_id) / filename
        if not path.is_file():
            raise ValueError("Galeriedatei nicht gefunden")
        return path.read_bytes(), allowed[filename]

    def gallery_control(self, command: dict[str, Any]) -> dict[str, Any]:
        action = str(command.get("action", "")).strip().lower()
        item_id = str(command.get("id", "")).strip()
        directory = self._gallery_directory(item_id)
        if action == "load_snapshot":
            snapshot = directory / "snapshot"
            if not snapshot.is_dir():
                raise ValueError("Dieses Werk besitzt keinen Nervensystem-Snapshot")
            with self._lock:
                if not self.library.tatarus_organism_load(
                    self.organism_handle, str(snapshot).encode("utf-8")
                ):
                    raise RuntimeError(self._last_error())
                self.paused = True
                self._refresh_snapshots()
                self.last_imaginatio_context = {
                    "operation": "gallery_snapshot",
                    "text": item_id,
                }
            return {
                "ok": True,
                "action": action,
                "imaginatio": json.loads(self.latest_imaginatio),
                "trace": json.loads(self.latest_imaginatio_trace),
            }
        if action == "delete":
            shutil.rmtree(directory)
            return {"ok": True, "action": action, "id": item_id}
        raise ValueError("Unbekannte Galerieaktion")

    def timeline_payload(self) -> dict[str, Any]:
        with self._lock:
            return {
                "schema": "tatarus-timeline-v1",
                "samples": list(self.timeline),
                "markers": list(self.timeline_markers),
            }

    def experiment_payload(self) -> dict[str, Any]:
        with self._lock:
            return {
                **self.experiment,
                "active_intervention": dict(self.intervention),
                "controller": self.controller_mode,
            }

    def list_snapshots(self) -> dict[str, Any]:
        snapshots: list[dict[str, Any]] = []
        for directory in sorted(self.snapshot_root.iterdir(), reverse=True):
            if not directory.is_dir() or directory.name.startswith("."):
                continue
            state_file = directory / "embodiment.json"
            manifest_file = directory / "manifest.json"
            if not state_file.is_file() or not manifest_file.is_file():
                continue
            try:
                state = json.loads(state_file.read_text(encoding="utf-8"))
                snapshots.append(
                    {
                        "id": directory.name,
                        "label": state.get("label", directory.name),
                        "created_at": state.get("created_at", ""),
                        "sample": state.get("runtime", {}).get("sample", 0),
                        "controller": state.get("runtime", {}).get("controller_mode", ""),
                        "seed": state.get("runtime", {}).get("seed", 0),
                        "map_name": state.get("world", {}).get("world", {}).get(
                            "map_name", "Klassischer Parcours"
                        ),
                    }
                )
            except (OSError, ValueError, TypeError):
                continue
        return {"schema": "tatarus-snapshot-index-v1", "snapshots": snapshots}

    def save_embodiment(self, label: str = "") -> dict[str, Any]:
        with self._lock:
            created = datetime.now(timezone.utc)
            snapshot_id = f"{created.strftime('%Y%m%dT%H%M%SZ')}-{uuid.uuid4().hex[:8]}"
            temporary = Path(tempfile.mkdtemp(prefix=".pending-", dir=self.snapshot_root))
            try:
                if self.organism_handle:
                    organism_directory = temporary / "organism"
                    if not self.library.tatarus_organism_save(
                        self.organism_handle,
                        str(organism_directory).encode("utf-8"),
                    ):
                        raise RuntimeError(self._last_error())
                else:
                    sdk_directory = temporary / "sdk"
                    if not self.library.tatarus_save(
                        self.handle, str(sdk_directory).encode("utf-8")
                    ):
                        raise RuntimeError(self._last_error())
                state = {
                    "schema": "tatarus-embodiment-snapshot-v2",
                    "id": snapshot_id,
                    "label": label.strip()[:120] or f"Snapshot {self._sample}",
                    "created_at": created.isoformat(),
                    "runtime": {
                        "seed": self.seed,
                        "tissue_profile": self.tissue_profile,
                        "neuron_count": self.neuron_count,
                        "sample": self._sample,
                        "speed": self.speed,
                        "paused": self.paused,
                        "controller_mode": self.controller_mode,
                        "evaluation_mode": self.evaluation_mode,
                        "active_map_id": self.active_map_id,
                        "intervention": self.intervention,
                        "next_motor": self._motor_payload(self.next_motor),
                        "atmosphere_preset": self.atmosphere_preset,
                        "atmosphere": {
                            "barometric_pressure_kpa": self.atmosphere.barometric_pressure_kpa,
                            "o2_fraction": self.atmosphere.o2_fraction,
                            "co2_fraction": self.atmosphere.co2_fraction,
                            "n2_fraction": self.atmosphere.n2_fraction,
                            "ambient_temperature_c": self.atmosphere.ambient_temperature_c,
                            "relative_humidity": self.atmosphere.relative_humidity,
                            "dust_ppm": self.atmosphere.dust_ppm,
                        },
                    },
                    "world": self.robot.export_state(),
                }
                state_file = temporary / "embodiment.json"
                state_file.write_text(
                    json.dumps(state, ensure_ascii=False, indent=2, allow_nan=False),
                    encoding="utf-8",
                )
                files = {
                    path.relative_to(temporary).as_posix(): self._sha256(path)
                    for path in sorted(temporary.rglob("*"))
                    if path.is_file()
                }
                manifest = {
                    "schema": "tatarus-embodiment-manifest-v1",
                    "snapshot_id": snapshot_id,
                    "files": files,
                }
                (temporary / "manifest.json").write_text(
                    json.dumps(manifest, indent=2), encoding="utf-8"
                )
                final = self.snapshot_root / snapshot_id
                temporary.replace(final)
                marker = {
                    "sample": self._sample,
                    "type": "snapshot_saved",
                    "snapshot_id": snapshot_id,
                    "label": state["label"],
                }
                self.timeline_markers.append(marker)
                return {"ok": True, **marker}
            except Exception:
                shutil.rmtree(temporary, ignore_errors=True)
                raise

    def _snapshot_directory(self, snapshot_id: str) -> Path:
        if not snapshot_id or Path(snapshot_id).name != snapshot_id:
            raise ValueError("invalid snapshot id")
        target = (self.snapshot_root / snapshot_id).resolve()
        if target.parent != self.snapshot_root.resolve() or not target.is_dir():
            raise ValueError("snapshot not found")
        return target

    def load_embodiment(self, snapshot_id: str) -> dict[str, Any]:
        with self._lock:
            directory = self._snapshot_directory(snapshot_id)
            manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
            if manifest.get("schema") != "tatarus-embodiment-manifest-v1":
                raise ValueError("unsupported snapshot manifest")
            for relative, expected in manifest.get("files", {}).items():
                path = (directory / relative).resolve()
                if directory.resolve() not in path.parents or not path.is_file():
                    raise ValueError("snapshot contains an invalid file path")
                if self._sha256(path) != expected:
                    raise ValueError(f"snapshot integrity check failed: {relative}")
            state = json.loads((directory / "embodiment.json").read_text(encoding="utf-8"))
            schema = state.get("schema")
            if schema not in {"tatarus-embodiment-snapshot-v1", "tatarus-embodiment-snapshot-v2"}:
                raise ValueError("unsupported embodiment snapshot")
            runtime_state = state["runtime"]
            restored_count = int(runtime_state.get("neuron_count", self.neuron_count))
            restored_seed = int(runtime_state["seed"])
            new_handle = self._create_handle(restored_seed, restored_count)
            new_organism = None
            try:
                intervention = normalized_intervention(runtime_state.get("intervention"))
                if schema == "tatarus-embodiment-snapshot-v2":
                    new_organism = self.library.tatarus_organism_create_sized(
                        restored_seed, restored_count
                    )
                    if not new_organism or not self.library.tatarus_organism_load(
                        new_organism, str(directory / "organism").encode("utf-8")
                    ):
                        raise RuntimeError(self._last_error())
                    self._apply_intervention(new_handle, intervention)
                else:
                    if not self.library.tatarus_load(
                        new_handle, str(directory / "sdk").encode("utf-8")
                    ):
                        raise RuntimeError(self._last_error())
                    self._apply_intervention(new_handle, intervention)
            except Exception:
                self.library.tatarus_destroy(new_handle)
                if new_organism:
                    self.library.tatarus_organism_destroy(new_organism)
                raise
            old_handle = self.handle
            self.handle = new_handle
            self.library.tatarus_destroy(old_handle)
            if new_organism:
                old_organism = self.organism_handle
                self.organism_handle = new_organism
                if old_organism:
                    self.library.tatarus_organism_destroy(old_organism)
            self.seed = restored_seed
            self.neuron_count = restored_count
            self.tissue_profile = str(
                runtime_state.get("tissue_profile", self._profile_for_count(restored_count))
            )
            self._sample = int(runtime_state["sample"])
            self.speed = float(runtime_state["speed"])
            self.controller_mode = str(runtime_state["controller_mode"])
            self.intervention = intervention
            atmosphere = runtime_state.get("atmosphere")
            if atmosphere:
                self.atmosphere = AtmosphericEnvironment(
                    float(atmosphere["barometric_pressure_kpa"]),
                    float(atmosphere["o2_fraction"]),
                    float(atmosphere["co2_fraction"]),
                    float(atmosphere["n2_fraction"]),
                    float(atmosphere["ambient_temperature_c"]),
                    float(atmosphere["relative_humidity"]),
                    float(atmosphere["dust_ppm"]),
                )
                self.atmosphere_preset = str(runtime_state.get("atmosphere_preset", "custom"))
            self.robot = RobotWorld.from_state(state["world"])
            self.evaluation_mode = bool(runtime_state.get(
                "evaluation_mode", not self.robot.learning_enabled
            ))
            self.robot.learning_enabled = not self.evaluation_mode
            self.active_map_id = self.robot.map_id
            self._store_robot_map()
            self._synchronize_native_navigation_state()
            self.next_motor = self._motor_from_payload(runtime_state["next_motor"])
            self.paused = True
            self.error = ""
            self.latest_robot = self.robot.snapshot()
            self._refresh_snapshots()
            marker = {
                "sample": self._sample,
                "type": "snapshot_loaded",
                "snapshot_id": snapshot_id,
                "label": state.get("label", snapshot_id),
            }
            self.timeline_markers.append(marker)
            self._wake.set()
            return {"ok": True, **marker, "paused": True}

    def _run_trial(
        self,
        seed: int,
        controller: str,
        intervention: dict[str, Any],
        steps: int,
        progress_start: float,
        progress_span: float,
    ) -> dict[str, Any]:
        handle = self._create_handle(seed)
        world = RobotWorld(seed, controller)
        try:
            self._synchronize_native_navigation_state(
                handle=handle,
                organism_handle=ctypes.c_void_p(),
                world=world,
            )
            self._apply_intervention(handle, intervention)
            self._apply_damage(handle, intervention, seed ^ 0xDA6A6E)
            motor = self._motor_state(handle)
            for step in range(steps):
                observation = world.advance(motor)
                observation.timestamp_ns = (step + 1) * 100_000_000
                action_id = world.motor_selected_direction + 1
                if not self.library.tatarus_begin_action(
                    handle, action_id, max(0.05, motor.confidence)
                ):
                    raise RuntimeError(self._last_error())
                product = ProductState()
                if not self.library.tatarus_observe(
                    handle, ctypes.byref(observation), ctypes.byref(product)
                ):
                    raise RuntimeError(self._last_error())
                motor = self._motor_state(handle)
                if not self.library.tatarus_end_action(
                    handle,
                    action_id,
                    world.last_reward,
                    1.0 if world.position == world.goal else 0.0,
                    1.0 if world.last_new_cell else 0.0,
                ):
                    raise RuntimeError(self._last_error())
                if world._reset_pending and not self.library.tatarus_end_episode(
                    handle, 1 if world.position == world.goal else 0
                ):
                    raise RuntimeError(self._last_error())
                world.observe_neural(product, motor)
                if step % 20 == 0:
                    with self._lock:
                        self.experiment["progress"] = progress_start + progress_span * (step + 1) / steps
            live = json.loads(self._json_snapshot("tatarus_get_live_json", handle))
            return {
                "training": json.loads(world.snapshot())["training"],
                "safety": world.safety.export_state(),
                "metrics": live.get("metrics", {}),
                "biology": live.get("biology", {}),
                "physiology": live.get("physiology", {}),
                "prospection": live.get("prospection", {}),
                "state_hash": live.get("state_hash"),
            }
        finally:
            self.library.tatarus_destroy(handle)

    @staticmethod
    def _trial_deltas(control: dict[str, Any], treatment: dict[str, Any]) -> dict[str, float]:
        pairs = {
            "successes": ("training", "successes"),
            "collisions": ("training", "collisions"),
            "total_reward": ("training", "total_reward"),
            "goal_progress": ("training", "goal_progress"),
            "mean_energy": ("metrics", "mean_energy"),
            "oxygen": ("biology", "oxygen"),
            "glucose": ("biology", "glucose"),
            "myelin": ("biology", "myelin_coverage"),
            "atp": ("physiology", "atp"),
            "pump": ("physiology", "pump"),
            "prediction_error": ("prospection", "prediction_error"),
        }
        return {
            label: float(treatment.get(section, {}).get(key, 0.0))
            - float(control.get(section, {}).get(key, 0.0))
            for label, (section, key) in pairs.items()
        }

    def start_ablation(
        self, preset: str, steps: int, controller: str | None = None
    ) -> dict[str, Any]:
        if preset not in INTERVENTION_PRESETS:
            raise ValueError("unknown intervention preset")
        steps = max(100, min(10000, int(steps)))
        mode = controller or self.controller_mode
        if mode not in RobotWorld.CONTROLLER_MODES:
            raise ValueError("unsupported controller mode")
        with self._lock:
            if self.experiment.get("status") == "running":
                raise ValueError("an experiment is already running")
            treatment = normalized_intervention(INTERVENTION_PRESETS[preset])
            experiment_id = f"EXP-{uuid.uuid4().hex[:8].upper()}"
            self.experiment = {
                "status": "running",
                "id": experiment_id,
                "preset": preset,
                "steps": steps,
                "seed": self.seed,
                "controller": mode,
                "progress": 0.0,
                "treatment": treatment,
                "result": None,
                "presets": INTERVENTION_PRESETS,
            }

        def worker() -> None:
            try:
                control = self._run_trial(
                    self.seed, mode, normalized_intervention(None), steps, 0.0, 0.5
                )
                treated = self._run_trial(self.seed, mode, treatment, steps, 0.5, 0.5)
                result = {
                    "control": control,
                    "treatment": treated,
                    "delta": self._trial_deltas(control, treated),
                    "interpretation": "Differenz = Intervention minus Kontrolle; gleiche Seeds und Zeitschritte.",
                }
                with self._lock:
                    self.experiment.update({"status": "complete", "progress": 1.0, "result": result})
                    self.timeline_markers.append(
                        {
                            "sample": self._sample,
                            "type": "experiment_complete",
                            "experiment_id": experiment_id,
                            "label": preset,
                        }
                    )
            except Exception as exc:
                with self._lock:
                    self.experiment.update({"status": "failed", "error": str(exc)})

        self._experiment_thread = threading.Thread(
            target=worker, name=f"tatarus-{experiment_id.lower()}", daemon=True
        )
        self._experiment_thread.start()
        return self.experiment_payload()

    def status(self) -> dict[str, Any]:
        return {
            "running": not self.stopping,
            "paused": self.paused,
            "speed": self.speed,
            "seed": self.seed,
            "tissue_profile": self.tissue_profile,
            "neuron_count": self.neuron_count,
            "tissue_profiles": dict(TISSUE_PROFILES),
            "sample": self._sample,
            "robot_episode": self.robot.episode,
            "robot_successes": self.robot.successes,
            "active_map_id": self.active_map_id,
            "active_map_name": self.robot.map_name,
            "maps": self.map_library_payload()["maps"],
            "control_source": self.controller_mode,
            "evaluation_mode": self.evaluation_mode,
            "learning_enabled": not self.evaluation_mode,
            "controller_modes": list(RobotWorld.CONTROLLER_MODES),
            "intervention": dict(self.intervention),
            "safety": self.robot.safety.export_state(),
            "experiment_status": self.experiment.get("status", "idle"),
            "atmosphere_preset": self.atmosphere_preset,
            "atmosphere_presets": ATMOSPHERE_PRESETS,
            "organism_active": bool(self.organism_handle),
            "imaginatio_active": bool(
                self.organism_handle
                and hasattr(self.library, "tatarus_organism_imaginatio_get_json")
            ),
            "imaginatio_only": self.imaginatio_only,
            "cortex_available": self.cortex_available,
            "cortex_configured": self.cortex_configured,
            "cortex_autonomous": self.cortex_autonomous,
            "cartography": dict(self.latest_cartography_update),
            "error": self.error,
        }

    def control(self, command: dict[str, Any]) -> dict[str, Any]:
        action = str(command.get("action", ""))
        if action == "pause":
            self.paused = True
        elif action == "resume":
            self.paused = False
        elif action == "evaluation_mode":
            with self._lock:
                self.evaluation_mode = bool(command.get("enabled", True))
                self.robot.learning_enabled = not self.evaluation_mode
                self.robot._reset_map_run()
                self._synchronize_native_navigation_state()
                self.latest_robot = self.robot.snapshot()
                self.timeline_markers.append({
                    "sample": self._sample,
                    "type": "evaluation_mode",
                    "label": (
                        "Eingefrorener Testmodus"
                        if self.evaluation_mode
                        else "Lernmodus"
                    ),
                })
        elif action == "speed":
            requested = float(command.get("value", 1.0))
            self.speed = min(8.0, max(0.25, requested))
        elif action == "reset":
            requested_seed = int(command.get("seed", self.seed))
            if requested_seed <= 0:
                requested_seed = 7411
            with self._lock:
                new_handle = self._create_handle(requested_seed)
                try:
                    self._apply_intervention(new_handle, self.intervention)
                    self._apply_damage(new_handle, self.intervention, requested_seed ^ 0xDA6A6E)
                except Exception:
                    self.library.tatarus_destroy(new_handle)
                    raise
                old_handle = self.handle
                self.handle = new_handle
                self.library.tatarus_destroy(old_handle)
                if self.organism_handle and hasattr(self.library, "tatarus_organism_destroy"):
                    self.library.tatarus_organism_destroy(self.organism_handle)
                    self.organism_handle = self.library.tatarus_organism_create_sized(
                        requested_seed, self.neuron_count
                    )
                    self._apply_organism_intervention(self.intervention)
                    self._configure_cortex_runtime()
                self.seed = requested_seed
                self._sample = 0
                self.robot = RobotWorld(
                    requested_seed, self.controller_mode, self._active_map_definition()
                )
                self.evaluation_mode = False
                self.robot.learning_enabled = True
                self._synchronize_native_navigation_state()
                self.next_motor = self._motor_state()
                self.latest_robot = self.robot.snapshot()
                self.timeline.clear()
                self.timeline_markers.clear()
                self.error = ""
                self._refresh_snapshots()
        elif action == "atmosphere":
            preset = str(command.get("preset", "earth"))
            if preset in ATMOSPHERE_PRESETS:
                self.atmosphere_preset = preset
                cfg = ATMOSPHERE_PRESETS[preset]
                self.atmosphere = AtmosphericEnvironment(
                    float(cfg["barometric_pressure_kpa"]),
                    float(cfg["o2_fraction"]),
                    float(cfg["co2_fraction"]),
                    float(cfg["n2_fraction"]),
                    float(cfg["ambient_temperature_c"]),
                    float(cfg["relative_humidity"]),
                    float(cfg["dust_ppm"]),
                )
                self.timeline_markers.append({
                    "sample": self._sample,
                    "type": "atmosphere_changed",
                    "label": cfg["label"],
                })
        elif action == "organism_infuse":
            if self.organism_handle and hasattr(self.library, "tatarus_organism_infuse"):
                vol = float(command.get("volume_ml", 500.0))
                na = float(command.get("na_mm", 154.0))
                k = float(command.get("k_mm", 4.0))
                glc = float(command.get("glucose_mm", 5.0))
                self.library.tatarus_organism_infuse(self.organism_handle, vol, na, k, glc)
                self.timeline_markers.append({
                    "sample": self._sample,
                    "type": "organism_infusion",
                    "label": f"+{vol:.0f} mL Infusion",
                })
        elif action == "organism_bleed":
            if self.organism_handle and hasattr(self.library, "tatarus_organism_bleed"):
                vol = float(command.get("volume_ml", 300.0))
                self.library.tatarus_organism_bleed(self.organism_handle, vol)
                self.timeline_markers.append({
                    "sample": self._sample,
                    "type": "organism_hemorrhage",
                    "label": f"-{vol:.0f} mL Blutung",
                })
        elif action == "organism_renal_function":
            if self.organism_handle and hasattr(self.library, "tatarus_organism_set_renal_function"):
                left = max(0.0, min(1.0, float(command.get("left_fraction", 1.0))))
                right = max(0.0, min(1.0, float(command.get("right_fraction", 1.0))))
                self.library.tatarus_organism_set_renal_function(
                    self.organism_handle, left, right
                )
                self.latest_organism = self._json_snapshot(
                    "tatarus_organism_get_json", self.organism_handle
                )
                self.timeline_markers.append({
                    "sample": self._sample,
                    "type": "organism_renal_function",
                    "label": f"Nierenfunktion L {left:.0%} / R {right:.0%}",
                })
        elif action == "organism_reset":
            with self._lock:
                if self.organism_handle and hasattr(self.library, "tatarus_organism_destroy"):
                    self.library.tatarus_organism_destroy(self.organism_handle)
                    self.organism_handle = self.library.tatarus_organism_create_sized(
                        self.seed, self.neuron_count
                    )
                    self._apply_organism_intervention(self.intervention)
                    self._configure_cortex_runtime()
                    self._synchronize_native_navigation_state()
                self.timeline_markers.append({
                    "sample": self._sample,
                    "type": "organism_reset",
                    "label": "Organismus Normalisiert",
                })
        elif action == "controller":
            mode = str(command.get("value", "neural_direct"))
            if mode not in RobotWorld.CONTROLLER_MODES:
                raise ValueError("unsupported controller mode")
            with self._lock:
                self.controller_mode = mode
                requested_seed = self.seed
                new_handle = self._create_handle(requested_seed)
                try:
                    self._apply_intervention(new_handle, self.intervention)
                    self._apply_damage(new_handle, self.intervention, requested_seed ^ 0xDA6A6E)
                except Exception:
                    self.library.tatarus_destroy(new_handle)
                    raise
                old_handle = self.handle
                self.handle = new_handle
                self.library.tatarus_destroy(old_handle)
                if self.organism_handle:
                    self.library.tatarus_organism_destroy(self.organism_handle)
                    self.organism_handle = self.library.tatarus_organism_create_sized(
                        requested_seed, self.neuron_count
                    )
                    self._apply_organism_intervention(self.intervention)
                    self._configure_cortex_runtime()
                self._sample = 0
                self.robot = RobotWorld(requested_seed, mode, self._active_map_definition())
                self.evaluation_mode = False
                self.robot.learning_enabled = True
                self._synchronize_native_navigation_state()
                self.next_motor = self._motor_state()
                self.latest_robot = self.robot.snapshot()
                self.timeline.clear()
                self.timeline_markers.clear()
                self._refresh_snapshots()
        elif action == "tissue_size":
            profile = str(command.get("profile", "standard"))
            requested_count = int(command.get("neurons", TISSUE_PROFILES.get(profile, 0)))
            if profile not in TISSUE_PROFILES and "neurons" not in command:
                raise ValueError("unknown tissue profile")
            if requested_count < 96 or requested_count > 65_536:
                raise ValueError("neuron count must be between 96 and 65536")
            with self._lock:
                new_handle = self._create_handle(self.seed, requested_count)
                try:
                    self._apply_intervention(new_handle, self.intervention)
                except Exception:
                    self.library.tatarus_destroy(new_handle)
                    raise
                old_handle = self.handle
                self.handle = new_handle
                self.library.tatarus_destroy(old_handle)
                self.neuron_count = requested_count
                self.tissue_profile = profile if profile in TISSUE_PROFILES \
                    else self._profile_for_count(requested_count)
                if self.organism_handle:
                    self.library.tatarus_organism_destroy(self.organism_handle)
                    self.organism_handle = self.library.tatarus_organism_create_sized(
                        self.seed, requested_count
                    )
                    self._apply_organism_intervention(self.intervention)
                    self._configure_cortex_runtime()
                self._sample = 0
                self.robot = RobotWorld(
                    self.seed, self.controller_mode, self._active_map_definition()
                )
                self.evaluation_mode = False
                self.robot.learning_enabled = True
                self._synchronize_native_navigation_state()
                self.next_motor = self._motor_state()
                self.latest_robot = self.robot.snapshot()
                self.timeline.clear()
                self.timeline_markers.clear()
                self._refresh_snapshots()
        elif action == "intervention":
            preset = command.get("preset")
            supplied = command.get("value", {})
            if preset is not None:
                if str(preset) not in INTERVENTION_PRESETS:
                    raise ValueError("unknown intervention preset")
                supplied = INTERVENTION_PRESETS[str(preset)]
            intervention = normalized_intervention(supplied)
            with self._lock:
                self._apply_intervention(self.handle, intervention)
                self._apply_damage(self.handle, intervention, self.seed ^ self._sample ^ 0xDA6A6E)
                self._apply_organism_intervention(intervention)
                self.intervention = intervention
                self.timeline_markers.append(
                    {
                        "sample": self._sample,
                        "type": "intervention",
                        "label": str(preset or "custom"),
                    }
                )
        elif action == "safety":
            with self._lock:
                self.robot.safety.enabled = bool(command.get("enabled", True))
        elif action == "map_select":
            result = self.select_map(str(command.get("id", "")))
            return {**self.status(), "map": result}
        elif action == "map_edit":
            result = self.edit_active_map(
                int(command.get("x", -1)),
                int(command.get("z", -1)),
                str(command.get("tool", "")),
            )
            return {**self.status(), "map": result}
        elif action == "map_rename":
            result = self.rename_active_map(str(command.get("name", "")))
            return {**self.status(), "map": result}
        elif action == "map_reset":
            result = self.reset_active_map()
            return {**self.status(), "map": result}
        elif action == "save_snapshot":
            result = self.save_embodiment(str(command.get("label", "")))
            return {**self.status(), "snapshot": result}
        elif action == "load_snapshot":
            result = self.load_embodiment(str(command.get("id", "")))
            return {**self.status(), "snapshot": result}
        elif action == "start_ablation":
            return self.start_ablation(
                str(command.get("preset", "control")),
                int(command.get("steps", 1200)),
                str(command.get("controller", self.controller_mode)),
            )
        else:
            raise ValueError("unsupported control action")
        self._wake.set()
        return self.status()

    def close(self) -> None:
        self.stopping = True
        self._wake.set()
        if self._thread:
            self._thread.join(timeout=2.0)
        if self._cortex_thread:
            self._cortex_thread.join(timeout=2.0)
        with self._lock:
            if self.handle:
                self.library.tatarus_destroy(self.handle)
                self.handle = ctypes.c_void_p()
            if self.organism_handle and hasattr(self.library, "tatarus_organism_destroy"):
                self.library.tatarus_organism_destroy(self.organism_handle)
                self.organism_handle = None


def make_handler(runtime: TatarusRuntime, web_root: Path):
    # START_TATARUS serves the full dashboard from tools/live_monitor while the
    # current IMAGINATIO V14 + Cortex studio lives in tools/imaginatio_lab.
    # Expose it under the same origin so both UIs operate on the exact same
    # SyntheticOrganism/RobotMind instance and the same /api/* endpoints.
    imaginatio_lab_root = (web_root.parent / "imaginatio_lab").resolve()

    class MonitorHandler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(web_root), **kwargs)

        def _send_bytes(
            self,
            payload: bytes,
            content_type: str,
            status: HTTPStatus = HTTPStatus.OK,
        ) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            self.wfile.write(payload)

        def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
            path = self.path.split("?", 1)[0]
            if path == "/imaginatio-lab" or path == "/imaginatio-lab/":
                path = "/imaginatio-lab/index.html"
            if path.startswith("/imaginatio-lab/"):
                relative = path[len("/imaginatio-lab/"):].lstrip("/")
                candidate = (imaginatio_lab_root / relative).resolve()
                try:
                    candidate.relative_to(imaginatio_lab_root)
                except ValueError:
                    self.send_error(HTTPStatus.FORBIDDEN)
                    return
                if not candidate.is_file():
                    self.send_error(HTTPStatus.NOT_FOUND)
                    return
                content_type = mimetypes.guess_type(candidate.name)[0] or "application/octet-stream"
                if content_type.startswith("text/") or content_type in {"application/javascript", "application/json"}:
                    content_type += "; charset=utf-8"
                self._send_bytes(candidate.read_bytes(), content_type)
                return
            if path == "/api/geometry":
                self._send_bytes(runtime.snapshot("geometry"), "application/json")
                return
            if path == "/api/frame":
                self._send_bytes(runtime.snapshot("frame"), "application/json")
                return
            if path == "/api/physiology":
                self._send_bytes(runtime.snapshot("physiology"), "application/json")
                return
            if path == "/api/organism":
                self._send_bytes(runtime.snapshot("organism"), "application/json")
                return
            if path == "/api/imaginatio":
                self._send_bytes(runtime.snapshot("imaginatio"), "application/json")
                return
            if path == "/api/imaginatio/trace":
                self._send_bytes(runtime.snapshot("imaginatio_trace"), "application/json")
                return
            if path == "/api/cortex":
                self._send_bytes(runtime.snapshot("cortex"), "application/json; charset=utf-8")
                return
            if path == "/api/imaginatio/gallery":
                payload = json.dumps(
                    runtime.gallery_payload(), ensure_ascii=False, separators=(",", ":")
                ).encode("utf-8")
                self._send_bytes(payload, "application/json; charset=utf-8")
                return
            if path.startswith("/api/imaginatio/gallery/"):
                parts = path.strip("/").split("/")
                try:
                    if len(parts) != 5:
                        raise ValueError("Ungültiger Galeriepfad")
                    payload, content_type = runtime.gallery_file(parts[3], parts[4])
                    self._send_bytes(payload, content_type)
                except (ValueError, OSError) as exc:
                    payload = json.dumps({"error": str(exc)}).encode("utf-8")
                    self._send_bytes(payload, "application/json", HTTPStatus.NOT_FOUND)
                return
            if path == "/api/throughput":
                self._send_bytes(runtime.snapshot("throughput"), "application/json")
                return
            if path == "/api/environment-map":
                self._send_bytes(runtime.snapshot("environment_map"), "application/json")
                return
            if path == "/api/atmospheres":
                payload = json.dumps(ATMOSPHERE_PRESETS, separators=(",", ":")).encode()
                self._send_bytes(payload, "application/json")
                return
            if path == "/api/robot":
                self._send_bytes(runtime.snapshot("robot"), "application/json")
                return
            if path == "/api/status":
                payload = json.dumps(runtime.status(), separators=(",", ":")).encode()
                self._send_bytes(payload, "application/json")
                return
            if path == "/api/timeline":
                payload = json.dumps(runtime.timeline_payload(), separators=(",", ":")).encode()
                self._send_bytes(payload, "application/json")
                return
            if path == "/api/experiment":
                payload = json.dumps(runtime.experiment_payload(), separators=(",", ":")).encode()
                self._send_bytes(payload, "application/json")
                return
            if path == "/api/snapshots":
                payload = json.dumps(runtime.list_snapshots(), separators=(",", ":")).encode()
                self._send_bytes(payload, "application/json")
                return
            if path == "/api/maps":
                payload = json.dumps(
                    runtime.map_library_payload(), separators=(",", ":")
                ).encode()
                self._send_bytes(payload, "application/json")
                return
            if path == "/health":
                self._send_bytes(b'{"ok":true}', "application/json")
                return
            super().do_GET()

        def do_POST(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
            path = self.path.split("?", 1)[0]
            if path not in {
                "/api/control",
                "/api/imaginatio/control",
                "/api/imaginatio/export",
                "/api/imaginatio/gallery/control",
                "/api/cortex/control",
            }:
                self.send_error(HTTPStatus.NOT_FOUND)
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                maximum = 2 * 1024 * 1024 if path == "/api/imaginatio/control" else 4096
                if length <= 0 or length > maximum:
                    raise ValueError("invalid request size")
                command = json.loads(self.rfile.read(length))
                if path == "/api/imaginatio/control":
                    result = runtime.imaginatio_control(command)
                elif path == "/api/imaginatio/export":
                    result = runtime.export_imaginatio(command)
                elif path == "/api/imaginatio/gallery/control":
                    result = runtime.gallery_control(command)
                elif path == "/api/cortex/control":
                    result = runtime.cortex_control(command)
                else:
                    result = runtime.control(command)
                payload = json.dumps(result, separators=(",", ":")).encode()
                self._send_bytes(payload, "application/json")
            except (ValueError, RuntimeError, OSError, json.JSONDecodeError) as exc:
                payload = json.dumps({"error": str(exc)}).encode()
                self._send_bytes(
                    payload,
                    "application/json",
                    HTTPStatus.BAD_REQUEST,
                )

        def log_message(self, format_string: str, *args: Any) -> None:
            if self.path.startswith("/api/") or self.path == "/health":
                return
            super().log_message(format_string, *args)

    return MonitorHandler


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="TATARUS synthetic-organism and robot-training monitor")
    parser.add_argument("--library", required=True, type=Path)
    parser.add_argument("--web-root", type=Path, default=Path(__file__).parent)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--seed", type=int, default=7411)
    parser.add_argument("--interval", type=float, default=0.10)
    parser.add_argument(
        "--tissue-profile", choices=tuple(TISSUE_PROFILES), default="standard"
    )
    parser.add_argument("--neurons", type=int)
    parser.add_argument("--no-browser", action="store_true")
    parser.add_argument(
        "--imaginatio-only",
        action="store_true",
        help="start the shared organism paused without the robot training loop",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_arguments()
    library_path = args.library.resolve()
    web_root = args.web_root.resolve()
    if not library_path.is_file():
        raise FileNotFoundError(f"SDK library not found: {library_path}")
    if not (web_root / "index.html").is_file():
        raise FileNotFoundError(f"monitor frontend not found: {web_root / 'index.html'}")

    runtime = TatarusRuntime(
        library_path,
        args.seed,
        args.interval,
        args.tissue_profile,
        args.neurons,
        args.imaginatio_only,
    )
    runtime.start()
    server = ThreadingHTTPServer(
        (args.host, args.port),
        make_handler(runtime, web_root),
    )
    server.daemon_threads = True
    url = f"http://{args.host}:{args.port}/"
    print(f"TATARUS Live-Monitor: {url}", flush=True)
    print("Beenden mit Strg+C.", flush=True)
    if not args.no_browser:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        pass
    finally:
        server.shutdown()
        server.server_close()
        runtime.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
