# ProTrail v0.1.6

## Release recovery

The immutable `v0.1.5` tag is retained as an incomplete package-build record.
This patch adds the committed `src/app/topology_retry.h` source required by the
CMake target list and keeps the approved multi-resolution product icon.

## Contents

- `ProTrail-v0.1.6-win-x64-portable.zip`
- `SHA256SUMS.txt`

The package is unsigned because no signing certificate is configured.

## Verification

Official publication requires a clean tagged tree, `PROTRAIL_REQUIRE_FINAL_ICON=ON`,
a zero-warning `/W4 /WX` Release build, complete CTest PASS, package smoke PASS,
and matching SHA-256 values.
