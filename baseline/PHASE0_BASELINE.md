# TATARUS Cortex Phase 0 Baseline

Created from the user-supplied Tatarus-5.zip before Cortex build integration.

Source archive SHA-256: `7fefe95e665a83791b6213984e8032951e1ed9711c7286efa9ef20cc7cf38c28`

## Frozen regression checks

- tatarus_sdk_tests: PASS
- tatarus_imaginatio_tests: PASS
- tatarus_neurobiology_tests: PASS
- 3/3 selected core tests passed on the Linux validation build.

## Existing warnings observed before Cortex integration

- tatarus_imaginatio.cpp: missing-field-initializers warnings in existing code.
- tatarus_heart.cpp: unused calculateNernstNa() warning in existing code.

## Core source hashes

- `CMakeLists.txt`: `b68f65de25b2a072729891606f92a28e8350ebd2a85524b8c5e746670468f987`
- `src/tatarus_mind.cpp`: `acfad19d79e674a4ed75a7c2a0d1f7dc643b1079e3fb69284a82bf1123c1d2bd`
- `src/tatarus_imaginatio.cpp`: `7d841ba0a99669477a3273590b9d955c3a8ae5b859665c66b40178fd3aaf7f6e`
- `modules/tatarus_neurobiology/tatarus_cognition.cpp`: `ca302c64059a2227502d4cb5a355fb656d388d111909f4aa31abd69cb37b1d56`
- `modules/tatarus_neurobiology/tatarus_neural_network.cpp`: `fdaa6c51a7998cff987c42d0e96d35f48d0fce86e6b46414680855791acaa65b`
- `modules/tatarus_organism/tatarus_organism.cpp`: `7a6042644c68f76e211df138bbf891a73cc2f990855815a48df1c3f0dce16370`
- `include/tatarus/robot_mind.hpp`: `392275e91da3c250825354da8d49d2fb70c72319772b2b51e3257800c41507ee`
- `include/tatarus/imaginatio.hpp`: `f7ee1a2439620dd0461a1de421df5a1333ed305450135bc2d0f9b3b4dae5e47f`

These files define the Phase-0 behavioral baseline. Phases 1-3 add a separate cortex library and do not alter the neural stepping path.
