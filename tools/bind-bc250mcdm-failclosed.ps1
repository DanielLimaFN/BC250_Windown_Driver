[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptBlackScreenRisk,
    [ValidateRange(30,600)][int]$WatchdogTimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$stagedAudit = Join-Path $root 'tools\audit-bc250mcdm-staged.ps1'
$collector = Join-Path $root 'tools\capture-bc250mcdm-binding.ps1'
$watchdog = Join-Path $root 'tools\watch-bc250mcdm-binding.ps1'
$confirmDisplay = Join-Path $root 'tools\confirm-bc250mcdm-display.ps1'
$packageInf = Join-Path $root 'driver\wdk\package\output_active\bc250mcdm.inf'
$manifestPath = Join-Path $root 'driver\wdk\package\output_active.manifest.json'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$recoveryPath = 'C:\BC250_RECOVERY.cmd'
$hardwareId = 'PCI\VEN_1002&DEV_13FE'

foreach ($path in @($stagedAudit,$collector,$watchdog,$confirmDisplay,$packageInf,$manifestPath,$recoveryPath,$markerPath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Artefato ausente: $path" }
}

& powershell -NoProfile -ExecutionPolicy Bypass -File $stagedAudit
if ($LASTEXITCODE -ne 0) { throw 'Binding bloqueado: gate de stage falhou.' }

$markerLines = @(Get-Content -LiteralPath $markerPath)
if ($markerLines.Count -ne 1 -or $markerLines[0] -notmatch '^oem\d+\.inf$') {
    throw 'Binding bloqueado: marcador OEM invalido.'
}
$publishedName = $markerLines[0].ToLowerInvariant()
$devices = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like "$hardwareId*"
})
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'BasicDisplay' -or
    $devices[0].ConfigManagerErrorCode -ne 0) {
    throw 'Binding bloqueado: BC-250 nao esta em BasicDisplay, Codigo 0.'
}
$device = $devices[0]

Write-Host "Preflight aprovado: $publishedName estagiado; BC-250 em BasicDisplay, Codigo 0."
Write-Warning 'O binding forcado pode apagar a tela imediatamente.'
Write-Warning 'Rollback: Modo de Seguranca -> C:\BC250_RECOVERY.cmd -> reboot.'
if (-not $Execute) {
    if ($IAcceptBlackScreenRisk) {
        throw '-IAcceptBlackScreenRisk nao e valido sem -Execute.'
    }
    Write-Host 'DRY-RUN: zero escrita e zero binding.'
    Write-Host 'Para executar, e necessaria nova autorizacao explicita e ambos: -Execute -IAcceptBlackScreenRisk.'
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 0
}
if (-not $IAcceptBlackScreenRisk) {
    throw 'Binding recusado: acrescente -IAcceptBlackScreenRisk somente apos autorizacao explicita.'
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Binding exige PowerShell elevado.'
}
if (-not [Environment]::Is64BitProcess) { throw 'Binding exige PowerShell 64-bit.' }

$stagedAuditOutput = (& powershell -NoProfile -ExecutionPolicy Bypass -File $stagedAudit 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'Gate de stage mudou antes da mutacao; binding abortado.' }

