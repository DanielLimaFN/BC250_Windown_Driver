[CmdletBinding()]
param(
    [switch]$EnableHardwareBringup,
    [switch]$EnableDangerousEscapes
)

$ErrorActionPreference = 'Stop'
$packageDir = $PSScriptRoot
$infPath = Join-Path $packageDir 'wddm.inf'
$sysPath = Join-Path $packageDir 'bc250wddm.sys'
$catPath = Join-Path $packageDir 'wddm.cat'
$cerPath = Join-Path $packageDir 'bc250-test.cer'
$diagPath = Join-Path $packageDir 'diag.ps1'
$hardwarePrefix = 'PCI\VEN_1002&DEV_13FE'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Execute este instalador em um PowerShell elevado (Executar como administrador).'
    }
}

function Invoke-PnpUtil {
    param([Parameter(Mandatory)][string[]]$Arguments)

    & pnputil.exe @Arguments
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        throw "pnputil falhou na etapa '$($Arguments -join ' ')' (exit code $code)."
    }
}

Assert-Administrator

foreach ($required in @($infPath, $sysPath, $catPath, $cerPath)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Arquivo obrigatorio ausente no pacote: $required"
    }
}

Write-Host '=== Preflight do ambiente ===' -ForegroundColor Cyan
$secureBoot = $false
try { $secureBoot = [bool](Confirm-SecureBootUEFI) } catch { }
if ($secureBoot) {
    throw 'Secure Boot esta ativo. Este pacote usa certificado de teste e nao deve ser instalado nesse estado.'
}

$bootConfig = (& bcdedit.exe /enum '{current}' 2>&1 | Out-String)
if ($bootConfig -notmatch '(?im)^testsigning\s+(Yes|On|Sim)\s*$') {
    throw "Test signing nao esta habilitado. Execute 'bcdedit /set testsigning on', reinicie e tente novamente."
}

$device = Get-PnpDevice -PresentOnly | Where-Object {
    $_.InstanceId -like "$hardwarePrefix*"
} | Select-Object -First 1
if ($null -eq $device) {
    throw "Nenhum dispositivo presente com Hardware ID $hardwarePrefix foi encontrado."
}
Write-Host "Dispositivo: $($device.InstanceId)"

Write-Host '=== Validando e confiando no certificado de teste ===' -ForegroundColor Cyan
$packageCert = [Security.Cryptography.X509Certificates.X509Certificate2]::new($cerPath)
$sysSignature = Get-AuthenticodeSignature -LiteralPath $sysPath
$catSignature = Get-AuthenticodeSignature -LiteralPath $catPath
if ($null -eq $sysSignature.SignerCertificate -or $null -eq $catSignature.SignerCertificate) {
    throw 'SYS ou catalogo nao possui assinatura Authenticode.'
}
if ($sysSignature.SignerCertificate.Thumbprint -ne $packageCert.Thumbprint -or
    $catSignature.SignerCertificate.Thumbprint -ne $packageCert.Thumbprint) {
    throw 'O certificado incluido nao corresponde ao certificado usado para assinar SYS/CAT.'
}

foreach ($store in @('Cert:\LocalMachine\Root', 'Cert:\LocalMachine\TrustedPublisher')) {
    if (-not (Get-ChildItem -Path $store | Where-Object Thumbprint -eq $packageCert.Thumbprint)) {
        Import-Certificate -FilePath $cerPath -CertStoreLocation $store | Out-Null
    }
}

$sysSignature = Get-AuthenticodeSignature -LiteralPath $sysPath
$catSignature = Get-AuthenticodeSignature -LiteralPath $catPath
if ($sysSignature.Status -ne 'Valid' -or $catSignature.Status -ne 'Valid') {
    throw "Assinatura invalida apos importar o certificado: SYS=$($sysSignature.Status), CAT=$($catSignature.Status)."
}
Write-Host "Certificado: $($packageCert.Thumbprint)"

Write-Host '=== Adicionando e selecionando o pacote ===' -ForegroundColor Cyan
Invoke-PnpUtil -Arguments @('/add-driver', $infPath, '/install')

$servicePath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250v2'
if (-not (Test-Path -LiteralPath $servicePath)) {
    throw 'O servico bc250v2 nao foi criado apos pnputil /add-driver.'
}
$skipBringup = 0
Set-ItemProperty -LiteralPath $servicePath -Name SkipHwBringup -Value $skipBringup
Write-Host "SkipHwBringup=$skipBringup (Hardware Bringup ATIVO)"
$dangerousEscapes = 1
Set-ItemProperty -LiteralPath $servicePath -Name EnableDangerousEscapes -Value $dangerousEscapes
Write-Host "EnableDangerousEscapes=$dangerousEscapes (Escapes de Hardware ATIVOS)"

$parametersPath = Join-Path $servicePath 'Parameters'
if (Test-Path -LiteralPath $parametersPath) {
    Remove-Item -LiteralPath $parametersPath -Recurse -Force
}

Write-Host '=== Reiniciando apenas o dispositivo detectado ===' -ForegroundColor Cyan
Invoke-PnpUtil -Arguments @('/restart-device', $device.InstanceId)

$selectedInf = (Get-PnpDeviceProperty -InstanceId $device.InstanceId `
    -KeyName 'DEVPKEY_Device_DriverInfPath' -ErrorAction Stop).Data
$selectedVersion = (Get-PnpDeviceProperty -InstanceId $device.InstanceId `
    -KeyName 'DEVPKEY_Device_DriverVersion' -ErrorAction Stop).Data
$current = Get-PnpDevice -InstanceId $device.InstanceId

Write-Host '=== Resultado ===' -ForegroundColor Cyan
[PSCustomObject]@{
    Status = $current.Status
    Problem = $current.Problem
    InfSelecionado = $selectedInf
    Versao = $selectedVersion
    InstanceId = $device.InstanceId
} | Format-List

if (Test-Path -LiteralPath $diagPath) {
    & $diagPath
}

if ($current.Status -ne 'OK' -or $current.Problem -ne 'CM_PROB_NONE') {
    throw "O pacote foi adicionado, mas o dispositivo nao terminou em estado OK ($($current.Problem))."
}

Write-Host 'Driver instalado e dispositivo iniciado com sucesso.' -ForegroundColor Green
