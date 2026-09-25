@echo off
REM Fail-closed deployment smoke: any missing condition fails with non-zero exit.
REM Usage: deploy.bat <deployment dir> [extra smoke_harness.ps1 arguments]
REM Checks (delegated to smoke_harness.ps1):
REM   A. Process launch / liveness (SMOKE_RUNNING_OK)
REM   B. Application-controlled graceful shutdown via PROTRAIL_SMOKE_AUTO_EXIT_MS
REM   C. Natural termination without taskkill /F
REM   D. Zero orphan processes launched from THIS deployment
REM   E. Runtime log evidence confirming Application::shutdown() executed
REM   F. Production config, log and Start with Windows entry left untouched
REM Forced cleanup is used ONLY for processes this smoke launched, only as
REM emergency cleanup on failure, and explicitly emits FORCED_CLEANUP_USED.
REM An operator's own running ProTrail is never terminated.

setlocal enabledelayedexpansion
set "DEPLOY_DIR=%~1"
if "%DEPLOY_DIR%"=="" (
    echo SMOKE_FAIL_USAGE: deploy.bat ^<deployment dir^> 1>&2
    exit /b 1
)
shift
set "EXTRA_ARGS="
:collect
if "%~1"=="" goto run
set "EXTRA_ARGS=!EXTRA_ARGS! %1"
shift
goto collect

:run
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0smoke_harness.ps1" -DeployDir "%DEPLOY_DIR%" !EXTRA_ARGS!
exit /b %ERRORLEVEL%
