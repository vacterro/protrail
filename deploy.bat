@echo off
REM Fail-closed deployment smoke: any missing condition fails with non-zero exit.
REM Checks:
REM   A. Process launch / liveness (SMOKE_RUNNING_OK)
REM   B. Application-controlled graceful shutdown via PROTRAIL_SMOKE_AUTO_EXIT_MS
REM   C. Natural termination without taskkill /F
REM   D. Zero orphan processes
REM   E. Runtime log evidence confirming Application::shutdown() executed
REM Forced cleanup (taskkill /F) is used ONLY as emergency cleanup on failure
REM and explicitly emits FORCED_CLEANUP_USED with non-zero exit.

setlocal enabledelayedexpansion
set "DEPLOY_DIR=%~1"
if "%DEPLOY_DIR%"=="" set "DEPLOY_DIR=artifacts\ProTrail-T017-Test-x64"
set "FULL_DEPLOY_DIR=%~dp0%DEPLOY_DIR%"
set "EXE=%FULL_DEPLOY_DIR%\protrail.exe"
set RC=0

if not exist "%EXE%" (
    echo SMOKE_FAIL_MISSING_EXE: %EXE% not found 1>&2
    exit /b 1
)

REM Required deployment files must exist
for %%F in (Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll platforms\qwindows.dll) do (
    if not exist "%FULL_DEPLOY_DIR%\%%F" (
        echo SMOKE_FAIL_MISSING_FILE: %%F 1>&2
        set RC=1
    )
)
if not "%RC%"=="0" exit /b %RC%

REM Clean slate: kill any pre-existing orphans from previous runs
%WINDIR%\System32\taskkill.exe /F /IM protrail.exe >nul 2>&1
%WINDIR%\System32\ping.exe -n 2 127.0.0.1 >nul

REM Verify no pre-existing orphan survived
%WINDIR%\System32\tasklist.exe /FI "IMAGENAME eq protrail.exe" | %WINDIR%\System32\find.exe /I "protrail.exe" >nul
if not errorlevel 1 (
    echo SMOKE_FAIL_PREEXISTING_ORPHAN: protrail.exe could not be killed before smoke test 1>&2
    exit /b 1
)

REM Delegate execution, isolated state directory, exit-code capture, and log sequence verification
REM to PowerShell smoke harness (T-017R2)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0smoke_harness.ps1" -DeployDir "%DEPLOY_DIR%"
exit /b %ERRORLEVEL%
