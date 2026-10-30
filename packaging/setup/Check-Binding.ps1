# One-time setup check for DropBridge: is the Wi-Fi interface of the RTL8821CU
# bound to WinUSB? Read-only: this script changes nothing and needs no admin.
#
#   exit 0  already bound (nothing to do)
#   exit 1  present, but still on the Realtek driver (binding needed)
#   exit 3  the card is not plugged in
param([switch]$Quiet)

$ErrorActionPreference = 'Continue'
$device = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object { $_.InstanceId -like 'USB\VID_0BDA&PID_C820&MI_02\*' } |
    Select-Object -First 1

if (-not $device) {
    if (-not $Quiet) {
        Write-Host 'NOT PRESENT: the card is not plugged in.' -ForegroundColor Yellow
        Write-Host 'Plug it into a rear USB port and run this again.'
    }
    exit 3
}

$service = (Get-PnpDeviceProperty -InstanceId $device.InstanceId `
        -KeyName 'DEVPKEY_Device_Service' -ErrorAction SilentlyContinue).Data

if ($service -eq 'WinUSB') {
    if (-not $Quiet) {
        Write-Host 'OK: MI_02 is bound to WinUSB.' -ForegroundColor Green
        Write-Host ('    ' + $device.InstanceId)
    }
    exit 0
}

if (-not $Quiet) {
    Write-Host ('NOT YET: MI_02 is using ' + $service + ', so libusb cannot open it.') -ForegroundColor Yellow
    Write-Host 'Run setup\Bind-WinUSB.cmd to bind it to WinUSB (one time per machine).'
}
exit 1
