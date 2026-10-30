# Q44: build the distributable folder + zip (tray app, everything inside one folder).
#
#   powershell -ExecutionPolicy Bypass -File tools\Package-Distributable.ps1
#   powershell -ExecutionPolicy Bypass -File tools\Package-Distributable.ps1 -SkipZip
#
# Output:
#   dist\DropBridge\            the folder a user unzips and keeps
#     DropBridge.exe            tray supervisor (entry point, winexe)
#     backend\                   radio, bridge, awdl_netd, awdld, python, airdrop
#     tools\Start-Chain.ps1      headless console fallback (troubleshooting)
#     licenses\                  upstream licence texts we must ship
#     README.txt                 user-facing instructions (Chinese)
#     VERSION.txt                build stamp, commit, backend hashes
#   dist\DropBridge-portable.zip
#
# Nothing is written outside the package folder at runtime: data\ (keys,
# instance id, received files, logs) is created next to the exe on first run.
[CmdletBinding()]
param([switch]$SkipZip)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist 'DropBridge'
$zip = Join-Path $dist 'DropBridge-portable.zip'

function Need([string]$Path, [string]$Hint) {
    if (-not (Test-Path $Path)) { throw "missing $Path - $Hint" }
}

$supervisor = Join-Path $root 'build\supervisor\Release\DropBridge.exe'
$radio = Join-Path $root 'build\radio_usb\Release\airbridge_radio.exe'
$bridge = Join-Path $root 'build\radio_usb\Release\radio_pipe_bridge.exe'
$netd = Join-Path $root 'build\awdl_netd\Release\awdl_netd.exe'
$awdld = Join-Path $root 'build\awdl\awdld.exe'
$libusb = Join-Path $root 'build\radio_usb\Release\libusb-1.0.dll'
$python = Join-Path $root 'build\python-embed'
$airdrop = Join-Path $root 'upstream\winawdl\airdrop'

foreach ($p in @($supervisor, $radio, $bridge, $netd, $awdld, $libusb)) {
    Need $p 'run tools\Build-*.ps1 first'
}
Need $python 'prepare build\python-embed (embeddable Python + cryptography) first'
Need (Join-Path $airdrop 'receiver.py') 'upstream/winawdl is missing'

# Refuse to delete anything that is not exactly the staging folder under dist.
$stageFull = [IO.Path]::GetFullPath($stage)
$distFull = [IO.Path]::GetFullPath($dist).TrimEnd('\') + '\'
if (-not $stageFull.StartsWith($distFull, [StringComparison]::OrdinalIgnoreCase)) {
    throw "refusing to replace a stage outside dist: $stageFull"
}
if (Test-Path (Join-Path $stage 'data')) {
    throw "stage contains user data; move it before packaging: $stageFull"
}
if (Test-Path $stage) { Remove-Item -LiteralPath $stageFull -Recurse -Force }

foreach ($dir in 'backend', 'backend\airdrop', 'backend\python', 'tools', 'licenses', 'setup', 'setup\licenses') {
    New-Item -ItemType Directory -Force (Join-Path $stage $dir) | Out-Null
}

Copy-Item $supervisor (Join-Path $stage 'DropBridge.exe') -Force
foreach ($file in @($radio, $bridge, $netd, $awdld, $libusb)) {
    Copy-Item $file (Join-Path $stage 'backend') -Force
}

# robocopy copies the contents of build\python-embed into backend\python; a plain
# Copy-Item -Recurse would nest it as backend\python\python-embed.
robocopy $python (Join-Path $stage 'backend\python') /E /NJH /NJS /NFL /NDL /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed with $LASTEXITCODE while staging Python" }
$pth = Join-Path $stage 'backend\python\python312._pth'
if (Test-Path $pth) {
    $text = Get-Content $pth -Raw
    if ($text -notmatch 'Lib\\site-packages') { Add-Content $pth 'Lib\site-packages' }
    if ($text -notmatch '\.\.\\airdrop') { Add-Content $pth '..\airdrop' }
}

Copy-Item (Join-Path $airdrop '*.py') (Join-Path $stage 'backend\airdrop') -Force

# Bluetooth beacon: keeps iPhone AWDL awake so the tray stays discoverable even
# when the phone's share sheet is closed (MIT, upstream winawdl).
$beacon = Join-Path $root 'src\beacon\airdrop-beacon.ps1'
if (Test-Path $beacon) { Copy-Item $beacon (Join-Path $stage 'backend\beacon.ps1') -Force }

foreach ($dll in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    $src = Join-Path $env:SystemRoot "System32\$dll"
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $stage 'backend') -Force
        Copy-Item $src (Join-Path $stage 'backend\python') -Force
    }
}

Copy-Item (Join-Path $root 'tools\Start-Chain.ps1') (Join-Path $stage 'tools') -Force

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

Copy-Item (Join-Path $root 'packaging\README.txt') (Join-Path $stage 'README.txt') -Force

# One-time driver binding helper: Zadig (GPLv3) + a script that only ever looks at
# USB\VID_0BDA&PID_C820&MI_02, plus its licence and written source offer.
robocopy (Join-Path $root 'packaging\setup') (Join-Path $stage 'setup') /E /NJH /NJS /NFL /NDL /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed with $LASTEXITCODE while staging setup" }

# The in-app installer (DropBridge.exe --install-driver) uses this signed package:
# it imports dropbridge-driver.cer and points pnputil at the INF. Built by
# tools\Build-DriverPackage.ps1; no Zadig needed when it works.
$driverSource = Join-Path $root 'packaging\driver'
if (Test-Path (Join-Path $driverSource 'airbridge-winusb.cat')) {
    New-Item -ItemType Directory -Force (Join-Path $stage 'setup\driver') | Out-Null
    Copy-Item (Join-Path $driverSource 'airbridge-winusb.inf') (Join-Path $stage 'setup\driver') -Force
    Copy-Item (Join-Path $driverSource 'airbridge-winusb.cat') (Join-Path $stage 'setup\driver') -Force
    Copy-Item (Join-Path $driverSource 'dropbridge-driver.cer') (Join-Path $stage 'setup\driver') -Force
}

$commit = (& git rev-parse --short HEAD 2>$null)
if (-not $commit) { $commit = 'unknown' }
$version = @(
    'DropBridge build'
    ('built      ' + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss zzz'))
    ('git        ' + $commit)
    ('supervisor ' + (Get-FileHash $supervisor -Algorithm SHA256).Hash.ToLowerInvariant())
    ('netd       ' + (Get-FileHash $netd -Algorithm SHA256).Hash.ToLowerInvariant())
    ('radio      ' + (Get-FileHash $radio -Algorithm SHA256).Hash.ToLowerInvariant())
    ('awdld      ' + (Get-FileHash $awdld -Algorithm SHA256).Hash.ToLowerInvariant())
    ('bridge     ' + (Get-FileHash $bridge -Algorithm SHA256).Hash.ToLowerInvariant())
    ('setup      setup\Bind-WinUSB.cmd + zadig-2.9.exe (GPLv3, setup\licenses\ZADIG.txt)')
    'layout     one folder; data\ is created next to DropBridge.exe on first run'
)
Set-Content -LiteralPath (Join-Path $stage 'VERSION.txt') -Value $version -Encoding utf8

if (-not $SkipZip) {
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path $stage -DestinationPath $zip -Force
    Write-Host ("distributable ZIP: {0} ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
}
$files = Get-ChildItem $stage -Recurse -File
Write-Host ("staged folder: {0} ({1} files, {2:N1} MB)" -f $stage, $files.Count,
            (($files | Measure-Object Length -Sum).Sum / 1MB))
