@echo off
REM Scratch probe (saihunt kitchen, `_`-prefixed). Runs the non-intrusive CTest
REM subset and writes the transcript to a FILE, so a wedged test still leaves
REM evidence instead of nothing. The intrusive protrail_input_dispatch suite is
REM excluded, exactly as the project's own ctest_safe.bat excludes it.
REM
REM This exists because `cmd /c "set PATH=... && ctest ..."` from an agent shell
REM produced an interactive cmd waiting on stdin (no transcript, banner only) --
REM a tooling trap, not a product defect. A .bat avoids the quoting entirely.
setlocal
cd /d "%~dp0..\..\..\..\.." || exit /b 2
set "PATH=C:\Qt\6.8.0\msvc2022_64\bin;S:\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
set "OUT=.saipen\extensions\subs\saihunt\kitchen\_ctest_probe_release.txt"
echo === ctest Release, intrusive target excluded, per-test timeout 60s === > "%OUT%"
ctest --test-dir build -C Release -E protrail_input_dispatch --timeout 60 --output-on-failure >> "%OUT%" 2>&1
echo CTEST_EXIT=%ERRORLEVEL% >> "%OUT%"
exit /b 0
