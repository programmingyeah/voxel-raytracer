@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%ROOT_DIR%\build-windows"
set "GENERATOR=MinGW Makefiles"

if not "%~1"=="" (
    set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=%~1"
) else (
    set "TOOLCHAIN_ARG="
)

echo Configuring with generator: %GENERATOR%
cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -G "%GENERATOR%" %TOOLCHAIN_ARG%
if errorlevel 1 goto :fail

cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 goto :fail

echo.
echo Build complete.
pause
echo Executable: "%BUILD_DIR%\Release\voxel_tracer.exe"
goto :eof

:fail
echo.
echo If build-windows only contains CMakeCache.txt and CMakeFiles, CMake configure may have succeeded but compilation failed.
pause
echo Build failed.
exit /b 1