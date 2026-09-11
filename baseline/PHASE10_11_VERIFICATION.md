# Phase 10–11 Verification

This record documents the verification performed for bounded top-down cognition
(Phase 10) and the sleep/dream Cortex (Phase 11).

## Phase 10 safety properties

Verified by `tatarus_cortex_tests`:

- neutral `CognitiveCue{}` follows the legacy `RobotMind::observe()` path and
  produces the same observable state and motor output;
- public `CognitiveCue` exposes no reward, direct motor, synapse, neuron, or
  physiology mutation field;
- the Cortex translation path sets legacy internal motor intent and reward to
  zero and uses `CognitiveCommand::contextOnly=true`;
- context-only mode suppresses the historical interoception energy boost,
  reward injection, and direct post-step motor override;
- recall strength, goal-bias strength, semantic channel count, and semantic
  magnitude are bounded by hard SDK limits;
- a prepared top-down cue is single-use and stale-state checked;
- over-limit cues are rejected by `RobotMind`;
- DREAM responses never create a wake top-down cue.

## Phase 11 sleep/dream properties

Verified by `tatarus_cortex_tests` with accelerated physiological time constants
(the production defaults remain unchanged):

- the real TATARUS sleep model reaches NREM and then REM without a synthetic
  `setSleepPhase()` override;
- NREM produces zero model calls;
- ordinary PLAN/HYBRID wake Cortex requests are blocked while asleep;
- REM emits `DREAM` / `SLEEP_REM` provenance only;
- DREAM capabilities are restricted to IMAGINATIO-related capabilities;
- real-world movement/scan strategies are rejected for DREAM requests;
- dream memory exposure is bounded by `maximum_dream_symbols`;
- REM dream responses route to IMAGINATIO only;
- REM never produces a wake top-down cue.

## Functional verification

Release build:

```text
$ ./build_phase1011/tatarus_cortex_tests
16/16 Cortex checks PASS

$ ./build_phase1011/tatarus_cortex_topdown_sleep_demo
Phase 10 cue ready: YES
Phase 10 applied: YES
NREM additional model calls: 0
REM dream requests: 1
Dream routed to IMAGINATIO: YES
```

All Release tests except the pre-existing causal-validation target pass:

```text
$ ctest --test-dir build_phase1011 \
    -E '^tatarus_causal_validation_tests$' --output-on-failure -j1
11/11 PASS
```

This includes the complete `tatarus_sdk_tests` integration test.

## Debug causal-baseline note

`tatarus_causal_validation_tests` must not be counted as a reliable Release pass,
because it uses C `assert()` and Release builds define `NDEBUG`.

With assertions enabled, the current Phase-10/11 Debug build still reaches the
same pre-existing failure recorded before these phases:

```text
[TEST 6] Robot Mechanical Workload & Metabolic Coupling...
Idle (5W):   O2 Uptake=1311.92 mL/min
Heavy (150W): O2 Uptake=0 mL/min
Assertion: heavyTel.lung.o2UptakeRateMlPerMin > idleTel.lung.o2UptakeRateMlPerMin
```

The unchanged Phase-8/9 build was re-run and produces the same values and the
same assertion failure at the same test line. The issue therefore predates
Phase 10/11 and is not attributed to this implementation.

The Phase-10/11 Debug Cortex suite itself passes all 16 checks.

## Cortex-off compatibility

A separate build was configured with:

```text
-DTATARUS_BUILD_CORTEX=OFF
```

The following targets compile successfully:

```text
tatarus_sdk
tatarus_minimal_robot
tatarus_c
```

`tatarus_minimal_robot` also executes normally. The Cortex therefore remains an
optional layer rather than a required RobotMind runtime dependency.

## Existing compiler warnings

The build still reports the previously existing warnings in:

- `modules/tatarus_organism/tatarus_heart.cpp` (`calculateNernstNa` unused);
- `src/tatarus_imaginatio.cpp` (missing aggregate field initializers).

No new Phase-10/11 compiler warning was introduced in the new Cortex/top-down
source files during this verification.
