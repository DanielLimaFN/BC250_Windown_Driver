[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$packageDir = Join-Path $root 'driver\wdk\package\output_lab'
$manifestPath = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
$sourceSys = Join-Path $root 'driver\wdk\output_lab\bc250mcdm.sys'
$sourcePdb = Join-Path $root 'driver\wdk\output_lab\bc250mcdm.pdb'
$recoveryPath = 'C:\BC250_RECOVERY.cmd'
$errors = [System.Collections.Generic.List[string]]::new()

function Test-BinaryContainsUnicode([string]$Path, [string]$Text) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    $bytes = [IO.File]::ReadAllBytes($Path)
    $needle = [Text.Encoding]::Unicode.GetBytes($Text)
    for ($i = 0; $i -le $bytes.Length - $needle.Length; ++$i) {
        if ($bytes[$i] -ne $needle[0]) { continue }
        $match = $true
        for ($j = 1; $j -lt $needle.Length; ++$j) {
            if ($bytes[$i + $j] -ne $needle[$j]) { $match = $false; break }
        }
        if ($match) { return $true }
    }
    return $false
}

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Manifesto de laboratorio ausente: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ([string]$manifest.VisualStudio -ne '2026' -or
    [string]$manifest.WindowsKitVersion -ne '10.0.28000.0' -or
    [string]$manifest.CompilerPath -notmatch '\\Microsoft Visual Studio\\18\\' -or
    [string]$manifest.Inf2CatPath -notmatch '\\10\.0\.28000\.0\\' -or
    [string]$manifest.SignToolPath -notmatch '\\10\.0\.28000\.0\\') {
    $errors.Add('manifesto nao prova VS2026 + SDK/WDK 28000')
}

function Test-BinaryContainsAscii([string]$Path, [string]$Text) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $needle = [System.Text.Encoding]::ASCII.GetBytes($Text)
    for ($i = 0; $i -le $bytes.Length - $needle.Length; $i++) {
        $matched = $true
        for ($j = 0; $j -lt $needle.Length; $j++) {
            if ($bytes[$i + $j] -ne $needle[$j]) { $matched = $false; break }
        }
        if ($matched) { return $true }
    }
    return $false
}
if ($manifest.BuildKind -ne 'MCDM_LAB_BRINGUP') {
    $errors.Add("BuildKind inesperado: $($manifest.BuildKind)")
}
if ([bool]$manifest.HardwareValidated) {
    $errors.Add('manifesto de laboratorio nao pode alegar HardwareValidated=true')
}
# Schema vem do manifesto; o literal fixo aqui recusava o 3.44/115.
$expectedSchema = [uint32]$manifest.TelemetrySchemaVersion
if ($expectedSchema -lt 114 -or [uint32]$manifest.SkipHwBringup -ne 0) {
    $errors.Add('contrato schema/bring-up invalido: exige Schema >= 114 e SkipHwBringup=0')
}

