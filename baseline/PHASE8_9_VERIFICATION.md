# TATARUS Hybrid Cortex — Phase 8/9 Verification

Date: 2026-09-07

## Implemented

- `CounterfactualSandbox` with exact `SyntheticOrganism` snapshot branches
- branch rollout through normal TATARUS organism/explorer code paths
- scanner, atmosphere and physical-load support in counterfactual frames
- exact before/after real-snapshot isolation audit
- branch-only utility telemetry with no real reward write-back
- counterfactual provenance (`SIMULATION` / `IMAGINATION`)
- `CortexTeacherTransfer`
- real ActionOutcome provenance and competence tracking
- bounded autonomy-probe gate
- persistent teacher/competence state
- orchestrator teaching API
- `lastRequest` / `lastSuppressedRequest` provenance
- optional Cortex remains build-time removable

## Cortex-specific test output

```text
PASS cortex_contract
PASS cortex_config_load
PASS cortex_disabled
PASS cortex_explicit_observe_only
PASS cortex_persistent_state_neutrality
PASS cortex_arbiter_capability_and_physiology
PASS cortex_rover_adapter_authority
PASS cortex_advisor_rover_integration
PASS cortex_imaginatio_adapter_grounding
PASS cortex_imagination_orchestrator_neutrality
PASS cortex_unified_hybrid_mode
PASS cortex_counterfactual_sandbox_isolation
PASS cortex_teacher_transfer_neutrality_and_competence
PASS cortex_orchestrator_teacher_api
```

## Safety invariants

```text
counterfactual branch != real experience
sandbox utility != reward
teacher bookkeeping != TATARUS learning
LLM proposal != motor command
LLM proposal != reward
```

A full project regression must be run after this file is generated; the final
packaged verification record should contain the resulting ctest count.

## Final verification in this environment

### Build

The complete Phase-8/9 source tree configured and compiled successfully with:

```text
TATARUS_BUILD_TESTS=ON
TATARUS_BUILD_EXAMPLES=ON
TATARUS_BUILD_SHARED=ON
TATARUS_BUILD_CORTEX=ON
```

This includes the new `tatarus_cortex_counterfactual_teacher_demo` target.

### Cortex suite

The final Cortex executable completed successfully with all 14 checks listed above.

### Offline Phase-8/9 demo

```text
Sandbox isolation: PASS
route-a utility=0.364259 distress=0.227463
route-b utility=-0.0431498 distress=0.404501
Teacher episodes completed: 1
```

### Cortex disabled

A separate configuration with `TATARUS_BUILD_CORTEX=OFF` successfully built:

```text
tatarus_sdk
tatarus_minimal_robot
tatarus_c
```

The Hybrid Cortex therefore remains optional at build time.

### Existing baseline test issue (not introduced by Phase 8/9)

A full `ctest` run cannot honestly be reported as 12/12 in this Linux environment.
The existing test `tatarus_causal_validation_tests` fails at the pre-existing
`Robot Mechanical Workload & Metabolic Coupling` assertion:

```text
Idle O2 Uptake  = 1311.92 mL/min
Heavy O2 Uptake = 0 mL/min
assert(heavy > idle) failed
```

The unmodified Phase-6/7 source package was independently rebuilt and produces
**the same failure with the same values**. This establishes that the failure is
present in the Phase-6/7 baseline and is not caused by Phase 8/9.

The existing `tatarus_sdk_tests` executable also exceeded a 180-second local
verification budget in this environment. No Phase-8/9 source is part of that
legacy SDK test path.

Existing compiler warnings remain in `tatarus_heart.cpp` and
`tatarus_imaginatio.cpp`; the new Phase-8/9 Cortex sources compile without new
warnings under the tested GCC C++20 build.
