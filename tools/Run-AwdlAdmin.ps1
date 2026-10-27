# Runs awdld elevated so it can create the Wintun adapter (error 5 otherwise).
# The radio and the pipe bridge must already be running.
#
# Usage (from an elevated shell, or right-click -> Run as administrator):
#   powershell -ExecutionPolicy Bypass -File tools/Run-AwdlAdmin.ps1 -Channel 149
[CmdletBinding()]
param([ValidateSet(6, 44, 149)][int]$Channel = 149)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$awdlDir = Join-Path $root 'build\awdl'
$log = Join-Path $root "evidence\awdld-admin-$Channel.log"
if (-not (Test-Path (Join-Path $awdlDir 'awdld.exe'))) {
    throw 'awdld.exe missing; run tools/Build-Awdl.ps1'
}
if (-not (Test-Path (Join-Path $awdlDir 'wintun.dll'))) {
    throw 'wintun.dll missing; run upstream/winawdl/scripts/fetch-wintun.ps1'
}
Set-Location $awdlDir
Write-Host "awdld channel $Channel, log: $log"
# cmd handles the native stderr redirect, avoiding PowerShell 5.1's
# NativeCommandError behavior when ErrorActionPreference is Stop.
cmd /c "awdld.exe -c $Channel -s 2 -j 400 -v > `"$log`" 2>&1"
