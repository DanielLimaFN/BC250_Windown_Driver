[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptBlackScreenRisk,
    [switch]$IAcceptDeliberateHangAndPossibleBugCheck,
    [switch]$RunRadvComputeTripod,
    [ValidateRange(30,600)][int]$WatchdogTimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$stagedAudit = Join-Path $root 'tools\audit-bc250mcdm-staged.ps1'
$collector = Join-Path $root 'tools\capture-bc250mcdm-binding.ps1'
$watchdog = Join-Path $root 'tools\watch-bc250mcdm-binding.ps1'
$confirmDisplay = Join-Path $root 'tools\confirm-bc250mcdm-display.ps1'
$armDwmCapture = Join-Path $root 'tools\arm-dwm-crash-capture.ps1'
$restoreDwmCapture = Join-Path $root 'tools\restore-dwm-crash-capture.ps1'
$radvComputeTripod = Join-Path $root 'tools\test-radv-wddm-compute-tripod.ps1'
$packageInf = Join-Path $root 'driver\wdk\package\output_active\bc250mcdm.inf'
$manifestPath = Join-Path $root 'driver\wdk\package\output_active.manifest.json'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$recoveryPath = 'C:\BC250_RECOVERY.cmd'
$hardwareId = 'PCI\VEN_1002&DEV_13FE'

# RunRadvComputeTripod nasceu aqui por engano e foi movido para a trilha de
# laboratorio. O pacote ativo e compilado SEM
# BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP e grava SkipHwBringup=1: o
# StartDevice retorna cedo, o motor P1 nunca sobe e o gate aborta sempre em
# P1EngineState=0 (medido em docs\evidence\2026-09-05_22-20-17-*). Deixar a
# chamada aqui so' gastaria corrida fisica para provar isso de novo.
if ($RunRadvComputeTripod) {
    throw ('RunRadvComputeTripod nao pertence a trilha ativa: este pacote nunca ' +
        'publica P1EngineState=1. Use a trilha de laboratorio -- ' +
        'tools\package-bc250mcdm-lab.ps1 -EnableRenderNode -EnableShaderDispatch ' +
        '-EnablePersistentSubmission, tools\test-gpuva-lab-promotion.ps1 -Action ' +
        'Prepare, tools\bind-bc250mcdm-lab.ps1 -- e depois ' +
        'tools\test-radv-wddm-compute-tripod.ps1 -Execute com o EvidenceDir do bind.')
}

foreach ($path in @($stagedAudit,$collector,$watchdog,$confirmDisplay,$armDwmCapture,$restoreDwmCapture,$radvComputeTripod,$packageInf,$manifestPath,$recoveryPath,$markerPath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Artefato ausente: $path" }
}

& powershell -NoProfile -ExecutionPolicy Bypass -File $stagedAudit
if ($LASTEXITCODE -ne 0) { throw 'Binding bloqueado: gate de stage falhou.' }

$markerLines = @(Get-Content -LiteralPath $markerPath)
if ($markerLines.Count -ne 1 -or $markerLines[0] -notmatch '^oem\d+\.inf$') {
    throw 'Binding bloqueado: marcador OEM invalido.'
}
$bootFusePath = 'C:\BC250_BOOT_FUSE.dat'
if (Test-Path -LiteralPath $bootFusePath) {
    $fuseRaw = [System.IO.File]::ReadAllBytes($bootFusePath)
    if ($fuseRaw.Length -eq 40) {
        $fuseMagic = [BitConverter]::ToUInt32($fuseRaw, 0)
        $fuseState = [BitConverter]::ToUInt32($fuseRaw, 8)
        if ($fuseMagic -eq 0x45535546 -and $fuseState -ne 0) {
            $name = if ($fuseState -eq 1) { 'ARMED (1)' } elseif ($fuseState -eq 2) { 'TRIPPED (2)' } else { "$fuseState" }
            throw "Binding bloqueado: Fusivel persistente em $name. O ensaio exige CLEAN (0). Use: powershell -ExecutionPolicy Bypass -File tools\inspect-boot-fuse.ps1 -Reset"
        }
    }
}
# A13: preflight do D3D11CapsProbe. A BASE_DE_AVANCO manda todo binder exigir o
# probe ausente ou zero, mas o gate nao existia aqui. Com o probe em 1 a UMD
# publica a tabela de adapter, o DWM falha em 3DPIPELINESUPPORT e a tela fica
# preta DE PROPOSITO -- gastando a corrida para medir um resultado ja conhecido.
$probeKey = 'HKLM:\SOFTWARE\BC250\Umd'
if (Test-Path -LiteralPath $probeKey) {
    $probeValue = (Get-ItemProperty -LiteralPath $probeKey -Name 'D3D11CapsProbe' `
        -ErrorAction SilentlyContinue).D3D11CapsProbe
    if ($null -ne $probeValue -and $probeValue -ne 0) {
        throw ('Binding bloqueado: D3D11CapsProbe = ' + $probeValue + '. Desarme antes: ' +
            'powershell -ExecutionPolicy Bypass -File tools\arm-d3d11-caps-probe.ps1 -Disarm')
    }
}

# A13/B3: o modo de submissao do candidato tem de ser DECLARADO antes da corrida.
# O default do driver e' somente INDIRECT -- o unico ramo que le o DMA buffer do
# dxgkrnl. Se o pacote trouxer SdmaPlanoAFallback=1, os caminhos PLANO A voltam a
# ter precedencia, absorvem as submissoes e o INDIRECT deixa de ser medido (foi o
# desfecho vazio da corrida 16:19). Nao bloqueia; impede a surpresa.
$submissionManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($submissionManifest.SdmaPlanoAFallbackEnabled) {
    Write-Warning ('Pacote com SdmaPlanoAFallback=' + $submissionManifest.SdmaPlanoAFallback +
        ': os caminhos PLANO A tem PRECEDENCIA. Eles reimplementam a operacao no anel e ' +
        'NAO leem o DMA buffer -- esta corrida NAO mede o ramo INDIRECT.')
} else {
    Write-Host ('Pacote com SdmaPlanoAFallback=' + [int]$submissionManifest.SdmaPlanoAFallback +
        ': somente INDIRECT. Submissao cuja GPUVA de IB nao resolva sai como RECUSA ' +
        '(STATUS_IO_DEVICE_ERROR), caminho conhecido do BugCheck 0x119 sem TDR.')
}

# item 25: se o pacote armar hang de proposito, AVISAR alto. O ensaio pode
# terminar em BugCheck -- e' resultado, nao acidente, mas nao pode ser surpresa.
if ($submissionManifest.TdrInduceHangArmed) {
    Write-Warning ('Pacote com TdrInduceHang=' + $submissionManifest.TdrInduceHang +
        ': a primeira submissao do Node 0 vai TRAVAR a SDMA0 de proposito e o completion ' +
        'NAO sera notificado. Se o TDR do Windows nao recuperar, o desfecho e BugCheck.')
    if (-not $IAcceptDeliberateHangAndPossibleBugCheck) {
        throw ('Binding do pacote TDR armado exige tambem ' +
            '-IAcceptDeliberateHangAndPossibleBugCheck.')
    }
} elseif ($IAcceptDeliberateHangAndPossibleBugCheck) {
    throw '-IAcceptDeliberateHangAndPossibleBugCheck so e valido para pacote TDR armado.'
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
$setupApiLog = Join-Path $env:SystemRoot 'INF\setupapi.dev.log'
if (-not (Test-Path -LiteralPath $setupApiLog)) {
    $latestDevLog = Get-ChildItem -Path (Join-Path $env:SystemRoot 'INF') -Filter 'setupapi.dev*.log' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($latestDevLog) { $setupApiLog = $latestDevLog.FullName }
}
if (Test-Path -LiteralPath $setupApiLog) {
    Get-Content -LiteralPath $setupApiLog -Tail 1600 |
        Set-Content -LiteralPath (Join-Path $evidenceDir 'setupapi-before-tail.txt') -Encoding UTF8
} else {
    "" | Set-Content -LiteralPath (Join-Path $evidenceDir 'setupapi-before-tail.txt') -Encoding UTF8
}
@'
SE A TELA ESTIVER PRETA:
1. Inicie o Windows em Modo de Seguranca com Prompt de Comando.
2. Execute C:\BC250_RECOVERY.cmd
3. Reinicie o Windows.
'@ | Set-Content -LiteralPath (Join-Path $evidenceDir 'RECOVERY.txt') -Encoding ASCII

Remove-Item -LiteralPath 'C:\BC250_DISPLAY_LOG.txt' -ErrorAction SilentlyContinue
# Log da UMD (bc250umd.dll roda dentro de dwm.exe/apps D3D; C:\ nao e' gravavel
# pela conta virtual do DWM, por isso o tracer usa C:\Windows\Temp).
Get-ChildItem -LiteralPath 'C:\Windows\Temp' -Filter 'BC250_UMD_LOG_*.txt' -File `
    -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

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

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $armDwmCapture `
    -EvidenceDir $evidenceDir -Token $watchdogToken -Execute
if ($LASTEXITCODE -ne 0) {
    throw 'Binding bloqueado: captura transacional do crash do DWM nao armou.'
}

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
    # A14: Trim() na entrada. Na corrida 17:13 a confirmacao foi digitada e
    # recusada por espaco em branco na borda -- o -ceq comparava a string crua e
    # o watchdog derrubou o candidato. Continua CASE-SENSITIVE de proposito:
    # aparar espacos nao afrouxa a intencao, digitar outra coisa afrouxaria.
    $visualAnswer = (Read-Host 'Confirmacao visual')
    if ($null -ne $visualAnswer) { $visualAnswer = $visualAnswer.Trim() }
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