$artifactMap = [ordered]@{
    'bc250mcdm.sys' = [string]$manifest.PackageSysSha256
    'bc250mcdm.pdb' = [string]$manifest.PackagePdbSha256
    'bc250umd.dll' = [string]$manifest.PackageUmdSha256
    'bc250mcdm.inf' = [string]$manifest.PackageInfSha256
    'bc250mcdm.cat' = [string]$manifest.PackageCatSha256
    'cyan_skillfish2_sdma.bin' = [string]$manifest.SdmaFirmwareSha256
    'cyan_skillfish2_rlc.bin' = [string]$manifest.RlcFirmwareSha256
    'cyan_skillfish2_sdma1.bin' = [string]$manifest.Sdma1FirmwareSha256
    'cyan_skillfish2_pfp.bin' = [string]$manifest.PfpFirmwareSha256
    'cyan_skillfish2_me.bin' = [string]$manifest.MeFirmwareSha256
    'cyan_skillfish2_ce.bin' = [string]$manifest.CeFirmwareSha256
    'cyan_skillfish2_mec.bin' = [string]$manifest.MecFirmwareSha256
    # WSI.1/ICD.1 (doc 757): driver Vulkan no DriverStore
    'vulkan_radeon.dll' = [string]$manifest.PackageRadvSha256
    'z-1.dll' = [string]$manifest.PackageZlibSha256
    'bc250_vk_icd.json' = [string]$manifest.PackageVkIcdSha256
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
$packageSys = Join-Path $packageDir 'bc250mcdm.sys'
$packageUmd = Join-Path $packageDir 'bc250umd.dll'
if ([bool]$manifest.Node1TripodDefaultArmed -or [bool]$manifest.D3D10_11NativeReady) {
    $errors.Add('manifesto tentou armar o tripe ou abrir o gate D3D por default')
}
foreach ($requiredUmdString in @(
        'NODE1_TRIPOD_AUTHORIZED',
        'NODE1_TRIPOD_RESULT',
        'BC250_D3D10_11_NATIVE_READY=0')) {
    if (-not (Test-BinaryContainsAscii $packageUmd $requiredUmdString)) {
        $errors.Add("string obrigatoria do candidato ausente da UMD: $requiredUmdString")
    }
}
if ($expectedSchema -ge 122) {
    foreach ($requiredTelemetry in @(
            'P1RearmQueueMaskApplied',
            'P1RearmQueueMaskReadback',
            'P1RearmQueueMaskPassed')) {
        if (-not (Test-BinaryContainsUnicode $packageSys $requiredTelemetry)) {
            $errors.Add("telemetria Schema 122 ausente do SYS: $requiredTelemetry")
        }
    }
}
if ($expectedSchema -ge 124) {
    foreach ($requiredTelemetry in @(
            'P1RingDword0',
            'P1HqdPqBaseLo',
            'P1PostCpcStatus')) {
        if (-not (Test-BinaryContainsUnicode $packageSys $requiredTelemetry)) {
            $errors.Add("telemetria Schema 124 ausente do SYS: $requiredTelemetry")
        }
    }
}
if ($expectedSchema -ge 128) {
    foreach ($requiredTelemetry in @(
            'GpuvaGateEligible',
            'GpuvaGateOperational',
            'GpuvaGateBlockMask',
            'GpuvaSnapshotGeneration',
            'GpuvaSnapshotEpoch',
            'GpuvaSnapshotLiveCount',
            'GpuvaLabPromotionRequested',
            'GpuvaLabPromotionPresent')) {
        if (-not (Test-BinaryContainsUnicode $packageSys $requiredTelemetry)) {
            $errors.Add("telemetria Schema 128 ausente do SYS: $requiredTelemetry")
        }
    }
}
if ($expectedSchema -ge 134) {
    # Catraca contra a armadilha do empacotador.
    #
    # tools\package-bc250mcdm-lab.ps1 NAO compila o SYS: ele copia e assina o
    # que estiver em driver\wdk\output. Pular o build de laboratorio assina o
    # binario do candidato anterior com o INF novo, e o ensaio inteiro roda
    # sobre codigo que ninguem escreveu neste ciclo. Procurar os nomes de
    # campo como UTF-16 dentro do SYS assinado e' a prova barata de que o
    # binario e o schema pertencem ao mesmo candidato.
    foreach ($requiredTelemetry in @(
            'AbiReadyContext',
            'AbiReadyAllocation',
            'AbiReadyRootPageTable',
            'AbiReadyPublishCount',
            'V5QaiTypeSeenLo',
            'V5QaiTypeSeenHi',
            'V5DdiHist00',
            'V5DdiHist15')) {
        if (-not (Test-BinaryContainsUnicode $packageSys $requiredTelemetry)) {
            $errors.Add("telemetria Schema 134 ausente do SYS: $requiredTelemetry")
        }
    }
}
if (-not (Test-Path -LiteralPath $sourceSys -PathType Leaf) -or
    (Get-FileHash -LiteralPath $sourceSys -Algorithm SHA256).Hash -ne
        $manifest.SourceSysSha256) {
    $errors.Add('hash divergente ou fonte SYS de laboratorio ausente')
}
if (-not (Test-Path -LiteralPath $sourcePdb -PathType Leaf) -or
    (Get-FileHash -LiteralPath $sourcePdb -Algorithm SHA256).Hash -ne
        $manifest.SourcePdbSha256) {
    $errors.Add('hash divergente ou PDB privado exato de laboratorio ausente')
}
$sourceUmd = Join-Path $root 'driver\umd\output\bc250umd.dll'
if (-not (Test-Path -LiteralPath $sourceUmd -PathType Leaf) -or
    (Get-FileHash -LiteralPath $sourceUmd -Algorithm SHA256).Hash -ne
        $manifest.SourceUmdSha256) {
    $errors.Add('hash divergente ou fonte UMD auditada ausente')
}
if (-not (Test-Path -LiteralPath $recoveryPath -PathType Leaf) -or
    (Get-FileHash -LiteralPath $recoveryPath -Algorithm SHA256).Hash -ne
        $manifest.RecoverySha256) {
    $errors.Add('hash divergente ou recuperacao ausente')
}

foreach ($fileName in @('bc250mcdm.sys','bc250umd.dll','bc250mcdm.cat')) {
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
        'Parameters\Probe,SchemaVersion' = $expectedSchema
        'Parameters\Driver,ActiveMode' = 1
        'Parameters,SkipHwBringup' = 0
        'Parameters,ShaderDispatch' = $(if ([bool]$manifest.ShaderDispatchEnabled) { 1 } else { 0 })
        'Parameters,CpSoftReset' = $(if ([bool]$manifest.CpSoftResetEnabled) { 1 } else { 0 })
        'Parameters,Vmid0PageTable' = $(if ([bool]$manifest.Vmid0PageTableEnabled) { 1 } else { 0 })
        'Parameters,PersistentSubmission' = $(if ([bool]$manifest.PersistentSubmissionEnabled) { 1 } else { 0 })
        'Parameters,GpuvaLabPromotion' = $(if ([bool]$manifest.GpuvaLabPromotionEnabled) { 1 } else { 0 })
        'Parameters,SdmaAutoCtxSw' = $(if ([bool]$manifest.SdmaAutoCtxSwEnabled) { 1 } else { 0 })
        'Parameters,RenderNode' = [uint32]$manifest.RenderNode
        'Parameters,RenderOnlyLab' = [uint32]$manifest.RenderOnlyLab
        'Parameters,WddmContractVersion' = [uint32]$manifest.WddmContractVersion
    }
    # GpuvaLabPromotion e' gravado como DEFAULT (0x00010003 inclui NOCLOBBER):
    # o pacote declara 0, mas nao apaga a promocao que a politica pre-bind do
    # binder tenha armado. Exigir o flag aqui impede que ele volte a
    # 0x00010001 sem que ninguem perceba -- foi assim que a promocao armada
    # antes do bind era zerada em silencio pelo instalador.
    $infValueFlags = @{ 'Parameters,GpuvaLabPromotion' = '0x00010003' }
    foreach ($entry in $expectedInfValues.GetEnumerator()) {
        $escapedName = [regex]::Escape($entry.Key)
        $flag = $(if ($infValueFlags.ContainsKey($entry.Key)) {
            $infValueFlags[$entry.Key]
        } else { '0x00010001' })
        if ([regex]::Matches(
                $infText,
                "(?m)^HKR,$escapedName,$flag,$($entry.Value)\s*$").Count -ne 1) {
            $errors.Add("INF diverge do manifesto: $($entry.Key)=$($entry.Value) (flag $flag)")
        }
    }
}

