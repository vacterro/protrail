@echo off
REM T-82 regression pair: compile the fixed oracle against the CURRENT subject
REM sources (the SAIPEN regression engine swaps the pre-fix subject in for the
REM red run and the repaired subject in for the green run).
REM Usage: t82_pair_build.bat [output-dir]
setlocal enabledelayedexpansion
cd /d "%~dp0.." || exit /b 1
set "OUT=%~1"
if "%OUT%"=="" set "OUT=build_t82_pair"
if not exist "%OUT%" mkdir "%OUT%"
call "S:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 exit /b 1
set "QT=C:\Qt\6.8.0\msvc2022_64"
set "INC=%QT%\include"
for /d %%D in ("%QT%\include\*") do set "INC=%%D;!INC!"
REM /I takes one directory per flag; the whole Qt module set goes through the
REM INCLUDE environment variable instead. The define set mirrors the
REM protrail_audio_tests Release target in CMakeLists.txt: without
REM WIN32_LEAN_AND_MEAN/NOMINMAX the Windows.h min/max function-like macros
REM break std::min/std::max in sound_engine.cpp.
set "INCLUDE=!INC!;%INCLUDE%"
set "DEFS=/DWIN32 /D_WINDOWS /DNDEBUG /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DWIN64 /D_WIN64 /DUNICODE /D_UNICODE /DQT_CORE_LIB /DQT_NO_DEBUG /DQT_MULTIMEDIA_LIB /DQT_GUI_LIB /DQT_NETWORK_LIB"
cl /nologo /std:c++20 /Zc:__cplusplus /Zc:preprocessor /EHsc /utf-8 /W4 /permissive- /MD ^
    !DEFS! ^
    tests\test_t82_pair.cpp ^
    src\config\audio_config.cpp ^
    src\audio\sound_engine.cpp ^
    src\audio\sound_backend.cpp ^
    src\audio\sound_decoder.cpp ^
    src\platform\monotonic_clock.cpp ^
    src\core\log.cpp ^
    /Fe:"%OUT%\t82_pair_test.exe" /Fo:"%OUT%\\" ^
    /link /LIBPATH:"%QT%\lib" Qt6Core.lib Qt6Multimedia.lib Qt6Gui.lib Qt6Network.lib shell32.lib ole32.lib
exit /b %ERRORLEVEL%
