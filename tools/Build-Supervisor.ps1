# Q5: build the tray supervisor with the in-box C# compiler.
#
#   powershell -ExecutionPolicy Bypass -File tools\Build-Supervisor.ps1
#
# Output: build\supervisor\Release\AirBridge32.exe (winexe, no console window).
# Package-Headless.ps1 copies it to backend\AirBridge32.exe, where the
# self-extracting stub starts it.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$source = Join-Path $root 'src\supervisor\Program.cs'
$outDir = Join-Path $root 'build\supervisor\Release'
$out = Join-Path $outDir 'AirBridge32.exe'
$csc = Join-Path $env:SystemRoot 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'

if (-not (Test-Path $csc)) { throw "in-box C# compiler missing: $csc" }
if (-not (Test-Path $source)) { throw "missing $source" }
New-Item -ItemType Directory -Force $outDir | Out-Null

& $csc /nologo /target:winexe /optimize+ "/out:$out" `
    /r:System.Windows.Forms.dll /r:System.Drawing.dll $source
if ($LASTEXITCODE -ne 0) { throw "csc failed with $LASTEXITCODE" }

Write-Host ("supervisor: {0} ({1:N0} KB)" -f $out, ((Get-Item $out).Length / 1KB))
