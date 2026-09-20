# ProTrail v0.1.0

ProTrail v0.1.0 is prepared as a **source-only** Windows release. It contains the Main home surface, advanced configurable Trail/Sparkle/Click/Hold/Motion Wake effects, tray lifecycle, per-monitor DPI-aware rendering, schema-validated atomic configuration, and developer-only canonical Release Defaults authoring.

Build from source with the documented Qt 6.8/MSVC toolchain. Verification builds and tests are required, but this release intentionally includes no `protrail.exe`, DLL bundle, installer, portable binary ZIP, MSI, MSIX, package-manager binary, or other executable artifact.

Official Windows binary distribution remains blocked by:

`FINAL_BINARY_RELEASE_BLOCKED: USER_PRODUCT_ICON_PENDING`

The generated QPainter cursor tray icon is a development fallback, not final product branding. When the final icon is supplied, the binary gate must cover executable resources, taskbar/window icon, tray state policy, multi-resolution ICO assets, and packaging metadata before any official binary release.
