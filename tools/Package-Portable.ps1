# Builds the portable ZIP: dist/AirBridge32-portable.zip
# The user extracts it and double-clicks AirBridge32.exe. No installer, no
# registry writes, no Python/VC++ install (runtime DLLs are included).
[CmdletBinding()]
param(
    [switch]$Build,
    [switch]$SkipZip
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$stage = Join-Path $root 'build\portable\AirBridge32'
$dist = Join-Path $root 'dist'
$zip = Join-Path $dist 'AirBridge32-portable.zip'

function Need($path, $hint) {
    if (-not (Test-Path $path)) { throw "missing $path; $hint" }
}

if ($Build) {
    $cmake = 'D:\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    $env:VCPKG_ROOT = Join-Path $root 'build\deps\vcpkg'
    & $cmake --build (Join-Path $root 'build\radio_usb') --config Release `
        --target airbridge_radio radio_pipe_bridge --parallel 8
    & $cmake --build (Join-Path $root 'build\awdl_netd') --config Release --parallel 8
    $mingw = Join-Path $root 'build\deps\vcpkg\downloads\tools\perl\5.42.2.1\c\bin'
    $env:PATH = "$mingw;$env:PATH"
    & $cmake --build (Join-Path $root 'build\awdl') --parallel 8
    $env:Path = "J:\New soft project\tools\flutter\bin;$env:Path"
    Push-Location (Join-Path $root 'app')
    try { flutter build windows --release } finally { Pop-Location }
}

$flutterRelease = Join-Path $root 'app\build\windows\x64\runner\Release'
Need $flutterRelease 'build the Flutter app first (or use -Build)'
$radio = Join-Path $root 'build\radio_usb\Release\airbridge_radio.exe'
$bridge = Join-Path $root 'build\radio_usb\Release\radio_pipe_bridge.exe'
$netd = Join-Path $root 'build\awdl_netd\Release\awdl_netd.exe'
$awdld = Join-Path $root 'build\awdl\awdld.exe'
$libusb = Join-Path $root 'build\radio_usb\Release\libusb-1.0.dll'
foreach ($p in @($radio, $bridge, $netd, $awdld, $libusb)) { Need $p 'build backends first' }

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
New-Item -ItemType Directory -Force (Join-Path $stage 'backend') | Out-Null
New-Item -ItemType Directory -Force (Join-Path $stage 'data\received') | Out-Null
New-Item -ItemType Directory -Force (Join-Path $stage 'licenses') | Out-Null

Copy-Item (Join-Path $flutterRelease '*') $stage -Recurse -Force
Rename-Item (Join-Path $stage 'airbridge32.exe') 'AirBridge32.exe'

Copy-Item $radio (Join-Path $stage 'backend') -Force
Copy-Item $bridge (Join-Path $stage 'backend') -Force
Copy-Item $netd (Join-Path $stage 'backend') -Force
Copy-Item $awdld (Join-Path $stage 'backend') -Force
Copy-Item $libusb (Join-Path $stage 'backend') -Force

$python = Join-Path $root 'build\python-embed'
Need $python 'prepare build/python-embed (embeddable Python + cryptography) first'
$pth = Join-Path $python 'python312._pth'
$pthText = Get-Content $pth -Raw
if ($pthText -notmatch 'Lib\\site-packages') { Add-Content $pth 'Lib\site-packages' }
if ($pthText -notmatch '\.\.\\airdrop') { Add-Content $pth '..\airdrop' }
Copy-Item $python (Join-Path $stage 'backend\python') -Recurse -Force
New-Item -ItemType Directory -Force (Join-Path $stage 'backend\airdrop') | Out-Null
Copy-Item (Join-Path $root 'upstream\winawdl\airdrop\*.py') `
    (Join-Path $stage 'backend\airdrop') -Force

# VC++ runtime DLLs (redistributable); Windows 11 has UCRT but not the VC++ runtime.
foreach ($dll in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    $src = Join-Path $env:SystemRoot "System32\$dll"
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $stage $dll) -Force
        Copy-Item $src (Join-Path $stage 'backend') -Force
    }
}

# Licenses / notices.
foreach ($license in @(
    @($root, 'upstream\winawdl\LICENSE'),
    @($root, 'upstream\devourer\LICENSE'),
    @($root, 'upstream\winawdl\upstream\owl\COPYING')
)) {
    $p = Join-Path $license[0] $license[1]
    if (Test-Path $p) {
        Copy-Item $p (Join-Path $stage ('licenses\' + (Split-Path $p -Leaf))) -Force
    }
}
@'
AirBridge32 portable

1. Extract this folder anywhere writable (Desktop/Downloads is fine).
2. If this is the first time this PC uses the RTL8821CU adapter, bind its
   MI_02 interface to WinUSB once (Zadig, libwdi). This is the only step that
   needs Administrator rights; no installer is used.
3. Double-click AirBridge32.exe.
4. On iPhone, open AirDrop, choose "Everyone for 10 Minutes". The receiver
   appears as AirBridge32 (or a 12-hex name). Send a file; it is saved under
   data\received and never opened automatically.

Defaults: 5 GHz channel 149, 40 MHz. v2ray TUN can stay enabled; AWDL data
does not use the Windows IPv6 stack.

No registry writes, no service, no Wintun adapter.
'@ | Set-Content (Join-Path $stage 'README.txt') -Encoding utf8

if (-not $SkipZip) {
    New-Item -ItemType Directory -Force $dist | Out-Null
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force
    Write-Host "portable ZIP: $zip"
}
Write-Host "staged folder: $stage"
