#requires -Version 5.1
<#
.SYNOPSIS
Freeze one supervisor session into build\diagnostics and summarise it.

.DESCRIPTION
The supervisor keeps only the most recent sessions under <root>\data\logs, so a
real-device run must be copied out before the next launch. This script:

  1. resolves a session directory (given, or the newest one);
  2. copies it to build\diagnostics\<time>-<tag>\session\ (logs only, no keys,
     no received files);
  3. runs tools\analyze_session.py on the copy (text + JSON);
  4. records git HEAD, uncommitted state and the SHA-256 of every frozen file
     plus the backend binaries that produced the session when they are known.

It never starts the chain, never touches the radio and never writes outside
the output directory.

.EXAMPLE
powershell -File tools\Collect-Session.ps1 -Latest -PackageRoot dist\DropBridge -Tag q31-fixed-merge-off

.EXAMPLE
powershell -File tools\Collect-Session.ps1 -Session dist\DropBridge\data\logs\20261028-194820 -Tag review
#>
param(
    [string]$Session,
    [switch]$Latest,
    [string]$PackageRoot = 'dist\DropBridge',
    [string]$Tag = 'session',
    [string]$OutRoot = 'build\diagnostics',
    [string]$Python = 'python'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repo

function Resolve-Session {
    if ($Session) {
        if (-not (Test-Path -LiteralPath $Session -PathType Container)) {
            throw "session directory not found: $Session"
        }
        return (Resolve-Path -LiteralPath $Session).Path
    }
    $logs = Join-Path $PackageRoot 'data\logs'
    if (-not (Test-Path -LiteralPath $logs -PathType Container)) {
        throw "no log directory: $logs (pass -Session explicitly)"
    }
    $dirs = Get-ChildItem -LiteralPath $logs -Directory | Sort-Object LastWriteTime
    if (-not $dirs) { throw "no sessions under $logs" }
    if (-not $Latest -and $dirs.Count -gt 1) {
        Write-Warning "no -Session given: using the newest session $($dirs[-1].Name) (pass -Latest to silence)"
    }
    return $dirs[-1].FullName
}

function Get-Sha256([string]$path) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}

$source = Resolve-Session
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$safeTag = ($Tag -replace '[^A-Za-z0-9._-]', '-')
$dest = Join-Path $OutRoot "$stamp-$safeTag"
$copy = Join-Path $dest 'session'
if (Test-Path -LiteralPath $dest) { throw "output already exists: $dest" }
New-Item -ItemType Directory -Path $copy -Force | Out-Null

# Logs only: the receiver writes files elsewhere, and keys must never be copied.
Get-ChildItem -LiteralPath $source -File | Copy-Item -Destination $copy -Force
$logFiles = Get-ChildItem -LiteralPath $copy -File | ForEach-Object { $_.Name }

$head = (& git rev-parse HEAD 2>$null)
$dirty = & git status --short 2>$null
$analysisJson = Join-Path $dest 'analysis.json'
$analysisText = Join-Path $dest 'analysis.txt'
# Analyse the ORIGINAL session: only there does `<data>\received` sit next to
# the logs, which is what turns announced bytes into a verified file size.
& $Python (Join-Path $repo 'tools\analyze_session.py') $source --json |
    Set-Content -LiteralPath $analysisJson -Encoding utf8
& $Python (Join-Path $repo 'tools\analyze_session.py') $source |
    Set-Content -LiteralPath $analysisText -Encoding utf8
$analyzeExit = $LASTEXITCODE

# Hash the stored files that the analysis could resolve: a real transfer is
# only proven by bytes on disk, so the hash belongs in the frozen manifest.
$receivedDir = Join-Path $PackageRoot 'data\received'
$receivedHashes = [ordered]@{}
try {
    $parsed = Get-Content -LiteralPath $analysisJson -Raw | ConvertFrom-Json
    foreach ($item in $parsed.transfers) {
        if ($item.status -eq 'complete' -and $item.name) {
            $path = Join-Path $receivedDir $item.name
            if (Test-Path -LiteralPath $path -PathType Leaf) {
                $receivedHashes[$item.name] = Get-Sha256 $path
            }
        }
    }
} catch {
    Write-Warning "could not hash received files: $($_.Exception.Message)"
}

# The session does not record which binaries produced it; hash the current ones
# so a later reader can tell whether the build moved underneath the evidence.
$binaries = @(
    'build\awdl_netd\Release\awdl_netd.exe',
    'build\supervisor\Release\AirBridge32.exe',
    'build\radio_usb\Release\airbridge_radio.exe'
)
$binaryHashes = [ordered]@{}
foreach ($path in $binaries) { $binaryHashes[$path] = Get-Sha256 $path }

$manifest = [ordered]@{
    collected_at = (Get-Date).ToString('o')
    source_session = $source
    source_session_name = Split-Path -Leaf $source
    tag = $Tag
    git_head = if ($head) { $head.Trim() } else { $null }
    git_dirty_files = @($dirty | Where-Object { $_ })
    log_files = $logFiles
    log_sha256 = [ordered]@{}
    received_sha256 = $receivedHashes
    current_build_binaries = $binaryHashes
    analyze_session_exit = $analyzeExit
    note = 'logs only; no keys, no received files, no radio or chain start'
}
foreach ($name in $logFiles) { $manifest.log_sha256[$name] = Get-Sha256 (Join-Path $copy $name) }
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $dest 'manifest.json') -Encoding utf8

Write-Output "frozen: $dest"
Write-Output "logs:   $($logFiles -join ', ')"
if ($analyzeExit -ne 0) { Write-Warning "analyze_session.py exited $analyzeExit (see $analysisText)" }
Get-Content -LiteralPath $analysisText | ForEach-Object { Write-Output "  $_" }
