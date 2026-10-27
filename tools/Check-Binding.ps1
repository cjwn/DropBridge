# Read-only check of how the target adapter's Wi-Fi interface (MI_02) is bound.
# Writes nothing. Exit codes: 0 = bound to WinUSB, 1 = still vendor/other driver,
# 2 = target interface not present.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Get-DeviceProperty {
    param([string]$InstanceId, [string]$Key)
    return (Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName $Key -ErrorAction SilentlyContinue).Data
}

$mi02 = @(Get-PnpDevice -PresentOnly |
    Where-Object { $_.InstanceId -like 'USB\VID_0BDA&PID_C820&MI_02*' }) | Select-Object -First 1

if (-not $mi02) {
    Write-Output 'MISSING: no USB\VID_0BDA&PID_C820&MI_02 device is present.'
    Write-Output 'Plug the adapter into a direct USB port and re-run; check tools/Inspect-Hardware.ps1.'
    exit 2
}

$inf = Get-DeviceProperty -InstanceId $mi02.InstanceId -Key 'DEVPKEY_Device_DriverInfPath'
$service = Get-DeviceProperty -InstanceId $mi02.InstanceId -Key 'DEVPKEY_Device_Service'
$version = Get-DeviceProperty -InstanceId $mi02.InstanceId -Key 'DEVPKEY_Device_DriverVersion'
$provider = Get-DeviceProperty -InstanceId $mi02.InstanceId -Key 'DEVPKEY_Device_DriverProvider'
$problem = Get-DeviceProperty -InstanceId $mi02.InstanceId -Key 'DEVPKEY_Device_ProblemCode'

$isWinUsb = ($service -match '^(WinUSB|libusbK|libusb0)$') -or ($inf -match 'winusb')
$bt = @(Get-PnpDevice -PresentOnly |
    Where-Object { $_.InstanceId -like 'USB\VID_0BDA&PID_C820&MI_00*' }) | Select-Object -First 1

Write-Output ''
Write-Output 'Target Wi-Fi interface (MI_02)'
Write-Output ("  instance   : {0}" -f $mi02.InstanceId)
Write-Output ("  status     : {0}  (problem code {1})" -f $mi02.Status, $problem)
Write-Output ("  driver inf : {0}" -f $inf)
Write-Output ("  driver     : {0} {1}" -f $provider, $version)
Write-Output ("  service    : {0}" -f $service)
Write-Output ''
Write-Output 'Bluetooth interface (MI_00, must stay untouched)'
if ($bt) {
    Write-Output ("  status     : {0}" -f $bt.Status)
} else {
    Write-Output '  not present (adapter may be unplugged or BT disabled)'
}
Write-Output ''

$net = @(Get-NetAdapter -ErrorAction SilentlyContinue |
    Where-Object { $_.InterfaceDescription -match '8821CU|RTL8821' })
if ($net.Count -gt 0) {
    Write-Output 'Realtek network adapter(s):'
    $net | Select-Object Name, InterfaceDescription, Status | Format-Table -AutoSize
} else {
    Write-Output 'No Realtek 8821CU network adapter (expected once WinUSB owns MI_02).'
}
Write-Output ''

if ($isWinUsb) {
    Write-Output 'RESULT: MI_02 is bound to WinUSB. libusb should be able to claim it.'
    Write-Output 'Next: run the read-only USB open probe. Do NOT run Init yet.'
    exit 0
}

Write-Output 'RESULT: MI_02 is still bound to a non-WinUSB driver.'
Write-Output 'Follow docs/BINDING_RUNBOOK.md to bind MI_02, then re-run this check.'
exit 1
