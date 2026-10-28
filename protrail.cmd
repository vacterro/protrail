@echo off
setlocal
REM ProTrail single launcher.
REM Usage: protrail.cmd build|test|run|smoke|package|clean  [extra args]
REM
REM   build    Configure (once) and build Release + Debug
REM   test     Run the full CTest suite (Release + Debug)
REM   run      Launch the Release build
REM   smoke    Smoke-test a deployment dir: protrail.cmd smoke <dir>
REM   package  Run the official packaging pipeline
REM   package -Rehearsal  Run the rehearsal pipeline (no icon gate)
REM   clean    Remove build/ and Testing/ (dist/ release artifacts kept)
REM
REM Toolchain: PROTRAIL_VCVARS + PROTRAIL_QT via environment or a gitignored
REM protrail.local.cmd next to this script (PROTRAIL_CMAKE_BIN optional).

set "ROOT=%~dp0"
if exist "%ROOT%protrail.local.cmd" call "%ROOT%protrail.local.cmd"
set "VCVARS=%PROTRAIL_VCVARS%"
set "QT=%PROTRAIL_QT%"

set "CMD=%~1"
if "%CMD%"=="" goto usage
if "%VCVARS%"=="" goto toolchain
if "%QT%"=="" goto toolchain

call "%VCVARS%" >nul
if errorlevel 1 (
    echo [protrail] ERROR: cannot load "%VCVARS%" 1>&2
    exit /b 1
)
set "PATH=%QT%\bin;%PATH%"
if not "%PROTRAIL_CMAKE_BIN%"=="" set "PATH=%PROTRAIL_CMAKE_BIN%;%PATH%"

if /i "%CMD%"=="build"   goto build
if /i "%CMD%"=="test"    goto test
if /i "%CMD%"=="run"     goto run
if /i "%CMD%"=="smoke"   goto smoke
if /i "%CMD%"=="package" goto package
if /i "%CMD%"=="clean"   goto clean
goto usage

:build
if not exist "%ROOT%build\CMakeCache.txt" (
    cmake -S "%ROOT%." -B "%ROOT%build" -G "Visual Studio 17 2022" -A x64
    if errorlevel 1 exit /b 1
)
cmake --build "%ROOT%build" --config Release -- -m
if errorlevel 1 exit /b 1
cmake --build "%ROOT%build" --config Debug -- -m
if errorlevel 1 exit /b 1
echo [protrail] BUILD OK (Release + Debug)
exit /b 0

:test
echo === RELEASE ===
ctest --test-dir "%ROOT%build" -C Release --output-on-failure
if errorlevel 1 exit /b 1
echo === DEBUG ===
ctest --test-dir "%ROOT%build" -C Debug --output-on-failure
if errorlevel 1 exit /b 1
echo [protrail] TEST OK (Release + Debug)
exit /b 0

:run
start "" "%ROOT%build\Release\protrail.exe"
exit /b 0

:smoke
if "%~2"=="" (
    echo [protrail] smoke requires a deployment dir: protrail.cmd smoke ^<dir^> 1>&2
    exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%smoke_harness.ps1" -DeployDir "%~2" %3 %4 %5 %6 %7 %8 %9
exit /b %ERRORLEVEL%

:package
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\release\package.ps1" %2 %3 %4 %5 %6 %7 %8 %9
exit /b %ERRORLEVEL%

:clean
for %%d in (build Testing) do (
    if exist "%ROOT%%%d" (
        rmdir /s /q "%ROOT%%%d"
        echo [protrail] removed %%d
    )
)
exit /b 0

:usage
echo Usage: protrail.cmd build^|test^|run^|smoke^|package^|clean
echo   build            configure + build Release and Debug
echo   test             full CTest suite, Release and Debug
echo   run              launch Release protrail.exe
echo   smoke ^<dir^>      smoke-test a deployment dir via smoke_harness.ps1
echo   package          official packaging pipeline (package.ps1)
echo   package -Rehearsal   rehearsal packaging without the icon gate
echo   clean            remove build/ and Testing/ (dist kept)
exit /b 1

:toolchain
echo [protrail] ERROR: toolchain not configured. Set PROTRAIL_VCVARS and PROTRAIL_QT, or create protrail.local.cmd (gitignored) beside this script setting them. 1>&2
exit /b 1
