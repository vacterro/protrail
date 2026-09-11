@echo off
REM Fail-closed: propagate first real failure. Never mask build errors.
setlocal
call "S:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=C:\Qt\6.8.0\msvc2022_64\bin;S:\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"

if not exist build\CMakeCache.txt (
    echo [build_t017] ERROR: build directory not configured. Run cmake configure first. 1>&2
    exit /b 1
)

cmake --build build --config Release -- -m 2>&1
if errorlevel 1 (
    echo [build_t017] FAILED: cmake build returned errorlevel %ERRORLEVEL% 1>&2
    exit /b 1
)
echo [build_t017] Release build succeeded.
exit /b 0
