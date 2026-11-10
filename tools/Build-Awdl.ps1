# Builds WinAWDL's awdld.exe with the portable MinGW that vcpkg already
# downloaded (no system-wide compiler install). Requires the OWL and radiotap
# submodules in upstream/winawdl/upstream/owl and patches\awdl-ipc-host.patch
# (tools/Apply-AwdlPatches.ps1). Always builds the IPC host backend
# (-DAWDL_HOST_IPC=ON) that talks to awdl_netd, so no Wintun is involved.
#
# Usage: powershell -ExecutionPolicy Bypass -File tools/Build-Awdl.ps1
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$PSDefaultParameterValues['Out-File:Encoding'] = 'utf8'
$root = Split-Path $PSScriptRoot -Parent
$repo = Join-Path $root 'upstream\winawdl'
$build = Join-Path $root 'build\awdl'

function Find-CMake {
    if ($env:CMAKE_EXE -and (Test-Path $env:CMAKE_EXE)) { return $env:CMAKE_EXE }
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $candidates = @(
        'D:\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    throw 'CMake not found. Set CMAKE_EXE or install the VS 2022 CMake component.'
}

$cmake = Find-CMake

$mingw = Get-ChildItem (Join-Path $root 'build\deps\vcpkg\downloads\tools\perl') -Directory -ErrorAction SilentlyContinue |
    ForEach-Object { Join-Path $_.FullName 'c\bin' } |
    Where-Object { Test-Path (Join-Path $_ 'gcc.exe') } |
    Select-Object -First 1
if (-not $mingw) { throw 'Portable MinGW not found under build/deps/vcpkg/downloads/tools/perl' }
if (-not (Test-Path (Join-Path $repo 'upstream\owl\radiotap\radiotap.c'))) {
    throw 'OWL/radiotap submodule missing; run git -C upstream/winawdl submodule update --init --recursive'
}
if (-not (Test-Path (Join-Path $repo 'awdl\win\host_if_ipc.c'))) {
    throw 'IPC host backend missing; run tools/Apply-AwdlPatches.ps1 first'
}

$env:PATH = "$mingw;$env:PATH"
$configure = @(
    '-S', (Join-Path $repo 'awdl'),
    '-B', $build,
    '-G', 'MinGW Makefiles',
    "-DCMAKE_C_COMPILER=$(Join-Path $mingw 'gcc.exe')",
    "-DCMAKE_MAKE_PROGRAM=$(Join-Path $mingw 'mingw32-make.exe')",
    '-DCMAKE_BUILD_TYPE=Release',
    # The option defaults OFF (Wintun backend); set it every time so a fresh
    # build directory produces the awdld the product chain needs.
    '-DAWDL_HOST_IPC=ON'
)
& $cmake @configure
if ($LASTEXITCODE -ne 0) { throw "awdld configure failed ($LASTEXITCODE)" }
& $cmake --build $build --parallel 8
if ($LASTEXITCODE -ne 0) { throw "awdld build failed ($LASTEXITCODE)" }

Write-Host "awdld: $(Join-Path $build 'awdld.exe')"
