# T-027: Hold FX controls

T-024 deliberately exposed a single `Hold FX` toggle. That limitation is now
retired by explicit user request (SRC-004) -- but retired into six controls, not
a particle-editor zoo.

## The control set

A compact `HOLD` section under Click, matching the existing Settings visual
language:

    HOLD
    [x] Hold FX
    [x] Motion Wake

    Intensity         [slider]  100%
    Wake Density      [slider]  100%
    Wake Life         [slider]  0.8 s
    Release Strength  [slider]  100%

`ClickStyle` remains the ONLY style authority -- there is deliberately no second
hold style selector. The activation threshold stays an internal constant and is
NOT exposed, so the short-click / HOLD gesture boundary stays stable and
predictable.

## Config model and hard bounds

    bool  hold_enabled               (existing, schema 8)
    bool  hold_wake_enabled          (new)
    float hold_intensity             0.25 .. 2.00   default 1.00
    float hold_wake_density          0.25 .. 2.00   default 1.00
    float hold_wake_lifetime_ms     150 .. 2500     default 800
    float hold_release_strength      0.50 .. 2.00   default 1.00

These are artistic multipliers, not renderer magic numbers. Every one is
EXACTLY the identity at 1.0, so a default config reproduces the accepted T-024
appearance and behaviour.

`validated()` clamps all four through `clamp_hold_multiplier`. A NaN fails every
comparison and is repaired to its documented default, so a slider cannot reach a
state that produces NaN geometry and no out-of-range value reaches the renderer.

## What each one means

**Wake Density is a SPATIAL control.** It maps to emission SPACING, never to
"particles per frame":

    density 0.25 -> ~32 px spacing
    density 1.00 -> ~12 px spacing
    density 2.00 ->  ~6 px spacing

The mapping is smooth, bounded and monotone. The style's `particle_amount`
still owns the within-emission particle budget; the two are different concepts.

**Wake Life is snapshotted at birth.** Each emission records the configured
lifetime when it is born, so changing Wake Life while old emissions exist never
retroactively stretches or shrinks them. It does not touch release duration or
ordinary click duration.

**Hold Intensity primarily scales the attached aura.** It may modestly affect
wake brightness/size, but it does not multiply particle count.

**Release Strength transforms the existing charge model** --
`final_release_power = charge_power * hold_release_strength` -- leaving particle
count bounded by the existing budgets. No nuclear 4000 px release.

## Schema 8 -> 9

New named schema boundary for Hold Controls / Motion Wake.

Historical configs (`schema <= 8`):

- keep the accepted HOLD behaviour exactly; `hold_enabled` still loads per the
  existing schema-8 migration semantics and historical enum values are never
  reinterpreted;
- **Motion Wake defaults OFF**, because it is materially new visual behaviour;
- all four multipliers reset to baseline.

Fresh schema-9 configs: Hold FX ON, Motion Wake ON, all multipliers at baseline.
If the user enables Motion Wake and saves, schema 9 persists it.

## UI behaviour

- programmatic population stays silent;
- each user action publishes exactly ONE coherent `ClickConfig`;
- Restore Click Defaults restores the Hold settings too;
- Motion Wake OFF disables only the wake-specific rows (Wake Density, Wake
  Life); Intensity and Release Strength still apply, so they stay enabled;
- Hold FX OFF disables the whole Hold group;
- layout stays stable -- no surprise vertical resizing when toggles change.

## Evidence

- clean Release + Debug, `/W4 /WX`: 0 errors, 0 warnings (`build_t026_clean`)
- config tests 34 checks, GUI tests 93 checks -- 0 failures in both configs,
  covering roundtrip, schema-8 migration, fresh schema-9 defaults, one coherent
  publish per action, silent population, defaults restore, the enable/disable
  matrix and min/default/max of every slider
- `artifacts/ProTrail-T026-Test-x64.zip` -- the ONE combined T-026/T-027 visual
  artifact; deployed smoke PASS, zero orphans, zero production state mutation
