# ProTrail v0.1.4

## Release recovery

`v0.1.3` is an immutable but incomplete publication record: its tag points to
 the pre-icon closure commit. This patch does not move or delete that tag. The
actual icon-bearing release is `v0.1.4`.

## Contents

- `ProTrail-v0.1.4-win-x64-portable.zip`
- `SHA256SUMS.txt`

The package contains `protrail.exe`, the Qt 6.8 runtime, and the app-local
MSVC runtime. It is unsigned because no signing certificate is configured.

## Branding

`resources/branding/protrail.ico` is the single product-icon authority. The
ICO contains 16, 20, 24, 32, 40, 48, 64, 128, and 256 px RGBA entries. Its exact
approval hash is recorded in `resources/branding/APPROVAL.md`; master artwork is
kept in `protrail-master.svg` and `protrail-master.png`.

## Verification

Publication requires a clean tagged tree, `PROTRAIL_REQUIRE_FINAL_ICON=ON`, a
zero-warning `/W4 /WX` Release build, complete CTest PASS, package smoke PASS,
and matching SHA-256 values. No binary is published when any gate fails.
