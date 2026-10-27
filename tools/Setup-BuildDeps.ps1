# Installs the Windows build dependencies for the devourer radio backend.
# Everything stays under build/deps (git-ignored). No global vcpkg integration,
# no PATH changes, no driver changes.
#
# Usage:  powershell -ExecutionPolicy Bypass -File tools/Setup-BuildDeps.ps1
[CmdletBinding()]
param(
    # Adds OpenSSL so the Windows station crypto selftests (supplicant,
    # station_sm, ccmp_framing) are built instead of skipped. sta_client_headless
    # is Linux-only because its data plane needs a TAP device.
    [switch]$WithOpenSsl
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$deps = Join-Path $root 'build\deps'
$vcpkgRoot = Join-Path $deps 'vcpkg'
$vcpkgExe = Join-Path $vcpkgRoot 'vcpkg.exe'

New-Item -ItemType Directory -Force -Path $deps | Out-Null

if (-not (Test-Path (Join-Path $vcpkgRoot '.git'))) {
    Write-Host "Cloning vcpkg into $vcpkgRoot (shallow; no global integration)..."
    git clone --depth 1 https://github.com/microsoft/vcpkg.git $vcpkgRoot
    if ($LASTEXITCODE -ne 0) { throw "vcpkg clone failed (exit $LASTEXITCODE)" }
}

if (-not (Test-Path $vcpkgExe)) {
    Write-Host 'Bootstrapping vcpkg...'
    cmd /c "`"$(Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat')`" -disableMetrics"
    if ($LASTEXITCODE -ne 0) { throw "vcpkg bootstrap failed (exit $LASTEXITCODE)" }
}

Write-Host 'Installing libusb and pkgconf for x64-windows...'
& $vcpkgExe install 'libusb:x64-windows' 'pkgconf:x64-windows'
if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed (exit $LASTEXITCODE)" }

if ($WithOpenSsl) {
    Write-Host 'Installing OpenSSL for x64-windows (station crypto selftests)...'
    & $vcpkgExe install 'openssl:x64-windows'
    if ($LASTEXITCODE -ne 0) { throw "vcpkg install openssl failed (exit $LASTEXITCODE)" }
}

$installed = Join-Path $vcpkgRoot 'installed\x64-windows'
if (-not (Test-Path (Join-Path $installed 'lib\libusb-1.0.lib'))) {
    throw "libusb import library missing under $installed"
}
if (-not (Test-Path (Join-Path $installed 'lib\pkgconfig\libusb-1.0.pc'))) {
    throw "libusb pkg-config file missing under $installed"
}
if (-not (Test-Path (Join-Path $installed 'tools\pkgconf\pkgconf.exe'))) {
    throw "pkgconf missing under $installed"
}
if ($WithOpenSsl -and -not (Test-Path (Join-Path $installed 'include\openssl\ssl.h'))) {
    throw "OpenSSL headers missing under $installed"
}

Write-Host ''
Write-Host 'Ready. Dependency root:'
Write-Host "  $installed"
if ($WithOpenSsl) { Write-Host '  OpenSSL installed: station crypto selftests will build.' }
Write-Host 'Next: powershell -ExecutionPolicy Bypass -File tools/Build-Devourer.ps1'
