param(
    [string]$BuildDir = "build-windows",
    [string]$Generator = "Visual Studio 17 2022"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$AbsoluteBuildDir = Join-Path $RootDir $BuildDir

cmake -S $RootDir -B $AbsoluteBuildDir -G $Generator
cmake --build $AbsoluteBuildDir --config Release