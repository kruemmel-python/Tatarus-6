# TATARUS – Ocular + Vestibular Sensor Fusion

This revision integrates a stateful optical eye front-end and an inner-ear vestibular pathway into the runtime sensor loop.

## Runtime path

`IMU -> semicircular canals / otolith estimate -> vestibular nerve afferents -> persistent nervous system`

`IMU -> vestibulo-ocular reflex (VOR) -> ocular gaze stabilization -> retina -> optic nerve -> V1/V2 -> persistent nervous system`

## Ocular system

- pupillary light reflex with asymmetric constriction/dilation dynamics
- retinal light/dark adaptation gain
- contrast-driven saliency centroid
- bounded saccadic gaze controller
- horizontal/vertical gaze state
- bilinear retinal resampling
- vestibulo-ocular reflex input

Files:
- `include/tatarus/ocular_system.hpp`
- `modules/tatarus_neurobiology/tatarus_ocular_system.cpp`

## Vestibular system

- three semicircular-canal population channels
- gravity estimate from accelerometer data
- gravity-separated linear acceleration
- otolith/tilt population coding
- VOR eye-velocity output
- 18 signed vestibular nerve afferents

Files:
- `include/tatarus/vestibular_system.hpp`
- `modules/tatarus_neurobiology/tatarus_vestibular_system.cpp`

## Neural integration

`SensorFrame` now carries a dedicated `vestibularEvents` vector. IMU data is no longer encoded as generic touch input. Vestibular afferents participate in:

- sensory drive
- stimulus-presence detection
- assembly signatures
- finite-value validation
- plasticity write-signal detection

## RobotMind fusion

`RobotMind::observe()` derives `dt` from observation timestamps, advances the vestibular model, routes VOR output into the ocular system, and sends the resulting retinal RGB frame into the existing biological visual pathway.

The vestibular motion state and ocular state are also fed into contextual neural input, providing multisensory coupling rather than isolated sensor preprocessing.

## IMAGINATIO

Static recalled/generated canvases now traverse the ocular front-end before the retina. Ocular transient state is reset for each symbolic/static recall so identical imagined images keep deterministic afferent signatures and can consolidate into the same assembly.

## Verification

- `tatarus_sensory_organs_tests`: PASS
- `tatarus_neurobiology_tests`: PASS
- CMake build with `TATARUS_BUILD_SHARED=ON`, `TATARUS_BUILD_CORTEX=OFF`: PASS
- The pre-existing C API `jsonEscape` scope issue in Cortex-off builds is corrected.

The full IMAGINATIO regression executable exceeded the execution window in the validation environment; it was therefore not reported as a pass/fail result here.
