# T-023 rework: sparkle amplification (user visual FAIL -> repair)

User visual result on `artifacts\ProTrail-T023-Test-x64`: the sparkle layer
renders but is "very barely noticeable"; explicit instruction was a much
stronger visual presence. No Shards PASS was given. The handoff NON-GOAL
"no retuning Stardust/Twinkle/Glitter/Firefly" was EXPLICITLY overridden by
the user, so the amplification covers all five families, not just Shards.

## Root cause: a double fade on one axis

`sparkle_for_point()` ends with

    out.alpha = alpha_for(config, point.t) * P.alpha_scale * env * pulse

`alpha_for()` ALREADY performs the entire lifetime fade (base_opacity +
fade_start + FadeCurve). The per-mode `env` then faded a SECOND time along
the same axis, because `1 - age01 == clamp01(point.t)`:

| mode     | old env                         | effect at mid-trail (t = 0.25) |
|----------|---------------------------------|--------------------------------|
| Glitter  | `(0.55+0.45h) * t^2`            | x0.06 .. x0.0625 of an already-faded alpha |
| Stardust | `0.85 * sqrt(t)`                | x0.425 |
| Firefly  | `sqrt(t)`                       | x0.50 |
| Twinkle  | `0.22 + 0.78*pulse`             | x0.22 off-peak |

Two fades stacked on one axis is how a decoration becomes invisible while
every unit test still passes: nothing asserted a MINIMUM visible alpha.

## Repair

Every envelope keeps its family's character but modulates around a floor;
the fade itself stays in `alpha_for()` where it belongs.

| mode     | new env                                   |
|----------|-------------------------------------------|
| Twinkle  | `0.45 + 0.55*pulse`                       |
| Glitter  | `(0.72 + 0.28h) * (0.40 + 0.60*t)`        |
| Firefly  | `0.55 + 0.45*sqrt(t)`                     |
| Shards   | `fade_in * (1 - 0.65*smooth)` (floor .35) |
| default  | `0.55 + 0.35*sqrt(t)`                     |

Shards keeps its fade-IN unchanged -- that is what detaches a fragment from
the head and it is the behaviour under user visual test.

## Size

`size_scale` x1.70 UNIFORMLY across all five families, so every documented
inter-family size relation is preserved bit-for-bit (the Twinkle-vs-Stardust
and Twinkle-vs-Glitter ratio regressions pass unchanged):

    Stardust 0.42 -> 0.71   Twinkle 2.35 -> 4.00   Glitter 0.48 -> 0.82
    Firefly  1.55 -> 2.64   Shards  1.55 -> 2.64

`alpha_scale`: Firefly 0.90 -> 1.00, Shards 0.82 -> 1.00. Stardust stays at
0.52 DELIBERATELY -- it is the dim family by construction and
`mode_motion_envelopes_are_visibly_distinct` asserts the Twinkle peak
outshines it; Stardust's visibility comes from size and the new floor.

## Headroom and defaults

    kMaxSparkleSizePx      8    -> 16
    kMaxSparkleEmittedPx   16   -> 48
    kDefaultSparkleAmount  0.45 -> 0.70
    kDefaultSparkleSizePx  3.0  -> 4.5

Widening a clamp range never reinterprets a persisted value (every stored
size stays in range and keeps its meaning), so this needs NO schema bump --
schema stays 7. The Settings sliders already derive their ranges from these
named constants, so the GUI widened with no UI edit.

## Rejected

A highlight LIFT toward white was implemented and then removed: it makes a
yellow trail emit bluish sparkles, breaking the "sparkle color IS the local
trail color" contract and its two regressions
(`color_follows_the_local_trail_color`,
`sparkle_color_accent_modes_follow_the_accent`). Visibility is bought with
size and alpha, never with a color the user did not choose. The plain
channel gain stays because it does real work on a dark user color.

## Evidence

- Incremental Release build: 0 errors, 0 warnings (`build_inc_release.txt`)
- `protrail_sparkle_tests`: 33 passed, 0 failed, 1 skipped (opt-in soak)
- Release CTest `-E protrail_input_dispatch`: 15/15 PASS, 0 failed
- One test updated, not weakened: `sparkle_changes_publish_once_with_fields`
  set the Amount slider to 70, which is now the DEFAULT -- Qt suppressed the
  no-op setValue and the third publication never fired. Changed to 85.
