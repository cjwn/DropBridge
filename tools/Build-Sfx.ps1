# Q4: pack dist\DropBridge-headless into a single self-extracting exe.
#
#   powershell -ExecutionPolicy Bypass -File tools\Build-Sfx.ps1
#
# Produces dist\DropBridge.exe: a ~16 MB stub+payload built with the in-box
# C# compiler (.NET Framework 4.8). It unpacks once per build into the folder's
# app\<build stamp> and starts the tray supervisor (the stub looks for
# backend\DropBridge.exe, then the legacy backend\AirBridge32.exe).
[CmdletBinding()]
param(
    [switch]$SkipPayload
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist 'DropBridge-headless'
$payload = Join-Path $dist 'payload.zip'
$exe = Join-Path $dist 'DropBridge.exe'
$source = Join-Path $root 'src\sfx\Program.cs'
$icon = Join-Path $root 'assets\dropbridge.ico'
$csc = Join-Path $env:SystemRoot 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'

if (-not (Test-Path $csc)) { throw "in-box C# compiler missing: $csc" }
if (-not (Test-Path $source)) { throw "missing $source" }
if (-not $SkipPayload -and -not (Test-Path $stage)) {
    & (Join-Path $PSScriptRoot 'Package-Headless.ps1') -SkipZip
    if ($LASTEXITCODE -ne 0) { throw 'Package-Headless.ps1 failed' }
}
if (-not (Test-Path (Join-Path $stage 'Start.cmd'))) { throw "staged package missing: $stage" }

New-Item -ItemType Directory -Force $dist | Out-Null
if (Test-Path $payload) { Remove-Item $payload -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $payload -Force

if (Test-Path $exe) { Remove-Item $exe -Force }

# Q59: the icon is optional (a checkout without assets\ still builds), but the
# single-file exe is what most users keep, so it should not fall back to the
# generic stub icon.
$iconArgument = @()
if (Test-Path $icon) { $iconArgument = @("/win32icon:$icon") }

& $csc /nologo /target:exe /optimize+ "/out:$exe" "/resource:$payload,payload.zip" @iconArgument `
    /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll $source
if ($LASTEXITCODE -ne 0) { throw "csc failed with $LASTEXITCODE" }

Write-Host ("single exe: {0} ({1:N1} MB)" -f $exe, ((Get-Item $exe).Length / 1MB))
Write-Host ("stub overhead: {0:N0} KB over the {1:N1} MB payload" -f `
    (((Get-Item $exe).Length - (Get-Item $payload).Length) / 1KB), `
    ((Get-Item $payload).Length / 1MB))
