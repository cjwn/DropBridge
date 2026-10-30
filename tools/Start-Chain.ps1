# Q3: start the whole AirDrop chain without Flutter, for A/B measurement.
#
#   powershell -ExecutionPolicy Bypass -File tools\Start-Chain.ps1            # dry run (default)
#   powershell -ExecutionPolicy Bypass -File tools\Start-Chain.ps1 -Run       # actually starts the chain
#
# Starting the chain initialises the RTL8821CU and makes awdld transmit AWDL
# frames, so -Run needs the explicit hardware authorisation described in
# docs/WORK_QUEUE.md. Without -Run the script only validates paths, ports and
# prints the exact command lines it would use.
[CmdletBinding()]
param(
    [int]$Channel = 149,
    [int]$Width = 20,
    [ValidateSet('social', 'fixed')]
    [string]$ChannelSequence = 'social',
    [switch]$NoTcpMerge,
    [string]$Python = '',
    [switch]$Trace,
    [switch]$RadioVerbose,
    [switch]$Run,
    [int]$ReadyTimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'

# Two layouts: the repository (backends under build\) and a packaged folder
# (backends under backend\), so the same script drives an A/B test in both.
$bundled = Test-Path (Join-Path $root 'backend\airbridge_radio.exe')
if ($bundled) {
    $binaries = [ordered]@{
        radio    = Join-Path $root 'backend\airbridge_radio.exe'
        bridge   = Join-Path $root 'backend\radio_pipe_bridge.exe'
        netd     = Join-Path $root 'backend\awdl_netd.exe'
        awdld    = Join-Path $root 'backend\awdld.exe'
        receiver = Join-Path $root 'backend\airdrop\receiver.py'
    }
    $logsRoot = Join-Path $root 'data\chain-logs'
    $dataDir = Join-Path $root 'data'
    $defaultPython = Join-Path $root 'backend\python\python.exe'
} else {
    $binaries = [ordered]@{
        radio    = Join-Path $root 'build\radio_usb\Release\airbridge_radio.exe'
        bridge   = Join-Path $root 'build\radio_usb\Release\radio_pipe_bridge.exe'
        netd     = Join-Path $root 'build\awdl_netd\Release\awdl_netd.exe'
        awdld    = Join-Path $root 'build\awdl\awdld.exe'
        receiver = Join-Path $root 'upstream\winawdl\airdrop\receiver.py'
    }
    $logsRoot = Join-Path $root 'build\chain-logs'
    $dataDir = Join-Path $root 'build\chain-data'
    $defaultPython = Join-Path $root 'build\python-embed\python.exe'
}

$runDir = Join-Path $logsRoot $stamp
$addrFile = Join-Path $dataDir 'awdl_netd.addr'
$received = Join-Path $dataDir 'received'
$keys = Join-Path $dataDir 'keys'

if ([string]::IsNullOrWhiteSpace($Python)) {
    $Python = if (Test-Path $defaultPython) { $defaultPython } else { 'python' }
}

# 1. Validate everything before touching the radio.
$missing = @()
foreach ($entry in $binaries.GetEnumerator()) {
    if (-not (Test-Path $entry.Value)) { $missing += "$($entry.Key): $($entry.Value)" }
}
if ($missing.Count -gt 0) {
    throw ("missing backends:`n  " + ($missing -join "`n  ") + "`nbuild them first (tools\Build-*.ps1)")
}

$ports = @(49876, 49877, 8771, 15353)
$busy = @()
foreach ($port in $ports) {
    $listener = Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue
    if ($listener) { $busy += $port }
}
if ($busy.Count -gt 0) {
    throw "ports already in use: $($busy -join ', '); stop the other DropBridge/radio instance first"
}

$radioArgs = @($Channel, $Width)
$joinWaitUs = if ($NoTcpMerge) { '0' } else { '400' }
# RTL8821CU is 1T1R: advertise only one receive spatial stream.
$awdlArgs = @('-c', $Channel, '-s', '1', '-j', $joinWaitUs, '-v')
$receiverArgs = @('-u', $binaries.receiver, '--name', 'DropBridge',
                  '--listen', '127.0.0.1', '--mdns-address-file', $addrFile,
                  '--outdir', $received, '--keys', $keys)
if ($Trace) { $receiverArgs += '--trace' }

$plan = @(
    "radio    $($binaries.radio) $($radioArgs -join ' ')",
    "bridge   $($binaries.bridge)",
    "netd     $($binaries.netd)   [AWDL_NETD_MDNS_PROXY=1]",
    "awdld    $($binaries.awdld) $($awdlArgs -join ' ')",
    "receiver $Python $($receiverArgs -join ' ')"
)
$awdlEnvironment = @{
    AWDL_ELECTION_METRIC = '200'
    AWDL_ELECTION_COUNTER = '100000'
    # OWL tests presence, not truthiness: fixed must REMOVE this variable.
    AWDL_CHANSEQ = $(if ($ChannelSequence -eq 'social') { 'social' } else { $null })
    AWDL_FOLLOW_MASTER_SEQ = $null
}

Write-Host "DropBridge chain, channel $Channel width $Width MHz, logs: $runDir"
$plan | ForEach-Object { Write-Host "  $_" }
Write-Host "  chanseq=$ChannelSequence (fixed removes AWDL_CHANSEQ; follow-master disabled)"
Write-Host "  join_us=$joinWaitUs (0 disables TCP merging)"

if (-not $Run) {
    Write-Host ''
    Write-Host 'Dry run only: no process started, no wireless frame sent.'
    Write-Host 'Add -Run after the hardware authorisation recorded in docs\WORK_QUEUE.md section 1.'
    exit 0
}

# 2. Start, in order, and wait for each one's readiness line.
New-Item -ItemType Directory -Force $runDir, $dataDir, $received, $keys | Out-Null
$chainLog = Join-Path $runDir 'chain.log'
$commit = (& git -C $root rev-parse --short HEAD 2>$null)
"chain start $(Get-Date -Format o) commit=$commit channel=$Channel width=$Width chanseq=$ChannelSequence join_us=$joinWaitUs python=$Python" |
    Set-Content -Encoding utf8 $chainLog

$processes = [ordered]@{}
$env:PYTHONUNBUFFERED = '1'

function Start-Backend {
    param([string]$Name, [string]$Exe, [string[]]$Arguments, [hashtable]$Environment)
    $stdout = Join-Path $runDir "$Name.log"
    $stderr = Join-Path $runDir "$Name.err"
    $previous = @{}
    foreach ($key in $Environment.Keys) {
        $previous[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
        [Environment]::SetEnvironmentVariable($key, $Environment[$key], 'Process')
    }
    try {
        $process = Start-Process -FilePath $Exe -ArgumentList $Arguments `
            -WorkingDirectory $root -WindowStyle Hidden `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    } finally {
        foreach ($key in $previous.Keys) {
            [Environment]::SetEnvironmentVariable($key, $previous[$key], 'Process')
        }
    }
    $processes[$Name] = $process
    "started $Name pid=$($process.Id)" | Add-Content -Encoding utf8 $chainLog
    return $process
}

function Wait-Ready {
    param([string]$Name, [string]$Needle, [int]$TimeoutSeconds)
    $log = Join-Path $runDir "$Name.log"
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($processes[$Name].HasExited) {
            throw "$Name exited with $($processes[$Name].ExitCode); see $log and $runDir\$Name.err"
        }
        if ((Test-Path $log) -and (Get-Content -Raw $log) -match [regex]::Escape($Needle)) {
            "ready $Name" | Add-Content -Encoding utf8 $chainLog
            return
        }
        Start-Sleep -Milliseconds 200
    }
    throw "$Name not ready after ${TimeoutSeconds}s; see $log"
}

function Stop-Chain {
    foreach ($name in @($processes.Keys | Sort-Object -Descending)) {
        $process = $processes[$name]
        if ($process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            "stopped $name" | Add-Content -Encoding utf8 $chainLog
        }
    }
    Start-Sleep -Milliseconds 500
    $left = @()
    foreach ($port in $ports) {
        if (Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue) { $left += $port }
    }
    if ($left.Count -gt 0) { "ports still held: $($left -join ', ')" | Add-Content -Encoding utf8 $chainLog }
}

$cancel = $false
$handler = [ConsoleCancelEventHandler] {
    param($sender, $eventArgs)
    $eventArgs.Cancel = $true
    $script:cancel = $true
}
[Console]::add_CancelKeyPress($handler)

try {
    Start-Backend -Name 'radio' -Exe $binaries.radio -Arguments $radioArgs `
        -Environment @{ AIRBRIDGE_LOG_LEVEL = $(if ($RadioVerbose) { 'info' } else { 'warn' }); AIRBRIDGE_LINK_STATS = '1'; AIRBRIDGE_FASTRETUNE_FW = '2' } | Out-Null
    Wait-Ready -Name 'radio' -Needle 'listening on 127.0.0.1:49876' -TimeoutSeconds $ReadyTimeoutSeconds

    Start-Backend -Name 'bridge' -Exe $binaries.bridge -Arguments @() -Environment @{} | Out-Null
    Wait-Ready -Name 'bridge' -Needle 'waiting for awdld' -TimeoutSeconds 30

    Start-Backend -Name 'netd' -Exe $binaries.netd -Arguments @() `
        -Environment @{ AWDL_NETD_MDNS_PROXY = '1'; AWDL_NETD_ADDR_FILE = $addrFile } | Out-Null
    Wait-Ready -Name 'netd' -Needle 'waiting for awdld' -TimeoutSeconds 30

    Start-Backend -Name 'awdld' -Exe $binaries.awdld -Arguments $awdlArgs `
        -Environment $awdlEnvironment | Out-Null
    Wait-Ready -Name 'awdld' -Needle 'host interface' -TimeoutSeconds 60

    $deadline = (Get-Date).AddSeconds(60)
    while (-not (Test-Path $addrFile) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
    if (-not (Test-Path $addrFile)) { throw "awdl_netd address file never appeared: $addrFile" }

    Start-Backend -Name 'receiver' -Exe $Python -Arguments $receiverArgs -Environment @{} | Out-Null
    Wait-Ready -Name 'receiver' -Needle 'receiving as' -TimeoutSeconds 60

    Write-Host ''
    Write-Host "Chain is up on channel $Channel / $Width MHz. Logs: $runDir"
    Write-Host 'Open AirDrop on the phone, send a 20 MB file, then press Ctrl+C here.'
} catch {
    Write-Host "startup failed: $_"
    Stop-Chain
    [Console]::remove_CancelKeyPress($handler)
    exit 1
}

while (-not $script:cancel) {
    foreach ($name in $processes.Keys) {
        if ($processes[$name].HasExited) {
            Write-Host "$name exited unexpectedly ($($processes[$name].ExitCode)); stopping the chain"
            "unexpected exit $name code=$($processes[$name].ExitCode)" | Add-Content -Encoding utf8 $chainLog
            $script:cancel = $true
        }
    }
    Start-Sleep -Milliseconds 500
}

Write-Host 'stopping chain...'
Stop-Chain
[Console]::remove_CancelKeyPress($handler)
"chain stop $(Get-Date -Format o)" | Add-Content -Encoding utf8 $chainLog
Write-Host "stopped. logs: $runDir"
