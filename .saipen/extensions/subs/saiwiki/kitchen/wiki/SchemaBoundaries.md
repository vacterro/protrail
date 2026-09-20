<!-- mirrors: app_config.h schema boundary constants rows 6-10 sha256:b45282ef63053766 -->
<!-- projection: row = "<schema number>|<boundary constant name>" -->

# Config schema boundaries (6 .. 10)

Mirror of the named schema-introduction boundaries, rebuilt **by ID**, where the
ID is the schema number the boundary introduces.

| Schema | Boundary constant | What became legitimate at this schema |
|---|---|---|
| 6 | `kElementalClickStyleSchema` | ClickStyle 7..10 (Air / Fire / Water / Earth) |
| 7 | `kShardsSparkleModeSchema` | TrailSparkleMode 5 (Shards) |
| 8 | `kHoldFxSchema` | the press-and-hold gesture and `click.hold_enabled` |
| 9 | `kHoldWakeSchema` | the Hold Controls / Motion Wake block |
| 10 | `kStartWithWindowsSchema` | per-user Windows autostart (`app.start_with_windows`); absent key at any schema <= 9 reads as OFF |

`AppConfig::kCurrentSchemaVersion` is currently **10**. It is deliberately not
part of the mirrored row set above: it is current state, not a boundary, and
listing it beside the boundaries would make a page whose rows mix two different
kinds of fact.

## The migration shapes

The boundaries above are not all the same kind of rule, and the difference is
the thing most likely to be got wrong:

**Enum legitimacy (schemas 6 and 7).** A newly introduced enum value is
legitimate only from the schema that introduced it onward. The gate is keyed to
the **source** schema, never to the current enum. A persisted value outside the
range its source schema allowed repairs to the documented safe default rather
than being reinterpreted.

**Behaviour default (schemas 8 and 9).** These govern a default, and the two
directions deliberately disagree:

| Situation | Result |
|---|---|
| fresh schema-8 default | Hold FX **ON** |
| historical schema <= 7, key absent | Hold FX **OFF** |
| fresh schema-9 default | Motion Wake **ON** |
| historical schema <= 8, key absent | Motion Wake **OFF** |
| key present at any schema | the persisted value wins |

A new install gets the feature. An existing user who merely upgraded the
executable does **not** silently acquire a new mouse gesture or a materially new
visual behaviour: their config predates the field, so the absent key is read as
OFF rather than as the struct default. Once they enable it and save, the current
schema persists it and reload preserves it.

**Opt-in OS side effect (schema 10).** `app.start_with_windows` is the one
boundary whose effect lands outside the configuration file, in the per-user
Windows Run key, so its gate has to say two things at once:

| Situation | Result |
|---|---|
| key present, any source schema | the persisted value wins; a value the user's own machine wrote is a decision, not something to re-derive |
| key absent, source schema <= 9 | **OFF**, and the Run key is not touched |
| key absent, fresh schema-10 config | OFF -- `start_with_windows` is defaulted `false` in the struct today, so a fresh config is opt-in like an upgraded one |
| preference ON, registered command stale | reconciled from the real executable path on the next start |

The absent-key direction is the load-bearing one: upgrading the executable
must never enroll an existing user into Windows startup. Its counterpart is
that the schema gate is not the only guard -- reconciliation is driven by the
saved preference, never by the schema bump, so raising the schema cannot by
itself change what the machine does at sign-in.

That last row is also why the `--startup-minimized` argument is minted by
the command builder and parsed from the same constant: the registry command
and the launch mode are one contract, not two strings that have to agree.

The four Hold multipliers (`hold_intensity`, `hold_wake_density`,
`hold_wake_lifetime_ms`, `hold_release_strength`) are a fourth shape. They are
gated by the schema-9 boundary like the toggles beside them -- a schema <= 8
writer's stray key is ignored and the struct defaults stand -- and they are
additionally clamped to hard bounds with each exactly the identity at 1.0. The
gate is what matters: the `else` branch never assigns them, so where the toggle
is explicitly forced OFF the multipliers simply keep the defaults that
reproduce the previously accepted appearance, rather than half-adopting a
behaviour the file never asked for.

## Sources

- `src/config/app_config.h` (boundary constants, `kCurrentSchemaVersion`, the migration notes)
- `src/config/config_storage.cpp` (the enforcement of each gate)
