# TATARUS Cortex UI Integration Verification

Date: 2026-09-08

## Build

The modified `tatarus_c` C ABI builds successfully with `TATARUS_BUILD_CORTEX=ON`
and links the optional `tatarus_cortex` library. The new native UI bridge test passes.

## Native bridge

`tatarus_cortex_ui_bridge_tests` verifies without LM Studio that:

- `config/cortex_hybrid.json` can be loaded through the C ABI;
- the status JSON reports `configured=true` and `mode=hybrid`;
- an Executive goal can be created and observed through the status JSON;
- Working Memory can be written and observed;
- the Cortex can be disabled again.

## Existing Cortex regressions

The following test executables pass after the UI integration:

- `tatarus_cortex_tests` — Phase 0–11 checks;
- `tatarus_cortex_phase12_15_tests` — Phase 12–15 checks;
- `tatarus_cortex_ui_bridge_tests` — C ABI / UI bridge.

## HTTP integration

The standalone IMAGINATIO server was launched with the rebuilt shared library on a
local test port. Verified endpoints:

```text
GET  /health
GET  /api/cortex
POST /api/cortex/control {action:add_goal}
POST /api/cortex/control {action:probe}
POST /api/cortex/control {action:analyze}
```

A local fake LM-Studio-compatible `/v1/models` + `/v1/chat/completions` server was
used to validate the complete transport path. The probe resolved the fake model,
an explicit analysis produced one validated response and the CortexArbiter returned
`ACCEPT` with `stale=0`.

## Poll-fingerprint correction

The C-ABI bridge records whether the pending request included IMAGINATIO state.
Polling now reconstructs exactly the same state shape; a pure analysis request is
therefore no longer incorrectly marked stale merely because the UI also has an
IMAGINATIO instance.

## Frontend

`tools/imaginatio_lab/index.html` now contains the fourth Cortex/Executive panel and
`app.js` consumes `/api/cortex`, renders status/goal/plan/grounding/metacognition,
and can submit Cortex actions. JavaScript syntax was checked with `node --check` and
the Python server with `python -m py_compile`.
