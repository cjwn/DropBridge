# Configures and builds the devourer radio backend for RTL8811CU/8821CU only,
# using the project-isolated dependencies from tools/Setup-BuildDeps.ps1.
# Writes configure/build/test logs under evidence/.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools/Build-Devourer.ps1
#   powershell -ExecutionPolicy Bypass -File tools/Build-Devourer.ps1 -Clean
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [string]$Generator = 'Visual Studio 17 2022',
    [switch]$Clean,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
# Windows PowerShell writes redirected output as UTF-16 by default; the
# evidence logs are meant to be readable and diffable as UTF-8.
$PSDefaultParameterValues['Out-File:Encoding'] = 'utf8'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build\devourer-8821c'
$vcpkgRoot = Join-Path $root 'build\deps\vcpkg'
$installed = Join-Path $vcpkgRoot 'installed\x64-windows'
$source = Join-Path $root 'upstream\devourer'
$evidence = Join-Path $root 'evidence'

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

if (-not (Test-Path $source)) { throw "Missing upstream checkout: $source" }
if (-not (Test-Path (Join-Path $installed 'lib\libusb-1.0.lib'))) {
    throw 'Dependencies missing. Run tools/Setup-BuildDeps.ps1 first.'
}

$cmake = Find-CMake
$ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
$pkgconf = Join-Path $installed 'tools\pkgconf\pkgconf.exe'
$toolchain = Join-Path $vcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
foreach ($path in @($cmake, $ctest, $pkgconf, $toolchain)) {
    if (-not (Test-Path $path)) { throw "Missing build tool: $path" }
}

# Everything below is inside this repository's git-ignored build directory.
if ($Clean -and (Test-Path $build)) {
    $resolved = (Resolve-Path $build).Path
    if (-not $resolved.StartsWith((Join-Path $root 'build'))) {
        throw "Refusing to delete outside build/: $resolved"
    }
    Remove-Item -Recurse -Force $resolved
}

$env:VCPKG_ROOT = $vcpkgRoot
$env:PKG_CONFIG_PATH = Join-Path $installed 'lib\pkgconfig'
# pkgconf emits MSVC /I and /libpath: flags instead of -I and -L.
$env:PKG_CONFIG_ARGN = '--msvc-syntax'

$configure = @(
    '-S', $source,
    '-B', $build,
    '-G', $Generator, '-A', 'x64',
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    '-DVCPKG_TARGET_TRIPLET=x64-windows',
    "-DPKG_CONFIG_EXECUTABLE=$pkgconf",
    '-DDEVOURER_JAGUAR1=OFF', '-DDEVOURER_8814=OFF',
    '-DDEVOURER_JAGUAR2_8822B=OFF', '-DDEVOURER_JAGUAR2_8821C=ON',
    '-DDEVOURER_JAGUAR3_8822C=OFF', '-DDEVOURER_JAGUAR3_8822E=OFF',
    '-DDEVOURER_8733B=OFF',
    '-DDEVOURER_KESTREL_8852B=OFF', '-DDEVOURER_KESTREL_8852C=OFF'
)

function Invoke-Native {
    param([string]$Exe, [string[]]$Arguments, [string]$Log)
    # Windows PowerShell turns native stderr into a terminating error when
    # ErrorActionPreference is Stop. Relax it for the call and judge the exit
    # code instead; the native output is already redirected to $Log.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $Exe @Arguments *> $Log
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    return $code
}

# Native tools write progress to stderr. Redirecting all streams to the log
# keeps PowerShell's ErrorActionPreference from turning a warning into a stop.
$configureLog = Join-Path $evidence 'build-8821c-configure.log'
$buildLog = Join-Path $evidence 'build-8821c-build.log'
$testLog = Join-Path $evidence 'build-8821c-ctest.log'

Write-Host 'Configuring devourer (RTL8821CU only)...'
$code = Invoke-Native -Exe $cmake -Arguments $configure -Log $configureLog
Get-Content $configureLog -Tail 12
if ($code -ne 0) {
    Get-Content $configureLog -Tail 60
    throw "CMake configure failed (exit $code); log: $configureLog"
}

Write-Host "Building ($Configuration)..."
$code = Invoke-Native -Exe $cmake -Arguments (@('--build', $build, '--config', $Configuration, '--parallel', '8')) -Log $buildLog
Get-Content $buildLog -Tail 15
if ($code -ne 0) {
    Get-Content $buildLog -Tail 80
    throw "Build failed (exit $code); log: $buildLog"
}

if ($SkipTests) {
    Write-Host 'Skipping ctest (-SkipTests).'
    exit 0
}

Write-Host 'Running ctest...'
$code = Invoke-Native -Exe $ctest -Arguments (@('--test-dir', $build, '-C', $Configuration, '--output-on-failure')) -Log $testLog
Get-Content $testLog -Tail 12
if ($code -ne 0) {
    Get-Content $testLog -Tail 80
    throw "ctest failed (exit $code); log: $testLog"
}

Write-Host ''
Write-Host "Build and selftests passed. Artifacts: $build"
