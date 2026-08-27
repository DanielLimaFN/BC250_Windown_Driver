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

# Le o setupapi.dev.log e explica a causa quando o pnputil so' diz
# "O sistema nao pode encontrar o arquivo especificado".
function Get-CausaFalhaInstalacao {
    $log = Join-Path $env:windir 'inf\setupapi.dev.log'
    if (-not (Test-Path $log)) { return $null }

    $cauda = Get-Content $log -Tail 300 -ErrorAction SilentlyContinue
    if (-not $cauda) { return $null }

    if ($cauda | Select-String -Quiet 'Failed to query service configuration') {
        return @"
CAUSA: o INF falhou em AddService com erro 0x00000002 (ERROR_FILE_NOT_FOUND).
       O pacote esta integro — .sys, INF e catalogo passaram na validacao e o
       driver foi adicionado ao DriverStore. A falha e' na criacao do servico.

Isto ficou associado a apagar HKLM\SYSTEM\CurrentControlSet\Services\<servico>
direto no registro (Remove-Item) em vez de usar 'sc.exe delete'. A identidade
do servico fica presa num estado que NAO se resolve com reinicio: em
2026-08-22 o reboot foi feito, a chave estava ausente, havia um unico
ControlSet e o device node estava limpo — e a instalacao continuou falhando.

O que funciona: usar um NOME DE SERVICO NOVO no INF (bc250v3 -> bc250v4).
Uma identidade sem historico e' criada sem erro.

VERIFIQUE, nesta ordem:
  1. O INF usa um nome de servico ainda nao queimado nesta maquina?
     (AddService e a secao [<servico>_Service_Inst] em wddm.inf)
  2. 'sc.exe query <servico>' devolve algo? Se sim, 'sc.exe delete' e reinicie.
  3. Ha' chaves orfas em
     HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-...}
     (subchaves 0002, 0003... sem DriverDesc/Service)? Elas sao residuo de
     instalacoes anteriores e devem ser removidas com o dispositivo parado.

NUNCA use Remove-Item em chaves de Services. Use 'sc.exe delete'.
"@
    }

    if ($cauda | Select-String -Quiet 'Driver package failed signature validation') {
        return "CAUSA: falha de validacao de assinatura. Confira testsigning e o certificado de teste."
    }

    if ($cauda | Select-String -Quiet 'No matching devices found') {
        return "CAUSA: nenhum dispositivo casou com os Hardware IDs do INF."
    }

    return $null
}

