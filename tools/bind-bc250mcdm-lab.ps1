[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptBlackScreenRisk,
    [ValidateRange(30,600)][int]$WatchdogTimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$packageInf = Join-Path $root 'driver\wdk\package\output_lab\bc250mcdm.inf'
$manifestPath = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
$watchdogScript = Join-Path $root 'tools\watch-bc250mcdm-lab.ps1'
$confirmScript = Join-Path $root 'tools\confirm-bc250mcdm-display.ps1'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$recoveryPath = 'C:\BC250_RECOVERY.cmd'
$hardwareId = 'PCI\VEN_1002&DEV_13FE'

if (-not (Test-Path -LiteralPath $packageInf) -or -not (Test-Path -LiteralPath $manifestPath)) {
    throw "Pacote de laboratorio nao encontrado. Execute tools\package-bc250mcdm-lab.ps1 primeiro."
}
if (-not (Test-Path -LiteralPath $recoveryPath)) {
    throw "C:\BC250_RECOVERY.cmd nao encontrado."
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em PowerShell elevado (Administrador).'
}

$devices = @(Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -like "$hardwareId*" })
if ($devices.Count -ne 1) {
    throw "Dispositivo BC-250 nao encontrado ou multiplos encontrados."
}
$device = $devices[0]

Write-Host "=== BINDING DE LABORATORIO (BC-250 MCDM) COM WATCHDOG ===" -ForegroundColor Magenta
Write-Host "  Dispositivo: $($device.PNPDeviceID)"
Write-Host "  Servico atual: $($device.Service)"
Write-Host "  Pacote: $packageInf"
Write-Host "  Watchdog Timeout: $WatchdogTimeoutSeconds segundos"
Write-Host ""

if (-not $Execute -or -not $IAcceptBlackScreenRisk) {
    Write-Host "Modo Dry-Run. Para executar:" -ForegroundColor Yellow
    Write-Host "powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk"
    exit 0
}

# 1. Limpeza de marcador OEM e pacotes anteriores
if (Test-Path -LiteralPath $markerPath) {
    $oldOem = (Get-Content -LiteralPath $markerPath -ErrorAction SilentlyContinue | Select-Object -First 1)
    if ($oldOem -match '^oem\d+\.inf$') {
        & pnputil.exe /delete-driver $oldOem /uninstall /force 2>&1 | Out-Null
    }
    Remove-Item -LiteralPath $markerPath -Force -ErrorAction SilentlyContinue
}

# 2. Stage do pacote de laboratorio
Write-Host "[1/4] Estagiando pacote de laboratorio via pnputil..." -ForegroundColor Yellow
$output = (& pnputil.exe /add-driver $packageInf 2>&1) -join "`n"
$exitCode = $LASTEXITCODE
$output | Write-Host
if ($exitCode -ne 0) { throw "pnputil /add-driver falhou: exit=$exitCode" }

$publishedNames = @(
    [regex]::Matches($output, '(?i)\boem\d+\.inf\b') |
        ForEach-Object { $_.Value.ToLowerInvariant() } |
        Select-Object -Unique
)
if ($publishedNames.Count -ne 1) {
    throw 'Nome oemNN.inf nao identificado de forma unica.'
}
$publishedName = $publishedNames[0]
$publishedName | Set-Content -LiteralPath $markerPath -Encoding ascii
Write-Host "  Pacote publicado como: $publishedName" -ForegroundColor Green

# 3. Armar o Watchdog em segundo plano
Write-Host "`n[2/4] Armando Watchdog de Rollback ($WatchdogTimeoutSeconds s)..." -ForegroundColor Yellow
$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-lab-binding"
New-Item -ItemType Directory -Path $evidenceDir -Force | Out-Null

$watchdogToken = [guid]::NewGuid().ToString('D')
$watchdogArguments = "-NoProfile -ExecutionPolicy Bypass -File `"$watchdogScript`" " +
    "-EvidenceDir `"$evidenceDir`" -InstanceId `"$($device.PNPDeviceID)`" " +
    "-TargetInf `"$publishedName`" -Token `"$watchdogToken`" " +
    "-TimeoutSeconds $WatchdogTimeoutSeconds -Execute"

