# ProTrail v0.1.1

> **Status: prepared, not yet released.** This is the text for the first
> official Windows binary release. It is not published until the final product
> icon is supplied and accepted
> (`FINAL_BINARY_RELEASE_BLOCKED: USER_PRODUCT_ICON_PENDING`, see
> [RELEASE_BLOCKERS.md](RELEASE_BLOCKERS.md)); this banner is removed in the
> release commit.

ProTrail v0.1.1 is the first Windows binary release of ProTrail, a native
desktop utility that renders configurable cursor trails, click and
press-and-hold effects and motion wake geometry in per-monitor click-through
overlays, without intercepting, delaying or swallowing a mouse click.

## Download

| Asset | Contents |
|-------|----------|
| `ProTrail-v0.1.1-win-x64-portable.zip` | Portable application: `protrail.exe`, the Qt 6.8 runtime and the MSVC runtime |
| `SHA256SUMS.txt` | SHA-256 of the portable archive |

- **Platform:** Windows 10 or newer, x64 only.
- **Package type:** portable archive. There is no installer.
- **Signing:** the executable is **not code-signed**. Windows SmartScreen may
  warn on first launch; verify the archive against `SHA256SUMS.txt` first.

## Install and use

1. Verify the download:
   `(Get-FileHash .\ProTrail-v0.1.1-win-x64-portable.zip -Algorithm SHA256).Hash`
   must equal the value in `SHA256SUMS.txt`.
2. Extract the archive to any folder you own, for example
   `%LOCALAPPDATA%\Programs\ProTrail`. Paths with spaces or non-ASCII
   characters are supported.
3. Run `protrail.exe`. ProTrail opens its window and places an icon in the
   notification area; closing the window hides it to the tray, and **Exit** in
   the tray menu quits.
4. Settings are stored per user in `%LOCALAPPDATA%\ProTrail\config.json`,
   with the log beside it. In normal operation nothing is written into the
   application folder (only if Windows cannot resolve the per-user
   application-data folder do config and log fall back to it).
5. **Start with Windows** (General tab) registers the executable you are
   running. If you move the folder, launch ProTrail once from the new location
   and the registration follows it. To remove ProTrail, turn Start with Windows
   off, exit, and delete the folder (and `%LOCALAPPDATA%\ProTrail` to drop your
   settings).

## What's in it

- One tabbed ProTrail window (General, Trail, Click) with live editing of
  Trail/Sparkle and Click/Hold/Motion Wake effects.
- Tray lifecycle: `Open ProTrail`, Enable/Disable, Exit; the tray icon shows the
  enabled/disabled state; a silent tray-only start when launched by Windows.
- Per-monitor DPI-aware Direct2D/DirectComposition rendering across multiple
  monitors, with an active-only render scheduler (no idle wake-ups).
- Schema-validated, atomically written configuration with safe recovery from
  a corrupt file; a single running instance, with a second launch restoring
  the existing window.

## Changes since v0.1.0

See [CHANGELOG.md](CHANGELOG.md). In short: Windows version and icon
resources, the verified portable packaging pipeline, and fixes for a leaked
developer path in Release binaries and for deployment smoke runs that could
touch the operator's real autostart entry.
