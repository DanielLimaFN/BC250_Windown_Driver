[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sourceSys = Join-Path $root 'driver\wdk\output\bc250mcdm.sys'
$sourceInf = Join-Path $root 'driver\wdk\package\bc250mcdm.inf'
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
Remove-Item -LiteralPath $packageCat -Force -ErrorAction SilentlyContinue
Copy-Item -LiteralPath $sourceSys -Destination $packageSys -Force
Copy-Item -LiteralPath $sourceInf -Destination $packageInf -Force

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
    RecoverySha256 = (Get-FileHash -LiteralPath $recoveryRoot -Algorithm SHA256).Hash
    SignerThumbprint = $certificateThumbprint
    Verdict = 'NAO VALIDADO EM HARDWARE'
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath $packageManifest -Encoding utf8

Write-Host "Pacote MCDM fail-closed construido: $packageDir"
Write-Host "Manifesto de build: $packageManifest"
Write-Host "NAO VALIDADO EM HARDWARE; ativacao permanece bloqueada ate os gates runtime."