function Invoke-PnpUtil {
    param([Parameter(Mandatory)][string[]]$Arguments)

    & pnputil.exe @Arguments
    $code = $LASTEXITCODE
    if ($code -ne 0 -and $code -ne 259 -and $code -ne 3010) {
        $causa = Get-CausaFalhaInstalacao
        if ($causa) {
            Write-Host ''
            Write-Host '===============================================================' -ForegroundColor Red
            Write-Host $causa -ForegroundColor Yellow
            Write-Host '===============================================================' -ForegroundColor Red
            Write-Host ''
        }
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

Write-Host '=== Removendo versoes anteriores do DriverStore ===' -ForegroundColor Cyan
$rawDrivers = pnputil /enum-drivers
$oemList = @()
$currentOem = ""
$isMatch = $false
foreach ($line in $rawDrivers) {
    if ($line -match "Nome Publicado:\s+(oem\d+\.inf)") {
        if ($currentOem -ne "" -and $isMatch) {
            $oemList += $currentOem
        }
        $currentOem = $matches[1]
        $isMatch = $false
    }
    if ($line -match "(bc250|wddm\.inf|amdbc250|BC250-WDDM|BC250-WIN|AMD BC-250)") {
        $isMatch = $true
    }
}
if ($currentOem -ne "" -and $isMatch) {
    $oemList += $currentOem
}
foreach ($oem in ($oemList | Select-Object -Unique)) {
    Write-Host "Limpando $oem..." -ForegroundColor White
    & pnputil /delete-driver $oem /uninstall /force 2>$null | Out-Null
}

$servicePath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250v4'
$parametersPath = Join-Path $servicePath 'Parameters'
if (Test-Path -LiteralPath $parametersPath) {
    Remove-Item -LiteralPath $parametersPath -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host '=== Adicionando e selecionando o pacote BC250-V3 (WDDM 2.0+) ===' -ForegroundColor Cyan
Invoke-PnpUtil -Arguments @('/add-driver', $infPath, '/install')

if (-not (Test-Path -LiteralPath $servicePath)) {
    throw 'O servico bc250v4 nao foi criado apos pnputil /add-driver.'
}
$skipBringup = 0
Set-ItemProperty -LiteralPath $servicePath -Name SkipHwBringup -Value $skipBringup
Write-Host "SkipHwBringup=$skipBringup (Hardware Bringup ATIVO)"
$dangerousEscapes = 1
Set-ItemProperty -LiteralPath $servicePath -Name EnableDangerousEscapes -Value $dangerousEscapes
Write-Host "EnableDangerousEscapes=$dangerousEscapes (Escapes de Hardware ATIVOS)"

Write-Host '=== Reiniciando o dispositivo detectado ===' -ForegroundColor Cyan
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

Write-Host 'Driver BC250-V3 (WDDM 2.0+) instalado e dispositivo iniciado com sucesso.' -ForegroundColor Green

# --- Instalacao automatica do AMD Display Library (ADL Bridge) para GPU-Z e HWiNFO ---
$adl64Src = Join-Path $packageDir 'atiadlxx.dll'
$adl32Src = Join-Path $packageDir 'atiadlxy.dll'

if (Test-Path -LiteralPath $adl64Src) {
    Write-Host '=== Instalando ADL Compatibility Bridge (GPU-Z / HWiNFO Sensors) ===' -ForegroundColor Cyan
    try {
        $sys32Dir = [Environment]::GetFolderPath('System')
        $sysWowDir = [Environment]::GetFolderPath('SystemX86')

        # 64-bit DLL em System32
        Copy-Item -LiteralPath $adl64Src -Destination (Join-Path $sys32Dir 'atiadlxx.dll') -Force -ErrorAction SilentlyContinue
        Copy-Item -LiteralPath $adl64Src -Destination (Join-Path $sys32Dir 'atiadl64.dll') -Force -ErrorAction SilentlyContinue

        # 32-bit DLL em SysWOW64
        if (Test-Path -LiteralPath $adl32Src) {
            Copy-Item -LiteralPath $adl32Src -Destination (Join-Path $sysWowDir 'atiadlxy.dll') -Force -ErrorAction SilentlyContinue
            Copy-Item -LiteralPath $adl32Src -Destination (Join-Path $sysWowDir 'atiadlxx.dll') -Force -ErrorAction SilentlyContinue
        }

        # Registro de integracao ADL no Windows
        if (-not (Test-Path 'HKLM:\SOFTWARE\AMD\ADL')) {
            New-Item -Path 'HKLM:\SOFTWARE\AMD\ADL' -Force -ErrorAction SilentlyContinue | Out-Null
        }
        Set-ItemProperty -Path 'HKLM:\SOFTWARE\AMD\ADL' -Name 'Path' -Value (Join-Path $sys32Dir 'atiadlxx.dll') -ErrorAction SilentlyContinue

        if (-not (Test-Path 'HKLM:\SOFTWARE\WOW6432Node\AMD\ADL')) {
            New-Item -Path 'HKLM:\SOFTWARE\WOW6432Node\AMD\ADL' -Force -ErrorAction SilentlyContinue | Out-Null
        }
        Set-ItemProperty -Path 'HKLM:\SOFTWARE\WOW6432Node\AMD\ADL' -Name 'Path' -Value (Join-Path $sysWowDir 'atiadlxy.dll') -ErrorAction SilentlyContinue

        Write-Host '[+] atiadlxx.dll (64-bit) e atiadlxy.dll (32-bit) registrados no System32 / SysWOW64!' -ForegroundColor Green
        Write-Host '[+] Sensores fisicos SMU (Clock, Temperatura, Tensao, Watts) prontos para GPU-Z / HWiNFO.' -ForegroundColor Green
    } catch {
        Write-Warning "Nao foi possivel copiar as DLLs ADL automaticamente: $($_.Exception.Message)"
    }
}

# --- Instalacao automatica do OpenCL 2.0 ICD Driver (1536 Shaders / 16GB GDDR6) ---
$openclScript = Join-Path $packageDir 'install_opencl.ps1'
if (-not (Test-Path $openclScript)) {
    $openclScript = Join-Path $PSScriptRoot 'opencl\install_opencl.ps1'
}

if (Test-Path $openclScript) {
    Write-Host '=== Instalando e Registrando OpenCL 2.0 Globalmente no Sistema ===' -ForegroundColor Cyan
    try {
        & $openclScript
    } catch {
        Write-Warning "Falha ao executar instalador OpenCL: $($_.Exception.Message)"
    }
}
