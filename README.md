# ProTrail

**v0.1.0**

ProTrail is a native Windows desktop utility that renders configurable cursor
trails, click and press-and-hold effects, and motion wake geometry without ever
intercepting, delaying or swallowing a mouse click. It is a Qt 6 / C++20
application rendering through Direct2D and DirectComposition into per-monitor
click-through overlays.

## Current scope

Windows 10/11 only. MSVC (Visual Studio 2022) with Qt 6.8 is the supported
toolchain.

- **ProTrail home window** — a compact Essentials surface for the settings that
  get changed often: Master FX, Trail FX and style, Sparkle Mode, Click FX and
  style, Hold FX, Motion Wake, Wake Density, Hold Intensity and Start with
  Windows. It also carries `Advanced Settings...` and `Restore Defaults`.
- **Advanced Settings editor** — the complete Trail, Sparkle, Click, Hold,
  Motion Wake, color, trigger, animation, preset and persistence configuration.
  The home window is a focused frequent-use view over the same state; it is not
  a second copy of the editor.

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
- **Startup modes** — a manual launch opens the ProTrail home window. An
  autostart launch is tray-only: no home window, no advanced window, no taskbar
  button, no focus steal. A second manual launch hands an activation request to
  the running instance, which restores its existing home window.
- **Tray and lifecycle** — `Open ProTrail`, `Settings...`, `Enable`/`Disable`
  and `Exit`. Closing either window hides it to the tray and keeps ProTrail
  running; exit is an explicit tray action.

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

The suite registers 23 CTest tests — 21 test executables (trail, sparkles,
click, hold/wake, multi-monitor, scheduler, config, release defaults,
autostart, single instance, tray lifecycle, GUI, ProTrail home) plus two
release-defaults gate script tests, one of which deliberately proves the gate
can fail. GUI tests run headless through Qt's `offscreen` platform.

## Repository layout

| Path | Contents |
|------|----------|
| `src/` | Application source (app, config, core, effects, platform, render, ui) |
| `tests/` | Test executables registered with CTest |
| `resources/` | Canonical defaults source and its Qt resource |
| `cmake/` | Configure-time release-defaults validator |
| `ROADMAP/` | Planning, references and historical design notes |
| `ROADMAP/evidence/` | Retained per-ticket design/measurement write-ups |

## Release status

**Source-only release.** The source is available for anyone who wants to build it
themselves. No official Windows EXE, installer, portable archive,
package-manager binary or signed binary is published, and CI deliberately does
not publish binaries.

Official Windows binary distribution is intentionally deferred until the final
ProTrail product icon is supplied and accepted
(`FINAL_BINARY_RELEASE_BLOCKED: USER_PRODUCT_ICON_PENDING`, see
[RELEASE_BLOCKERS.md](RELEASE_BLOCKERS.md)). The generated tray cursor icon is a
development fallback, not approved final branding.

## Contributing and security

See [CONTRIBUTING.md](CONTRIBUTING.md) for the build/test workflow and
[SECURITY.md](SECURITY.md) for private vulnerability reports. Licensing is
intentionally undecided until a license is selected; no license is implied by
this repository.
