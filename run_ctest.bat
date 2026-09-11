@echo off
REM Fail-closed: propagate first real test failure. Never mask ctest errors.
setlocal
call "S:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=C:\Qt\6.8.0\msvc2022_64\bin;S:\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"

echo === RELEASE ===
ctest --test-dir build -C Release --output-on-failure 2>&1
if errorlevel 1 (
    echo [run_ctest] FAILED: Release ctest reported failures 1>&2
    exit /b 1
)

echo === DEBUG ===
ctest --test-dir build -C Debug --output-on-failure 2>&1
if errorlevel 1 (
    echo [run_ctest] FAILED: Debug ctest reported failures 1>&2
    exit /b 1
)

echo [run_ctest] ALL PASS (Release + Debug)
exit /b 0
