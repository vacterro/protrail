@echo off
call "S:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d V:\___VAC\__K\__CODE\__MAIN__\ProTrail
rmdir /s /q build 2>nul
set "CTEST=S:\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
set "CMAKE=S:\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
"%CMAKE%" -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
if errorlevel 1 exit /b 1
"%CMAKE%" --build build --config Release -- /m /v:m
if errorlevel 1 exit /b 1
"%CMAKE%" --build build --config Debug -- /m /v:m
if errorlevel 1 exit /b 1
set PATH=C:\Qt\6.8.0\msvc2022_64\bin;%PATH%
echo === CTEST RELEASE ===
"%CTEST%" --test-dir build -C Release --output-on-failure --overwrite OMIT_DEPRECATED_TEST_OUTPUT=ON
if errorlevel 1 exit /b 1
echo === CTEST DEBUG ===
"%CTEST%" --test-dir build -C Debug --output-on-failure --overwrite OMIT_DEPRECATED_TEST_OUTPUT=ON
if errorlevel 1 exit /b 1
