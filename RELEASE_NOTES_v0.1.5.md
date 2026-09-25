# ProTrail v0.1.5

## Release recovery

The root `VERSION` file was renamed to `RELEASE_VERSION` so Windows builds no
longer resolve a repository file as the C++ `<version>` header. Immutable
`v0.1.3` and `v0.1.4` tags remain untouched.

## Contents

- `ProTrail-v0.1.5-win-x64-portable.zip`
- `SHA256SUMS.txt`

The package is unsigned because no signing certificate is configured. It
contains `protrail.exe`, the Qt 6.8 runtime, and the app-local MSVC runtime.

## Branding

`resources/branding/protrail.ico` is the single product-icon authority, with
16, 20, 24, 32, 40, 48, 64, 128, and 256 px RGBA entries. Its exact approval
hash is recorded in `resources/branding/APPROVAL.md`.

## Verification

Official publication requires a clean tagged tree, `PROTRAIL_REQUIRE_FINAL_ICON=ON`,
a zero-warning `/W4 /WX` Release build, complete CTest PASS, package smoke PASS,
and matching SHA-256 values.
