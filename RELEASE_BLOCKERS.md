# Release blockers

## FINAL_BINARY_RELEASE_BLOCKED: USER_PRODUCT_ICON_PENDING

The user will design or select the final ProTrail product icon later. Until that asset is supplied and accepted, do not distribute an official EXE, installer, portable binary package, signed executable, or branded Windows release.

The current generated QPainter cursor tray icon may remain as a development fallback. It is not approved final branding.

Future icon integration must cover the Windows executable resource, taskbar/window icon, tray icon policy, multi-resolution ICO sizes, enabled/disabled tray-state recognizability, packaging metadata, and the final binary-release gate.

This does not block source cleanup, automated tests, CI, documentation, or the source-only v0.1.0 release.
