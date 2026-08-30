[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$packageDir = Join-Path $root 'driver\wdk\package\output_lab'
$manifestPath = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
$sourceSys = Join-Path $root 'driver\wdk\output_lab\bc250mcdm.sys'
$recoveryPath = 'C:\BC250_RECOVERY.cmd'
$errors = [System.Collections.Generic.List[string]]::new()

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Manifesto de laboratorio ausente: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.BuildKind -ne 'MCDM_LAB_BRINGUP') {
    $errors.Add("BuildKind inesperado: $($manifest.BuildKind)")
}
if ([bool]$manifest.HardwareValidated) {
    $errors.Add('manifesto de laboratorio nao pode alegar HardwareValidated=true')
}
if ([uint32]$manifest.TelemetrySchemaVersion -ne 114 -or
    [uint32]$manifest.SkipHwBringup -ne 0) {
    $errors.Add('contrato schema/bring-up invalido: exige Schema 114 e SkipHwBringup=0')
}

$artifactMap = [ordered]@{
    'bc250mcdm.sys' = [string]$manifest.PackageSysSha256
    'bc250mcdm.inf' = [string]$manifest.PackageInfSha256
    'bc250mcdm.cat' = [string]$manifest.PackageCatSha256
    'cyan_skillfish2_sdma.bin' = [string]$manifest.SdmaFirmwareSha256
    'cyan_skillfish2_rlc.bin' = [string]$manifest.RlcFirmwareSha256
    'cyan_skillfish2_pfp.bin' = [string]$manifest.PfpFirmwareSha256
    'cyan_skillfish2_me.bin' = [string]$manifest.MeFirmwareSha256
    'cyan_skillfish2_ce.bin' = [string]$manifest.CeFirmwareSha256
    'cyan_skillfish2_mec.bin' = [string]$manifest.MecFirmwareSha256
}
foreach ($entry in $artifactMap.GetEnumerator()) {
    $path = Join-Path $packageDir $entry.Key
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $errors.Add("artefato ausente: $($entry.Key)")
        continue
    }
    $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ([string]::IsNullOrWhiteSpace($entry.Value) -or $actualHash -ne $entry.Value) {
        $errors.Add("hash divergente: $($entry.Key)")
    }
}
if (-not (Test-Path -LiteralPath $sourceSys -PathType Leaf) -or
    (Get-FileHash -LiteralPath $sourceSys -Algorithm SHA256).Hash -ne
        $manifest.SourceSysSha256) {
    $errors.Add('hash divergente ou fonte SYS de laboratorio ausente')
}
if (-not (Test-Path -LiteralPath $recoveryPath -PathType Leaf) -or
    (Get-FileHash -LiteralPath $recoveryPath -Algorithm SHA256).Hash -ne
        $manifest.RecoverySha256) {
    $errors.Add('hash divergente ou recuperacao ausente')
}

foreach ($fileName in @('bc250mcdm.sys','bc250mcdm.cat')) {
    $path = Join-Path $packageDir $fileName
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
    $signature = Get-AuthenticodeSignature -LiteralPath $path
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid -or
        $null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne $manifest.SignerThumbprint) {
        $errors.Add("assinatura ou signer invalido: $fileName")
    }
}

$infPath = Join-Path $packageDir 'bc250mcdm.inf'
if (Test-Path -LiteralPath $infPath -PathType Leaf) {
    $infText = Get-Content -LiteralPath $infPath -Raw
    $driverVerMatches = [regex]::Matches(
        $infText, "(?m)^DriverVer=$([regex]::Escape([string]$manifest.DriverVer))\s*$")
    if ($driverVerMatches.Count -ne 1) {
        $errors.Add('DriverVer do INF diverge do manifesto')
    }
    $expectedInfValues = [ordered]@{
        'Parameters\Probe,SchemaVersion' = 114
        'Parameters\Driver,ActiveMode' = 1
        'Parameters,SkipHwBringup' = 0
        'Parameters,ShaderDispatch' = $(if ([bool]$manifest.ShaderDispatchEnabled) { 1 } else { 0 })
        'Parameters,CpSoftReset' = $(if ([bool]$manifest.CpSoftResetEnabled) { 1 } else { 0 })
        'Parameters,Vmid0PageTable' = $(if ([bool]$manifest.Vmid0PageTableEnabled) { 1 } else { 0 })
        'Parameters,SdmaAutoCtxSw' = $(if ([bool]$manifest.SdmaAutoCtxSwEnabled) { 1 } else { 0 })
    }
    foreach ($entry in $expectedInfValues.GetEnumerator()) {
        $escapedName = [regex]::Escape($entry.Key)
        if ([regex]::Matches(
                $infText,
                "(?m)^HKR,$escapedName,0x00010001,$($entry.Value)\s*$").Count -ne 1) {
            $errors.Add("INF diverge do manifesto: $($entry.Key)=$($entry.Value)")
        }
    }
}

if ([bool]$manifest.ShaderDispatchEnabled -and
    (-not [bool]$manifest.CpSoftResetEnabled -or
     [bool]$manifest.Vmid0PageTableEnabled -or
     [bool]$manifest.SdmaAutoCtxSwEnabled)) {
    $errors.Add('receita F8b invalida: exige CpSoftReset=1, Vmid0PageTable=0 e SdmaAutoCtxSw=0')
}

Write-Host 'BC250 MCDM LAB PACKAGE AUDIT'
Write-Host "  DriverVer: $($manifest.DriverVer)"
Write-Host "  schema/skip/shader: $($manifest.TelemetrySchemaVersion) / $($manifest.SkipHwBringup) / $([uint32][bool]$manifest.ShaderDispatchEnabled)"
Write-Host "  recipe cp/vmid0/sdma: $([uint32][bool]$manifest.CpSoftResetEnabled) / $([uint32][bool]$manifest.Vmid0PageTableEnabled) / $([uint32][bool]$manifest.SdmaAutoCtxSwEnabled)"
Write-Host "  package SYS: $($manifest.PackageSysSha256)"
Write-Host "  package INF: $($manifest.PackageInfSha256)"
Write-Host "  package CAT: $($manifest.PackageCatSha256)"
Write-Host '  mode: READ-ONLY'
if ($errors.Count -ne 0) {
    Write-Host 'BC250 MCDM LAB PACKAGE AUDIT: BLOCKED'
    foreach ($errorMessage in $errors) { Write-Host "  - $errorMessage" }
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 1
}

Write-Host 'BC250 MCDM LAB PACKAGE AUDIT: PASS'
Write-Host 'INSTALL/BINDING NOT PERFORMED; NAO VALIDADO EM HARDWARE'
exit 0
