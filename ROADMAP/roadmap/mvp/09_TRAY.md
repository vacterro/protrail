# MVP 09 - Tray Integration and MVP Stop Gate

Status: CLOSED

## Goal

Turn ProTrail into a normal background desktop utility.

## Scope

Implement a system tray icon with:

- Enable/Disable ProTrail
- Open Settings
- Enable/Disable Trail
- Enable/Disable Click Effect
- Exit

Optional if trivial and reliable:

- Start with Windows

Do not expand this milestone into a full profile system.

## Window behavior

Closing the settings window should normally hide it while ProTrail continues running.

Exit must be explicit through the tray command or another intentional quit path.

## Startup behavior

The application should restore saved settings and start in a deterministic state.

## Acceptance criteria

- tray icon appears reliably;
- settings can be reopened;
- enable/disable works without restarting;
- exit terminates cleanly;
- no orphan process remains;
- overlay windows and renderer resources are destroyed;
- config is persisted;
- no admin rights are required.

# MVP STOP GATE

When this milestone closes:

STOP.

Do not begin files under `roadmap/future/`.

Prepare an MVP checkpoint report containing:

- closed milestone list;
- build status;
- test status;
- known defects;
- performance numbers;
- current screenshots;
- repository/archive for user review.

Future features require explicit user approval after this checkpoint.

## Close evidence

- **User Verification**: MANUAL-VERIFY RESULT: PASS
  - user confirmed tray workflow works as intended.
- **Machine Verification**:
  - Build: MSVC 2022 x64 /W4 /WX clean in both Release and Debug configurations (0 errors, 0 warnings).
  - CTest Release: 12/12 PASS (5.14s); CTest Debug: 12/12 PASS (29.42s).
  - Unit & Integration Suite: `protrail_tray_tests` 12/12 PASS:
    - tray icon creation and tooltip initialization;
    - Settings hide-on-close lifecycle (closing SettingsWindow hides rather than exits);
    - restore / show SettingsWindow from tray menu;
    - single tray instance invariant (no duplicate icons);
    - master enable/disable synchronization with overlay visibility and runtime config;
    - settings menu action toggle state reflection;
    - explicit Exit triggers clean Application shutdown;
    - overlay windows and renderer resources cleanly destroyed;
    - zero orphan process on shutdown;
    - repeated hide/show cycles stability;
    - configuration persistence round-trip across tray operations.
  - Smoke verification: deployed artifact smoke PASS without development Qt PATH (tray initialized, settings hidden on WM_CLOSE, process stays alive in tray, clean exit, 0 orphans).
- **Packaging**:
  - Executable: `artifacts/ProTrail-MVP-Test-x64/protrail.exe` (274944 B, SHA256: `3B14836FEEB3445FA09309399E6E1300359825021E5B2D3614B1522468BE0F7B`).
  - ZIP: `artifacts/ProTrail-MVP-Test-x64.zip` (11693576 B, SHA256: `E67E5956903D3F3CF5DEAEA5D91C4FA2D8CA1161B64742A09FB381282CDFF1A4`).
