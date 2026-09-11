@echo off
call "S:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=C:\Qt\6.8.0\msvc2022_64\bin;S:\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
cmake --build build --config Release -- -m 2>&1
if errorlevel 1 exit /b 1
cmake --build build --config Debug -- -m 2>&1
if errorlevel 1 exit /b 1
exit /b 0
