# ProTrail v0.1.7

## Release recovery

The immutable `v0.1.6` tag is retained as an incomplete package-build record.
This patch commits the remaining `src/ui/branding.h` and `src/ui/branding.cpp`
sources required by the clean CMake target list, alongside the approved
multi-resolution product icon.

## Contents

- `ProTrail-v0.1.7-win-x64-portable.zip`
- `SHA256SUMS.txt`

The package is unsigned because no signing certificate is configured.

## Verification

Official publication requires a clean tagged tree, `PROTRAIL_REQUIRE_FINAL_ICON=ON`,
a zero-warning `/W4 /WX` Release build, complete CTest PASS, package smoke PASS,
and matching SHA-256 values.
