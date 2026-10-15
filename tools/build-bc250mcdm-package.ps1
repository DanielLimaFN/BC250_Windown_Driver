[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')]
    [string]$PackageDriverVersion,
    [switch]$ArmTdrInducedHang,
    [switch]$IAcceptDeliberateHangAndPossibleBugCheck
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sourceSys = Join-Path $root 'driver\wdk\output\bc250mcdm.sys'
$sourceInf = Join-Path $root 'driver\wdk\package\bc250mcdm.inf'
$sourceSdmaFirmware = Join-Path $root 'firmware\cyan_skillfish2_sdma.bin'
$sourceRlcFirmware = Join-Path $root 'firmware\cyan_skillfish2_rlc.bin'
$sourceSdma1Firmware = Join-Path $root 'firmware\cyan_skillfish2_sdma1.bin'
$sourcePfpFirmware = Join-Path $root 'firmware\cyan_skillfish2_pfp.bin'
$sourceMeFirmware = Join-Path $root 'firmware\cyan_skillfish2_me.bin'
$sourceCeFirmware = Join-Path $root 'firmware\cyan_skillfish2_ce.bin'
$sourceMecFirmware = Join-Path $root 'firmware\cyan_skillfish2_mec.bin'
$sourceUmd = Join-Path $root 'driver\umd\output\bc250umd.dll'
# WSI.1\ICD.1 (doc 757): a RADV vai no DriverStore e o loader do Vulkan a acha
# pelo adaptador (VulkanDriverName no INF), como nos drivers da AMD. E' a mesma
# DLL que a UMD carrega da pasta de build.
$sourceRadv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$sourceZlib = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\z-1.dll'
$sourceVkIcd = Join-Path $root 'driver\wdk\package\bc250_vk_icd.json'
$driverObject = Join-Path $root 'driver\wdk\output\bc250_active_miniport_driver.obj'
$buildContract = Join-Path $root 'driver\wdk\build-contract.bat'
$packageDir = Join-Path $root 'driver\wdk\package\output_active'
$packageManifest = Join-Path $root 'driver\wdk\package\output_active.manifest.json'
$recoveryRoot = 'C:\BC250_RECOVERY.cmd'
$certificateThumbprint = '8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B'
$manifestAudit = Join-Path $root 'tools\audit-bc250-cp-manifest.py'
$safetyAudit = Join-Path $root 'tools\audit-bc250mcdm-safety.py'
$ddiAudit = Join-Path $root 'tools\audit-mcdm-ddi-matrix.py'

if ($ArmTdrInducedHang -and -not $IAcceptDeliberateHangAndPossibleBugCheck) {
    throw ('Pacote TDR armado exige -IAcceptDeliberateHangAndPossibleBugCheck. ' +
        'Ele trava a SDMA0 de proposito na primeira submissao do Node 0 e pode ' +
        'terminar em BugCheck/reboot.')
}
if (-not $ArmTdrInducedHang -and $IAcceptDeliberateHangAndPossibleBugCheck) {
    throw '-IAcceptDeliberateHangAndPossibleBugCheck so e valido com -ArmTdrInducedHang.'
}
if ($ArmTdrInducedHang -and [string]::IsNullOrWhiteSpace($PackageDriverVersion)) {
    throw ('Pacote TDR armado exige -PackageDriverVersion com uma versao nova. ' +
        'Nao reutilize a identidade do pacote fail-closed desarmado.')
}

function Find-Tool([string]$Name, [string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw "$Name nao encontrado."
}

if (-not (Test-Path -LiteralPath $sourceSys)) {
    throw "Imagem ausente: $sourceSys. Execute driver\wdk\build-contract.bat."
}
if (-not (Test-Path -LiteralPath $sourceInf)) { throw "INF ausente: $sourceInf" }
$sourceInfTextForPreflight = Get-Content -LiteralPath $sourceInf -Raw
if ($sourceInfTextForPreflight -notmatch
    '(?m)^DriverVer=([^,]+),(\d+\.\d+\.\d+\.\d+)\s*$') {
    throw 'Pacote bloqueado: DriverVer fonte ausente ou invalido.'
}
$sourceDriverVersionForPreflight = $Matches[2]
if ($ArmTdrInducedHang -and
    $PackageDriverVersion -eq $sourceDriverVersionForPreflight) {
    throw ('Pacote TDR bloqueado: -PackageDriverVersion precisa diferir da ' +
        'versao fonte fail-closed.')
}
if (-not (Test-Path -LiteralPath $sourceSdmaFirmware)) { throw "Firmware SDMA ausente: $sourceSdmaFirmware" }
if (-not (Test-Path -LiteralPath $sourceRlcFirmware)) { throw "Firmware RLC ausente: $sourceRlcFirmware" }
if (-not (Test-Path -LiteralPath $sourceSdma1Firmware)) { throw "Firmware SDMA1 ausente: $sourceSdma1Firmware" }
if ((Get-Item -LiteralPath $sourceSdma1Firmware).Length -ne 33792 -or
    (Get-FileHash -LiteralPath $sourceSdma1Firmware -Algorithm SHA256).Hash -ne
        'BD1C0B0F6A6A4F17EDE034F2C844B6553FC444E08915C7BB500C09FDE59D6255') {
    throw 'Firmware SDMA1 diverge do tamanho/hash aprovado.'
}
if (-not (Test-Path -LiteralPath $sourcePfpFirmware)) { throw "Firmware PFP ausente: $sourcePfpFirmware" }
if (-not (Test-Path -LiteralPath $sourceMecFirmware)) { throw "Firmware MEC ausente: $sourceMecFirmware" }
if ((Get-Item -LiteralPath $sourceSdmaFirmware).Length -ne 33792 -or
    (Get-FileHash -LiteralPath $sourceSdmaFirmware -Algorithm SHA256).Hash -ne
        '15D0D3626DA7F2513F03B13BB7E02EEEECCAB276276AE27D0B6067B5F25E9E95') {
    throw 'Firmware SDMA diverge do tamanho/hash aprovado.'
}
if ((Get-Item -LiteralPath $sourceRlcFirmware).Length -ne 25344 -or
    (Get-FileHash -LiteralPath $sourceRlcFirmware -Algorithm SHA256).Hash -ne
        '20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB') {
    throw 'Firmware RLC diverge do tamanho/hash aprovado.'
}
if ((Get-Item -LiteralPath $sourcePfpFirmware).Length -ne 263424 -or
    (Get-FileHash -LiteralPath $sourcePfpFirmware -Algorithm SHA256).Hash -ne
        '50E56DC1913571FF589425D2B059BDDFC5E8A2B4BC22D3EB78CAC75D1E0479A1') {
    throw 'Firmware PFP diverge do tamanho/hash aprovado.'
}
if ((Get-Item -LiteralPath $sourceMecFirmware).Length -ne 268592 -or
    (Get-FileHash -LiteralPath $sourceMecFirmware -Algorithm SHA256).Hash -ne
        'B1C1F843A8FAAA2DE537D6BA05052A3776B5A1B73F8A3C3F5ED9D568489776A4') {
    throw 'Firmware MEC diverge do tamanho/hash aprovado.'
}
$driverSourceFiles = @(
    Get-ChildItem -LiteralPath (Join-Path $root 'driver\core') -Recurse -File |
        Where-Object { $_.Extension -in @('.c', '.h') }
    Get-ChildItem -LiteralPath (Join-Path $root 'driver\wdk') -Recurse -File |
        Where-Object {
            $_.Extension -in @('.c', '.h') -and
            $_.FullName -notmatch '\\output(\\|$)' -and
            $_.FullName -notmatch '\\package(\\|$)'
        }
)
$newestDriverSource = $driverSourceFiles |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1
if ($null -ne $newestDriverSource -and
    (Get-Item -LiteralPath $sourceSys).LastWriteTimeUtc -lt
        $newestDriverSource.LastWriteTimeUtc) {
    throw "Imagem stale: $($newestDriverSource.FullName) e mais novo que bc250mcdm.sys. Execute driver\wdk\build-contract.bat."
}
if (-not (Test-Path -LiteralPath $recoveryRoot)) {
    throw 'Recuperacao C:\BC250_RECOVERY.cmd ausente.'
}
if ((Get-FileHash -LiteralPath $recoveryRoot -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath (Join-Path $root 'tools\BC250_RECOVERY.cmd') -Algorithm SHA256).Hash) {
    throw 'Pacote bloqueado: recovery instalado diverge da fonte versionada.'
}

& python $ddiAudit
if ($LASTEXITCODE -ne 0) {
    throw 'Pacote bloqueado: matriz estrutural MCDM/GPUVA incompleta.'
}

& python $safetyAudit
if ($LASTEXITCODE -ne 0) {
    throw 'Pacote bloqueado: auditoria fail-closed do MCDM recusou o codigo.'
}

& python $manifestAudit
if ($LASTEXITCODE -ne 0) {
    throw 'Pacote bloqueado: contrato dos firmwares CP divergiu.'
}

$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$dumpbinCandidates = @()
if (Test-Path -LiteralPath $vswhere) {
    $dumpbinCandidates += & $vswhere -latest -find 'VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe'
}
$dumpbin = Find-Tool 'dumpbin.exe' $dumpbinCandidates
$inf2cat = Find-Tool 'Inf2Cat.exe' @(
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x86\Inf2Cat.exe',
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\Inf2Cat.exe',
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x86\Inf2Cat.exe'
)
$signtool = Find-Tool 'signtool.exe' @(
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe',
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe',
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe'
)
$compilerPath = Find-Tool 'cl.exe' @(
    'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\cl.exe'
)
if ($inf2cat -notmatch '\\10\.0\.28000\.0\\' -or
    $signtool -notmatch '\\10\.0\.28000\.0\\' -or
    $compilerPath -notmatch '\\Microsoft Visual Studio\\18\\') {
    throw 'Pacote bloqueado: toolchain efetiva nao e VS2026 + SDK/WDK 28000.'
}

$symbols = (& $dumpbin /symbols $driverObject 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'dumpbin falhou ao auditar o objeto de DriverEntry.' }
if ($symbols -notmatch '(?im)UNDEF.+\|\s*DxgkInitialize\s*$' -or
    (Get-Content -LiteralPath $buildContract -Raw) -notmatch
        'bc250_active_miniport_driver\.obj[\s\S]+displib\.lib' -or
    (Get-Item -LiteralPath $sourceSys).LastWriteTimeUtc -lt
        (Get-Item -LiteralPath $driverObject).LastWriteTimeUtc) {
    throw @'
PACOTE BLOQUEADO: a imagem nao comprovou referencia a DxgkInitialize e link por displib.
'@
}

$certificate = Get-ChildItem Cert:\CurrentUser\My | Where-Object {
    $_.Thumbprint -eq $certificateThumbprint -and $_.HasPrivateKey
}
if (-not $certificate) {
    throw "Certificado de teste com chave privada ausente: $certificateThumbprint"
}

New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
$packageSys = Join-Path $packageDir 'bc250mcdm.sys'
$packageInf = Join-Path $packageDir 'bc250mcdm.inf'
$packageCat = Join-Path $packageDir 'bc250mcdm.cat'
$packageSdmaFirmware = Join-Path $packageDir 'cyan_skillfish2_sdma.bin'
$packageRlcFirmware = Join-Path $packageDir 'cyan_skillfish2_rlc.bin'
$packageSdma1Firmware = Join-Path $packageDir 'cyan_skillfish2_sdma1.bin'
$packagePfpFirmware = Join-Path $packageDir 'cyan_skillfish2_pfp.bin'
$packageMeFirmware = Join-Path $packageDir 'cyan_skillfish2_me.bin'
$packageCeFirmware = Join-Path $packageDir 'cyan_skillfish2_ce.bin'
$packageMecFirmware = Join-Path $packageDir 'cyan_skillfish2_mec.bin'
$packageUmd = Join-Path $packageDir 'bc250umd.dll'
$packageRadv = Join-Path $packageDir 'vulkan_radeon.dll'
$packageZlib = Join-Path $packageDir 'z-1.dll'
$packageVkIcd = Join-Path $packageDir 'bc250_vk_icd.json'
Remove-Item -LiteralPath $packageCat -Force -ErrorAction SilentlyContinue
Copy-Item -LiteralPath $sourceSys -Destination $packageSys -Force
Copy-Item -LiteralPath $sourceInf -Destination $packageInf -Force
Copy-Item -LiteralPath $sourceSdmaFirmware -Destination $packageSdmaFirmware -Force
Copy-Item -LiteralPath $sourceRlcFirmware -Destination $packageRlcFirmware -Force
Copy-Item -LiteralPath $sourceSdma1Firmware -Destination $packageSdma1Firmware -Force
Copy-Item -LiteralPath $sourcePfpFirmware -Destination $packagePfpFirmware -Force
Copy-Item -LiteralPath $sourceMeFirmware -Destination $packageMeFirmware -Force
Copy-Item -LiteralPath $sourceCeFirmware -Destination $packageCeFirmware -Force
Copy-Item -LiteralPath $sourceMecFirmware -Destination $packageMecFirmware -Force
foreach ($vulkanSource in @($sourceRadv, $sourceZlib, $sourceVkIcd)) {
    if (-not (Test-Path -LiteralPath $vulkanSource -PathType Leaf)) {
        throw "Arquivo do driver Vulkan ausente: $vulkanSource. Compile a RADV (build_radv.bat)."
    }
}

# item 25: o pacote de uma unica corrida precisa nascer armado, porque o AddReg
# cria a chave durante a instalacao e StartDevice le o valor no mesmo bind. A
# fonte permanece fail-closed (0); somente o INF EMPACOTADO recebe 1, antes do
# Inf2Cat, e o manifesto deriva o estado desse INF materializado.
$packageInfText = Get-Content -LiteralPath $packageInf -Raw
if (-not [string]::IsNullOrWhiteSpace($PackageDriverVersion)) {
    if ($packageInfText -notmatch '(?m)^DriverVer=([^,]+),(\d+\.\d+\.\d+\.\d+)\s*$') {
        throw 'Pacote bloqueado: DriverVer fonte ausente ou invalido.'
    }
    $driverDate = $Matches[1]
    $packageInfText = [regex]::Replace(
        $packageInfText,
        '(?m)^DriverVer=[^,]+,\d+\.\d+\.\d+\.\d+\s*$',
        "DriverVer=$driverDate,$PackageDriverVersion")
}
if ($ArmTdrInducedHang) {
    if ($packageInfText -notmatch '(?m)^HKR,Parameters,TdrInduceHang,0x00010001,0\s*$') {
        throw 'Pacote TDR bloqueado: a fonte nao esta explicitamente fail-closed em TdrInduceHang=0.'
    }
    $packageInfText = [regex]::Replace(
        $packageInfText,
        '(?m)^HKR,Parameters,TdrInduceHang,0x00010001,0\s*$',
        'HKR,Parameters,TdrInduceHang,0x00010001,1')
}
[System.IO.File]::WriteAllText($packageInf, $packageInfText, [System.Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath $sourceVkIcd -Destination $packageVkIcd -Force
foreach ($vulkanDll in @(@($sourceRadv, $packageRadv), @($sourceZlib, $packageZlib))) {
    Copy-Item -LiteralPath $vulkanDll[0] -Destination $vulkanDll[1] -Force
    & $signtool sign /sha1 $certificateThumbprint /fd SHA256 $vulkanDll[1]
    if ($LASTEXITCODE -ne 0) { throw "Assinatura de $($vulkanDll[1]) falhou." }
    & $signtool verify /pa /v $vulkanDll[1] | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Verificacao Authenticode de $($vulkanDll[1]) falhou." }
}
if (Test-Path -LiteralPath $sourceUmd) {
    Copy-Item -LiteralPath $sourceUmd -Destination $packageUmd -Force
    & $signtool sign /sha1 $certificateThumbprint /fd SHA256 $packageUmd
    if ($LASTEXITCODE -ne 0) { throw 'Assinatura da DLL UMD falhou.' }
    & $signtool verify /pa /v $packageUmd | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Verificacao Authenticode da DLL UMD falhou.' }
}

& $signtool sign /sha1 $certificateThumbprint /fd SHA256 $packageSys
if ($LASTEXITCODE -ne 0) { throw 'Assinatura do .sys falhou.' }
& $inf2cat /driver:$packageDir /os:10_X64
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $packageCat)) {
    throw 'Inf2Cat recusou o pacote.'
}
& $signtool sign /sha1 $certificateThumbprint /fd SHA256 $packageCat
if ($LASTEXITCODE -ne 0) { throw 'Assinatura do catalogo falhou.' }

& $signtool verify /pa /v $packageSys | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Verificacao Authenticode do .sys falhou.' }
& $signtool verify /pa /v $packageCat | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Verificacao Authenticode do catalogo falhou.' }
foreach ($signedPath in @($packageSys, $packageCat)) {
    $signature = Get-AuthenticodeSignature -LiteralPath $signedPath
    if ($signature.Status -ne 'Valid' -or
        $signature.SignerCertificate.Thumbprint -ne $certificateThumbprint) {
        throw "Assinatura inesperada em $signedPath"
    }
}

$packageInfLines = Get-Content -LiteralPath $packageInf
$driverVersionLine = $packageInfLines |
    Where-Object { $_ -match '^DriverVer=' } |
    Select-Object -First 1
# A13: o manifesto NAO pode declarar por literal o que o INF grava no registro.
# SdmaPlanoAFallback muda o comportamento do SubmitCommandVirtual, e hardcodar
# aqui criaria a segunda fonte de verdade que ja mordeu este projeto na
# geometria de GPUVA (faixa A11). Sai do INF empacotado, ou o pacote e recusado.
$planoAFallbackLine = $packageInfLines |
    Where-Object { $_ -match '^HKR,Parameters,SdmaPlanoAFallback,0x000100(?:01|03),(\d+)$' } |
    Select-Object -First 1
if (-not $planoAFallbackLine) {
    throw 'Pacote bloqueado: INF sem HKR,Parameters,SdmaPlanoAFallback -- o manifesto nao pode declarar o modo de submissao.'
}
$planoAFallbackLine -match '^HKR,Parameters,SdmaPlanoAFallback,0x000100(?:01|03),(\d+)$' | Out-Null
$planoAFallbackValue = [int]$Matches[1]
# item 25: o manifesto tambem declara se o pacote arma hang de proposito. Sai do
# INF empacotado pelo mesmo motivo do PLANO A -- nao pode haver segunda fonte de
# verdade sobre o que o driver vai fazer no silicio.
$induceHangLine = $packageInfLines |
    Where-Object { $_ -match '^HKR,Parameters,TdrInduceHang,0x00010001,(\d+)$' } |
    Select-Object -First 1
if (-not $induceHangLine) {
    throw 'Pacote bloqueado: INF sem HKR,Parameters,TdrInduceHang -- o manifesto nao pode declarar o ensaio de TDR.'
}
$induceHangLine -match '^HKR,Parameters,TdrInduceHang,0x00010001,(\d+)$' | Out-Null
$induceHangValue = [int]$Matches[1]
# B.1: o manifesto tambem declara se o pacote pede a MEDICAO do anel grafico.
# Mesmo motivo do PLANO A e do ensaio de TDR -- nao pode haver segunda fonte de
# verdade sobre o que o driver fara no silicio. A medicao e' somente leitura,
# mas o operador tem direito de saber, pelo manifesto, o que esta armado.
$gfxRingLine = $packageInfLines |
    Where-Object { $_ -match '^HKR,Parameters,GfxRingPreflight,0x00010001,(\d+)$' } |
    Select-Object -First 1
if (-not $gfxRingLine) {
    throw 'Pacote bloqueado: INF sem HKR,Parameters,GfxRingPreflight -- o manifesto nao pode declarar a medicao do anel grafico.'
}
$gfxRingLine -match '^HKR,Parameters,GfxRingPreflight,0x00010001,(\d+)$' | Out-Null
$gfxRingValue = [int]$Matches[1]
$gfxBringupLine = $packageInfLines |
    Where-Object { $_ -match '^HKR,Parameters,GfxRingBringup,0x00010001,(\d+)$' } |
    Select-Object -First 1
if (-not $gfxBringupLine) {
    throw 'Pacote bloqueado: INF sem HKR,Parameters,GfxRingBringup -- o manifesto nao pode declarar o ensaio do anel grafico.'
}
$gfxBringupLine -match '^HKR,Parameters,GfxRingBringup,0x00010001,(\d+)$' | Out-Null
$gfxBringupValue = [int]$Matches[1]
$manifest = [ordered]@{
    SchemaVersion = 1
    BuildKind = if ($induceHangValue -eq 1) { 'MCDM_TDR_INDUCED_HANG' } else { 'MCDM_FAIL_CLOSED' }
    BuiltAtUtc = [DateTime]::UtcNow.ToString('o')
    DriverVer = $driverVersionLine -replace '^DriverVer=', ''
    HardwareValidated = $false
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
    SdmaPlanoAFallback = $planoAFallbackValue
    SdmaPlanoAFallbackEnabled = ($planoAFallbackValue -ne 0)
    TdrInduceHang = $induceHangValue
    TdrInduceHangArmed = ($induceHangValue -ne 0)
    GfxRingPreflight = $gfxRingValue
    GfxRingPreflightArmed = ($gfxRingValue -ne 0)
    GfxRingBringup = $gfxBringupValue
    GfxRingBringupArmed = ($gfxBringupValue -ne 0)
    VisualStudio = '2026'
    CompilerPath = $compilerPath
    CompilerFileVersion = (Get-Item -LiteralPath $compilerPath).VersionInfo.FileVersion
    WindowsKitVersion = '10.0.28000.0'
    Inf2CatPath = $inf2cat
    SignToolPath = $signtool
    SourceSysSha256 = (Get-FileHash -LiteralPath $sourceSys -Algorithm SHA256).Hash
    PackageSysSha256 = (Get-FileHash -LiteralPath $packageSys -Algorithm SHA256).Hash
    PackageInfSha256 = (Get-FileHash -LiteralPath $packageInf -Algorithm SHA256).Hash
    PackageCatSha256 = (Get-FileHash -LiteralPath $packageCat -Algorithm SHA256).Hash
    SdmaFirmwareSha256 = (Get-FileHash -LiteralPath $packageSdmaFirmware -Algorithm SHA256).Hash
    RlcFirmwareSha256 = (Get-FileHash -LiteralPath $packageRlcFirmware -Algorithm SHA256).Hash
    Sdma1FirmwareSha256 = (Get-FileHash -LiteralPath $packageSdma1Firmware -Algorithm SHA256).Hash
    PfpFirmwareSha256 = (Get-FileHash -LiteralPath $packagePfpFirmware -Algorithm SHA256).Hash
    MeFirmwareSha256 = (Get-FileHash -LiteralPath $packageMeFirmware -Algorithm SHA256).Hash
    CeFirmwareSha256 = (Get-FileHash -LiteralPath $packageCeFirmware -Algorithm SHA256).Hash
    MecFirmwareSha256 = (Get-FileHash -LiteralPath $packageMecFirmware -Algorithm SHA256).Hash
    RecoverySha256 = (Get-FileHash -LiteralPath $recoveryRoot -Algorithm SHA256).Hash
    SignerThumbprint = $certificateThumbprint
    Verdict = 'NAO VALIDADO EM HARDWARE'
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath $packageManifest -Encoding utf8

Write-Host "Pacote MCDM construido: $packageDir"
Write-Host "Manifesto de build: $packageManifest"
if ($induceHangValue -eq 1) {
    Write-Warning ('PACOTE TDR ARMADO: a primeira submissao do Node 0 trava a SDMA0 ' +
        'de proposito; BugCheck/reboot e um desfecho possivel do ensaio.')
}
Write-Host "NAO VALIDADO EM HARDWARE; ativacao permanece bloqueada ate os gates runtime."
