<!-- mirrors: TrailSparkleMode enum rows 0-5 sha256:d1f5f79b7e064b6c -->
<!-- projection: row = "<enumerator value>|<enumerator name>" -->

# TrailSparkleMode (0 .. 5)

Mirror of the persisted sparkle-decoration enum, rebuilt **by ID**.

This is a separate layer over the trail, not a trail style: it decorates the
already-computed path instead of changing the stroke.

| ID | Name | Character |
|---|---|---|
| 0 | Off | no sparkle layer; the product default |
| 1 | Stardust | soft tiny dust hugging the path |
| 2 | Twinkle | sparse star-like flashes that pulse |
| 3 | Glitter | dense tiny sharp shimmer |
| 4 | Firefly | sparse drifting luminous points |
| 5 | Shards | detached rotating triangular fragments |

## Schema gate

| ID range | Minimum source schema | Boundary constant |
|---|---|---|
| 0 .. 4 | any | -- (original range) |
| 5 | 7 | `kShardsSparkleModeSchema` |

A persisted value outside the range its **source schema** allowed repairs to
`TrailSparkleMode::Off`. As with ClickStyle, the gate is keyed to the source
schema, so a file written before Shards existed cannot have a stray `5`
silently become the new fragment effect.

## Related

`Shards` is the one mode whose geometry is world-anchored rather than
path-relative: each fragment keeps a fixed birth anchor and a deterministic
drift/rotation derived from that anchor, so it does not follow the cursor.

## Sources

- `src/effects/trail_config.h` (`enum class TrailSparkleMode` and its comments)
