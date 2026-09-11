# MVP 00 - Project Bootstrap

Status: CLOSED

## Goal

Create a clean, buildable native Windows project foundation.

## Scope

Create:

- CMake project;
- C++20-or-newer build;
- Win32 application entry point;
- Qt 6 Widgets integration;
- Direct2D/DirectComposition link setup;
- basic application lifecycle;
- minimal logging;
- debug and release configurations.

The first executable may show only a minimal settings shell or stay in tray/debug mode.

## Required structure

Use a structure equivalent to:

```text
src/
  app/
  platform/
  core/
  render/
  effects/
  ui/
  persistence/
tests/
cmake/
assets/
```

Do not create empty folders merely to imitate this tree. Add directories as their first real code appears.

## Non-goals

Do not implement the trail yet.
Do not implement click effects yet.
Do not add particles.
Do not add profiles.

## Acceptance criteria

- Fresh clone/configure/build works on Windows x64 with MSVC.
- Release executable launches and exits cleanly.
- Qt UI integration is verified.
- Direct2D and DirectComposition dependencies link successfully.
- No administrator privilege is required.
- A short build section exists in the repository README.

## Validation

Record the exact CMake configure and build commands used.

## Close evidence

Append:

- build result;
- executable path;
- smoke-test result;
- known limitations.

---

## Milestone Close Record

Milestone: MVP 00 - Project Bootstrap
Status: CLOSED
Date: 2026-09-09
Agent/Model: opencode (SAIFREN)

## Implemented

- CMake project, C++20, MSVC x64, Debug + Release configurations.
- Win32 entry point (`wWinMain`) with `Application` lifecycle (init/run/shutdown).
- Qt 6.8 Widgets settings shell window.
- Direct2D factory + DirectComposition device creation verified at startup.
- Minimal logging to `%LOCALAPPDATA%\ProTrail\protrail.log` + stdout.

## Files changed

- `CMakeLists.txt` (new)
- `src/app/main.cpp`, `src/app/application.cpp`, `src/app/application.h` (new)
- `tests/test_log.cpp` (new)
- `README.md`, `.gitignore` (new)

## Validation performed

- Configure: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64` -> OK
- Build: `cmake --build build --config Release` and `--config Debug` -> OK (MSVC 19.44, cmake 3.31.6)
- Runtime: `build\Release\protrail.exe` launches, shows window, closes with exit code 0; log confirms D2D+DComp init and clean shutdown.

## Automated tests

- `protrail_tests.exe` (QtTest): 4 passed, 0 failed — Qt Widgets link + window lifecycle.

## Manual tests

- Launch + graceful close observed twice (19:57 and 20:05 local); exit code 0; log lines present.

## Performance observations

- Idle cost not yet measured; no render loop exists yet (by design for this milestone).

## Regressions checked

- None possible (first milestone, no prior baseline).

## Known limitations

- No trail rendering, no overlay, no tray, no persistence yet (later milestones).
- Qt runtime DLLs must be on PATH or copied next to the exe.
- ctest through the agent shell hangs (interactive GUI test); run `protrail_tests.exe` directly.

## Evidence

- Build log excerpts and runtime log `%LOCALAPPDATA%\ProTrail\protrail.log` (D2D+DComp verified, clean shutdown).
- Test output: `Totals: 4 passed, 0 failed`.
