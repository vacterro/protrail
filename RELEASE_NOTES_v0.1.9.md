# ProTrail v0.1.9

## Release authority restored

The root `VERSION` file is once again the single release-version authority,
reverting the v0.1.5 rename to `RELEASE_VERSION`.

That rename was a workaround for an MSVC `<version>` header shadow: on
case-insensitive Windows a root file named `VERSION` could be resolved in place
of the C++ standard `<version>` header. v0.1.8 fixed the underlying cause by
removing the repository root from the remaining test-target include paths, and a
clean build with a root `VERSION` present now produces the executable with zero
errors, zero warnings and no C2059.

The rename had a second cost: the canonical `saipen ship` release path requires
a root `VERSION` file and refused to publish while it was absent. This release
is the first one produced through that canonical path, so it carries a committed
release receipt naming the published tag.

## Contents

- `ProTrail-v0.1.9-win-x64-portable.zip`
- `SHA256SUMS.txt`

The package is unsigned because no signing certificate is configured. It
contains `protrail.exe`, the Qt 6.8 runtime, and the app-local MSVC runtime.

## No product change

No product behaviour changed in this release. The portable package matches
v0.1.8 apart from the VERSIONINFO version resource, which now reports 0.1.9.

## Verification

Official publication requires a clean tagged tree,
`PROTRAIL_REQUIRE_FINAL_ICON=ON`, a zero-warning `/W4 /WX` Release build,
complete CTest PASS, package smoke PASS, and matching SHA-256 values.
