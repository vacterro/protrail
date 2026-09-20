<!-- mirrors: BOARD.md rows 1-42 sha256:2d4faad8113f423c -->
<!-- projection: row = "<ticket number>|<exact BOARD title segment>" -->
<!-- title segment = the BOARD line text between "] " and the first " | " -->

# Tickets (T-001 .. T-042)

Mirror of the main project's canonical ticket ledger, rebuilt **by ID**.

The digest above covers the ID range this page claims, over the `number|title`
projection named in the second comment. Equal row counts are not the test:
editing a mirrored row's title breaks the digest, adding a new ticket above the
range does not. A page rebuilt by POSITION instead of by ID is the defect this
marker exists to catch.

Every row below is the exact BOARD title segment. Core's own board compaction
truncates long titles with a trailing `...` and externalizes the full original
record to `detail_ref`; rows in that state are marked `compacted` and the
pointer is listed in the provenance column rather than the prose being
re-transcribed here (re-transcribing would create a second source of truth).

## Ledger

| ID | State | Pri | Canonical title | Provenance |
|---|---|---|---|---|
| T-001 | DONE | -- | CMake project skeleton, C++20 MSVC toolchain, debug/release configs | -- |
| T-002 | DONE | -- | Win32 application entry point, basic app lifecycle, minimal logging | -- |
| T-003 | DONE | -- | Qt 6 Widgets integration, minimal settings shell window | -- |
| T-004 | DONE | -- | Direct2D + DirectComposition link setup and runtime init check | -- |
| T-005 | DONE | -- | README build section, smoke-test result, milestone close evidence | -- |
| T-006 | DONE | -- | MVP 01 Native overlay: click-through, non-activating, transparent per-monitor overlay window | -- |
| T-007 | DONE | -- | MVP 02 Mouse input + sampling: raw-input (RIDEV_INPUTSINK, message-only HWND) + GetCursorPos -> bounded CursorHistory (512), QPC timestamps, coalesced duplicates, preserved button transitions | -- |
| T-008 | DONE | -- | MVP 03 Basic Trail: render cursor trail from CursorHistory samples; smoothing contract repaired (T-008A): continuous 0..1 smoothing (0=polyline .. 1=full centripetal curve), regression tests prove 0/0.25/0.5/1.0 differ, corner bounded, no NaN, default 0.5 appearance unchanged | -- |
| T-009 | DONE | -- | MVP 04 Click Bubble: ClickBubbleEffect + spawn on ButtonAction::Down + shared active-only scheduler | -- |
| T-010 | DONE | -- | MVP 05 Live Settings GUI: SettingsWindow (General/Trail/Click) + Golden Default theme + interactive color swatches (QColorDialog) + per-effect live config + scheduler master-disable/clear | -- |
| T-011 | DONE | -- | MVP 06 Configuration and Persistence: durable settings storage (%LOCALAPPDATA%), JSON serialization, schema validation, atomic write, safe fallback; master toggle decoupled from child prefs across restart | -- |
| T-012 | DONE | -- | MVP 07 Render Scheduler and Performance: frame pacing near monitor refresh rate, delta-time calculation, bounded per-frame work, performance diagnostics (PROTRAIL_PERF_DIAG=1) | -- |
| T-013 | DONE | -- | MVP 08 Multi-Monitor and DPI: per-monitor overlay architecture, explicit PER_MONITOR_AWARE_V2 DPI awareness, physical-pixel transform, deferred topology reconciliation, R3 retry/recreate/DPI-normalization regression suites green (28/28 PASS), Release/Debug CTest 11/11 PASS, deployed artifact smoke tested | source_receipts: SRC-001 |
| T-014 | DONE | -- | MVP 09 Tray: tray icon implementation, hide-on-close lifecycle, tray context menu (Settings, Enable/Disable, Exit), canonical master state sync, clean shutdown on Exit complete; 12/12 CTest PASS (Release 5.14s, Debug 29.42s), deployed artifact smoke verified; clean explicit Exit, zero orphan process | -- |
| T-015 | DONE | -- | Post-MVP V1 Colors & Quick Presets: dual trail colors (Start/Fade), 14-swatch palettes, custom QColorDialog, trail color modes (Full, Start only, Fade only, Gradient), click 14-swatch palette, 6 quick presets (Classic, Fire, Ice, Neon, Toxic, Violet), backward-compatible config migration; non-goals: glow/comet/neon/particles/shaders/preview | -- |
| T-016 | DONE | -- | Post-MVP V2 Trail Effects: 8 trail styles (Classic, Soft Glow, Comet, Neon, Dotted, Pulse, Ribbon, Spark), glow strength + segment spacing parameters, style-aware effect math + renderer stroke policy, schema v3 persistence; non-goals: shaders/particles/preview | -- |
| T-017 | DONE | -- | Post-MVP V3 Click Effects & T-017R1/R2 Integrity Repairs: 7 click styles, particle amount, schema v4 persistence, click RGB 0..255->0..1 normalization, test persistence isolation (sentinel intact, 0 production mutation), deployed smoke state isolation (PROTRAIL_SMOKE_STATE_DIR, fail-closed, isolated log/config, zero user state mutation), exact exit log assertions, shutdown completion marker, deploy process exit-code 0 verification via smoke_harness.ps1, zero forced kill; Release+Debug /W4 /WX 0 err 0 warn, CTest 12/12 both configs, TestTray 18/18 PASS; deployed smoke PASS (exit 0, 0 orphans, zero prod touch) | -- |
| T-018 | DONE | -- | Post-MVP V4 System Stability, Device Resilience, and Settings Predictability (R1 bounded renderer recovery + events-before-mutex single-instance protocol; R2 real-mutex... | compacted |
| T-019 | DONE | -- | Smooth Continuous Default Trail: continuous-stroke cap policy (cached flat-flat/flat-round/round-round stroke styles), one-segment deferred flush in OverlayWindow (no per-segment alloc), same policy on SoftGlow/Neon outer passes, Dotted/Spark keep round stubs, defaults smoothing 0.75 + fade Smooth (yellow/Full kept), Phase 8 cap-policy regressions, multimon culling preserved | -- |
| T-020 | DONE | -- | Settings UI Consolidation (R1 selector-state + UI predictability repair; recovered as the active UI ticket): exclusive selector buttons replace combo boxes, explicit... | compacted |
| T-021 | DONE | -- | Deterministic Trail Sparkles (HIGH effort; Phase 0 = trail enable-lifecycle freshness DONE + DRIFT RECOVERY: the in-tree sparkle implementation advanced past the E-115... | compacted |
| T-022 | DONE | -- | Elemental Click VFX (HIGH effort): four new elemental click styles on top of the accepted T-017 click-effect architecture -- Air / Fire / Water / Earth, added as ClickStyle... | compacted |
| T-23 | DONE | P1 | World-Anchored Trail Shards: additive TrailSparkleMode::Shards with Triangle geometry, deterministic world-anchored drift/rotation, UI/config/tests/package; stop at VERIFY... | compacted |
| T-24 | DONE | P1 | Click HOLD effects: press-and-hold on any enabled mouse button must produce its own continuous, style-aware VFX distinct from the one-shot down bubble (charg... | compacted |
| T-25 | DONE | P1 | Burst/SparkBurst per-click randomization: click_bubble_effect.cpp:371 spaces particles at exactly 2*pi*i/n and derives jitter only from the particle index, s... | compacted |
| T-26 | DONE | P1 | Hold Motion Wake / world-space style emissions: while an ActiveHold moves, continuously shed style-specific emissions into WORLD SPACE. Each emission gets an immutable... | compacted |
| T-27 | DONE | P1 | Hold FX Controls: retire the T-24 one-toggle limitation and expose real artistic Hold controls without building a particle-editor zoo. Add a compact HOLD section under... | compacted |
| T-28 | DONE | P1 | ProTrail | -- |
| T-29 | DONE | P2 | Config persistence failures are silent | -- |
| T-30 | DONE | P3 | default_config_path silent relative fallback | -- |
| T-31 | DONE | P3 | stale scratch build logs | -- |
| T-32 | BLOCKED | P1 | Silent Windows autostart: per-user HKCU Run entry under a stable ProTrail-owned value name whose command is the fully quoted current executable path plus an explicit... | compacted |
| T-33 | TODO | P1 | Canonical Release Defaults system: one authoritative resources/release_defaults.json holding every persisted user-facing setting (Trail, Trail Style, sparkles, Click,... | compacted |
| T-34 | TODO | P2 | Developer Release Defaults panel in dev builds only: Capture Current Settings, Save as Dev Preset, Apply Dev Preset, Promote Current or Selected Preset to Release Defaults,... | compacted |
| T-35 | TODO | P1 | Default coverage regression: a deliberately non-default sentinel AppConfig carrying distinct legal values across every persisted field proves serialize then deserialize then release-default representation then apply-defaults preserves all semantic values, so a future developer who adds a persisted field and forgets Release Defaults, Restore Defaults, DEV capture/promote or validation gets a failing test instead of silent default drift | -- |
| T-36 | TODO | P1 | Advanced Motion Wake controls and movement-condition state: Wake Strength, Wake Size, Wake Spread, Speed Response, Minimum Motion Speed, Turn Accent and Stop Accent as... | compacted |
| T-37 | TODO | P1 | Main UI Essentials quick controls: a compact two-column Essentials surface on the Main window exposing Master FX, Trail FX, Trail Style, Sparkle Mode, Click FX, Click... | compacted |
| T-38 | TODO | P2 | Malformed-config backup failure is silent and can lose the user file: src/config/config_storage.cpp load_from_file lines 407-409 builds the .corrupt backup with std::filesystem::remove(corrupt_path, ec) followed by std::filesystem::rename(path, corrupt_path, ec) and never inspects ec, so a failed backup (locked destination, denied permission, stale .corrupt that could not be removed) is logged as a successful backup while the original malformed config stays in place and is then overwritten with defaults by the next save; the recovery path must detect the failure, report it instead of claiming a backup it did not make, and not silently replace the user file | -- |
| T-39 | TODO | P2 | Autostart registry read failures are indistinguishable from not-registered and reconcile reports a false success: src/platform/autostart.cpp is_enabled line 274 and reconcile line 305 treat any failed backend read as the value being absent, so on an unreadable or denied per-user Run key reconcile returns true and Application::reconcile_autostart logs that the registered command was reconciled although nothing was verified or repaired; Win32RunKeyBackend already records last_error, so the manager must distinguish absent from failed and surface the failure | -- |
| T-40 | TODO | P2 | The new Start with Windows control has no GUI regression coverage: T-032 added chk_start_with_windows to the Settings General tab and the start_with_windows_changed signal, but tests/test_settings_window.cpp contains zero references to start_with_windows, so silent programmatic population, exactly-one-signal publication, the restore-all default reset and the no-recursion contract are all unverified and a future refactor can publish on load or double-publish without any test failing | -- |
| T-41 | TODO | P3 | Log directory creation failure is silent: src/core/log.cpp log_init line 116 calls std::filesystem::create_directories(g_path.parent_path(), ec) and discards ec while still setting g_initialized, so when the log directory cannot be created every later write silently no-ops and the product runs with no log and no report of why | -- |
| T-42 | TODO | P3 | Safety-critical single-instance signaling resets ignore their results: src/app/single_instance.cpp calls ResetEvent(ready_event_) and ResetEvent(shutdown_event_) without checking the result (lines 155-156 in claim, 192-193 in complete_takeover, 442 and 450 in release) while the same file documents these as safety-critical signaling resets that must be fail-closed under ownership, so a failed reset leaves a stale signaled readiness or a stale takeover beacon that a claimant would trust | -- |

## Counts

| State | Count | IDs |
|---|---|---|
| DONE | 31 | T-001 .. T-022, T-23 .. T-31 |
| TODO | 10 | T-33 .. T-42 |
| BLOCKED | 1 | T-32 |
| DOING | 0 | -- |

The ID space is contiguous: 1..42 with no gaps, which is the
invariant Core's own allocator guarantees. A missing ID here would mean the
mirror skipped a row, not that the ticket never existed.

## How to read a row

- **State** comes from the checkbox plus the section heading; `DONE` wins
  because a checked box is authoritative regardless of where the line sits.
- **Pri** is the canonical `[P#]` priority bracket. The older T-001..T-022 rows
  predate mandatory priorities and carry none, shown as `--`, not invented.
- **Provenance** lists which machine fields the row carries, by one stated
  rule rather than by hand: `compacted` first when the canonical title is
  truncated, then the remaining field names alphabetically, `verify` prose
  and the empty `needs` placeholder omitted, and a value shown for the two
  keys whose value is itself an identifier (`source_receipts`,
  `blocker_scope`). It is the one column that is neither the ID nor the
  canonical title, so it is derived rather than transcribed. `compacted`
  means Core externalized the full record; the pointer is under the
  project's own `recovery/board-compaction/` tree and is deliberate Core
  state, not a defect.

## Sources

- `.saipen/BOARD.md` (title segments, checkbox state, section headings, machine-field tails)
- `.saipen/recovery/board-compaction/` (existence only, for the compacted column)
