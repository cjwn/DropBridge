# Builds the read-only libusb probe (tools/usb_probe/usb_probe.c) against the
# project-isolated libusb install. No hardware access happens here.
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$PSDefaultParameterValues['Out-File:Encoding'] = 'utf8'
$root = Split-Path $PSScriptRoot -Parent
$source = Join-Path $root 'tools\usb_probe'
$build = Join-Path $root 'build\usb_probe'
$vcpkgRoot = Join-Path $root 'build\deps\vcpkg'
$installed = Join-Path $vcpkgRoot 'installed\x64-windows'

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

if (-not (Test-Path (Join-Path $installed 'lib\libusb-1.0.lib'))) {
    throw 'Dependencies missing. Run tools/Setup-BuildDeps.ps1 first.'
}

$cmake = Find-CMake
$pkgconf = Join-Path $installed 'tools\pkgconf\pkgconf.exe'
$toolchain = Join-Path $vcpkgRoot 'scripts\buildsystems\vcpkg.cmake'

$env:VCPKG_ROOT = $vcpkgRoot
$env:PKG_CONFIG_PATH = Join-Path $installed 'lib\pkgconfig'
$env:PKG_CONFIG_ARGN = '--msvc-syntax'

$configureLog = Join-Path $root 'evidence\usb-probe-configure.log'
$buildLog = Join-Path $root 'evidence\usb-probe-build.log'

function Invoke-Native {
    param([string]$Exe, [string[]]$Arguments, [string]$Log)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $Exe @Arguments *> $Log
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    return $code
}

$configure = @(
    '-S', $source,
    '-B', $build,
    '-G', 'Visual Studio 17 2022', '-A', 'x64',
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    '-DVCPKG_TARGET_TRIPLET=x64-windows',
    "-DPKG_CONFIG_EXECUTABLE=$pkgconf"
)

Write-Host 'Configuring usb_probe...'
$code = Invoke-Native -Exe $cmake -Arguments $configure -Log $configureLog
Get-Content $configureLog -Tail 10
if ($code -ne 0) { throw "Configure failed (exit $code); log: $configureLog" }

Write-Host "Building usb_probe ($Configuration)..."
$code = Invoke-Native -Exe $cmake -Arguments (@('--build', $build, '--config', $Configuration)) -Log $buildLog
Get-Content $buildLog -Tail 10
if ($code -ne 0) { throw "Build failed (exit $code); log: $buildLog" }

Write-Host ''
$probe = Join-Path $build "$Configuration\usb_probe.exe"
$dll = Join-Path $installed 'bin\libusb-1.0.dll'
if (Test-Path $dll) {
    Copy-Item -LiteralPath $dll -Destination (Join-Path $build $Configuration) -Force
}
Write-Host ("Probe: {0}" -f $probe)
