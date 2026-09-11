# MVP 06 - Configuration and Persistence

Status: CLOSED

## Goal

Make all MVP settings durable, validated, and safe.

## Scope

Implement configuration types equivalent to:

```text
ApplicationConfig
TrailConfig
ClickConfig
RenderConfig
```

The exact names may differ.

## Runtime contract

GUI:
- edits configuration.

Configuration layer:
- validates;
- normalizes;
- publishes a runtime snapshot.

Renderer/effects:
- consume the validated snapshot.

## Persistence location

Installed mode:

```text
%LOCALAPPDATA%\ProTrail\
```

Portable mode may later use a local configuration directory.

For MVP, one clear location is sufficient.

## Save safety

Do not overwrite the only valid settings file with partially written data.

Use an atomic-replacement strategy:

1. write temporary file;
2. close/flush;
3. replace destination safely.

## Recovery

If configuration is missing:
- use defaults.

If configuration is malformed:
- preserve/rename the bad file if useful;
- load defaults;
- log the recovery;
- do not crash.

## Acceptance criteria

- settings survive restart;
- malformed config does not crash;
- invalid numeric values are clamped or rejected safely;
- a failed write does not destroy the previous valid config;
- defaults can be restored;
- configuration publication is safe for the current threading model.

## Close evidence

- **Persistence path**: `%LOCALAPPDATA%\ProTrail\config.json` resolved via `SHGetKnownFolderPath(FOLDERID_LocalAppData)`. Parent folder `%LOCALAPPDATA%\ProTrail` created automatically if missing.
- **Schema version strategy**: `schema_version = 1` serialized in root JSON object; `AppConfig::validated()` enforces valid integer schema version on load.
- **Atomic replacement**: Temporary file `.tmp` written, flushed, closed, then atomically replaces target via Win32 `ReplaceFileW` (or `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`).
- **Malformed config recovery**: When JSON parsing fails, the bad file is renamed to `config.json.corrupt`, warning is logged, and clean defaults are loaded without crashing.
- **Build verification**: MSVC /W4 /WX green in both Release and Debug configurations.
- **Automated tests**: `protrail_config_tests` (7/7 unit tests PASS), CTest regression suite 8/8 PASS in Release and Debug.
- **Restart simulation**: `TestConfig::restart_simulation_persists_all_settings()` verifies end-to-end durability across process lifetimes.
