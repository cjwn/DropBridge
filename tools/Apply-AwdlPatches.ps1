# Applies the tracked patches to an upstream/winawdl tree.
# The upstream tree is a research copy and is git-ignored; the patches are the
# tracked artifacts. Run from the repository root.
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

$patches = @(
    'awdl-ipc-host.patch',     # AWDL host IPC backend + receiver proxy/mDNS changes
    'airdrop-identity.patch'   # self-signed certificate without external openssl
)

# Windows PowerShell turns a native command's stderr into a terminating error
# while $ErrorActionPreference is Stop; ask git quietly and read the exit code.
function Invoke-Git([string[]]$Arguments) {
    $ErrorActionPreference = 'Continue'
    & git @Arguments 2>$null | Out-Null
    return $LASTEXITCODE
}

foreach ($name in $patches) {
    $patch = Join-Path $root "patches\$name"
    if (-not (Test-Path $patch)) { throw "patch missing: $patch" }

    if ((Invoke-Git @('-c', "safe.directory=$Repo", '-C', $Repo, 'apply', '--check', $patch)) -eq 0) {
        if ((Invoke-Git @('-c', "safe.directory=$Repo", '-C', $Repo, 'apply', $patch)) -ne 0) {
            throw "git apply failed: $name"
        }
        Write-Host "applied $name"
        continue
    }

    if ((Invoke-Git @('-c', "safe.directory=$Repo", '-C', $Repo, 'apply', '--check', '-R', $patch)) -eq 0) {
        Write-Host "already applied: $name"
        continue
    }
    throw "patch conflicts with the tree and is not already applied: $name"
}
