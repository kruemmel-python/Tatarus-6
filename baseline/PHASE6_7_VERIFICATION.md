# TATARUS Cortex Phase 6–7 Verification

Date: 2026-09-07

## Build

Configured with:

```text
TATARUS_BUILD_CORTEX=ON
TATARUS_BUILD_TESTS=ON
TATARUS_BUILD_EXAMPLES=ON
TATARUS_BUILD_SHARED=ON
CMAKE_BUILD_TYPE=Release
```

The complete build succeeded, including:

- `tatarus_cortex`
- `tatarus_cortex_tests`
- `tatarus_cortex_rover_demo`
- `tatarus_cortex_hybrid_demo`

Existing warnings remain in pre-existing heart/IMAGINATIO implementation code; no new Cortex warning was observed in the Phase 6/7 build.

## Regression suite

```text
12/12 tests passed
0 failed
```

Covered subsystems include SDK, IMAGINATIO, neurobiology, physiology, circulation, heart, kidney, causal validation, Cortex, organism, cartography C ABI and embodiment Python integration.

## Cortex-off regression

A separate build with:

```text
TATARUS_BUILD_CORTEX=OFF
```

successfully built and ran `tatarus_sdk_tests`:

```text
TATARUS SDK integration tests: PASS
```

## Phase 6 assertions

- Cortex cannot supply pixels or paint motor actions.
- Adapter resolves symbols against persistent TATARUS symbol memory.
- Unknown model symbols are rejected.
- Orchestrator does not mutate `RobotMind` before explicit adapter execution.
- Arbiter requires an available IMAGINATIO state for imagination strategies.

## Phase 7 assertions

- `CortexMode::Hybrid` is constructible and operational.
- One orchestrator/model-client instance can service Rover `PLAN` and combined `HYBRID_PLAN` requests.
- Combined requests contain both spatial and imagination state.
- Imagination state participates in the request fingerprint.
- Rover and imagination execution remain separated into dedicated bounded adapters.
