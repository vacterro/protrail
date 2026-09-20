# OUTBOX

## HUNT-001: prior unbounded CTest finding

- **status:** stale
- **summary:** The prior package was bound to an older source fingerprint and is superseded by the current scan.
- **main_project_refs:** [CMakeLists.txt, ctest_safe.bat]
- **critical:** false
- **severity:** P3
- **producer:** saihunt
- **source_head:** 1fc09f09410da3493ed8f7ffca621ca0467ca259
- **source_tree_fingerprint:** git-delta-v1:f74afc7dc0722faaf697148c627eaa6b3ee1bbac78c5b4ea83ed18a87dc5286a
- **role_revision:** sha256:4edb04181cb07e0946afd06fbe711166fa9dcc403e56b52e9be3844f0a71b0a5
- **coverage:** superseded by current HUNT-002 scan
- **payload:** none
- **verified:** BLOCKED -- prior package superseded by current HUNT-002; no stale package should be collected
- **instructions:** Ignore this historical package; review HUNT-002 instead.
- **details:** Retained as history only.

## HUNT-002: CTest registrations have no finite timeout

- **status:** stale
- **summary:** The current CMake test suite still declares no TIMEOUT property, allowing a wedged Windows/Qt test to block CTest indefinitely.
- **main_project_refs:** [CMakeLists.txt, ctest_safe.bat]
- **critical:** false
- **severity:** P3
- **producer:** saihunt
- **source_head:** 1fc09f09410da3493ed8f7ffca621ca0467ca259
- **source_tree_fingerprint:** git-delta-v1:f74afc7dc0722faaf697148c627eaa6b3ee1bbac78c5b4ea83ed18a87dc5286a
- **role_revision:** sha256:4edb04181cb07e0946afd06fbe711166fa9dcc403e56b52e9be3844f0a71b0a5
- **coverage:** six-signal scan: tests/failing suite state, stale markers, config save/load symmetry, silent failures, symmetry gaps, and orphan source files
- **payload:** none -- report only; main Core owns any fix
- **verified:** BLOCKED -- package became stale after the Core timeout repair changed CMakeLists.txt; re-run HUNT only if the current tree still warrants it
- **instructions:** 1. Add a bounded TIMEOUT property to every registered test, preferably through one target loop. 2. Add the matching finite timeout to ctest_safe.bat. 3. Preserve the existing input-dispatch exclusion. 4. Re-run the full applicable suite in Release and Debug.
- **details:** Package was collected into T-48, then became stale by the allowed source-freshness rule when CMakeLists.txt changed during the Core repair. The original finding is represented by T-48 and must not be re-collected as a ghost.
