# B: 24 h soak sampler for the tray build.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\Monitor-Soak.ps1
#   ... -Hours 24 -IntervalSeconds 300 -Out build\soak\soak.csv
#
# Samples the supervisor and every backend: working set, private bytes, handle
# count and CPU seconds, plus whether the expected processes and ports are
# still there. Writes CSV into the repository (build\ is git-ignored). It does
# not start or stop anything; run the app separately and leave it alone.
[CmdletBinding()]
param(
    [int]$Hours = 24,
    [int]$IntervalSeconds = 300,
    [string]$Out = 'build\soak\soak.csv'
)

$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
if (-not [IO.Path]::IsPathRooted($Out)) { $Out = Join-Path $root $Out }
New-Item -ItemType Directory -Force (Split-Path $Out -Parent) | Out-Null

$names = @('AirBridge32', 'airbridge_radio', 'radio_pipe_bridge', 'awdl_netd', 'awdld', 'python')
$header = 'time,process,pid,working_set_mb,private_mb,handles,cpu_seconds,note'
if (-not (Test-Path $Out)) { Set-Content -Path $Out -Value $header -Encoding utf8 }

$deadline = (Get-Date).AddHours($Hours)
while ((Get-Date) -lt $deadline) {
    $stamp = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
    $found = @{}
    foreach ($name in $names) {
        $procs = @(Get-Process -Name $name -ErrorAction SilentlyContinue | Where-Object {
                if ($name -ne 'python') { return $true }
                return ($_.Path -like '*\backend\python\python.exe')
            })
        if ($procs.Count -eq 0) { continue }
        foreach ($proc in $procs) {
            $key = "$name/$($proc.Id)"
            $found[$key] = $true
            $line = '{0},{1},{2},{3:N1},{4:N1},{5},{6:N1},' -f `
                $stamp, $name, $proc.Id,
                ($proc.WorkingSet64 / 1MB), ($proc.PrivateMemorySize64 / 1MB),
                $proc.HandleCount, $proc.CPU
            Add-Content -Path $Out -Value $line -Encoding utf8
        }
    }
    foreach ($name in $names) {
        if ($name -eq 'python') { continue }
        if (-not ($found.Keys | Where-Object { $_ -like "$name/*" })) {
            Add-Content -Path $Out -Value ('{0},{1},0,0,0,0,0,missing' -f $stamp, $name) -Encoding utf8
        }
    }
    $ports = @(49876, 49877, 8771, 15353 | ForEach-Object {
            if (Get-NetTCPConnection -State Listen -LocalPort $_ -ErrorAction SilentlyContinue) { $_ }
        })
    Add-Content -Path $Out -Value ('{0},ports,,0,0,0,0,{1}' -f $stamp, ($ports -join ';')) -Encoding utf8
    Start-Sleep -Seconds $IntervalSeconds
}
Add-Content -Path $Out -Value ('{0},soak,,0,0,0,0,finished' -f (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')) -Encoding utf8
