# TATARUS Cortex Phase 4–5 Verification

## Build

Configuration used:

```text
TATARUS_BUILD_TESTS=ON
TATARUS_BUILD_EXAMPLES=ON
TATARUS_BUILD_SHARED=ON
TATARUS_BUILD_CORTEX=ON
CMAKE_BUILD_TYPE=Release
```

All targets compiled successfully on the verification host.

Existing warnings remain confined to pre-existing code in `tatarus_heart.cpp` and `tatarus_imaginatio.cpp`; no new Cortex warning was observed in the Phase-4/5 target build.

## Regression

```text
12/12 tests passed
0 tests failed
```

Validated suites:

1. tatarus_sdk_tests
2. tatarus_imaginatio_tests
3. tatarus_neurobiology_tests
4. tatarus_physiology_tests
5. tatarus_circulation_tests
6. tatarus_heart_tests
7. tatarus_kidney_tests
8. tatarus_causal_validation_tests
9. tatarus_cortex_tests
10. tatarus_organism_tests
11. tatarus_cartography_c_tests
12. tatarus_embodiment_tests

## Cortex-off compatibility

A separate build with:

```text
TATARUS_BUILD_CORTEX=OFF
```

compiled and `tatarus_sdk_tests` returned PASS.

## Phase-4/5 invariants tested

- unavailable capability cannot be accepted
- critical organism distress blocks movement-like LM proposals
- valid information request remains non-actuating
- rover direction is copied only from TATARUS neural motor output
- Advisor mode does not mutate RobotMind merely by consulting the LLM/arbiter
- stale source fingerprints are not eligible for actuation
- existing Phase-0–3 ObserveOnly persistent-state neutrality remains PASS
