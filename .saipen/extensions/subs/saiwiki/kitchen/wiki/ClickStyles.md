<!-- mirrors: ClickStyle enum rows 0-10 sha256:9ce452d65f8d6e24 -->
<!-- projection: row = "<enumerator value>|<enumerator name>" -->

# ClickStyle (0 .. 10)

Mirror of the persisted click-effect style enum, rebuilt **by ID**.

These numbers are **persisted config values**, so the ID is the durable
identity, not the source order. A style's numeric value can never be reused for
a different visual meaning; that is why the enum is explicitly numbered rather
than left implicit, and why the digits below are the contract.

| ID | Name | Character |
|---|---|---|
| 0 | Ring | approved MVP 04 bubble ring, plus a subtle fill |
| 1 | DoubleRing | main ring plus an inner secondary ring |
| 2 | Ripple | thin pure ring, no fill |
| 3 | Burst | ring plus particles flying outward |
| 4 | SparkBurst | deterministic-jitter particle burst, fewer rings |
| 5 | SoftFlash | soft filled flash disc, no ring |
| 6 | DotRing | central dot plus ring |
| 7 | Air | fast thin wide ring plus tangentially swirling motes |
| 8 | Fire | buoyant embers rising with wobble, cooling with age |
| 9 | Water | up to three concentric ripple rings, staggered births |
| 10 | Earth | low dust ring plus chunky debris arcing under gravity |

IDs 0..6 are the MVP 04 / T-017 families. IDs 7..10 are the T-022 elemental
families, which differ by **motion signature first** — recognizable without
reading which button was pressed — and carry an elemental hue bias blended over
the user's configured Click colour by `ClickConfig::element_tint`. They never
ignore the configured colour.

## Schema gate

The elemental values are only legitimate from the schema that introduced them:

| ID range | Minimum source schema | Boundary constant |
|---|---|---|
| 0 .. 6 | any | -- (original range) |
| 7 .. 10 | 6 | `kElementalClickStyleSchema` |

A persisted value outside the range its **source schema** allowed repairs to
the documented safe default `ClickStyle::Ring`. The gate is keyed to the source
schema, never to the current enum: a corrupt value written by an older writer
must not be silently reinterpreted as a newly added effect.

## Sources

- `src/effects/click_config.h` (`enum class ClickStyle`, its per-enumerator comments, the gate constants)
