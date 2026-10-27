[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$devices = @(Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like 'USB\VID*' } | Select-Object Class,FriendlyName,InstanceId,Status)
$report = [ordered]@{ capturedAtUtc = (Get-Date).ToUniversalTime().ToString('o'); scope = 'present USB devices; identity is not chip verification'; devices = $devices }
$destination = Join-Path $root 'evidence/usb-devices.local.json'
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $destination -Encoding utf8
$devices | Format-Table -AutoSize
Write-Output "已保存只读枚举结果：$destination"
