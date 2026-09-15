@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%ROOT_DIR%\build-windows"

if not "%~1"=="" (
    set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=%~1"
) else (
    set "TOOLCHAIN_ARG="
)

cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" %TOOLCHAIN_ARG%
if errorlevel 1 goto :fail

cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 goto :fail

echo.
echo Build complete.
echo Executable: "%BUILD_DIR%\Release\voxel_tracer.exe"
goto :eof

:fail
echo.
echo Build failed.
exit /b 1