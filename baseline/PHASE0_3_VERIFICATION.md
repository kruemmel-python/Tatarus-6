# TATARUS Cortex Phases 0–3 Verification

## Implemented

- Phase 0: baseline archive/core hashes and pre-integration regression record.
- Phase 1: native loopback LM Studio HTTP transport and `tatarus_cortex_probe`.
- Phase 2: strict JSON parser, Cortex request/response contract, config loader and validation limits.
- Phase 3: asynchronous `CortexOrchestrator` in Observe-Only mode.

## Final validation

Full CTest suite after implementation:

```text
12/12 tests passed
0 tests failed
```

The suite includes the new `tatarus_cortex_tests` plus every pre-existing CTest target in the supplied tree.

The Cortex-specific regression verifies:

- malformed model JSON is rejected;
- request/fingerprint mismatches are rejected;
- unknown strategy enums are rejected;
- out-of-range normalized values are rejected;
- unsafe Phase-0–3 config flags are rejected;
- disabled Cortex performs no model calls;
- explicit Observe-Only calls do not mutate `RobotMind`;
- asynchronous Observe-Only operation preserves the same TATARUS `core_state_hash` and `biological_state_hash` as an identical no-Cortex run;
- persisted `sdk_state.bin` and public `stateJson()` remain identical.

## Optionality check

A separate CMake configuration with:

```text
-DTATARUS_BUILD_CORTEX=OFF
```

successfully built and passed `tatarus_sdk_tests`, confirming that the existing SDK remains buildable without the Cortex target.

## Offline probe behavior

With no LM Studio server running, `tatarus_cortex_probe` exits cleanly with a connection diagnostic. No TATARUS process is required to depend on LM Studio availability.

## Known pre-existing warnings

The warnings recorded in `PHASE0_BASELINE.md` remain pre-existing and are unrelated to the new Cortex module.
