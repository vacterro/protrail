# ProTrail v0.1.8

## Release closure

The immutable `v0.1.5`, `v0.1.6`, and `v0.1.7` tags are retained as
incomplete package-build records. This release commits the complete product
source closure required by the canonical CMake target list: the unified
ProTrail window, removal of the retired `MainWindow` implementation, the
T-59 runtime repairs, corrected test include paths, and hardened deployment
smoke behavior.

The `v0.1.5` release-version rename from `VERSION` to `RELEASE_VERSION` is
retained, so the repository root can no longer shadow the C++ `<version>`
header during a Windows build.

## Contents

- `ProTrail-v0.1.8-win-x64-portable.zip`
- `SHA256SUMS.txt`

The package is unsigned because no signing certificate is configured. It
contains `protrail.exe`, the Qt 6.8 runtime, and the app-local MSVC runtime.

## Branding

`resources/branding/protrail.ico` is the single product-icon authority, with
16, 20, 24, 32, 40, 48, 64, 128, and 256 px RGBA entries. Its exact approval
hash is recorded in `resources/branding/APPROVAL.md`.

## Verification

Official publication requires a clean tagged tree,
`PROTRAIL_REQUIRE_FINAL_ICON=ON`, a zero-warning `/W4 /WX` Release build,
complete CTest PASS, package smoke PASS, and matching SHA-256 values.
