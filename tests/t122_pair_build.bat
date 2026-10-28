@echo off
REM T-122 regression pair: compile the fixed oracle (tests/test_sound_engine.cpp)
REM against whatever subject currently sits in src/audio/. The SAIPEN regression
REM engine swaps the pre-fix subject in from .saipen/evidence/pre/ for the red
REM run and keeps the repaired subject for the green run.
REM
REM The flag set mirrors the protrail_audio_tests Release target in
REM CMakeLists.txt (see build_t122_rel/protrail_audio_tests.vcxproj): a pair
REM built with different defines than the project builds would measure the
REM flags rather than the subject.
REM Usage: t122_pair_build.bat [output-dir]
setlocal enabledelayedexpansion
cd /d "%~dp0.." || exit /b 1
set "OUT=%~1"
if "%OUT%"=="" set "OUT=build_t122_pair"
if not exist "%OUT%" mkdir "%OUT%"
call "S:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 exit /b 1
set "QT=C:\Qt\6.8.0\msvc2022_64"
set "INC=%QT%\include"
for /d %%D in ("%QT%\include\*") do set "INC=%%D;!INC!"
set "INCLUDE=%QT%\mkspecs\win32-msvc;!INC!;%INCLUDE%"
set "DEFS=/DWIN32 /D_WINDOWS /DNDEBUG /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DQT_CORE_LIB /DQT_NO_DEBUG /D_ENABLE_EXTENDED_ALIGNED_STORAGE /DWIN64 /D_WIN64 /DUNICODE /D_UNICODE /DQT_MULTIMEDIA_LIB /DQT_GUI_LIB /DQT_NETWORK_LIB /DQT_TESTLIB_LIB"
cl /nologo /std:c++20 /Zc:__cplusplus /Zc:preprocessor /EHsc /utf-8 /W4 /WX /permissive- /MD ^
    !DEFS! ^
    tests\test_sound_engine.cpp ^
    src\config\audio_config.cpp ^
    src\audio\sound_engine.cpp ^
    src\platform\monotonic_clock.cpp ^
    src\audio\sound_backend.cpp ^
    src\audio\sound_decoder.cpp ^
    src\core\log.cpp ^
    /Fe:"%OUT%\t122_pair_test.exe" /Fo:"%OUT%\\" ^
    /link /LIBPATH:"%QT%\lib" Qt6Core.lib Qt6Multimedia.lib Qt6Gui.lib Qt6Network.lib shell32.lib ole32.lib
exit /b %ERRORLEVEL%
