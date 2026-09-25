# Changelog

## 0.1.2 - Croatian documentation patch (source-only)

- Added the complete Croatian translation set for the seven maintained documentation surfaces under the `hr` locale.
- Updated release-facing mirrors and translation kitchen metadata to the `v0.1.2` source-only release identity.
- No official Windows binary, installer, portable archive, or signed executable is included; the final product-icon gate remains open.

## 0.1.1 - Windows x64 portable release preparation (unreleased)

- Made the root `VERSION` file the single release-version authority: CMake
  `project()`, the Windows VERSIONINFO resource and the packaging pipeline all
  derive from it, and a new `protrail_release_identity` CTest gate fails when
  the README badge, newest CHANGELOG entry or release notes disagree with it.
- Added the Windows product resource: VERSIONINFO (ProductName/FileDescription
  `ProTrail`, FileVersion/ProductVersion from `VERSION`, OriginalFilename
  `protrail.exe`) and the `IDI_ICON1` application-icon seam. The approved
  product icon at `resources/branding/protrail.ico` becomes the one icon
  authority for the executable, window/taskbar and tray; the tray disabled
  state is a deterministic desaturated, dimmed treatment of it.
- Added `tools/release/package.ps1`, the portable packaging pipeline: clean
  Release build and full CTest suite, windeployqt runtime plus app-local MSVC
  runtime, dependency-closure and developer-path-leak checks, sanitized-PATH
  smoke from plain/spaced/nested/Unicode paths, deterministic ZIP with
  SHA-256, and a smoke of the independently extracted archive.
- Fixed: Release binaries embedded the developer's source checkout path; it
  is now defined for developer builds only.
- Fixed: a smoke run with its fresh isolated configuration reconciled the
  operator's real `HKCU\...\Run` entry and would have removed a genuine Start
  with Windows registration; smoke mode now uses an in-memory autostart backend
  and the harness fails if the Run entry changes.
- Fixed: `deploy.bat` force-killed every running `protrail.exe`, including an
  operator's own instance; it now delegates to the harness, which only ever
  cleans up processes it launched.
- Fixed: the deployment smoke looked for the retired `ProTrail Settings`
  window title instead of the unified `ProTrail` window.
- Six test targets no longer put the repository root on the include path, so a
  root `VERSION` file can never again shadow the C++ `<version>` header.

## 0.1.0 - Source-only release preparation

- Replaced the split home/advanced-editor UX with one ProTrail product window:
  General, Trail, Click, and Developer in developer builds only.
- Kept complete Trail/Sparkle and Click/Hold/Motion Wake configuration in
  scrollable domain tabs, using visible selector-button grids instead of combo
  boxes for ordinary style, mode, and easing choices.
- Unified tray navigation around `Open ProTrail`; close hides the same window to
  the tray, autostart remains tray-only, and interactive second-instance
  activation restores the existing window.
- Kept `Application::app_config` as the canonical authority, with one logical
  transaction for Restore All Defaults and developer-only `Set Current as
  Release Defaults` authoring from the complete persisted configuration.
- Fixed: a completed durable configuration write left the deferred-save pending
  flag set, so an expired debounce could rewrite an already-persisted state.
- Added focused unified-window regressions for tab identity, selector state,
  silent population, one-publication-per-action, hide-to-tray close,
  startup-mode selection, tray reuse, and canonical Restore Defaults.
- Added Windows CI, contribution/security guidance, issue and pull-request
  templates, repository hygiene rules and generated-artifact ignores.
- Documented multi-monitor DPI behavior, tray lifecycle, autostart behavior,
  configuration storage, the repository layout and source build instructions.

No official Windows executable, installer, portable package or other binary is
attached. Final binary release is blocked by
`FINAL_BINARY_RELEASE_BLOCKED: USER_PRODUCT_ICON_PENDING`.
