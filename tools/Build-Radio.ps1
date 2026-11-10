[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'Prepare-Devourer.ps1')
if ($LASTEXITCODE -ne 0) { throw 'prepare devourer failed' }
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
$vcpkg = Join-Path $root 'build\deps\vcpkg'
& $cmake -S (Join-Path $root 'src\radio_usb') -B (Join-Path $root 'build\radio_usb') `
    -G 'Visual Studio 17 2022' -A x64 `
    "-DCMAKE_TOOLCHAIN_FILE=$vcpkg\scripts\buildsystems\vcpkg.cmake" `
    "-DPKG_CONFIG_EXECUTABLE=$vcpkg\installed\x64-windows\tools\pkgconf\pkgconf.exe" `
    '-DPKG_CONFIG_ARGN=--msvc-syntax'
if ($LASTEXITCODE -ne 0) { throw 'radio configure failed' }
& $cmake --build (Join-Path $root 'build\radio_usb') --config Release --parallel 8 `
    --target airbridge_radio radio_pipe_bridge peer_telemetry_test channel_completion_test
if ($LASTEXITCODE -ne 0) { throw 'radio build failed' }
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir (Join-Path $root 'build\radio_usb') `
    -C Release -R '^(peer_telemetry|channel_completion)$' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'radio offline regression failed' }
