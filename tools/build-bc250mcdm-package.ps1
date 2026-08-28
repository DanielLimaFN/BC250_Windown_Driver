[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sourceSys = Join-Path $root 'driver\wdk\output\bc250mcdm.sys'
$sourceInf = Join-Path $root 'driver\wdk\package\bc250mcdm.inf'
$sourceSdmaFirmware = Join-Path $root 'firmware\cyan_skillfish2_sdma.bin'
$sourceRlcFirmware = Join-Path $root 'firmware\cyan_skillfish2_rlc.bin'
$sourcePfpFirmware = Join-Path $root 'firmware\cyan_skillfish2_pfp.bin'
$sourceMeFirmware = Join-Path $root 'firmware\cyan_skillfish2_me.bin'
$sourceCeFirmware = Join-Path $root 'firmware\cyan_skillfish2_ce.bin'
$sourceMecFirmware = Join-Path $root 'firmware\cyan_skillfish2_mec.bin'
$driverObject = Join-Path $root 'driver\wdk\output\bc250_active_miniport_driver.obj'
$buildContract = Join-Path $root 'driver\wdk\build-contract.bat'
$packageDir = Join-Path $root 'driver\wdk\package\output_active'
$packageManifest = Join-Path $root 'driver\wdk\package\output_active.manifest.json'
$recoveryRoot = 'C:\BC250_RECOVERY.cmd'
$certificateThumbprint = '8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B'
$manifestAudit = Join-Path $root 'tools\audit-bc250-cp-manifest.py'
$safetyAudit = Join-Path $root 'tools\audit-bc250mcdm-safety.py'
$ddiAudit = Join-Path $root 'tools\audit-mcdm-ddi-matrix.py'

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
if (-not (Test-Path -LiteralPath $sourceSdmaFirmware)) { throw "Firmware SDMA ausente: $sourceSdmaFirmware" }
if (-not (Test-Path -LiteralPath $sourceRlcFirmware)) { throw "Firmware RLC ausente: $sourceRlcFirmware" }
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
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\Inf2Cat.exe',
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x86\Inf2Cat.exe'
)
$signtool = Find-Tool 'signtool.exe' @(
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe',
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe'
)

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
$packagePfpFirmware = Join-Path $packageDir 'cyan_skillfish2_pfp.bin'
$packageMeFirmware = Join-Path $packageDir 'cyan_skillfish2_me.bin'
$packageCeFirmware = Join-Path $packageDir 'cyan_skillfish2_ce.bin'
$packageMecFirmware = Join-Path $packageDir 'cyan_skillfish2_mec.bin'
Remove-Item -LiteralPath $packageCat -Force -ErrorAction SilentlyContinue
Copy-Item -LiteralPath $sourceSys -Destination $packageSys -Force
Copy-Item -LiteralPath $sourceInf -Destination $packageInf -Force
Copy-Item -LiteralPath $sourceSdmaFirmware -Destination $packageSdmaFirmware -Force
Copy-Item -LiteralPath $sourceRlcFirmware -Destination $packageRlcFirmware -Force
Copy-Item -LiteralPath $sourcePfpFirmware -Destination $packagePfpFirmware -Force
Copy-Item -LiteralPath $sourceMeFirmware -Destination $packageMeFirmware -Force
Copy-Item -LiteralPath $sourceCeFirmware -Destination $packageCeFirmware -Force
Copy-Item -LiteralPath $sourceMecFirmware -Destination $packageMecFirmware -Force

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

$driverVersionLine = Get-Content -LiteralPath $packageInf |
    Where-Object { $_ -match '^DriverVer=' } |
    Select-Object -First 1
$manifest = [ordered]@{
    SchemaVersion = 1
    BuildKind = 'MCDM_FAIL_CLOSED'
    BuiltAtUtc = [DateTime]::UtcNow.ToString('o')
    DriverVer = $driverVersionLine -replace '^DriverVer=', ''
    HardwareValidated = $false
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

Write-Host "Pacote MCDM fail-closed construido: $packageDir"
Write-Host "Manifesto de build: $packageManifest"
Write-Host "NAO VALIDADO EM HARDWARE; ativacao permanece bloqueada ate os gates runtime."
