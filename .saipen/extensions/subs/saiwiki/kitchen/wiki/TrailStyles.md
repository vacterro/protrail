<!-- mirrors: TrailStyle enum rows 0-7 sha256:167af58625992845 -->
<!-- projection: row = "<enumerator value>|<enumerator name>" -->

# TrailStyle (0 .. 7)

Mirror of the persisted trail-style enum, rebuilt **by ID**. Same contract as
ClickStyle: the number is the durable persisted value and is never reused.

| ID | Name | Character |
|---|---|---|
| 0 | Classic | approved baseline stroke |
| 1 | SoftGlow | wide soft outer pass plus core (renderer multi-pass) |
| 2 | Comet | bright enlarged head with a thin fading tail |
| 3 | Neon | intense glow: wider and stronger outer pass |
| 4 | Dotted | evenly spaced dots along the path |
| 5 | Pulse | traveling spatial width/alpha wave |
| 6 | Ribbon | smooth alternating broad/narrow twist lobes |
| 7 | Spark | dots plus deterministic per-dot alpha flicker |

## Stroke-policy note

Which styles can be drawn as one continuous stroke is not part of this enum.
The renderer decides it from the per-segment cap combination through the stroke
policy in `src/render/trail_stroke_policy.h`: flat/flat, flat/round and
round/round are cacheable continuous strokes, while Dotted and Spark keep round
stubs because their identity is the stub. Style and stroke policy are separate
authorities on purpose — a style is persisted user intent, a stroke policy is a
renderer capability.

## Sources

- `src/effects/trail_config.h` (`enum class TrailStyle` and its per-enumerator comments)
- `src/render/trail_stroke_policy.h` (stroke-policy capability only)
