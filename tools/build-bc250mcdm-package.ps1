[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sourceSys = Join-Path $root 'driver\wdk\output\bc250mcdm.sys'
$sourceInf = Join-Path $root 'driver\wdk\package\bc250mcdm.inf'
$driverObject = Join-Path $root 'driver\wdk\output\bc250_active_miniport_driver.obj'
$buildContract = Join-Path $root 'driver\wdk\build-contract.bat'
$packageDir = Join-Path $root 'driver\wdk\package\output_active'
$recoveryRoot = 'C:\BC250_RECOVERY.cmd'
$certificateThumbprint = '8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B'
$manifestAudit = Join-Path $root 'tools\audit-bc250-cp-manifest.py'
$safetyAudit = Join-Path $root 'tools\audit-bc250mcdm-safety.py'
$ddiAudit = Join-Path $root 'tools\audit-mcdm-ddi-matrix.py'
$recoveryAudit = Join-Path $root 'tools\audit-bc250-recovery-readiness.ps1'

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
if (-not (Test-Path -LiteralPath $recoveryRoot)) {
    throw 'Recuperacao C:\BC250_RECOVERY.cmd ausente.'
}
& powershell -NoProfile -ExecutionPolicy Bypass -File $recoveryAudit
if ($LASTEXITCODE -ne 0) {
    throw 'Pacote bloqueado: recovery instalado nao cobre integralmente bc250mcdm.'
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

Write-Host "Pacote MCDM fail-closed construido: $packageDir"
Write-Host "NAO VALIDADO EM HARDWARE; ativacao permanece bloqueada ate os gates runtime."
