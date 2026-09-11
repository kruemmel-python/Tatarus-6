# Phase 12–15 Verification

## New phase-specific tests

`tests/tatarus_cortex_phase12_15_tests.cpp` verifies:

1. **Phase 12 executive memory** — goal and working memory reach the Cortex without changing
   `RobotMind`; executive state round-trips through its independent snapshot.
2. **Phase 13 grounded plan** — an accepted two-step plan is adopted, and the active step
   advances only after a real `ActionOutcome` has been processed by `RobotMind`.
3. **Phase 14 metacognition** — repeated Cortex failures produce `DEGRADED`; automatic
   consultation is suppressed while explicit consultation remains possible.
4. **Phase 15 record/replay + autonomous executive** — record mode writes a tape, replay mode
   reproduces the accepted decision with no LM client, and `autonomousCycle()` creates an
   executive plan without mutating `RobotMind`.

The legacy 16 Cortex phase 0–11 regression blocks are also executed unchanged.

## Existing baseline caveat

The previously documented debug-only oxygen uptake failure in
`tatarus_causal_validation_tests` predates Phase 12–15 and is not modified by these modules.
Release builds using C `assert()` must not be treated as evidence for that assertion when
`NDEBUG` is active.

## Final Phase 12–15 verification

Release build verification after the final implementation:

- legacy Cortex phase 0–11 checks: **16/16 PASS**;
- Phase 12–15 Cortex checks: **4/4 PASS**;
- regular CTest suite excluding the pre-existing debug causal O2 assertion: **12/12 PASS**;
- `TATARUS_BUILD_CORTEX=OFF`: `tatarus_sdk`, `tatarus_minimal_robot`, and `tatarus_c` build successfully;
- the Cortex-off `tatarus_minimal_robot` executable runs successfully.

Record/replay was verified with a recorded Cortex response and a separate replay run without
an LM client. Strict replay matches task, goal, capabilities, state fingerprint, and executive
plan position before returning the recorded response.
