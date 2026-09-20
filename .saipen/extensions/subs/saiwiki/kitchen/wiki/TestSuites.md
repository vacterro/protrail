<!-- mirrors: CMakeLists.txt CTest targets rows 1-18 sha256:452b1744c11b1d43 -->
<!-- projection: row = "<registration ordinal>|<add_test NAME token>" -->

# CTest targets (1 .. 18)

Mirror of the registered CTest targets, rebuilt **by ID**. The numeric ID here
is the registration ordinal; the **NAME** is the identity that matters and the
one to cite. Ordinals shift if a target is inserted; names do not.

| # | Target | Source |
|---|---|---|
| 1 | protrail_tests | tests/test_log.cpp |
| 2 | protrail_history_tests | tests/test_cursor_history.cpp |
| 3 | protrail_timestamp_tests | tests/test_timestamp.cpp |
| 4 | protrail_button_flags_tests | tests/test_button_flags.cpp |
| 5 | protrail_input_dispatch | tests/test_input_dispatch.cpp |
| 6 | protrail_trail_tests | tests/test_trail_effect.cpp |
| 7 | protrail_stroke_policy_tests | tests/test_trail_stroke_policy.cpp |
| 8 | protrail_click_tests | tests/test_click_bubble_effect.cpp |
| 9 | protrail_hold_wake_tests | tests/test_hold_wake.cpp |
| 10 | protrail_gui_tests | tests/test_settings_window.cpp |
| 11 | protrail_config_tests | tests/test_config.cpp |
| 12 | protrail_scheduler_tests | tests/test_render_scheduler.cpp |
| 13 | protrail_multimonitor_tests | tests/test_multi_monitor.cpp |
| 14 | protrail_tray_tests | tests/test_tray.cpp |
| 15 | protrail_trail_lifecycle_tests | tests/test_trail_lifecycle.cpp |
| 16 | protrail_sparkle_tests | tests/test_trail_sparkles.cpp |
| 17 | protrail_autostart_tests | tests/test_autostart.cpp |
| 18 | protrail_single_instance_tests | tests/test_single_instance.cpp |

## The intrusive target

`protrail_input_dispatch` (#5) is the one target that injects synthetic
input into the real desktop. It is a legitimate CTest target and it is
registered like every other, but day-to-day verification runs exclude it and
require explicit authorization to include it:

```
ctest --test-dir <build> -C Release --output-on-failure -E protrail_input_dispatch
```

Under that exclusion the suite is **17/18**, not 18/18. A report claiming
17 of 18 without saying the intrusive target was excluded is ambiguous
about which suite actually ran.

## The autostart target

`protrail_autostart_tests` (#17) was added with the silent-autostart work and
is a non-intrusive target, so it runs in the ordinary suite. It drives the
autostart platform component through an in-memory registry backend and reads
the real per-user Run key before and after the run, which is what proves a
routine CTest pass cannot enroll the machine.

## Sources

- `CMakeLists.txt` (`add_test(NAME ...)` registration order, target-to-source `target_sources`)
- `tests/` (the source files themselves)
