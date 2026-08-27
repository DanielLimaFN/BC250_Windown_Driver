[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$packageDir = Join-Path $root 'driver\wdk\package\output_active'
$manifestPath = Join-Path $root 'driver\wdk\package\output_active.manifest.json'
$sourceSys = Join-Path $root 'driver\wdk\output\bc250mcdm.sys'
$recoveryPath = 'C:\BC250_RECOVERY.cmd'
$errors = [System.Collections.Generic.List[string]]::new()

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        $errors.Add("arquivo ausente ($Label): $Path")
        return $false
    }
    return $true
}

$packageSys = Join-Path $packageDir 'bc250mcdm.sys'
$packageInf = Join-Path $packageDir 'bc250mcdm.inf'
$packageCat = Join-Path $packageDir 'bc250mcdm.cat'
$packageSdmaFirmware = Join-Path $packageDir 'cyan_skillfish2_sdma.bin'
$packageRlcFirmware = Join-Path $packageDir 'cyan_skillfish2_rlc.bin'
$packagePfpFirmware = Join-Path $packageDir 'cyan_skillfish2_pfp.bin'
$requiredFiles = @(
    @{ Path = $manifestPath; Label = 'manifesto' },
    @{ Path = $sourceSys; Label = 'SYS bruto' },
    @{ Path = $packageSys; Label = 'SYS assinado' },
    @{ Path = $packageInf; Label = 'INF' },
    @{ Path = $packageCat; Label = 'catalogo' },
    @{ Path = $packageSdmaFirmware; Label = 'firmware SDMA' },
    @{ Path = $packageRlcFirmware; Label = 'firmware RLC' },
    @{ Path = $packagePfpFirmware; Label = 'firmware PFP' },
    @{ Path = $recoveryPath; Label = 'recovery instalado' }
)
foreach ($required in $requiredFiles) {
    [void](Require-File $required.Path $required.Label)
}
if ($errors.Count -ne 0) {
    Write-Host 'BC250 MCDM PACKAGE AUDIT: BLOCKED'
    foreach ($errorMessage in $errors) { Write-Host "  - $errorMessage" }
    exit 1
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.SchemaVersion -ne 1) { $errors.Add('SchemaVersion inesperada') }
if ($manifest.BuildKind -ne 'MCDM_FAIL_CLOSED') {
    $errors.Add('BuildKind nao e MCDM_FAIL_CLOSED')
}
if ($manifest.HardwareValidated -ne $false -or
    $manifest.Verdict -ne 'NAO VALIDADO EM HARDWARE') {
    $errors.Add('manifesto nao preserva o veredito fail-closed')
}

$hashChecks = @(
    @{ Path = $sourceSys; Expected = $manifest.SourceSysSha256; Label = 'SYS bruto' },
    @{ Path = $packageSys; Expected = $manifest.PackageSysSha256; Label = 'SYS assinado' },
    @{ Path = $packageInf; Expected = $manifest.PackageInfSha256; Label = 'INF' },
    @{ Path = $packageCat; Expected = $manifest.PackageCatSha256; Label = 'catalogo' },
    @{ Path = $packageSdmaFirmware; Expected = $manifest.SdmaFirmwareSha256; Label = 'firmware SDMA' },
    @{ Path = $packageRlcFirmware; Expected = $manifest.RlcFirmwareSha256; Label = 'firmware RLC' },
    @{ Path = $packagePfpFirmware; Expected = $manifest.PfpFirmwareSha256; Label = 'firmware PFP' },
    @{ Path = $recoveryPath; Expected = $manifest.RecoverySha256; Label = 'recovery' }
)
foreach ($check in $hashChecks) {
    $actual = (Get-FileHash -LiteralPath $check.Path -Algorithm SHA256).Hash
    if ($actual -ne $check.Expected) {
        $errors.Add("hash divergente ($($check.Label))")
    }
}

foreach ($signedPath in @($packageSys, $packageCat)) {
    $signature = Get-AuthenticodeSignature -LiteralPath $signedPath
    if ($signature.Status -ne 'Valid' -or
        $null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne $manifest.SignerThumbprint) {
        $errors.Add("assinatura invalida ou signer inesperado: $signedPath")
    }
}

$inf = Get-Content -LiteralPath $packageInf -Raw
foreach ($needle in @(
    'Class=ComputeAccelerator',
    'PCI\VEN_1002&DEV_13FE',
    'AddService=bc250mcdm,',
    'ServiceBinary=%12%\bc250mcdm.sys',
    'CatalogFile=bc250mcdm.cat',
    'cyan_skillfish2_sdma.bin',
    'cyan_skillfish2_rlc.bin',
    'cyan_skillfish2_pfp.bin'
)) {
    if (-not $inf.Contains($needle)) { $errors.Add("identidade INF ausente: $needle") }
}
if (-not $inf.Contains("DriverVer=$($manifest.DriverVer)")) {
    $errors.Add('DriverVer do INF diverge do manifesto')
}

Write-Host 'BC250 MCDM PACKAGE AUDIT'
Write-Host "  DriverVer: $($manifest.DriverVer)"
Write-Host "  source SYS: $($manifest.SourceSysSha256)"
Write-Host "  signed SYS: $($manifest.PackageSysSha256)"
Write-Host "  catalog:    $($manifest.PackageCatSha256)"
Write-Host "  SDMA FW:    $($manifest.SdmaFirmwareSha256)"
Write-Host "  RLC FW:     $($manifest.RlcFirmwareSha256)"
Write-Host "  PFP FW:     $($manifest.PfpFirmwareSha256)"
Write-Host "  signer:     $($manifest.SignerThumbprint)"
Write-Host '  install/binding performed: NO'
Write-Host '  hardware validation: NO'

if ($errors.Count -ne 0) {
    Write-Host 'BC250 MCDM PACKAGE AUDIT: BLOCKED'
    foreach ($errorMessage in $errors) { Write-Host "  - $errorMessage" }
    exit 1
}

Write-Host 'BC250 MCDM PACKAGE AUDIT: PASS (INTEGRITY ONLY)'
Write-Host 'NAO VALIDADO EM HARDWARE'
exit 0
