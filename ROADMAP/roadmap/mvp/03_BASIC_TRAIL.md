# MVP 03 - Basic Cursor Trail

Status: CLOSED (T-008, closed 2026-09-10)

## Goal

Render the first real ProTrail effect: a clean, smooth cursor trail.

## Visual target

The initial trail should resemble the supplied references:

- thin cyan line;
- smooth path;
- rounded visual character;
- short fading history;
- no particles;
- no triangles;
- no glow storm;
- no physics lag.

## Scope

Implement:

- `TrailEffect`;
- time-bounded cursor history consumption;
- smooth path generation;
- alpha decay by sample age;
- configurable thickness;
- configurable lifetime;
- configurable opacity;
- configurable smoothing;
- Direct2D antialiased rendering.

## Smoothing

Use a stable interpolation method such as:

- Catmull-Rom converted to Bezier; or
- carefully controlled quadratic/cubic interpolation.

Avoid excessive overshoot.

The visual trail may be smoothed, but the real Windows cursor must never be delayed.

## Defaults

Reasonable starting defaults:

- color: cyan;
- thickness: 3 px;
- lifetime: approximately 350 ms;
- smoothing: moderate;
- opacity: high at head, fading to zero at tail.

These are tuning defaults, not hard requirements.

## Memory/performance constraints

- no unbounded point growth;
- no permanent geometry accumulation;
- avoid per-frame heap churn where practical;
- discard stale samples promptly.

## Acceptance criteria

- trail follows cursor globally;
- trail fades out fully after movement stops;
- slow and fast movement both look continuous;
- no stale geometry remains;
- behavior is time-consistent across different frame rates;
- CPU/GPU use remains low.

## Non-goals

No click bubble yet.
No particles.
No gradients.
No profiles.

## Close evidence

Record:

- smoothing approach;
- history lifetime;
- max point count;
- active performance observations.

Closed with the following evidence (T-008 + T-008A):

- Smoothing approach: centripetal Catmull-Rom converted to cubic Bezier
  control points, evaluated with 12 subdivisions per span (overshoot-
  resistant; alpha = 0.5 parameterization). T-008A repair: Bezier control
  points are blended linearly between the straight-line chord controls and
  the full centripetal controls by the `smoothing` value
  (`effective = linear + s * (catmull - linear)`), so 0.0 -> raw polyline,
  1.0 -> full centripetal curve, intermediate values continuous in between.
  Regression tests prove 0 / 0.25 / 0.5 / 1.0 produce distinct geometry on
  a non-collinear path and all stay bounded around a sharp corner with no
  NaN/Inf (tests/test_trail_effect.cpp, `smoothing ladder` + `smoothing
  corner boundedness` blocks).
- History lifetime: 350 ms default (TrailConfig::lifetime_ms), consumed
  as a time window in TrailEffect::build_geometry; tail boundary
  interpolated at exactly now-lifetime.
- Max point count: CursorHistory bounded at 512 samples; segment count
  bounded by TrailEffect::max_segments_for().
- USER VISUAL VERIFICATION: PASS (2026-09-10, manual in-app check):
  trail visible, follows cursor, fades as specified. Approved by user.
- Performance observations: no numbers were measured beyond the automated
  contracts (active-only render scheduler: zero idle timer wakeups after
  the final clear, verified via PROTRAIL_RENDER_DIAG=1 burst lines;
  per-frame segment count bounded by max_segments_for()).
