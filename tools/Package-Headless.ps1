# Q3/Q4: build a Dart-free package (folder + ZIP) that starts the chain with
# tools\Start-Chain.ps1. This is the A/B counterpart of dist\DropBridge-portable
# and the payload a single-file wrapper will carry later.
#
#   powershell -ExecutionPolicy Bypass -File tools\Package-Headless.ps1
#   powershell -ExecutionPolicy Bypass -File tools\Package-Headless.ps1 -SkipZip
[CmdletBinding()]
param(
    [switch]$SkipZip
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist 'DropBridge-headless'
$zip = Join-Path $dist 'DropBridge-headless.zip'

function Need([string]$Path, [string]$Hint) {
    if (-not (Test-Path $Path)) { throw "missing $Path - $Hint" }
}

$radio = Join-Path $root 'build\radio_usb\Release\airbridge_radio.exe'
$bridge = Join-Path $root 'build\radio_usb\Release\radio_pipe_bridge.exe'
$netd = Join-Path $root 'build\awdl_netd\Release\awdl_netd.exe'
$awdld = Join-Path $root 'build\awdl\awdld.exe'
$libusb = Join-Path $root 'build\radio_usb\Release\libusb-1.0.dll'
$supervisor = Join-Path $root 'build\supervisor\Release\DropBridge.exe'
$python = Join-Path $root 'build\python-embed'
$airdrop = Join-Path $root 'upstream\winawdl\airdrop'

foreach ($p in @($radio, $bridge, $netd, $awdld, $libusb)) { Need $p 'build the backends first' }
Need $supervisor 'run tools\Build-Supervisor.ps1 first'
Need $python 'prepare build\python-embed (embeddable Python + cryptography) first'
Need (Join-Path $airdrop 'receiver.py') 'upstream/winawdl is missing'

$stageFull = [IO.Path]::GetFullPath($stage)
$distFull = [IO.Path]::GetFullPath($dist).TrimEnd('\') + '\'
if (-not $stageFull.StartsWith($distFull, [StringComparison]::OrdinalIgnoreCase)) {
    throw "refusing to replace stage outside dist: $stageFull"
}
if (Test-Path (Join-Path $stage 'data')) {
    throw "stage contains user data; preserve it before packaging: $stageFull"
}
if (Test-Path $stage) { Remove-Item -LiteralPath $stageFull -Recurse -Force }
foreach ($dir in 'backend\airdrop', 'tools', 'licenses') {
    New-Item -ItemType Directory -Force (Join-Path $stage $dir) | Out-Null
}

foreach ($file in @($radio, $bridge, $netd, $awdld, $libusb, $supervisor)) {
    Copy-Item $file (Join-Path $stage 'backend') -Force
}

# robocopy copies the contents of build\python-embed into backend\python; a
# plain Copy-Item -Recurse would nest it as backend\python\python-embed.
robocopy $python (Join-Path $stage 'backend\python') /E /NJH /NJS /NFL /NDL /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed with $LASTEXITCODE while staging Python" }
$pth = Join-Path $stage 'backend\python\python312._pth'
if (Test-Path $pth) {
    $text = Get-Content $pth -Raw
    if ($text -notmatch 'Lib\\site-packages') { Add-Content $pth 'Lib\site-packages' }
    if ($text -notmatch '\.\.\\airdrop') { Add-Content $pth '..\airdrop' }
}

Copy-Item (Join-Path $airdrop '*.py') (Join-Path $stage 'backend\airdrop') -Force
Copy-Item (Join-Path $root 'tools\Start-Chain.ps1') (Join-Path $stage 'tools') -Force

foreach ($dll in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    $src = Join-Path $env:SystemRoot "System32\$dll"
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $stage 'backend') -Force
        Copy-Item $src (Join-Path $stage 'backend\python') -Force
    }
}

foreach ($license in @(
    @($root, 'upstream\winawdl\LICENSE'),
    @($root, 'upstream\devourer\LICENSE'),
    @($root, 'upstream\winawdl\upstream\owl\COPYING')
)) {
    $path = Join-Path $license[0] $license[1]
    if (Test-Path $path) {
        Copy-Item $path (Join-Path $stage ('licenses\' + (Split-Path $path -Leaf))) -Force
    }
}

@'
@echo off
rem Starts the DropBridge backend chain without Flutter.
rem This initialises the RTL8821CU and makes awdld transmit AWDL frames:
rem run it only when you intend to test with the phone.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Start-Chain.ps1" -Run %*
pause
'@ | Set-Content (Join-Path $stage 'Start.cmd') -Encoding ascii

@'
DropBridge headless package

1. Extract this folder anywhere writable.
2. Bind the RTL8821CU MI_02 interface to WinUSB once if this PC never used it
   (Zadig; the only step that needs Administrator). See docs\BINDING_RUNBOOK.md.
3. Double-click Start.cmd. It starts radio (149 / 20 MHz), bridge, awdl_netd,
   awdld and the Python receiver, all without Flutter or a Dart runtime.
4. On iPhone open AirDrop, choose "Everyone for 10 Minutes", send a file.
   Files land in data\received; logs in data\chain-logs\<timestamp>.
5. Ctrl+C stops the chain in reverse order.

Dry run (prints the command lines, starts nothing):
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\Start-Chain.ps1

Notes
- No registry writes, no service, no Wintun adapter, no installer.
- The self-signed TLS certificate is created on first start by the bundled
  cryptography module; Git/OpenSSL are not needed.
- v2ray TUN can stay enabled; AWDL traffic does not use the Windows IPv6 stack.
'@ | Set-Content (Join-Path $stage 'README.txt') -Encoding utf8

if (-not $SkipZip) {
    New-Item -ItemType Directory -Force $dist | Out-Null
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force
    Write-Host ("headless ZIP: {0} ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
}
Write-Host "staged folder: $stage"
