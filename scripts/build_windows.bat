@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%ROOT_DIR%\build-windows"
set "GENERATOR=Visual Studio 17 2022"
set "CONFIG=Release"

set "TOOLCHAIN_ARG="
set "GENERATOR_ARG="

if not "%~1"=="" set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=%~1"
if not "%~2"=="" set "GENERATOR=%~2"

echo Configuring with generator: %GENERATOR%
cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -G "%GENERATOR%" %TOOLCHAIN_ARG%
if errorlevel 1 goto :fail

cmake --build "%BUILD_DIR%" --config %CONFIG%
if errorlevel 1 goto :fail

echo.
echo Build complete.
echo Executable: "%BUILD_DIR%\%CONFIG%\voxel_tracer.exe"
pause
goto :eof

:fail
echo.
echo Build failed.
echo.
echo Common fixes:
echo   1. Install Visual Studio 2022 or Build Tools with C++ workload.
echo   2. Run this from a Developer Command Prompt for Visual Studio.
echo   3. If using vcpkg, pass the toolchain path as the first argument.
echo   4. Optional second argument overrides the generator, e.g. "Ninja".
echo.
echo Examples:
echo   scripts\build_windows.bat
echo   scripts\build_windows.bat C:\vcpkg\scripts\buildsystems\vcpkg.cmake
echo   scripts\build_windows.bat "" "Ninja"
pause
exit /b 1