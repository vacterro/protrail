# ProTrail v0.1.3

## Release

Official Windows x64 portable release preparation. The approved deterministic
ProTrail product icon is integrated into the PE resource, window/taskbar, and
tray surfaces. The portable package is unsigned; no signing certificate is
configured.

## Contents

- `ProTrail-v0.1.3-win-x64-portable.zip`
- `SHA256SUMS.txt`

The package contains `protrail.exe`, the Qt 6.8 runtime, and the app-local
MSVC runtime. The official pipeline verifies dependency closure, developer-path
leaks, sanitized-PATH smoke, the path matrix, deterministic ZIP bytes, and a
fresh extraction.

## Branding

`resources/branding/protrail.ico` is the single product-icon authority. Its
multi-resolution ICO and exact approval hash are recorded in
`resources/branding/APPROVAL.md`; master artwork is kept beside it.

## Verification

The release requires a clean tagged tree, `PROTRAIL_REQUIRE_FINAL_ICON=ON`, a
zero-warning `/W4 /WX` Release build, complete CTest PASS, package smoke PASS,
and matching SHA-256 values. No binary is published when any gate fails.
