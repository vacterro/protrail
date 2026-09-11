# MVP 05 - Settings GUI

Status: CLOSED (T-010, closed 2026-09-10)

## Goal

Provide a compact native GUI for controlling the two MVP effects.

## Technology

Qt 6 Widgets.

Do not use:

- QML for the main settings UI;
- WebView;
- Electron;
- browser UI;
- React/Vue.

## Required pages

### General

- Enable ProTrail
- Enable Trail
- Enable Click Effect

### Trail

- Color
- Thickness
- Lifetime
- Opacity
- Smoothing

### Click

- Color
- Size
- Duration
- Opacity
- Outline thickness

## Behavior

Settings should apply live.

Avoid a mandatory Apply button unless a setting genuinely cannot be applied safely in real time.

## UI quality

The window must remain usable at normal Windows scaling.

Avoid fixed-height layouts that create huge empty regions or clip content when labels change.

Prefer standard layouts with sensible minimums.

## Non-goals

No profiles.
No application rules.
No particle settings.
No advanced diagnostics page.

## Acceptance criteria

- every MVP setting can be changed from the GUI;
- changes are visible immediately;
- settings remain readable at 100%, 125%, 150%, and 175% scaling where practical;
- closing settings does not exit the effect engine unless explicitly designed to do so;
- renderer never reads Qt widgets directly.

## Close evidence

### User live visual verification
- USER VISUAL PASS (2026-09-10): User live-tested the MVP 05 implementation and confirmed: "Yes, everything that is declared works." Covers live controls for Trail (color, thickness, taper, lifetime, opacity, smoothing, fade), Click (color, sizes, duration, opacity, outline, fill, easing, triggers), Master enable/disable effect clear and resume, default restoration, checkbox tick cues, and clean process termination.

### Color picker addition & verification
- Swatch converted to interactive control (`QPushButton#swatchButton`, 24x16 px) adhering to `UI.md` Golden Default tokens (2px bevel, hover/focus highlight in `#F0D060`, tooltip "Choose color", `Qt::StrongFocus`).
- Opens `QColorDialog` initialized with current RGB; alpha-channel selection disabled (opacity remains controlled by dedicated opacity settings).
- Atomic application via `apply_color(r, g, b)` with spinbox signals blocked, updating R, G, B spinboxes and swatch, emitting exactly one `color_changed` signal.
- Verified: manual spinbox edit updates swatch and publishes config; picker-applied color updates spinboxes, swatch, and publishes config once; cancellation produces zero changes; Trail and Click remain strictly isolated.

### Pre-tray lifecycle contract (temporary exception)
- Settings is currently the only user-visible application control surface.
- Closing Settings exits ProTrail cleanly (`QApplication::quitOnLastWindowClosed` remains `true` by default).
- This is intentional only until MVP 09 (Tray Integration) exists.
- We deliberately do NOT set `quitOnLastWindowClosed(false)` in MVP 05, to avoid creating a hidden, unreopenable orphan process.
- MVP 09 owns the production contract:
  - Settings window close -> hide window
  - Effect engine continues running in background
  - System tray icon allows reopening Settings window
  - Explicit tray menu "Exit" action terminates ProTrail cleanly.

### Automated verification results
- Build: MSVC 19.44 Debug and Release builds compile clean with `/W4 /WX`.
- `protrail_gui_tests`: 35/35 PASS in both Release and Debug configs.
  - Tests SettingsWindow signal emission, General/Trail/Click tabs, slider/spin synchronization, default restorations, coherence rules, non-color checkbox indicator (tick bitmap), focus behavior, and 6 dedicated color picker integration tests.
  - Scope accuracy: `master_disable_emits_window_signals` proves SettingsWindow signal emission contract; full runtime clearing and resumption of rendered effects verified via Application logic and user live visual verification.
- CTest suite: 8/8 automated baseline established.

### UI and DPI scaling verification
- Dynamic layouts using standard Qt widgets (`QTabWidget`, `QFormLayout`, `QVBoxLayout`, `QHBoxLayout`) without fixed-height clipping.
- Reference viewport and minimum size: `setMinimumSize(640, 540)` and `resize(640, 540)` in `src/ui/settings_window.cpp`, directly adhering to `UI.md` (Iron Law 4: 640x540 compact survival viewport without horizontal scroll).
- Layout verified across 100%, 125%, 150%, and 175% scaling.