if ([bool]$manifest.ShaderDispatchEnabled -and
    (-not [bool]$manifest.CpSoftResetEnabled -or
     [bool]$manifest.Vmid0PageTableEnabled -or
     [bool]$manifest.SdmaAutoCtxSwEnabled)) {
    $errors.Add('receita F8b invalida: exige CpSoftReset=1, Vmid0PageTable=0 e SdmaAutoCtxSw=0')
}
if ([bool]$manifest.GpuvaLabPromotionEnabled) {
    $errors.Add('pacote distribuível não pode habilitar GpuvaLabPromotion por padrão')
}

# O perfil e' derivado pelo miniport a partir do par (RenderNode, RenderOnlyLab)
# em bc250_active_miniport.c:7124. Um manifesto cujo ProfileMode nao case com o
# par declararia um perfil que o driver nunca vai assumir.
if (@(20, 27) -notcontains [uint32]$manifest.WddmContractVersion) {
    $errors.Add("WddmContractVersion=$($manifest.WddmContractVersion); o DriverEntry so aceita 20 (WDDM2_0) ou 27 (WDDM2_7)")
}
$manifestRenderNode = [uint32]$manifest.RenderNode
$manifestRenderOnlyLab = [uint32]$manifest.RenderOnlyLab
if ($manifestRenderNode -gt 1 -or $manifestRenderOnlyLab -gt 1) {
    $errors.Add("par de perfil fora de {0,1}: RenderNode=$manifestRenderNode RenderOnlyLab=$manifestRenderOnlyLab")
} elseif ($manifestRenderOnlyLab -eq 1 -and $manifestRenderNode -ne 1) {
    $errors.Add('par de perfil invalido: RenderOnlyLab=1 exige RenderNode=1; o driver recusa com STATUS_DEVICE_CONFIGURATION_ERROR')
} else {
    $expectedProfileMode = $(if ($manifestRenderOnlyLab -eq 1) {
        'PHYSICAL_RENDER_ONLY'
    } elseif ($manifestRenderNode -eq 1) {
        'DISPLAY_RENDER_LAB'
    } else {
        'DISPLAY_LAB'
    })
    if ([string]$manifest.ProfileMode -ne $expectedProfileMode) {
        $errors.Add("ProfileMode='$($manifest.ProfileMode)' diverge do par $manifestRenderNode/$manifestRenderOnlyLab; esperado '$expectedProfileMode'")
    }
}

