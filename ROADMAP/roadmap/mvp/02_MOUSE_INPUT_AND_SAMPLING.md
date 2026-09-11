# MVP 02 - Mouse Input and Cursor Sampling

Status: CLOSED

## Goal

Provide reliable global cursor movement and click events without blocking or altering user input.

## Scope

Implement:

- Raw Input registration for mouse activity;
- cursor position sampling through `GetCursorPos`;
- high-resolution timestamps;
- normalized mouse button events;
- cursor sample structure;
- bounded cursor history;
- unit tests for history pruning and timestamp ordering.

Recommended model:

```text
WM_INPUT
  |
  v
MouseInput
  |
  v
GetCursorPos
  |
  v
CursorSample
  |
  v
CursorHistory
```

Use Raw Input as the activity/event source.

Use actual screen coordinates for rendering state.

## Cursor sample data

At minimum:

- timestamp;
- virtual screen x/y;
- button transition when applicable.

Velocity may be derived later from adjacent samples.

## Constraints

- Do not move the system cursor.
- Do not suppress input.
- Do not inject input.
- Do not use a low-level global hook unless measured testing proves Raw Input cannot satisfy a required case.

## Edge cases

Handle:

- duplicate positions;
- very high event rates;
- stationary clicks;
- virtual-screen negative coordinates;
- quick click sequences.

## Acceptance criteria

- Movement samples arrive while another application is foreground.
- Left/right/middle click transitions are detected globally.
- Stored history remains bounded.
- Timestamps are monotonic.
- No visible input lag is introduced.
- Shutting down input registration is clean.

## Close evidence

Record test event rates and any hardware-specific observations.

---

### Milestone Close Record

Milestone: MVP 02 - Mouse Input and Cursor Sampling
Status: CLOSED
Date: 2026-09-09
Agent/Model: opencode (SAIFREN)

#### Architecture

- Input layer: `src/platform/mouse_input.{h,cpp}` owns a dedicated message-only HWND
  (`HWND_MESSAGE`) registered with `RIDEV_INPUTSINK`, so ProTrail receives `WM_INPUT`
  while another window is foreground and never steals focus from the overlay or the
  application underneath. Raw Input is the activity/button-event source only; the
  authoritative cursor position comes from `GetCursorPos()` (virtual-screen physical
  pixels) on every event. The input layer does NOT depend on trail rendering or
  `CursorHistory` internals (wired via an injected sink callback).
- `CursorSample` (`src/core/cursor_sample.h`): `{ timestamp_ns, x, y, button, action }`
  where `timestamp_ns` is `QueryPerformanceCounter`-derived nanoseconds (monotonic,
  never wall-clock) and x/y are virtual-screen physical pixels (may be negative on
  multi-monitor layouts whose left/top monitor is left of/above primary).
- `CursorHistory` (`src/core/cursor_history.{h,cpp}`): bounded deque (default 512),
  default no age pruning. Pushes coalesce duplicate stationary movement into the
  newest sample (newest timestamp wins); button transitions are never coalesced away.
  Out-of-order timestamps are clamped to preserve ordering. Generic `push`,
  `prune_before`, `prune`, `size`, `at`, `last`, `samples`, `clear` ops expose what
  the future trail will consume.

#### Raw Input flags

- `RAWINPUTDEVICE{ usUsagePage = 0x01 (HID generic), usUsage = 0x02 (mouse),
  dwFlags = RIDEV_INPUTSINK, hwndTarget = message-only window }`.
- Cleanup: re-register with `RIDEV_REMOVE` (`hwndTarget = nullptr`) on shutdown.

#### Timestamp strategy

- `now_ns()` in `mouse_input.cpp`: `QueryPerformanceCounter()` scaled by the cached
  QPC frequency, split into seconds + remainder to avoid int64 overflow on long
  uptimes. Monotonic, high-resolution, Windows-backed.

#### Logging / performance

- Normal operation performs NO per-sample disk logging (the log sink opens the file
  per `log_write`; per-event writes would wreck latency). With `PROTRAIL_INPUT_DIAG=1`
  a single rate-limited summary line is emitted once per second
  (`<events>/s <btn>/s last=(x,y) lastbtn=<L|R|M><d|u>`). Observed: 40 lines over a
  40s run, working set stayed ~46 MB under sustained 276-770 events/s input —
  history is bounded, no unbounded allocation.

#### Automated tests

- `tests/test_cursor_history.cpp` (target `protrail_history_tests`): 11/11 PASS —
  insertion ordering, monotonic timestamp clamping, max-size pruning, age pruning,
  duplicate-movement coalescing, stationary-click preservation (button not dropped),
  rapid button transitions, negative virtual-screen coordinates, degenerate
  zero-max-samples fallback.
- `protrail_tests` (bootstrap): 4/4 PASS. Both Release and Debug configs green.

#### Verification evidence

- Build: Release + Debug green (MSVC 19.44).
- Runtime log (`%LOCALAPPDATA%\ProTrail\protrail.log`):
  `mouse_input: raw input registered (RIDEV_INPUTSINK, message-only window)` and
  `mouse input layer started (raw input + GetCursorPos sampling)`.
- Input received while ProTrail was NOT foreground: during all runs the agent console
  was foreground and `mouse_input diag:` reported sustained 276-770 events/s — proves
  global capture independent of foreground.
- Normalized button transitions observed: a 3-click burst (L/R/M down+up = 6
  transitions) produced a `6 btn/s` window; click pass-through to the underlying
  window was confirmed by `tests/hittest_probe.ps1` (all probed points owned by the
  underlying apps, with and without `PROTRAIL_DIAG=1`).
- Clean shutdown: WM_CLOSE to the settings window -> `ProTrail shutting down`,
  `overlay: resources released`, `mouse_input: raw input unregistered`, exit 0.
- Click-through regression: `tests/hittest_probe.ps1` still resolves underlying
  windows at the red-square / blue-bar / empty points (no overlay ownership).

#### Known limitations

- `taskkill /PID` (WM_CLOSE broadcast to top-level windows) does not always drive
  Qt's last-window-closed quit path while the overlay is present; closing the
  settings window (X button or WM_CLOSE to that HWND) exits cleanly. Manual exit via
  the UI is unaffected.
- `CursorHistory` is single-threaded by design (samples produced/consumed on the
  message thread that owns the raw-input window); a future render thread must cross
  via a bounded queue (per ARCHITECTURE.md).
- No DPI/explicit per-monitor coordinate transform is applied; `GetCursorPos` returns
  physical pixels under Qt6 per-monitor DPI awareness (MVP 08 owns DPI).
- Wheel and extra buttons are intentionally ignored (extensible later).

#### Next milestone

MVP 03 - Basic Trail.
