# T-025: per-click randomization for Burst / Spark Burst / elemental

User report: "Spark burst kind of looks hardcoded, needs randomness, burst
and so on."

They were right, and it was worse than reported.

## The defect

`click_bubble_effect.cpp` placed every burst particle at exactly
`2*pi*i/n`, and the only variation -- `spark_scale(i)` -- was a function of
the particle INDEX. Nothing about the click itself entered the math, so:

- every Spark Burst click ever rendered produced the identical rosette at
  the identical angles with the identical per-particle sizes;
- plain Burst had no variation at all;
- the four elemental families (Air/Fire/Water/Earth) had exactly the same
  bug: their jitter came from `hash01(index * k)`, so each element replayed
  one fixed arrangement on every click;
- Water was the purest case -- not a single hash in the whole droplet path.

## The fix

`Bubble` gains a `seed`, computed once at spawn:

    bubble_seed(x, y, timestamp) = mix(quantized x, quantized y,
                                       timestamp lo, timestamp hi)

Pure: no RNG state, no global entropy. The same (x, y, timestamp) always
produces the same seed, so replay and the determinism regressions stay
meaningful; the coordinates are quantized to whole pixels on purpose, and
the timestamp dominates, so clicking the same pixel twice still gives two
different bursts.

Because the seed is FIXED AT SPAWN and never mutated, every derived value
is still a pure function of (bubble, elapsed) -- the frame-cadence
independence contract is untouched.

### Burst / Spark Burst

- whole-burst rotation `hash01(seed) * 2pi` -- without it every burst still
  lands particle 0 on the +x axis and reads as "the same shape again";
- per-spoke angular scatter inside its own sector: 0.85 for Spark Burst
  (chaotic sparks), 0.45 for Burst (a readable rosette that is no longer a
  stencil);
- per-particle speed, size and alpha.

Travel stays MONOTONE non-decreasing in progress because the per-particle
speed is constant for the particle's whole life -- the T-017 Phase 5
contract the existing regressions assert.

### Elemental

`b.seed` is folded into the variation input (`u = index ^ b.seed`). The
formulas are untouched, so every element keeps its motion signature and its
bounds; only the variation widened. Water additionally got real per-droplet
variation (angle, throw distance, launch speed, gravity, size, alpha) with
its arc signature -- thrown out low, pulled back down under the rings --
preserved exactly.

## Evidence

- `protrail_click_tests`: 1847 checks, 0 failures
- Release CTest `-E protrail_input_dispatch`: 15/15 PASS
- Incremental Release build: 0 errors, 0 warnings

New regressions (all six affected styles):

- identical click, rebuilt from scratch, replays bit-identically;
- two different clicks scatter differently -- compared as OFFSETS from each
  anchor, so it cannot pass merely because the clicks are at different
  screen positions;
- Spark Burst is no longer pinned to the +x axis across 8 clicks.
