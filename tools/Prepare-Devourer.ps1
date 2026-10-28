# Prepare a patched, locked build copy; never edit upstream/devourer.
[CmdletBinding()]
param([string]$Repo = 'build\devourer-airbridge')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$sources = Get-Content (Join-Path $root 'docs\sources.lock.json') -Raw | ConvertFrom-Json
$base = ($sources | Where-Object { $_.name -eq 'devourer' }).commit
$source = Join-Path $root 'upstream\devourer'
if (-not [IO.Path]::IsPathRooted($Repo)) { $Repo = Join-Path $root $Repo }
if (-not (Test-Path $Repo)) {
    & git clone --shared --no-checkout $source $Repo
    if ($LASTEXITCODE -ne 0) { throw 'local devourer clone failed' }
    & git -C $Repo config core.autocrlf false
    & git -C $Repo checkout --detach $base
    if ($LASTEXITCODE -ne 0) { throw 'locked devourer checkout failed' }
}
$head = & git -C $Repo rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $head -ne $base) { throw "unexpected devourer base: $head" }
$patch = Join-Path $root 'patches\devourer-airbridge.patch'
function Test-ReversePatch {
    $ErrorActionPreference = 'Continue'
    & git -C $Repo apply --reverse --check $patch 2>$null
    return $LASTEXITCODE -eq 0
}
if (Test-ReversePatch) { Write-Host "already applied: $Repo"; exit 0 }
& git -C $Repo apply --check $patch
if ($LASTEXITCODE -ne 0) { throw 'devourer patch conflicts; preserve local changes and inspect' }
& git -C $Repo apply $patch
if ($LASTEXITCODE -ne 0) { throw 'devourer patch failed' }
Write-Host "prepared: $Repo ($base + devourer-airbridge.patch)"
