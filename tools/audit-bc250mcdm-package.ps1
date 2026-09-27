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
$packageMeFirmware = Join-Path $packageDir 'cyan_skillfish2_me.bin'
$packageCeFirmware = Join-Path $packageDir 'cyan_skillfish2_ce.bin'
$packageMecFirmware = Join-Path $packageDir 'cyan_skillfish2_mec.bin'
$requiredFiles = @(
    @{ Path = $manifestPath; Label = 'manifesto' },
    @{ Path = $sourceSys; Label = 'SYS bruto' },
    @{ Path = $packageSys; Label = 'SYS assinado' },
    @{ Path = $packageInf; Label = 'INF' },
    @{ Path = $packageCat; Label = 'catalogo' },
    @{ Path = $packageSdmaFirmware; Label = 'firmware SDMA' },
    @{ Path = $packageRlcFirmware; Label = 'firmware RLC' },
    @{ Path = $packagePfpFirmware; Label = 'firmware PFP' },
    @{ Path = $packageMeFirmware; Label = 'firmware ME' },
    @{ Path = $packageCeFirmware; Label = 'firmware CE' },
    @{ Path = $packageMecFirmware; Label = 'firmware MEC' },
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
if ($manifest.BuildKind -notin @('MCDM_FAIL_CLOSED', 'MCDM_TDR_INDUCED_HANG')) {
    $errors.Add('BuildKind desconhecido')
}
if ($manifest.HardwareValidated -ne $false -or
    $manifest.Verdict -ne 'NAO VALIDADO EM HARDWARE') {
    $errors.Add('manifesto nao preserva o veredito fail-closed')
}
$expectedPolicy = [ordered]@{
    ProfileMode = 'DISPLAY_RENDER_FAIL_CLOSED'
    WddmContractVersion = 20
    RenderNode = 1
    RenderOnlyLab = 0
    SkipHwBringup = 1
    ShaderDispatchEnabled = $false
    CpSoftResetEnabled = $false
    Vmid0PageTableEnabled = $false
    SdmaAutoCtxSwEnabled = $true
    PersistentSubmissionEnabled = $true
    GpuvaLabPromotionEnabled = $false
}
foreach ($entry in $expectedPolicy.GetEnumerator()) {
    $actualProperty = $manifest.PSObject.Properties[$entry.Key]
    if ($null -eq $actualProperty -or $actualProperty.Value -ne $entry.Value) {
        $errors.Add("politica do manifesto diverge: $($entry.Key)")
    }
}
if ([string]$manifest.VisualStudio -ne '2026' -or
    [string]$manifest.WindowsKitVersion -ne '10.0.28000.0' -or
    [string]$manifest.CompilerPath -notmatch '\\Microsoft Visual Studio\\18\\' -or
    [string]$manifest.Inf2CatPath -notmatch '\\10\.0\.28000\.0\\' -or
    [string]$manifest.SignToolPath -notmatch '\\10\.0\.28000\.0\\') {
    $errors.Add('manifesto nao prova VS2026 + SDK/WDK 28000')
}

$hashChecks = @(
    @{ Path = $sourceSys; Expected = $manifest.SourceSysSha256; Label = 'SYS bruto' },
    @{ Path = $packageSys; Expected = $manifest.PackageSysSha256; Label = 'SYS assinado' },
    @{ Path = $packageInf; Expected = $manifest.PackageInfSha256; Label = 'INF' },
    @{ Path = $packageCat; Expected = $manifest.PackageCatSha256; Label = 'catalogo' },
    @{ Path = $packageSdmaFirmware; Expected = $manifest.SdmaFirmwareSha256; Label = 'firmware SDMA' },
    @{ Path = $packageRlcFirmware; Expected = $manifest.RlcFirmwareSha256; Label = 'firmware RLC' },
    @{ Path = $packagePfpFirmware; Expected = $manifest.PfpFirmwareSha256; Label = 'firmware PFP' },
    @{ Path = $packageMeFirmware; Expected = $manifest.MeFirmwareSha256; Label = 'firmware ME' },
    @{ Path = $packageCeFirmware; Expected = $manifest.CeFirmwareSha256; Label = 'firmware CE' },
    @{ Path = $packageMecFirmware; Expected = $manifest.MecFirmwareSha256; Label = 'firmware MEC' },
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
    'Class=Display',
    'PCI\VEN_1002&DEV_13FE',
    'AddService=bc250mcdm,',
    'ServiceBinary=%12%\bc250mcdm.sys',
    'CatalogFile=bc250mcdm.cat',
    'cyan_skillfish2_sdma.bin',
    'cyan_skillfish2_rlc.bin',
    'cyan_skillfish2_pfp.bin',
    'cyan_skillfish2_me.bin',
    'cyan_skillfish2_ce.bin',
    'cyan_skillfish2_mec.bin',
    'HKR,Parameters,WddmContractVersion,0x00010001,20',
    'HKR,Parameters,RenderNode,0x00010001,1',
    'HKR,Parameters,RenderOnlyLab,0x00010001,0',
    'HKR,Parameters,SkipHwBringup,0x00010001,1',
    'HKR,Parameters,ShaderDispatch,0x00010001,0',
    'HKR,Parameters,CpSoftReset,0x00010001,0',
    'HKR,Parameters,Vmid0PageTable,0x00010001,0',
    'HKR,Parameters,SdmaAutoCtxSw,0x00010001,1',
    'HKR,Parameters,PersistentSubmission,0x00010001,1',
    'HKR,Parameters,GpuvaLabPromotion,0x00010001,0'
)) {
    if (-not $inf.Contains($needle)) { $errors.Add("identidade INF ausente: $needle") }
}
if (-not $inf.Contains("DriverVer=$($manifest.DriverVer)")) {
    $errors.Add('DriverVer do INF diverge do manifesto')
}
$tdrInfMatch = [regex]::Match(
    $inf,
    '(?m)^HKR,Parameters,TdrInduceHang,0x00010001,(\d+)\s*$')
if (-not $tdrInfMatch.Success) {
    $errors.Add('TdrInduceHang ausente do INF')
} else {
    $tdrInfValue = [int]$tdrInfMatch.Groups[1].Value
    if ($tdrInfValue -ne [int]$manifest.TdrInduceHang -or
        [bool]($tdrInfValue -ne 0) -ne [bool]$manifest.TdrInduceHangArmed) {
        $errors.Add('TdrInduceHang diverge entre INF e manifesto')
    }
    if ($tdrInfValue -notin @(0, 1)) {
        $errors.Add('TdrInduceHang do INF nao e booleano 0/1')
    }
    if (($manifest.BuildKind -eq 'MCDM_TDR_INDUCED_HANG') -ne ($tdrInfValue -eq 1)) {
        $errors.Add('BuildKind nao corresponde ao estado TDR do INF')
    }
}

Write-Host 'BC250 MCDM PACKAGE AUDIT'
Write-Host "  DriverVer: $($manifest.DriverVer)"
Write-Host "  source SYS: $($manifest.SourceSysSha256)"
Write-Host "  signed SYS: $($manifest.PackageSysSha256)"
Write-Host "  catalog:    $($manifest.PackageCatSha256)"
Write-Host "  SDMA FW:    $($manifest.SdmaFirmwareSha256)"
Write-Host "  RLC FW:     $($manifest.RlcFirmwareSha256)"
Write-Host "  PFP FW:     $($manifest.PfpFirmwareSha256)"
Write-Host "  ME FW:      $($manifest.MeFirmwareSha256)"
Write-Host "  CE FW:      $($manifest.CeFirmwareSha256)"
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
