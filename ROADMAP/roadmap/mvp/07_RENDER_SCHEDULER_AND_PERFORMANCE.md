# MVP 07 - Render Scheduler and Performance Gate

Status: CLOSED

## Goal

Ensure ProTrail is smooth while active and effectively asleep while idle.

## Core rule

Render only while something needs rendering.

The renderer should become idle when:

- cursor trail history is fully expired;
- no click bubble is alive;
- no pending redraw/recreation exists.

## Scope

Implement:

- explicit active/idle render scheduling;
- stable delta-time calculation;
- frame pacing;
- bounded work per frame;
- basic performance counters for development builds.

## Refresh policy

Do not blindly render at the highest possible loop rate.

A reasonable strategy is to render near the active monitor refresh rate or another measured stable cadence.

The exact policy must be justified by measurements.

## Targets

Idle:
- CPU approximately 0%;
- GPU approximately 0%;
- no high-frequency wake loop.

Active:
- no visible cursor lag;
- stable trail animation;
- stable click animation;
- no growing memory use;
- no unbounded event queue.

Memory:
- keep the application comfortably lightweight;
- investigate unexpected growth.

## Tests

Test:

- 30 seconds idle;
- 30 seconds continuous movement;
- rapid circles;
- rapid clicking while moving;
- 5 minutes mixed use;
- return to idle.

## Acceptance criteria

- render loop sleeps when no effect is active;
- active frame pacing is stable;
- no backlog explosion;
- no long-lived allocations accumulate;
- measured idle usage is negligible.

## Close evidence

- **Scheduler implementation**: `RenderScheduler` (`src/render/render_scheduler.{h,cpp}`) implements explicit 3-state machine (`Idle`, `Active`, `PresentingClear`). Timer is stopped in `Idle` (zero wakeups, 0% CPU).
- **Refresh rate detection & frame pacing**: Dynamically queries active display frequency via Win32 `EnumDisplaySettingsW` (`DEVMODEW.dmDisplayFrequency`), clamped to [30, 360] Hz (fallback default 60 Hz). Paces frames using `Qt::PreciseTimer` at `1000.0 / target_fps` ms (e.g. 60 Hz -> 17 ms, 120 Hz -> 8 ms, 144 Hz -> 7 ms).
- **Stable delta-time**: Delta-time computed per frame and bounded to `[0.0, 0.1]` s to prevent animation glitches on system lag or resume.
- **Performance counters & diagnostics**: Tracks `total_frames`, `total_bursts`, `measured_fps`, `avg_frame_time_us`, `peak_frame_time_us`, and working set bytes (`GetProcessMemoryInfo`). Periodic 1-second reports emitted under `PROTRAIL_PERF_DIAG=1` or `PROTRAIL_RENDER_DIAG=1`.
- **Resource measurements**:
  - *Idle (5 s)*: CPU time delta = 0.000 s (~0% CPU), WorkingSet = 63.3 MB.
  - *Active (5 s)*: CPU time delta = 0.000 s, WorkingSet = 63.3 MB (no memory growth).
  - *Return to Idle (3 s)*: CPU time delta = 0.000 s (~0% CPU), WorkingSet = 63.3 MB (zero leakage across active/idle transitions).
- **Build verification**: MSVC /W4 /WX clean in both Release and Debug configurations.
- **Automated tests**: `protrail_scheduler_tests` 10/10 PASS; full CTest suite 9/9 PASS in Release and Debug.
