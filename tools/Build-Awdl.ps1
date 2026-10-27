# Builds WinAWDL's awdld.exe with the portable MinGW that vcpkg already
# downloaded (no system-wide compiler install). Requires the OWL and radiotap
# submodules in upstream/winawdl/upstream/owl.
#
# Usage: powershell -ExecutionPolicy Bypass -File tools/Build-Awdl.ps1
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$PSDefaultParameterValues['Out-File:Encoding'] = 'utf8'
$root = Split-Path $PSScriptRoot -Parent
$repo = Join-Path $root 'upstream\winawdl'
$build = Join-Path $root 'build\awdl'
$cmake = 'D:\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

$mingw = Get-ChildItem (Join-Path $root 'build\deps\vcpkg\downloads\tools\perl') -Directory -ErrorAction SilentlyContinue |
    ForEach-Object { Join-Path $_.FullName 'c\bin' } |
    Where-Object { Test-Path (Join-Path $_ 'gcc.exe') } |
    Select-Object -First 1
if (-not $mingw) { throw 'Portable MinGW not found under build/deps/vcpkg/downloads/tools/perl' }
if (-not (Test-Path (Join-Path $repo 'upstream\owl\radiotap\radiotap.c'))) {
    throw 'OWL/radiotap submodule missing; run git -C upstream/winawdl submodule update --init --recursive'
}
if (-not (Test-Path (Join-Path $repo 'deps\wintun\wintun.dll'))) {
    throw 'Wintun missing; run upstream/winawdl/scripts/fetch-wintun.ps1'
}

$env:PATH = "$mingw;$env:PATH"
$configure = @(
    '-S', (Join-Path $repo 'awdl'),
    '-B', $build,
    '-G', 'MinGW Makefiles',
    "-DCMAKE_C_COMPILER=$(Join-Path $mingw 'gcc.exe')",
    "-DCMAKE_MAKE_PROGRAM=$(Join-Path $mingw 'mingw32-make.exe')",
    '-DCMAKE_BUILD_TYPE=Release'
)
& $cmake @configure
if ($LASTEXITCODE -ne 0) { throw "awdld configure failed ($LASTEXITCODE)" }
& $cmake --build $build --parallel 8
if ($LASTEXITCODE -ne 0) { throw "awdld build failed ($LASTEXITCODE)" }

Copy-Item (Join-Path $repo 'deps\wintun\wintun.dll') $build -Force
Write-Host "awdld: $(Join-Path $build 'awdld.exe')"
Write-Host "wintun: $(Join-Path $build 'wintun.dll')"
