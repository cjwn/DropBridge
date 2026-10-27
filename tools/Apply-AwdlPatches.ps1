# Applies the user-space AWDL data-plane changes to upstream/winawdl.
# The upstream tree is a research copy and is git-ignored; the patch is the
# tracked artifact. Run from the repository root.
#
#   powershell -ExecutionPolicy Bypass -File tools/Apply-AwdlPatches.ps1
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$repo = Join-Path $root 'upstream\winawdl'
$patch = Join-Path $root 'patches\awdl-ipc-host.patch'
if (-not (Test-Path $patch)) { throw "patch missing: $patch" }

git -c safe.directory="$repo" -C $repo apply --check $patch
if ($LASTEXITCODE -ne 0) {
    Write-Host 'patch is already applied, or the tree conflicts; no changes made'
    exit 0
}
git -c safe.directory="$repo" -C $repo apply $patch
Write-Host "applied $patch"
