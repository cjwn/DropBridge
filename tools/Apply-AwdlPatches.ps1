# Applies the tracked patches to the upstream research copies.
# upstream/ is git-ignored; these patches are the tracked artifacts.
#
#   powershell -ExecutionPolicy Bypass -File tools/Apply-AwdlPatches.ps1
#   powershell -ExecutionPolicy Bypass -File tools/Apply-AwdlPatches.ps1 -Repo build\winawdl-pristine
#
# Each patch is applied at most once. A patch that neither applies nor reverses
# applies means the tree and the tracked artifacts disagree: fail loudly instead
# of reporting success (the old version exited 0 in that case).
[CmdletBinding()]
param(
    [string]$Repo = 'upstream\winawdl'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not [IO.Path]::IsPathRooted($Repo)) { $Repo = Join-Path $root $Repo }
if (-not (Test-Path $Repo)) { throw "upstream tree missing: $Repo" }

# (patch file, repository it applies to, relative to the upstream copy)
$patches = @(
    @{ Name = 'awdl-ipc-host.patch'; Repo = $Repo },
    @{ Name = 'airdrop-identity.patch'; Repo = $Repo },
    @{ Name = 'owl-airbridge.patch'; Repo = (Join-Path $Repo 'upstream\owl') }
)

# Windows PowerShell turns a native command's stderr into a terminating error
# while $ErrorActionPreference is Stop; ask git quietly and read the exit code.
function Invoke-Git([string[]]$Arguments) {
    $ErrorActionPreference = 'Continue'
    & git @Arguments 2>$null | Out-Null
    return $LASTEXITCODE
}

foreach ($entry in $patches) {
    $patch = Join-Path $root "patches\$($entry.Name)"
    $target = $entry.Repo
    if (-not (Test-Path $patch)) { throw "patch missing: $patch" }
    if (-not (Test-Path $target)) {
        Write-Host "skip $($entry.Name): no tree at $target"
        continue
    }

    if ((Invoke-Git @('-c', "safe.directory=$target", '-C', $target, 'apply', '--check', $patch)) -eq 0) {
        if ((Invoke-Git @('-c', "safe.directory=$target", '-C', $target, 'apply', $patch)) -ne 0) {
            throw "git apply failed: $($entry.Name)"
        }
        Write-Host "applied $($entry.Name)"
        continue
    }

    if ((Invoke-Git @('-c', "safe.directory=$target", '-C', $target, 'apply', '--check', '-R', $patch)) -eq 0) {
        Write-Host "already applied: $($entry.Name)"
        continue
    }
    throw "patch conflicts with the tree and is not already applied: $($entry.Name)"
}
