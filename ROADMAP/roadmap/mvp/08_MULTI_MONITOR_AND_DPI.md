# MVP 08 - Multi-Monitor and DPI

Status: CLOSED

## Goal

Make ProTrail correct across the Windows virtual desktop.

## Preferred model

Use one overlay per monitor.

Reasons:

- per-monitor DPI;
- cleaner bounds;
- easier topology changes;
- easier resource recreation;
- reduced giant-surface complexity.

## Scope

Implement:

- monitor enumeration;
- per-monitor overlay creation;
- virtual-to-local coordinate conversion;
- per-monitor DPI handling;
- display topology refresh;
- monitor add/remove handling;
- monitor resolution change handling;
- trail continuity when crossing monitor boundaries.

## Windows events

Handle relevant display/DPI notifications such as:

- `WM_DISPLAYCHANGE`;
- `WM_DPICHANGED`;

Use additional platform APIs as required.

## Edge cases

Test if possible:

- monitor left of primary;
- monitor right of primary;
- monitor above/below primary;
- negative virtual coordinates;
- mixed scaling;
- different refresh rates;
- monitor disconnect/reconnect;
- primary monitor change.

## Acceptance criteria

- effects appear on the monitor containing the cursor;
- effects do not jump because of coordinate origin mistakes;
- crossing displays is visually continuous;
- topology change does not require restarting ProTrail;
- per-monitor overlay resources are recreated safely.

## Close evidence

- **User Visual Verification**: PASS on real three-monitor Windows configuration. User verified:
  - Trail and click bubbles render correctly on monitor 1, 2, and 3.
  - Continuous movement across all monitor seams with no visual coordinate jumps or frozen pixels.
  - Click-through transparency preserved across all display overlays.
  - Dynamic topology change (disconnect/reconnect secondary monitor) handled gracefully without process restart.
  - Mixed-DPI scaling preserves physical pixel invariant, cursor alignment, and bubble centering.
- **Machine Verification**:
  - Build: MSVC 2022 x64 /W4 /WX clean in both Release and Debug configurations.
  - CTest Release: 11/11 PASS (5.73s); CTest Debug: 11/11 PASS (29.07s).
  - Regression suites: `protrail_multimonitor_tests` 28/28 PASS (including all 4 R3 regressions: `transient_create_failure_is_retried_by_refresh`, `changed_monitor_recreate_failure_is_retried`, `replacement_hwnd_dpi_difference_snapshot_regression`, `newly_added_monitor_dpi_normalizes_snapshot`).
- **Packaging**: Self-contained Release package `artifacts/ProTrail-T013-Test-x64/` (windeployqt deployed, verified smoke test without development Qt PATH).

