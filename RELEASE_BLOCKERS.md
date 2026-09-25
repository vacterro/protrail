# Release blockers

## FINAL_BINARY_RELEASE_READY: USER_PRODUCT_ICON_APPROVED

Status: **RESOLVED** for the autonomous standard-default release path.

The deterministic ProTrail product mark is supplied at
`resources/branding/protrail.ico`, with master artwork in
`resources/branding/protrail-master.svg` and `protrail-master.png`.
`APPROVAL.md` records the exact ICO SHA-256 and the operator-authorized
selection basis. The icon gate no longer blocks packaging.

Official binary distribution still requires the clean-tree, build, test,
packaging, checksum, and release-publication gates. Signing credentials are
not configured; the portable release is explicitly unsigned.

### What is already in place

- `resources/windows/protrail.rc.in` builds VERSIONINFO and, when the asset
  exists, links it as the `IDI_ICON1` icon resource (Explorer/PE icon; Qt's
  default window and taskbar icon).
- `ptd::ui::branding` reads that same resource for Qt surfaces and the tray:
  the enabled tray state is the product icon, the disabled state is its
  deterministic treatment (fully desaturated, 45% opacity).
  `protrail_tray_tests` proves the resource matches the build input and that
  the two tray states differ visibly at 16, 20, 24 and 32 px.
- `-DPROTRAIL_REQUIRE_FINAL_ICON=ON` (used by the official pipeline) fails
  configure without the asset.
- `tools/release/package.ps1` refuses official packaging unless the icon is
  present **and** `APPROVAL.md` records its SHA-256. The smoke harness then
  requires the icon resource in the packaged executable.

### Resolved asset

| Item | Value |
|------|-------|
| File | `resources/branding/protrail.ico` |
| Format | Windows ICO, 32-bit RGBA entries |
| Sizes | 16, 20, 24, 32, 40, 48, 64, 128, 256 px |
| Master artwork | `resources/branding/protrail-master.svg` and `protrail-master.png` (1024 px) |
| Disabled state | Derived automatically by the existing tray branding code |
| Approval record | `resources/branding/APPROVAL.md`, SHA-256 `f28d55e219cc535702884f3fc2e1788098040cba7bf68e7cb5bfa145460fd3cf` |

### Next action

1. Configure and build with `-DPROTRAIL_REQUIRE_FINAL_ICON=ON`.
2. Run the complete Release CTest suite and inspect the icon in Explorer,
   taskbar, window, and enabled/disabled tray states.
3. Run the official packaging pipeline from the clean, tagged tree:
   `powershell -NoProfile -ExecutionPolicy Bypass -File tools\release\package.ps1`
4. Publish the generated `ProTrail-v0.1.5-win-x64-portable.zip` and
   `SHA256SUMS.txt` only after the pipeline and tagged-tree verification pass.

This blocker is resolved. Source cleanup, tests, CI, documentation, and
source-only releases remain independent of binary publication.
