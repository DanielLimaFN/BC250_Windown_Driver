<#
.SYNOPSIS
    Empacota e assina a imagem de LABORATORIO do bc250mcdm.

.DESCRIPTION
    Copia driver\wdk\output_lab\bc250mcdm.sys, o INF e firmwares para
    driver\wdk\package\output_lab, gera o catalogo bc250mcdm.cat via Inf2Cat
    e assina ambos com o certificado de teste.
#>
[CmdletBinding()]
param(
    [switch]$EnableShaderDispatch
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sourceSys = Join-Path $root 'driver\wdk\output_lab\bc250mcdm.sys'
$sourceInf = Join-Path $root 'driver\wdk\package\bc250mcdm.inf'
$sourceSdmaFirmware = Join-Path $root 'firmware\cyan_skillfish2_sdma.bin'
$sourceRlcFirmware = Join-Path $root 'firmware\cyan_skillfish2_rlc.bin'
$sourcePfpFirmware = Join-Path $root 'firmware\cyan_skillfish2_pfp.bin'
$sourceMeFirmware = Join-Path $root 'firmware\cyan_skillfish2_me.bin'
$sourceCeFirmware = Join-Path $root 'firmware\cyan_skillfish2_ce.bin'
$sourceMecFirmware = Join-Path $root 'firmware\cyan_skillfish2_mec.bin'
$packageDir = Join-Path $root 'driver\wdk\package\output_lab'
$packageManifest = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
$recoveryRoot = 'C:\BC250_RECOVERY.cmd'
$certificateThumbprint = '8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B'

function Find-Tool([string]$Name, [string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw "$Name nao encontrado."
}

if (-not (Test-Path -LiteralPath $sourceSys)) {
    throw "Imagem de lab ausente: $sourceSys. Execute tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup"
}
if (-not (Test-Path -LiteralPath $sourceInf)) { throw "INF ausente: $sourceInf" }
if (-not (Test-Path -LiteralPath $sourceSdmaFirmware)) { throw "Firmware SDMA ausente: $sourceSdmaFirmware" }
if (-not (Test-Path -LiteralPath $sourceRlcFirmware)) { throw "Firmware RLC ausente: $sourceRlcFirmware" }
if (-not (Test-Path -LiteralPath $sourcePfpFirmware)) { throw "Firmware PFP ausente: $sourcePfpFirmware" }
if (-not (Test-Path -LiteralPath $sourceMeFirmware)) { throw "Firmware ME ausente: $sourceMeFirmware" }
if (-not (Test-Path -LiteralPath $sourceCeFirmware)) { throw "Firmware CE ausente: $sourceCeFirmware" }
if (-not (Test-Path -LiteralPath $sourceMecFirmware)) { throw "Firmware MEC ausente: $sourceMecFirmware" }

if (-not (Test-Path -LiteralPath $recoveryRoot)) {
    throw 'Recuperacao C:\BC250_RECOVERY.cmd ausente.'
}

$inf2cat = Find-Tool 'Inf2Cat.exe' @(
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\Inf2Cat.exe',
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x86\Inf2Cat.exe'
)
$signtool = Find-Tool 'signtool.exe' @(
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe',
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe'
)

$certificate = Get-ChildItem Cert:\CurrentUser\My | Where-Object {
    $_.Thumbprint -eq $certificateThumbprint -and $_.HasPrivateKey
}
if (-not $certificate) {
    throw "Certificado de teste com chave privada ausente: $certificateThumbprint"
}

# Guarda de frescor: o empacotador apenas COPIA driver\wdk\output_lab\bc250mcdm.sys.
# Quem o compila e tools\build-bc250mcdm-lab-bringup.ps1 -Execute. Sem esta
# checagem, um SYS obsoleto era assinado e empacotado em silencio, gastando um
# ensaio fisico inteiro com o binario do candidato anterior.
if (-not (Test-Path -LiteralPath $sourceSys -PathType Leaf)) {
    throw ("Imagem de laboratorio ausente: $sourceSys. " +
        'Rode tools\build-bc250mcdm-lab-bringup.ps1 -Execute ' +
        '-IUnderstandThisEnablesDestructiveBringup antes de empacotar.')
}
$sourceSysStamp = (Get-Item -LiteralPath $sourceSys).LastWriteTimeUtc
$staleSources = @(
    Get-ChildItem -LiteralPath (Join-Path $root 'driver\core'),
                                (Join-Path $root 'driver\wdk') `
        -Recurse -File -Include '*.c', '*.h' -ErrorAction SilentlyContinue |
    Where-Object {
        $_.FullName -notlike '*\package\*' -and
        $_.FullName -notlike '*\output*\*' -and
        $_.LastWriteTimeUtc -gt $sourceSysStamp
    }
)
if ($staleSources.Count -ne 0) {
    Write-Host 'FONTES MAIS NOVAS QUE A IMAGEM DE LABORATORIO:'
    foreach ($stale in ($staleSources | Select-Object -First 10)) {
        Write-Host ("  - {0} ({1:u})" -f
            $stale.FullName.Substring($root.Length + 1), $stale.LastWriteTimeUtc)
    }
    if ($staleSources.Count -gt 10) {
        Write-Host ("  ... e mais {0} arquivo(s)" -f ($staleSources.Count - 10))
    }
    throw ('SYS de laboratorio esta obsoleto em relacao as fontes. ' +
        'Rode tools\build-bc250mcdm-lab-bringup.ps1 -Execute ' +
        '-IUnderstandThisEnablesDestructiveBringup e empacote de novo.')
}

New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
$packageSys = Join-Path $packageDir 'bc250mcdm.sys'
$packageInf = Join-Path $packageDir 'bc250mcdm.inf'
$packageCat = Join-Path $packageDir 'bc250mcdm.cat'
$packageSdmaFirmware = Join-Path $packageDir 'cyan_skillfish2_sdma.bin'
$packageRlcFirmware = Join-Path $packageDir 'cyan_skillfish2_rlc.bin'
$packagePfpFirmware = Join-Path $packageDir 'cyan_skillfish2_pfp.bin'
$packageMeFirmware = Join-Path $packageDir 'cyan_skillfish2_me.bin'
$packageCeFirmware = Join-Path $packageDir 'cyan_skillfish2_ce.bin'
$packageMecFirmware = Join-Path $packageDir 'cyan_skillfish2_mec.bin'

Remove-Item -LiteralPath $packageCat -Force -ErrorAction SilentlyContinue
Copy-Item -LiteralPath $sourceSys -Destination $packageSys -Force

# No INF de laboratorio, colocamos DriverVer mais novo e os gates explicitos.
$infContent = Get-Content -LiteralPath $sourceInf
$infContent = $infContent | ForEach-Object {
    if ($_ -match '^DriverVer=') {
        'DriverVer=08/30/2026,3.43.0.0'
    } elseif ($_ -match 'HKR,Parameters\\Probe,SchemaVersion,0x00010001,98') {
        'HKR,Parameters\Probe,SchemaVersion,0x00010001,114'
    } elseif ($_ -match 'HKR,Parameters,SkipHwBringup,0x00010001,1') {
        'HKR,Parameters,SkipHwBringup,0x00010001,0'
    } elseif ($_ -match 'HKR,Parameters,ShaderDispatch,0x00010001,0') {
        'HKR,Parameters,ShaderDispatch,0x00010001,{0}' -f
            ($(if ($EnableShaderDispatch) { 1 } else { 0 }))
    } elseif ($_ -match 'HKR,Parameters,CpSoftReset,0x00010001,0') {
        'HKR,Parameters,CpSoftReset,0x00010001,{0}' -f
            ($(if ($EnableShaderDispatch) { 1 } else { 0 }))
    } elseif ($_ -match 'HKR,Parameters,Vmid0PageTable,0x00010001,0') {
        'HKR,Parameters,Vmid0PageTable,0x00010001,0'
    } elseif ($_ -match 'HKR,Parameters,SdmaAutoCtxSw,0x00010001,1') {
        'HKR,Parameters,SdmaAutoCtxSw,0x00010001,{0}' -f
            ($(if ($EnableShaderDispatch) { 0 } else { 1 }))
    } else {
        $_
    }
}
$infContent | Set-Content -LiteralPath $packageInf -Encoding ascii

$expectedInfValues = [ordered]@{
    'Parameters\Probe,SchemaVersion' = 114
    'Parameters\Driver,ActiveMode' = 1
    'Parameters,SkipHwBringup' = 0
    'Parameters,ShaderDispatch' = $(if ($EnableShaderDispatch) { 1 } else { 0 })
    'Parameters,CpSoftReset' = $(if ($EnableShaderDispatch) { 1 } else { 0 })
    'Parameters,Vmid0PageTable' = 0
    'Parameters,SdmaAutoCtxSw' = $(if ($EnableShaderDispatch) { 0 } else { 1 })
}
$renderedInf = Get-Content -LiteralPath $packageInf -Raw
foreach ($entry in $expectedInfValues.GetEnumerator()) {
    $escapedName = [regex]::Escape($entry.Key)
    $matches = [regex]::Matches(
        $renderedInf,
        "(?m)^HKR,$escapedName,0x00010001,$($entry.Value)\s*$")
    if ($matches.Count -ne 1) {
        throw "INF de laboratorio nao materializou exatamente uma vez: $($entry.Key)=$($entry.Value)."
    }
}

Copy-Item -LiteralPath $sourceSdmaFirmware -Destination $packageSdmaFirmware -Force
Copy-Item -LiteralPath $sourceRlcFirmware -Destination $packageRlcFirmware -Force
Copy-Item -LiteralPath $sourcePfpFirmware -Destination $packagePfpFirmware -Force
Copy-Item -LiteralPath $sourceMeFirmware -Destination $packageMeFirmware -Force
Copy-Item -LiteralPath $sourceCeFirmware -Destination $packageCeFirmware -Force
Copy-Item -LiteralPath $sourceMecFirmware -Destination $packageMecFirmware -Force

& $signtool sign /sha1 $certificateThumbprint /fd SHA256 $packageSys
if ($LASTEXITCODE -ne 0) { throw 'Assinatura do .sys de laboratorio falhou.' }

& $inf2cat /driver:$packageDir /os:10_X64
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $packageCat)) {
    throw 'Inf2Cat recusou o pacote de laboratorio.'
}

& $signtool sign /sha1 $certificateThumbprint /fd SHA256 $packageCat
if ($LASTEXITCODE -ne 0) { throw 'Assinatura do catalogo de laboratorio falhou.' }

& $signtool verify /pa /v $packageSys | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Verificacao Authenticode do .sys de laboratorio falhou.' }
& $signtool verify /pa /v $packageCat | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Verificacao Authenticode do catalogo de laboratorio falhou.' }

$driverVersionLine = Get-Content -LiteralPath $packageInf |
    Where-Object { $_ -match '^DriverVer=' } |
    Select-Object -First 1

$manifest = [ordered]@{
    SchemaVersion = 1
    BuildKind = 'MCDM_LAB_BRINGUP'
    BuiltAtUtc = [DateTime]::UtcNow.ToString('o')
    DriverVer = $driverVersionLine -replace '^DriverVer=', ''
    HardwareValidated = $false
    TelemetrySchemaVersion = 114
    SkipHwBringup = 0
    ShaderDispatchEnabled = [bool]$EnableShaderDispatch
    CpSoftResetEnabled = [bool]$EnableShaderDispatch
    Vmid0PageTableEnabled = $false
    SdmaAutoCtxSwEnabled = -not [bool]$EnableShaderDispatch
    SourceSysSha256 = (Get-FileHash -LiteralPath $sourceSys -Algorithm SHA256).Hash
    PackageSysSha256 = (Get-FileHash -LiteralPath $packageSys -Algorithm SHA256).Hash
    PackageInfSha256 = (Get-FileHash -LiteralPath $packageInf -Algorithm SHA256).Hash
    PackageCatSha256 = (Get-FileHash -LiteralPath $packageCat -Algorithm SHA256).Hash
    SdmaFirmwareSha256 = (Get-FileHash -LiteralPath $packageSdmaFirmware -Algorithm SHA256).Hash
    RlcFirmwareSha256 = (Get-FileHash -LiteralPath $packageRlcFirmware -Algorithm SHA256).Hash
    PfpFirmwareSha256 = (Get-FileHash -LiteralPath $packagePfpFirmware -Algorithm SHA256).Hash
    MeFirmwareSha256 = (Get-FileHash -LiteralPath $packageMeFirmware -Algorithm SHA256).Hash
    CeFirmwareSha256 = (Get-FileHash -LiteralPath $packageCeFirmware -Algorithm SHA256).Hash
    MecFirmwareSha256 = (Get-FileHash -LiteralPath $packageMecFirmware -Algorithm SHA256).Hash
    RecoverySha256 = (Get-FileHash -LiteralPath $recoveryRoot -Algorithm SHA256).Hash
    SignerThumbprint = $certificateThumbprint
    Verdict = 'NAO VALIDADO EM HARDWARE'
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath $packageManifest -Encoding utf8

Write-Host "Pacote MCDM de LABORATORIO construido: $packageDir" -ForegroundColor Green
Write-Host "Manifesto de build: $packageManifest"
Write-Host "NAO VALIDADO EM HARDWARE; imagem de laboratorio com bring-up ativo."
