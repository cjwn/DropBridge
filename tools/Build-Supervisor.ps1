# Q5: build the tray supervisor with the in-box C# compiler.
#
#   powershell -ExecutionPolicy Bypass -File tools\Build-Supervisor.ps1
#
# Output: build\supervisor\Release\DropBridge.exe (winexe, no console window).
# Package-Headless.ps1 copies it to backend\AirBridge32.exe, where the
# self-extracting stub starts it. Package-Distributable.ps1 puts it at the
# package root, next to backend\.
[CmdletBinding()]
param([switch]$Test)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$source = Join-Path $root 'src\supervisor\Program.cs'
$outDir = Join-Path $root 'build\supervisor\Release'
$out = Join-Path $outDir 'DropBridge.exe'
$icon = Join-Path $root 'assets\dropbridge.ico'
$csc = Join-Path $env:SystemRoot 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'

if (-not (Test-Path $csc)) { throw "in-box C# compiler missing: $csc" }
if (-not (Test-Path $source)) { throw "missing $source" }
New-Item -ItemType Directory -Force $outDir | Out-Null

# The icon is optional so a checkout without assets\ still builds; the tray
# falls back to a stock icon in that case.
$iconArgument = @()
if (Test-Path $icon) { $iconArgument = @("/win32icon:$icon") }

& $csc /nologo /target:winexe /optimize+ "/out:$out" @iconArgument `
    /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Management.dll $source
if ($LASTEXITCODE -ne 0) { throw "csc failed with $LASTEXITCODE" }

Write-Host ("supervisor: {0} ({1:N0} KB)" -f $out, ((Get-Item $out).Length / 1KB))

# Legacy name: the frozen test packages in build\diagnostics still point at
# this path, so keep a copy until those launchers are updated.
$legacy = Join-Path $outDir 'AirBridge32.exe'
Copy-Item $out $legacy -Force

if ($Test) {
    $testExe = Join-Path $outDir 'channel_sequence_test.exe'
    & $csc /nologo /target:exe /main:ChannelSequenceTest "/out:$testExe" `
        /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Management.dll $source `
        (Join-Path $root 'src\supervisor\channel_sequence_test.cs')
    if ($LASTEXITCODE -ne 0) { throw "test compile failed: $LASTEXITCODE" }
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw "offline test failed: $LASTEXITCODE" }
}
