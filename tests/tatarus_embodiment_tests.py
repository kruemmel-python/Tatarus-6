#!/usr/bin/env python3
"""Determinism and neural-motor control checks for the bundled robot world."""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
import tempfile
import time
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SERVER_PATH = PROJECT_ROOT / "tools" / "live_monitor" / "server.py"
SPEC = importlib.util.spec_from_file_location("tatarus_live_server", SERVER_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot import {SERVER_PATH}")
SERVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SERVER)
RobotWorld = SERVER.RobotWorld
default_map_catalog = SERVER.default_map_catalog
MotorState = SERVER.MotorState
ProductState = SERVER.ProductState
TatarusRuntime = SERVER.TatarusRuntime


def motor_for(action: int) -> MotorState:
    motor = MotorState()
    motor.available = 1
    motor.selected_direction = action
    motor.directional_activity[:] = tuple(1.0 if index == action else 0.02 for index in range(4))
    motor.confidence = 0.8
    motor.attention = 0.7
    return motor


def main(library_path: Path | None = None) -> int:
    first = RobotWorld(7411, "neural_direct")
    second = RobotWorld(7411, "neural_direct")
    assert first.shortest_path == second.shortest_path
    assert len(first.shortest_path) - 1 == 29
    assert first.goal not in first.obstacles
    assert first.start not in first.obstacles

    # Ten distinct map slots are bundled and every one remains solvable.
    catalog = default_map_catalog()
    assert len(catalog) == 10
    assert len({item["id"] for item in catalog}) == 10
    assert len({tuple(tuple(cell) for cell in item["obstacles"]) for item in catalog}) == 10
    navigation_contexts = {
        RobotWorld(7411, "neural_direct", definition).navigation_context_id
        for definition in catalog
    }
    assert len(navigation_contexts) == 10, "map layouts share episodic memory context"
    for definition in catalog:
        mapped = RobotWorld(7411, "neural_direct", definition)
        assert mapped.shortest_path
        assert mapped.start not in mapped.obstacles
        assert mapped.goal not in mapped.obstacles

    product_a = ProductState()
    product_b = ProductState()
    product_a.assembly_id = 1
    product_b.assembly_id = 1
    for _ in range(2400):
        if first._reset_pending:
            first._begin_next_episode()
            second._begin_next_episode()
        route = first._breadth_first_path(first.position, first.goal)
        target = route[1] if len(route) > 1 else first.goal
        delta = (target[0] - first.position[0], target[1] - first.position[1])
        desired_action = first.ACTIONS.index(delta)
        motor_a = motor_for(desired_action)
        motor_b = motor_for(desired_action)
        observation_a = first.advance(motor_a)
        observation_b = second.advance(motor_b)
        assert first.position == second.position
        assert first.last_action == second.last_action
        assert observation_a.reward == 0.0
        assert observation_a.reward == observation_b.reward
        assert math.isfinite(first.last_reward)
        first.observe_neural(product_a, motor_a)
        second.observe_neural(product_b, motor_b)
        if first.successes >= 3:
            break

    assert first.successes >= 1, "direct neural motor commands did not reach the goal"
    assert first.best_steps is not None
    assert first.best_steps >= len(first.shortest_path) - 1
    assert first.visited

    payload = json.loads(first.snapshot())
    assert payload["schema"] == "tatarus-robot-training-v2"
    assert payload["training"]["controller"] == "neural_direct"
    assert payload["neural"]["control_source"] == "neural_direct"
    assert payload["training"]["motor_decisions"] > 0
    assert payload["training"]["policy_updates"] == 0
    assert not hasattr(first, "q_values")
    assert payload["training"]["route_complete"] is True
    assert payload["training"]["successes"] == first.successes
    assert payload["world"]["shortest_steps"] == 29
    assert payload["trail"]
    assert payload["visited"]
    assert payload["learned_route"][0] == [1.0, 0.0, 1.0]
    assert payload["learned_route"][-1] == [18.0, 0.0, 13.0]
    assert payload["safety"]["enabled"] is True

    # A neural command is used without an actor-policy rewrite.
    direct = RobotWorld(177, "neural_direct")
    direct.advance(motor_for(2))
    assert direct.motor_raw_direction == 2
    assert direct.motor_selected_direction == 2
    assert direct.last_action == "south"

    # Potential-based shaping makes every closed movement pair negative even
    # before the mild revisit/backtrack penalties are applied.
    toward = RobotWorld._navigation_reward(
        10, 9, new_cell=False, previous_episode_visits=0, cycle_length=0
    )
    away = RobotWorld._navigation_reward(
        9, 10, new_cell=False, previous_episode_visits=0, cycle_length=0
    )
    assert math.isclose(
        toward + away, 2.0 * RobotWorld.STEP_COST, abs_tol=1e-12
    )
    assert toward + away < 0.0

    loop_map = {
        "id": "map-10", "name": "Loop check", "width": 6, "depth": 6,
        "start": [1, 1], "goal": [4, 4], "obstacles": [],
    }
    loop_world = RobotWorld(177, "neural_direct", loop_map)
    loop_world.advance(motor_for(1))
    loop_world.advance(motor_for(3))
    first_pair_reward = loop_world.episode_reward
    loop_world.advance(motor_for(1))
    loop_world.advance(motor_for(3))
    # Ordinary reversal remains possible. Only the next identical A-B-A-B
    # command is redirected by short-term motor inhibition.
    assert loop_world.episode_backtracks == 2
    assert loop_world.episode_loops == 2
    assert loop_world.episode_revisits == 2
    assert loop_world.episode_cycle_preventions == 1
    assert loop_world.motor_raw_direction == 3
    assert loop_world.motor_selected_direction != 3
    loop_payload = json.loads(loop_world.snapshot())["training"]
    assert loop_payload["episode_cycle_preventions"] == 1
    assert loop_payload["revisit_ratio"] == 0.5

    # The independent safety monitor vetoes a known obstacle cell.
    guarded = RobotWorld(177, "neural_direct")
    guarded.position = (4, 1)
    guarded.advance(motor_for(1))
    assert guarded.position == (4, 1)
    assert guarded.last_action == "safe_hold"
    assert guarded.safety.interventions == 1
    assert guarded.last_safety_reason == "collision_prevented"

    # Full embodied-world state is JSON-safe and resumes without divergence.
    restored = RobotWorld.from_state(json.loads(json.dumps(first.export_state())))
    assert restored.export_state() == first.export_state()
    next_motor = motor_for(1)
    first.advance(next_motor)
    restored.advance(next_motor)
    assert restored.export_state() == first.export_state()

    # Hybrid mode remains deterministic but is explicitly a separate baseline.
    hybrid_a = RobotWorld(901, "hybrid_reward")
    hybrid_b = RobotWorld(901, "hybrid_reward")
    for step in range(80):
        motor = motor_for(step % 4)
        hybrid_a.advance(motor)
        hybrid_b.advance(motor)
        hybrid_a.observe_neural(product_a, motor)
        hybrid_b.observe_neural(product_b, motor)
    assert hybrid_a.export_state() == hybrid_b.export_state()
    assert hybrid_a.policy_updates > 0

    # Switching maps resets only map-local evaluation data, not learned policy state.
    learned_preferences = json.loads(json.dumps(hybrid_a.export_state()["actor_preferences"]))
    learned_steps = hybrid_a.learning_steps
    learned_epsilon = hybrid_a.epsilon
    learned_map_context = hybrid_a.navigation_context_id
    hybrid_a.switch_map(catalog[5])
    assert hybrid_a.map_id == "map-6"
    assert hybrid_a.total_steps == 0
    assert hybrid_a.successes == 0
    assert hybrid_a.position == hybrid_a.start
    assert hybrid_a.learning_steps == learned_steps
    assert hybrid_a.epsilon == learned_epsilon
    assert hybrid_a.export_state()["actor_preferences"] == learned_preferences
    assert hybrid_a.navigation_context_id != learned_map_context

    # Frozen evaluation keeps the policy active but stops all Python-side
    # learning counters and removes epsilon sampling from hybrid selection.
    hybrid_a.learning_enabled = False
    frozen_learning_steps = hybrid_a.learning_steps
    frozen_policy_updates = hybrid_a.policy_updates
    for _ in range(8):
        motor = motor_for(2)
        hybrid_a.advance(motor)
        hybrid_a.observe_neural(product_a, motor)
    assert hybrid_a.learning_steps == frozen_learning_steps
    assert hybrid_a.policy_updates == frozen_policy_updates
    frozen_payload = json.loads(hybrid_a.snapshot())
    assert frozen_payload["training"]["mode"] == "frozen_evaluation"
    # Manual edits are atomic: valid changes apply, disconnected maps are rejected.
    editable = RobotWorld(55, "neural_direct", catalog[9])
    revision = editable.map_revision
    editable.edit_map_cell(2, 2, "obstacle")
    assert (2, 2) in editable.obstacles
    assert editable.map_revision == revision + 1
    editable.edit_map_cell(2, 2, "erase")
    assert (2, 2) not in editable.obstacles
    bottleneck = {
        "id": "map-1", "name": "Bottleneck", "width": 6, "depth": 6,
        "start": [1, 1], "goal": [4, 4],
        "obstacles": [[3, z] for z in range(6) if z != 3],
    }
    guarded_edit = RobotWorld(55, "neural_direct", bottleneck)
    before_edit = guarded_edit.map_definition()
    try:
        guarded_edit.edit_map_cell(3, 3, "obstacle")
        raise AssertionError("an unsolvable map edit was accepted")
    except ValueError:
        pass
    assert guarded_edit.map_definition() == before_edit

    # Classical reference reaches the target and is labeled as non-neural control.
    baseline = RobotWorld(7411, "classical_baseline")
    for _ in range(80):
        baseline.advance(motor_for(0))
        if baseline.successes:
            break
    assert baseline.successes == 1
    assert json.loads(baseline.snapshot())["training"]["controller"] == "classical_baseline"

    # The bundled runtime atomically restores SDK and embodied world state.
    library = library_path or PROJECT_ROOT / "build" / "Release" / "tatarus4_c.dll"
    if library_path is not None and not library.is_file():
        raise FileNotFoundError(f"SDK library not found: {library}")
    if library.is_file():
        runtime = TatarusRuntime(library, 7411, 0.01)
        try:
            assert runtime.status()["neuron_count"] == 384
            scaled = runtime.control({"action": "tissue_size", "profile": "compact"})
            assert scaled["neuron_count"] == 96
            assert scaled["tissue_profile"] == "compact"
            geometry = json.loads(runtime.snapshot("geometry"))
            assert geometry["scale"]["neurons"] == 96
            assert geometry["scale"]["regions"] == 6
            with tempfile.TemporaryDirectory(prefix="tatarus-embodiment-test-") as directory:
                runtime.snapshot_root = Path(directory)
                expected_world = runtime.robot.export_state()
                saved = runtime.save_embodiment("test state")
                runtime.robot.advance(motor_for(2))
                assert runtime.robot.export_state() != expected_world
                runtime.load_embodiment(saved["snapshot_id"])
                assert runtime.robot.export_state() == expected_world
                assert runtime.paused is True

                experiment = runtime.start_ablation("hypoxia", 100, "neural_direct")
                assert experiment["status"] == "running"
                deadline = time.monotonic() + 30.0
                while runtime.experiment_payload()["status"] == "running" and time.monotonic() < deadline:
                    time.sleep(0.02)
                completed = runtime.experiment_payload()
                assert completed["status"] == "complete", completed
                assert completed["result"]["control"]
                assert completed["result"]["treatment"]
                assert "oxygen" in completed["result"]["delta"]
        finally:
            runtime.close()

    print(
        "robot world checks passed: "
        f"{first.successes} successes, best={first.best_steps}, "
        f"explored={len(first.visited)}"
    )
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", type=Path)
    arguments = parser.parse_args()
    raise SystemExit(main(arguments.library))
