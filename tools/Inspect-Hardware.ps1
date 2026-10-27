# Read-only USB enumeration for the DropBridge / AirBridge32 target adapter.
# Writes evidence/usb-devices.local.json (git-ignored) and prints a summary.
# It does NOT initialize the radio, replace drivers or transmit frames.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

$devices = @(
    Get-PnpDevice -PresentOnly |
        Where-Object { $_.InstanceId -like 'USB\VID*' } |
        Select-Object Class, FriendlyName, InstanceId, Status
)

# Known target forms: RTL8811CU/8821CU (c811/c820) and the ZeroCD stage (1a2b).
$targetPattern = 'VID_0BDA&PID_(C811|C820|1A2B)'
$targets = @($devices | Where-Object { $_.InstanceId -match $targetPattern })
$realtek = @($devices | Where-Object { $_.InstanceId -like 'USB\VID_0BDA&PID_*' })

$report = [ordered]@{
    capturedAtUtc    = (Get-Date).ToUniversalTime().ToString('o')
    scope            = 'present USB devices; identity is not chip verification'
    targetPattern    = $targetPattern
    targetCandidates = $targets
    realtekUsb       = $realtek
    devices          = $devices
}
$destination = Join-Path $root 'evidence/usb-devices.local.json'
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $destination -Encoding utf8

$devices | Format-Table -AutoSize

Write-Output ("USB devices present: {0}" -f $devices.Count)
if ($targets.Count -gt 0) {
    Write-Output "TARGET CANDIDATE(S) FOUND:"
    $targets | Format-Table -AutoSize
    Write-Output "Record VID/PID, interfaces, current driver and port before any binding."
} else {
    Write-Output "NO KNOWN TARGET VID/PID PRESENT (expected 0bda:c811 or 0bda:c820; ZeroCD 0bda:1a2b)."
    Write-Output "Plug the adapter into a direct USB port and re-run this script."
}
Write-Output "If it appears as 0bda:1a2b (ZeroCD), do not eject or switch modes automatically."
Write-Output ("Saved read-only enumeration: {0}" -f $destination)
Write-Output "VID/PID alone does not prove the chip; chip ID is read only during an authorized Init."
