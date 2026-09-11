# START_TATARUS full UI integration verification

Date: 2026-09-08

## Problem reproduced

The full monitor (`START_TATARUS.bat`, port 8765) still exposed the legacy 32x32
dashboard drawlab while the current IMAGINATIO v7/Cortex UI lived only in the
standalone lab. The old visible drawlab was not compatible with the current
512x512 sRGB/base64 canvas state and the full Windows starter did not explicitly
force `TATARUS_BUILD_CORTEX=ON`.

## Fix

- `start_live_monitor.ps1` now configures `TATARUS_BUILD_CORTEX=ON` explicitly.
- `server.py` serves `tools/imaginatio_lab/` at `/imaginatio-lab/` under the same
  origin and same runtime instance.
- `tools/live_monitor/index.html` embeds the current IMAGINATIO v7 + Cortex studio
  in the full dashboard.
- the legacy 32x32 dashboard drawlab remains hidden only for compatibility with
  old inline DOM code and is no longer polled.
- the embedded studio reports its content height to the parent dashboard.

## Native/server verification

Built the shared C ABI with Cortex enabled and started the full live monitor
against the resulting library.

Observed:

```text
/api/status:
  organism_active=true
  imaginatio_active=true
  cortex_available=true
  cortex_configured=true

/api/imaginatio:
  schema=tatarus-imaginatio-v7

/api/cortex:
  schema=tatarus-cortex-ui-v1
  mode=hybrid
```

The integrated static route returned both:

```text
/imaginatio-lab/       HTTP 200
/imaginatio-lab/app.js HTTP 200
```

## Real IMAGINATIO operation through the full server

A 512x512 RGB test house was submitted through `/api/imaginatio/control` using
`rgb8_base64` and learned successfully:

```text
learn=true
visual_engrams=1
category_engrams=1
trace_events=768
```

A subsequent `draw_free` recall of the learned concept completed successfully:

```text
actions=766
marked_pixels=13212
rgb8_base64 canvas present
```

This verifies that the drawing path used by the integrated v7 UI is operational
through the START_TATARUS backend, not merely rendered in HTML.

## Cortex/Executive bridge through the full server

Using `/api/cortex/control` on the same runtime:

```text
add_goal -> active goal created
remember -> working-memory item persisted in Cortex executive state
```

The embedded HTML contains the `Hybrid Cortex · Executive` panel and the full
monitor root contains the `/imaginatio-lab/index.html?embedded=1` frame.

## Syntax checks

```text
python3 -m py_compile tools/live_monitor/server.py  PASS
node --check tools/imaginatio_lab/app.js            PASS
node --check extracted live-monitor module JS       PASS
```
