# T-026: Hold Motion Wake / world-space style emissions

User report (SRC-004): the HOLD system works, but its continuous style content
stays attached to the cursor. Moving the mouse drags the whole visual field
along, which reads like an animated GIF pinned to the pointer.

The requested model is the opposite: **the cursor is an emitter.** The live aura
may keep following the pointer, but movement must continuously shed
style-specific effects into WORLD SPACE, and those emissions must stay where
they were born.

## Three layers

| Layer | Anchor | Lifetime owner |
|-------|--------|----------------|
| A. Attached HOLD aura | current cursor | the ActiveHold |
| B. Detached motion wake | immutable birth anchor | the emission itself |
| C. Release payoff | release position | the release |

The three cooperate visually but share no lifecycle state. Layer B is the new
one, and it is what this ticket adds.

## The emission

`HoldWakeEmission` is a **pure record**, not a particle:

    world_x, world_y             immutable birth anchor
    birth_timestamp_ns
    seed                         deterministic
    style, color, element_tint   snapshot at birth
    tangent_x, tangent_y         movement direction at birth
    motion_speed, motion_energy  clamp(speed / kWakeReferenceSpeedPxPerSec)
    charge                       charge snapshot at birth
    particle_amount              within-emission budget snapshot
    wake_intensity, radius_px
    lifetime_ms                  hold_wake_lifetime_ms at birth

Nothing in it evolves. There is no per-particle state, no per-frame
integration, no per-frame RNG and no heap allocation per rendered particle.
Rendering is `geometry = f(emission, elapsed)` -- the same record plus the same
elapsed time produces byte-identical geometry at any frame cadence, which is
exactly what the frame-cadence regressions assert.

Because every visual property is snapshotted at birth, changing ClickStyle,
color or any Hold setting affects **new** emissions only. Old ones finish
according to the state they were born with.

## Distance-based shedding, not event- or frame-based

A mouse event is not a visual unit, so emissions are not spawned per event and
not spawned per frame. Each active hold carries input-side bookkeeping only:

    last point, last timestamp, residual distance, deterministic wake_ordinal

For each `previous_position -> new_position` segment the effect walks the
segment at a configurable spacing and interpolates BOTH the anchor and the birth
timestamp along it. A 60 px movement at 12 px spacing yields ~5 anchors even
when Windows delivered that movement as one sample, and the same geometric path
delivered as many small collinear samples yields effectively the same spatial
pattern. Anchors are never all dumped at the newest cursor position, and a fast
sweep never produces a wake whose contents are artificially all the same age.

Bounds:

    kMaxWakeEmissions        = 192   global, birth-ordered
    kMaxWakeBirthsPerMovement = 14   per movement sample
    kMaxWakeMarksPerEmission  = 10   geometry per emission

A cursor teleport that would need more than 14 crossings is sampled
deterministically across the budget instead. When the global cap is reached,
**expired emissions prune first**; if that is not enough the OLDEST emission is
discarded. Overflow never touches active hold state.

`wake_spacing_px(density)` is smooth, bounded and monotone: density is a
SPATIAL spacing control, never "particles per frame". The style's own
`particle_amount` remains the within-emission particle budget.

## Per-style wake grammar

Eleven styles, eleven recognisable wakes -- not one generic dot trail in
eleven colours:

- **Ring** -- expanding ring stamps; slow movement reads as a sequence of
  echoes, fast movement as separated pressure marks.
- **DoubleRing** -- paired concentric echo stamps expanding at different rates.
- **Ripple** -- world-space ripple sources, each emitting staggered small waves
  over its lifetime; the cursor looks like it disturbed an invisible surface.
- **Burst** -- reduced micro-bursts: a few particles, deterministic angular
  scatter, mild backward bias from the tangent, short lifetime. Not the full
  click explosion replayed at every anchor.
- **SparkBurst** -- aggressive crackling spark shedding: stronger angular
  jitter, wider speed variation, per-particle lifetime variation, motion bias,
  occasional hotter spark.
- **SoftFlash** -- small translucent world-space glow discs; an illuminated
  afterimage, not one disc escorting the cursor.
- **DotRing** -- a chain of bright detached beads plus the occasional ring echo.
- **Air** -- tangential motes and small curling eddies; motes initially lag
  behind the motion, then curve sideways, then keep vortexing in place.
- **Fire** -- embers shed continuously, keep their anchor, rise, wobble and
  cool with age; fast movement throws them slightly backward before buoyancy
  wins.
- **Water** -- droplets thrown along bounded arcs plus a local mini-ripple left
  at the emission point.
- **Earth** -- heavy dust puffs, chunky debris that lags behind the movement,
  deterministic tremble and downward settling. Debris is left behind; nothing
  orbits the cursor.

A stationary hold produces no persistent wake at all -- the attached aura
already supplies stationary life. Wake is a consequence of movement.

## Lifecycle

- Before the Candidate -> ActiveHold threshold: no wake. A candidate never
  emits.
- On **Up**: stop creating new wake, born wake keeps fading, release payoff
  spawns normally. Dying debris after release is the desired outcome.
- On **lost-Up**: stop new births, no release payoff, born wake finishes
  naturally.
- **Master OFF / Click FX OFF**: clear active holds AND all wake.
- **Hold FX OFF**: cancel Candidate / ActiveHold and clear its remaining wake.
- **Motion Wake OFF**: clear alive wake so the setting response is immediate.
- The active-only scheduler stays awake while wake is alive and returns to idle
  once everything expires.

## Evidence

- clean Release + Debug, `/W4 /WX`: 0 errors, 0 warnings (`build_t026_clean`)
- `protrail_hold_wake_tests`: 44609 checks, 0 failures (both configs)
- `protrail_click_tests` 1883, config 34, gui 93, scheduler 12,
  trail lifecycle 6 checks -- 0 failures in both configs
- `artifacts/ProTrail-T026-Test-x64.zip` -- the ONE combined T-026/T-027 visual
  artifact; deployed smoke PASS, zero orphans, zero production config/log
  mutation, no forced kill

## Non-goals kept

No shaders, no physics engine, no per-particle heap objects, no per-frame RNG,
no unbounded historical trail, no second style selector, no renderer
architecture change.
