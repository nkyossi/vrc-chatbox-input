# Builds VRChat Chatbox Input with Visual Studio 2022 (C++ workload), using the
# CMake and Ninja that come with Visual Studio.
#
#   powershell -ExecutionPolicy Bypass -File tools\build.ps1 [-Config Release|Debug] [-Test]
#
# The build tree goes to %LOCALAPPDATA%\vrc-chatbox-input-build so that it is
# not synced by OneDrive.

param(
    [ValidateSet('Release', 'Debug')] [string] $Config = 'Release',
    [switch] $Test
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with the C++ tools was not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'

$build = Join-Path $env:LOCALAPPDATA 'vrc-chatbox-input-build'
$cmd = "call `"$vcvars`" x64 >nul && " +
       "`"$cmake`" -S `"$root`" -B `"$build`" -G Ninja -DCMAKE_MAKE_PROGRAM=`"$ninja`" -DCMAKE_BUILD_TYPE=$Config && " +
       "`"$cmake`" --build `"$build`""
if ($Test) { $cmd += " && cd /d `"$build`" && `"$ctest`" --output-on-failure" }
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
