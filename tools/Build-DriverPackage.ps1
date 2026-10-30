# Build the signed WinUSB driver package that DropBridge installs on first run.
#
#   powershell -ExecutionPolicy Bypass -File tools\Build-DriverPackage.ps1
#   powershell -ExecutionPolicy Bypass -File tools\Build-DriverPackage.ps1 -Force
#
# Produces, in packaging\driver\:
#   airbridge-winusb.inf    the narrow WinUSB INF (checked in, not generated)
#   airbridge-winusb.cat    catalog over the INF, signed with the cert below
#   dropbridge-driver.cer   the public cert, imported at install time
#
# The signing certificate is self-signed and created once per repository, then
# reused: the .cer is what the runtime installer trusts (Root + TrustedPublisher),
# so it must be the same cert that signed the .cat. It is a code-signing cert
# valid for a long time and kept in the user's certificate store by thumbprint
# in packaging\driver\signing-thumbprint.txt (the private key never leaves this
# machine and is not shipped).
#
# Requires signtool.exe + makecat.exe from the Windows SDK (both ship with the
# Visual Studio Build Tools). inf2cat (WDK) is deliberately not required.
using namespace System.Security.Cryptography
[CmdletBinding()]
param(
    [switch]$Force,
    [string]$Subject = 'CN=DropBridge WinUSB Driver'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$driverDir = Join-Path $root 'packaging\driver'
$inf = Join-Path $driverDir 'airbridge-winusb.inf'
$cat = Join-Path $driverDir 'airbridge-winusb.cat'
$cer = Join-Path $driverDir 'dropbridge-driver.cer'
$thumbFile = Join-Path $driverDir 'signing-thumbprint.txt'

function Find-SdkTool([string]$name) {
    $candidates = @(
        "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\$name",
        "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\$name",
        "C:\Program Files\Windows Kits\10\bin\10.0.22621.0\x64\$name"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    $found = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter $name -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1
    if ($found) { return $found.FullName }
    throw "$name not found; install the Windows SDK (Visual Studio Build Tools)"
}

$signtool = Find-SdkTool 'signtool.exe'
$makecat = Find-SdkTool 'makecat.exe'
Write-Host "signtool: $signtool"
Write-Host "makecat : $makecat"

if (-not (Test-Path $inf)) { throw "missing INF: $inf" }

# --- certificate -------------------------------------------------------------
$cert = $null
if ((Test-Path $thumbFile) -and -not $Force) {
    $thumb = (Get-Content $thumbFile -Raw).Trim()
    $cert = Get-ChildItem Cert:\CurrentUser\My |
        Where-Object { $_.Thumbprint -eq $thumb -and $_.NotAfter -gt (Get-Date) } |
        Select-Object -First 1
    if ($cert) { Write-Host "reusing certificate $($cert.Thumbprint)" }
}
if (-not $cert) {
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $Subject `
        -CertStoreLocation Cert:\CurrentUser\My `
        -KeyAlgorithm RSA -KeyLength 2048 `
        -HashAlgorithm SHA256 `
        -NotAfter (Get-Date).AddYears(15) `
        -KeyUsage DigitalSignature `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3')
    Set-Content -LiteralPath $thumbFile -Value $cert.Thumbprint -Encoding ascii
    Write-Host "created certificate $($cert.Thumbprint)"
}
Export-Certificate -Cert $cert -FilePath $cer -Type CERT | Out-Null
Write-Host "public cert: $cer"

# --- catalog -----------------------------------------------------------------
# makecat rather than inf2cat: the INF has a single file and Windows validates
# the hashes and the signature chain, which is what SetupAPI checks.
$cdf = Join-Path $env:TEMP 'airbridge-winusb.cdf'
$cdfText = @"
[CatalogHeader]
Name=airbridge-winusb.cat
PublicVersion=0x0000001
EncodingType=0x00010000
CATATTR1=0x10010001:OSAttr:2:10.0

[CatalogFiles]
<hash>airbridge-winusb.inf=airbridge-winusb.inf
"@
Set-Content -LiteralPath $cdf -Value $cdfText -Encoding ascii

if (Test-Path $cat) { Remove-Item $cat -Force }
# makecat resolves the CDF member names relative to the current directory.
Push-Location $driverDir
try {
    & $makecat $cdf
    if ($LASTEXITCODE -ne 0) { throw "makecat failed: $LASTEXITCODE" }
    if (-not (Test-Path $cat)) { throw "makecat produced no catalog" }

    & $signtool sign /fd SHA256 /sha1 $cert.Thumbprint /s My /tr 'http://timestamp.digicert.com' /td SHA256 $cat
    if ($LASTEXITCODE -ne 0) {
        Write-Warning 'timestamped signing failed; signing without a timestamp'
        & $signtool sign /fd SHA256 /sha1 $cert.Thumbprint /s My $cat
        if ($LASTEXITCODE -ne 0) { throw "signtool sign failed: $LASTEXITCODE" }
    }

    # Self-check without touching any certificate store: the machine does not
    # trust this cert yet (that is what the runtime installer adds), so validate
    # the chain against the exported .cer explicitly.
    Pop-Location
    # Self-check: the machine does not trust this cert yet (the runtime
    # installer adds it to Root + TrustedPublisher). Trust it in the *current
    # user's* root store just for the verification, then remove it again, so the
    # check proves the catalog and its hashes without changing the machine.
    $trusted = Import-Certificate -FilePath $cer -CertStoreLocation Cert:\CurrentUser\Root
    try {
        & $signtool verify /pa /v $cat
        $verifyExit = $LASTEXITCODE
    } finally {
        Remove-Item -LiteralPath ('Cert:\CurrentUser\Root\' + $trusted.Thumbprint) -Force -ErrorAction SilentlyContinue
    }
    if ($verifyExit -ne 0) { throw "signtool verify failed: $verifyExit" }
    Push-Location $driverDir
} finally {
    Pop-Location
}

Write-Host ''
Get-ChildItem $driverDir | Select-Object Name, Length | Format-Table -AutoSize
Write-Host 'Driver package ready: packaging\driver\'