Write-Host 'BC250 MCDM LAB PACKAGE AUDIT'
Write-Host "  DriverVer: $($manifest.DriverVer)"
Write-Host "  schema/skip/shader: $($manifest.TelemetrySchemaVersion) / $($manifest.SkipHwBringup) / $([uint32][bool]$manifest.ShaderDispatchEnabled)"
Write-Host "  recipe cp/vmid0/sdma: $([uint32][bool]$manifest.CpSoftResetEnabled) / $([uint32][bool]$manifest.Vmid0PageTableEnabled) / $([uint32][bool]$manifest.SdmaAutoCtxSwEnabled)"
Write-Host "  package SYS: $($manifest.PackageSysSha256)"
Write-Host "  package PDB: $($manifest.PackagePdbSha256)"
Write-Host "  package UMD: $($manifest.PackageUmdSha256)"
Write-Host "  package INF: $($manifest.PackageInfSha256)"
Write-Host "  package CAT: $($manifest.PackageCatSha256)"
Write-Host ("  profile: {0} (RenderNode={1}, RenderOnlyLab={2})" -f
    $manifest.ProfileMode, $manifestRenderNode, $manifestRenderOnlyLab)
Write-Host ("  contrato WDDM: {0} ({1})" -f $manifest.WddmContractVersion,
    $(if ([uint32]$manifest.WddmContractVersion -eq 20) { 'WDDM2_0' } else { 'WDDM2_7' }))
Write-Host ("  mode: {0}" -f $(if ([bool]$manifest.PersistentSubmissionEnabled) {
    'P1 PERSISTENT SUBMISSION'
} elseif ([bool]$manifest.ShaderDispatchEnabled) {
    'SHADER DISPATCH'
} else {
    'READ-ONLY'
}))
if ($errors.Count -ne 0) {
    Write-Host 'BC250 MCDM LAB PACKAGE AUDIT: BLOCKED'
    foreach ($errorMessage in $errors) { Write-Host "  - $errorMessage" }
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 1
}

Write-Host 'BC250 MCDM LAB PACKAGE AUDIT: PASS'
Write-Host 'INSTALL/BINDING NOT PERFORMED; NAO VALIDADO EM HARDWARE'
exit 0