$watchdogProcess = Start-Process -FilePath powershell.exe -ArgumentList $watchdogArguments `
    -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 500
if ($watchdogProcess.HasExited) {
    throw "Watchdog falhou ao inicializar (exit=$($watchdogProcess.ExitCode))."
}
Write-Host "  Watchdog ativo (PID: $($watchdogProcess.Id))" -ForegroundColor Green

# 4. Executar o binding via NewDev ou pnputil
Write-Host "`n[3/4] Vinculando driver ao hardware..." -ForegroundColor Yellow
if (-not ('Bc250LabNewDev.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace Bc250LabNewDev
{
    public static class NativeMethods
    {
        [DllImport("newdev.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool UpdateDriverForPlugAndPlayDevicesW(
            IntPtr hwndParent,
            string hardwareId,
            string fullInfPath,
            UInt32 installFlags,
            [MarshalAs(UnmanagedType.Bool)] ref bool rebootRequired);
    }
}
'@
}

$installFlagForce = [uint32]0x00000001
$rebootRequired = $false
$nativeOk = [Bc250LabNewDev.NativeMethods]::UpdateDriverForPlugAndPlayDevicesW(
    [IntPtr]::Zero,
    $hardwareId,
    $packageInf,
    $installFlagForce,
    [ref]$rebootRequired)

$win32Error = if ($nativeOk) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
Write-Host "  UpdateDriverForPlugAndPlayDevicesW: success=$nativeOk win32=$win32Error reboot=$rebootRequired"

if (-not $nativeOk) {
    Write-Warning "NewDev falhou, tentando fallback com pnputil /install..."
    & pnputil.exe /install /driver $publishedName
}

# 5. Configurar SkipHwBringup = 0 nos parametros do servico
Write-Host "`n[4/4] Configurando parametros de execucao no registro..." -ForegroundColor Yellow
$paramKey = "HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters"
if (-not (Test-Path $paramKey)) {
    New-Item -Path $paramKey -Force | Out-Null
}
Set-ItemProperty -Path $paramKey -Name "SkipHwBringup" -Value 0 -Type DWord -Force
Write-Host "  SkipHwBringup = 0 configurado." -ForegroundColor Green

# Verificar estado do dispositivo
Start-Sleep -Seconds 3
$devAfter = Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -like "$hardwareId*" }
Write-Host "`nEstado do dispositivo:" -ForegroundColor Cyan
Write-Host "  Service: $($devAfter.Service)"
Write-Host "  Status: $($devAfter.Status)"
Write-Host "  ConfigManagerErrorCode: $($devAfter.ConfigManagerErrorCode)"

$driverInstalledPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$pkgSysPath = Join-Path $root 'driver\wdk\package\output_lab\bc250mcdm.sys'
if (Test-Path $driverInstalledPath) {
    $instHash = (Get-FileHash -LiteralPath $driverInstalledPath -Algorithm SHA256).Hash
    $pkgHash = (Get-FileHash -LiteralPath $pkgSysPath -Algorithm SHA256).Hash
    Write-Host "  Hash instalado: $($instHash.Substring(0,16))"
    Write-Host "  Hash do pacote: $($pkgHash.Substring(0,16))"
    if ($instHash -eq $pkgHash) {
        Write-Host "  Binario em System32\drivers ATUALIZADO com sucesso!" -ForegroundColor Green
    } else {
        Write-Host "  AVISO: Binario no System32 nao atualizou automaticamente; copiando..." -ForegroundColor Yellow
        Copy-Item -LiteralPath $pkgSysPath -Destination $driverInstalledPath -Force
    }
}

Write-Host "`n==========================================================================" -ForegroundColor Green
Write-Host "DRIVER DE LABORATORIO INSTALADO!" -ForegroundColor Green
Write-Host "O Watchdog continua armado por $WatchdogTimeoutSeconds segundos." -ForegroundColor Yellow
Write-Host "Se o video estiver visivel, confirme para desarmar o rollback:" -ForegroundColor Yellow
Write-Host "powershell -ExecutionPolicy Bypass -File tools\confirm-bc250mcdm-display.ps1 -EvidenceDir `"$evidenceDir`" -InstanceId `"$($device.PNPDeviceID)`" -Token `"$watchdogToken`" -IConfirmDisplayIsVisible"
Write-Host "==========================================================================" -ForegroundColor Green

[ordered]@{
    evidence_dir = $evidenceDir
    watchdog_token = $watchdogToken
    instance_id = $device.PNPDeviceID
    published_inf = $publishedName
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'tools\_last_lab_binding.json') -Encoding UTF8