if (-not ('Bc250McdmNewDev.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace Bc250McdmNewDev
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

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-mcdm-display-binding"
New-Item -ItemType Directory -Path $evidenceDir | Out-Null
$startTime = Get-Date
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$watchdogToken = [guid]::NewGuid().ToString('D')
$confirmationCommand =
    "powershell -NoProfile -ExecutionPolicy Bypass -File `"$confirmDisplay`" " +
    "-EvidenceDir `"$evidenceDir`" -InstanceId `"$($device.PNPDeviceID)`" " +
    "-Token `"$watchdogToken`" -IConfirmDisplayIsVisible"
$before = [ordered]@{
    started = $startTime.ToString('o')
    published_inf = $publishedName
    instance_id = $device.PNPDeviceID
    service_before = $device.Service
    problem_code_before = $device.ConfigManagerErrorCode
    binding_api = 'UpdateDriverForPlugAndPlayDevicesW_INSTALLFLAG_FORCE'
    package_inf_sha256 = $manifest.PackageInfSha256
    package_sys_sha256 = $manifest.PackageSysSha256
    package_cat_sha256 = $manifest.PackageCatSha256
    recovery_sha256 = $manifest.RecoverySha256
    watchdog_timeout_seconds = $WatchdogTimeoutSeconds
    watchdog_token = $watchdogToken
    visual_confirmation_command = $confirmationCommand
    hardware_validated = $false
    verdict = 'NAO VALIDADO EM HARDWARE'
}
$before | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $evidenceDir 'manifest-before.json') -Encoding UTF8
$stagedAuditOutput |
    Set-Content -LiteralPath (Join-Path $evidenceDir 'staged-audit-before.txt') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $env:SystemRoot 'INF\setupapi.dev.log') -Tail 1600 |
    Set-Content -LiteralPath (Join-Path $evidenceDir 'setupapi-before-tail.txt') -Encoding UTF8
@'
SE A TELA ESTIVER PRETA:
1. Inicie o Windows em Modo de Seguranca com Prompt de Comando.
2. Execute C:\BC250_RECOVERY.cmd
3. Reinicie o Windows.
'@ | Set-Content -LiteralPath (Join-Path $evidenceDir 'RECOVERY.txt') -Encoding ASCII

$watchdogArguments =
    "-NoProfile -ExecutionPolicy Bypass -File `"$watchdog`" " +
    "-EvidenceDir `"$evidenceDir`" " +
    "-InstanceId `"$($device.PNPDeviceID)`" " +
    "-TargetInf `"$publishedName`" -Token `"$watchdogToken`" " +
    "-TimeoutSeconds $WatchdogTimeoutSeconds -Execute"
$watchdogProcess = Start-Process -FilePath powershell.exe -ArgumentList $watchdogArguments `
    -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 750
if ($watchdogProcess.HasExited) {
    throw "Binding bloqueado: watchdog encerrou antes da mutacao (exit=$($watchdogProcess.ExitCode))."
}
$watchdogProcess.Id | Set-Content -LiteralPath (Join-Path $evidenceDir 'watchdog-process-id.txt') -Encoding ASCII
$confirmationCommand | Set-Content -LiteralPath (Join-Path $evidenceDir 'CONFIRM_DISPLAY.cmd.txt') -Encoding ASCII
Write-Warning "WATCHDOG ARMADO: rollback automatico em $WatchdogTimeoutSeconds segundos sem confirmacao visual."
Write-Host "Com a imagem visivel, confirme usando: $confirmationCommand"

$collectorArguments =
    "-NoProfile -ExecutionPolicy Bypass -File `"$collector`" " +
    "-EvidenceDir `"$evidenceDir`" " +
    "-InstanceId `"$($device.PNPDeviceID)`" " +
    "-StartTimeIso `"$($startTime.ToString('o'))`""
$collectorProcess = Start-Process -FilePath powershell.exe -ArgumentList $collectorArguments `
    -WindowStyle Hidden -PassThru

$installFlagForce = [uint32]0x00000001
$rebootRequired = $false
$nativeOk = [Bc250McdmNewDev.NativeMethods]::UpdateDriverForPlugAndPlayDevicesW(
    [IntPtr]::Zero,
    $hardwareId,
    $packageInf,
    $installFlagForce,
    [ref]$rebootRequired)
$win32Error = if ($nativeOk) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
$bindingResult = [ordered]@{
    api = 'UpdateDriverForPlugAndPlayDevicesW'
    hardware_id = $hardwareId
    inf = $packageInf
    install_flags = ('0x{0:X8}' -f $installFlagForce)
    success = $nativeOk
    win32_error = $win32Error
    reboot_required = $rebootRequired
    hardware_validated = $false
}
$bindingResult | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $evidenceDir 'force-binding-result.json') -Encoding UTF8
$bindingResult | Format-List | Out-String | Write-Host

try {
    Wait-Process -Id $collectorProcess.Id -Timeout 45 -ErrorAction Stop
} catch {
    Write-Warning 'O coletor ainda esta ativo e continuara oculto.'
}

Write-Host "Evidencia: $evidenceDir"
Write-Host "Binding API: success=$nativeOk win32=$win32Error reboot=$rebootRequired"
Write-Host 'Analise device-after.json e probe-registry.json; Codigo 0 nao implica compute.'
if ($nativeOk) {
    Write-Warning "O watchdog continua armado. Digite DISPLAY_OK somente se a imagem estiver visivel."
    $visualAnswer = Read-Host 'Confirmacao visual'
    if ($visualAnswer -ceq 'DISPLAY_OK') {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $confirmDisplay `
            -EvidenceDir $evidenceDir -InstanceId $device.PNPDeviceID `
            -Token $watchdogToken -IConfirmDisplayIsVisible
        if ($LASTEXITCODE -ne 0) {
            Write-Warning 'Confirmacao recusada; watchdog permanece armado.'
        }
    } else {
        Write-Warning 'Confirmacao ausente ou incorreta; watchdog permanece armado.'
    }
}
Write-Host 'NAO VALIDADO EM HARDWARE'
if (-not $nativeOk) { exit 1 }
exit 0
