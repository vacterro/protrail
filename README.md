# ProTrail

**v0.1.7**

ProTrail is a native Windows desktop utility that renders configurable cursor
trails, click and press-and-hold effects, and motion wake geometry without ever
intercepting, delaying or swallowing a mouse click. It is a Qt 6 / C++20
application rendering through Direct2D and DirectComposition into per-monitor
click-through overlays.

## Current scope

Windows 10/11 only. MSVC (Visual Studio 2022) with Qt 6.8 is the supported
toolchain.

- **One ProTrail window** — the product surface has General, Trail, and Click
  tabs, plus Developer in developer builds. Every setting has one visible editor
  location; Trail and Click use visible checkable selector grids rather than
  drop-down style controls. General is the default launch tab and contains
  enable toggles, startup, quick color presets, and Restore All Defaults.
- **Trail and Click tabs** — the complete Trail/Sparkle and Click/Hold/Motion
  Wake editors live in scrollable tabs, each with its own domain restore action.
  There is no second settings window and no duplicate editor surface.

## Effects

- **Trail** — 8 styles (Classic, Soft Glow, Comet, Neon, Dotted, Pulse, Ribbon,
  Spark) with dual start/fade colors, four color modes, 14-swatch palettes,
  custom color picking, taper, smoothing and glow controls.
- **Sparkles** — 5 modes (Stardust, Twinkle, Glitter, Firefly, Shards) layered
  deterministically onto the visible trail path, with amount/size/spread
  controls.
- **Click** — 11 styles (Ring, Double Ring, Ripple, Burst, Spark Burst, Soft
  Flash, Dot + Ring, and the elemental Air, Fire, Water, Earth), per-click
  randomization that is stable at any frame cadence, per-button triggers, and
  particle/color/easing controls.
- **Hold** — press-and-hold is a distinct gesture from a click: a continuous
  charge aura while the button is down, and a release payoff scaled by how long
  it was held. Dropped button-up events are reconciled against the physical
  button state, so a lost Up cannot leave a hold running.
- **Motion Wake** — while an active hold moves, style-specific geometry is shed
  into world space. Each emission keeps its birth anchor permanently and
  animates independently of the cursor. Advanced controls cover wake strength,
  size, spread, speed response, a minimum motion-speed gate, and turn/stop
  accents.

## Windows integration

- **Multi-monitor and DPI** — one click-through, non-activating overlay per
  monitor under a per-monitor-DPI-aware process contract, with deferred
  topology reconciliation for display changes, plug/unplug and mixed DPI.
- **Idle behavior** — rendering runs only while content is alive. An idle
  ProTrail has no permanent render timer.
- **Start with Windows** — a ProTrail-owned per-user `Run` entry whose command
  is the fully quoted current executable path plus an explicit startup
  argument. Only the owned value is ever written or removed, and the
  registration is reconciled against the real executable path at startup.
- **Startup modes** — a manual launch opens the single ProTrail window on
  General. An autostart launch is tray-only: no product window, taskbar button,
  or focus steal. A second manual launch hands an activation request to the
  running instance, which restores that same window.
- **Tray and lifecycle** — `Open ProTrail`, `Enable`/`Disable`, and `Exit`.
  Closing the window hides it to the tray and keeps ProTrail running; exit is
  an explicit tray action. Tray double-click uses the same Open action.

## Configuration

User configuration lives in `%LOCALAPPDATA%\ProTrail\config.json`, written
atomically and validated against the schema on load (currently schema 11).
Older schema files migrate forward; a value introduced by a newer schema is
repaired to its documented safe default rather than reinterpreted.

**Restore Defaults** applies the canonical defaults embedded at build time from
`resources/release_defaults.json` — there is exactly one defaults authority for
both a fresh install and an explicit restore.

Developer builds additionally expose **Set Current as Release Defaults**, which
takes the complete canonical application configuration, writes the defaults
source atomically, re-reads and re-parses it to prove semantic equality, and
reports success plus the changed settings. It is refused outright in a
production Release build and never ships to ordinary users.

## Build requirements

- Windows 10 or newer
- Visual Studio 2022 / MSVC v143
- CMake 3.24+
- Qt 6.8.x `msvc2022_64` with the Widgets and Test modules

Configure and build from a VS Developer PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
cmake --build build --config Debug --parallel
```

The configure step validates the canonical release defaults source and fails
loudly if it is missing or malformed. MSVC builds use `/W4 /WX`. Configure with
`-DPROTRAIL_DEV_BUILD=ON` to enable the developer defaults authoring panel in a
Release build (Debug builds always have it).

## Tests

Run the whole suite for a configuration:

```powershell
ctest --test-dir build -C Release --output-on-failure
ctest --test-dir build -C Debug --output-on-failure
```

The suite registers the full CTest matrix for trail, sparkles, click,
hold/wake, multi-monitor, scheduler, config, release defaults, autostart,
single instance, tray lifecycle, and the unified GUI, plus two release-defaults
gate script tests; one deliberately proves the gate can fail. GUI tests run
headless through Qt's `offscreen` platform.

## Repository layout

| Path | Contents |
|------|----------|
| `src/` | Application source (app, config, core, effects, platform, render, ui) |
| `tests/` | Test executables registered with CTest |
| `resources/` | Canonical defaults source and its Qt resource |
| `resources/windows/` | Windows VERSIONINFO / icon resource template |
| `resources/branding/` | Approved product icon (`protrail.ico`) and its approval record |
| `cmake/` | Configure-time release-defaults validator and release-identity gate |
| `tools/release/` | Windows x64 portable packaging and verification pipeline |
| `ROADMAP/` | Planning, references and historical design notes |
| `ROADMAP/evidence/` | Retained per-ticket design/measurement write-ups |

## Release status

**v0.1.7 is the official Windows x64 portable release candidate. The
operator-authorized deterministic product icon is integrated and the source
release is published.** The portable binary package remains subject to the
official packaging, smoke, checksum, and publication gates.

Official Windows binary distribution is gated on the final ProTrail product
icon being supplied and accepted
(`FINAL_BINARY_RELEASE_BLOCKED: USER_PRODUCT_ICON_PENDING`, see
[RELEASE_BLOCKERS.md](RELEASE_BLOCKERS.md)). The generated tray cursor icon is a
development fallback, not approved final branding.

### Packaging

The planned binary release is a portable archive,
`ProTrail-v<VERSION>-win-x64-portable.zip`, plus `SHA256SUMS.txt`. One command
from a clean checkout builds, tests, stages, verifies and packages it:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\release\package.ps1
```

The pipeline runs a fresh `/W4 /WX` Release build and the complete CTest suite,
deploys the Qt runtime with `windeployqt` and the MSVC runtime app-locally, and
proves the package's dependency closure. It then smokes the staged package
under a system-only `PATH` from plain, spaced, nested and Unicode directories,
writes a deterministic ZIP and its SHA-256, and smokes a fresh extraction of
that ZIP. Official mode refuses a dirty tree and a missing or unapproved icon;
`-Rehearsal` runs the same gates without those two and names every output
`...-REHEARSAL` so it cannot be mistaken for a release artifact.

## Contributing and security

See [CONTRIBUTING.md](CONTRIBUTING.md) for the build/test workflow and
[SECURITY.md](SECURITY.md) for private vulnerability reports. Licensing is
intentionally undecided until a license is selected; no license is implied by
this repository.
