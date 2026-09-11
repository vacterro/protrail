# ProTrail

Native Windows cursor-effects application. See `ROADMAP/` for the full plan.

## Build

Requirements (Windows 10/11 x64):

- Visual Studio 2022 Build Tools with the "Desktop development with C++" workload (MSVC 14.4x, CMake 3.24+).
- Qt 6.8 MSVC 2022 64-bit, installed at `C:\Qt\6.8.0\msvc2022_64` (adjust with `-DCMAKE_PREFIX_PATH` if elsewhere).
  Install without the online installer: `python -m aqt install-qt windows desktop 6.8.0 win64_msvc2022_64 -O C:\Qt`.

Configure (from a "Developer Command Prompt" / after calling `vcvars64.bat`):

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
```

Build:

```bat
cmake --build build --config Release
cmake --build build --config Debug
```

Run tests:

```bat
build\Release\protrail_tests.exe
```

Qt runtime DLLs are loaded through the standard Qt search path; add `C:\Qt\6.8.0\msvc2022_64\bin`
and `C:\Qt\6.8.0\msvc2022_64\plugins\platforms` to `PATH`, or copy `Qt6Core.dll`, `Qt6Gui.dll`,
`Qt6Widgets.dll` and the `platforms\qwindows.dll` plugin next to the executables.

Output: `build\Release\protrail.exe`. No administrator privileges required.

## Status

- MVP `00_PROJECT_BOOTSTRAP`: CLOSED.
- MVP `01_NATIVE_OVERLAY`: CLOSED — native transparent, click-through D2D/DComp overlay
  (`WS_EX_LAYERED` + `WS_EX_TRANSPARENT` hit-test recipe, DirectComposition swapchain;
  verified by `tests/hittest_probe.ps1` and manual check).
- MVP `02_MOUSE_INPUT_AND_SAMPLING`: CLOSED — global raw-input (RIDEV_INPUTSINK,
  message-only window) feeding a bounded `CursorHistory` (512 samples, coalesced
  duplicates, preserved button transitions, QPC timestamps).
- MVP `03_BASIC_TRAIL`: CLOSED — user-verified trail (yellow, ~350 ms life,
  centripetal Catmull-Rom with continuous smoothing 0..1, time-based fade,
  active-only scheduler; `tests/test_trail_effect.cpp` regression).
- MVP `04_CLICK_BUBBLE`: CLOSED — user-verified click bubble (cyan ring, ease-out
  8→26 px / 250 ms / 0.85, bounded 128 bubble lifecycle, active-only shared
  scheduler; `tests/test_click_bubble_effect.cpp` — 107 checks).
- MVP `05_SETTINGS_GUI`: CLOSED — user-verified live Settings GUI (General/Trail/Click,
  Golden Default theme, interactive color swatches with QColorDialog, 2px bevel,
  slider+spin sync, active-only scheduler; `tests/test_settings_window.cpp` — 35 checks).
- MVP `06_CONFIGURATION_AND_PERSISTENCE`: CLOSED — durable JSON settings at
  `%LOCALAPPDATA%\ProTrail\config.json`, atomic ReplaceFileW write, malformed
  fallback with `.corrupt` backup, thread-safe publication (`tests/test_config.cpp` — 7 tests).
- MVP `07_RENDER_SCHEDULER_AND_PERFORMANCE`: CLOSED — formal active-only `RenderScheduler`
  with display refresh rate pacing (EnumDisplaySettings), stable delta-time, 0 wakeups when idle,
  performance counters and zero-leakage resource measurements (`tests/test_render_scheduler.cpp` — 10 tests).
- MVP `08_MULTI_MONITOR_AND_DPI`: CLOSED — per-monitor overlay architecture, explicit
  `PER_MONITOR_AWARE_V2` DPI awareness, physical-pixel transform, deferred topology reconciliation,
  multi-monitor regression suite (`tests/test_multimonitor.cpp` — 28 tests).
- MVP `09_TRAY`: CLOSED — system tray icon, hide-on-close SettingsWindow lifecycle, tray
  context menu (Settings, Enable/Disable, Exit), clean application shutdown with zero orphan processes
  (`tests/test_tray.cpp` — 12 tests).

### Core MVP Status

Core MVP (Milestones 00 through 09) is complete and fully closed.
All functional and acceptance criteria verified across both automated test suites (/W4 /WX clean, 12/12 CTest PASS) and live multi-monitor / tray user testing.

### Post-MVP Extensions

- Post-MVP `V1_COLORS_AND_PRESETS`: CLOSED — dual trail colors (Start/Fade), 14-swatch palettes, custom `QColorDialog`, 4 trail color modes (Full, Start only, Fade only, Gradient), click 14-swatch palette, 6 quick presets (Classic, Fire, Ice, Neon, Toxic, Violet), schema v2 backward-compatible config migration.
- Post-MVP `V2_TRAIL_EFFECTS`: CLOSED — 8 trail styles (Classic, Soft Glow, Comet, Neon, Dotted, Pulse, Ribbon, Spark), glow strength + segment spacing parameters, style-aware effect math and renderer stroke policy, schema v3 persistence.
- Post-MVP `V3_CLICK_EFFECTS`: CLOSED — 7 click styles (Ring, Double Ring, Ripple, Burst, Spark Burst, Soft Flash, Dot + Ring), particle amount (0..24), schema v4 persistence, isolated smoke testing with 0 production state mutation.
- Post-MVP `V4_SYSTEM_STABILITY`: CLOSED — single-instance mutex guard (`Local\ProTrail_SingleInstance_Mutex`) with window activation via `ProTrail_ActivateInstance`, Direct2D/DXGI device loss handling and recovery (`D2DERR_RECREATE_TARGET`, `DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_RESET`) preserving HWND, "Restore All Defaults" instant recovery.

