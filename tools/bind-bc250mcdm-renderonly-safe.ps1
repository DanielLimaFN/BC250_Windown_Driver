<#
.SYNOPSIS
    Instala e vincula o pacote RENDERONLY_SAFE. Recusa o pacote de laboratorio.

.DESCRIPTION
    Bind proprio do fluxo seguro. Nao compartilha caminho com
    bind-bc250mcdm-lab.ps1 de proposito: aquele exige SkipHwBringup=0 e aceita
    shader e fila persistente ligados, que e' exatamente a combinacao que nao
    pode chegar perto do Gate 1.5.

    Este bind sobe o adaptador em KMDOD (RenderNode=0). A promocao para o
    perfil render-only e' passo separado, apos confirmar Codigo 0.
#>
[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptDisplayBindRisk,
    [switch]$IAcceptKnownRenderOnlyFailure,
    [switch]$IAcceptAutomaticRecoveryReboot,
    [ValidateRange(180,600)][int]$WatchdogTimeoutSeconds = 240
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'bc250-safe-common.ps1')

$hardwareId   = 'PCI\VEN_1002&DEV_13FE'
$recoveryPath = 'C:\BC250_RECOVERY.cmd'
$markerPath   = 'C:\BC250_TEST_OEMINF.txt'

$manifest   = Assert-Bc250SafePackage -Root $root
$packageDir = Get-Bc250SafePackageDir -Root $root
$packageInf = Join-Path $packageDir 'bc250mcdm.inf'

if (-not (Test-Path -LiteralPath $recoveryPath)) {
    throw "Recusado: $recoveryPath ausente. A recuperacao e obrigatoria."
}
# A recuperacao implantada tem de ser a mesma que o pacote conhece. A versao
# anterior nao removia o no PnP orfao e terminava em laco de reboot no
# Codigo 19 — vincular com ela seria vincular sem saida.
$deployedRecovery = (Get-FileHash -LiteralPath $recoveryPath -Algorithm SHA256).Hash
if ($deployedRecovery -ne $manifest.RecoverySha256) {
    throw ("Recusado: $recoveryPath diverge do pacote.`n" +
           "  implantada: $deployedRecovery`n" +
           "  esperada  : $($manifest.RecoverySha256)`n" +
           '  Copie tools\BC250_RECOVERY.cmd para C:\ e tente de novo.')
}

$devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop | Where-Object {
    $_.PNPDeviceID -like "$hardwareId*" })
if ($devices.Count -ne 1) { throw "Recusado: esperava 1 BC-250, achei $($devices.Count)." }
$device = $devices[0]

# Boot limpo: estado de engine nao cai com desbind, so' com cold boot. Vincular
# sobre um bind anterior contamina a telemetria da corrida nova.
if ($device.Service -ne 'BasicDisplay' -or $device.ConfigManagerErrorCode -ne 0) {
    throw ("Recusado: BC-250 esta em $($device.Service) / Codigo " +
           "$($device.ConfigManagerErrorCode). Exigido BasicDisplay / Codigo 0. " +
           'Rode C:\BC250_RECOVERY.cmd e reinicie.')
}
if (Test-Path -LiteralPath $markerPath) {
    throw "Recusado: $markerPath presente — binding anterior nao foi limpo."
}

Write-Host ''
Write-Host 'BC250 BIND RENDERONLY_SAFE - PREFLIGHT: PASS' -ForegroundColor Green
Write-Host "  pacote    : $($manifest.BuildKind) $($manifest.DriverVer)"
Write-Host "  schema    : $($manifest.TelemetrySchemaVersion)"
Write-Host "  SYS       : $($manifest.PackageSysSha256)"
Write-Host '  bring-up  : ausente do binario (SkipHwBringup=1, macro nao compilada)'
Write-Host "  device    : $($device.Service) / Codigo $($device.ConfigManagerErrorCode)"
Write-Host ("  perfil    : {0}" -f $(if ($manifest.ProfileMode -eq 'DIRECT_RENDERONLY') {
    'RENDER_ONLY_LAB ja no primeiro boot; nenhum boot KMDOD' } else {
    'KMDOD (RenderNode=0). Promocao e passo separado.' }))
Write-Host ''

if (-not $Execute) {
    Write-Host 'DRY-RUN: nada foi instalado.'
    Write-Host 'Para instalar: -Execute -IAcceptDisplayBindRisk'
    exit 0
}
if (-not $IAcceptDisplayBindRisk) {
    throw 'Recusado: falta -IAcceptDisplayBindRisk.'
}
if (-not $IAcceptAutomaticRecoveryReboot) {
    throw ('Recusado: falta -IAcceptAutomaticRecoveryReboot. Se a recuperacao online ' +
           'nao puder restaurar o no PnP, o watchdog precisa reiniciar o Windows.')
}
if ([string]$manifest.PriorDirectRenderOnlyResult -ne '' -and
    -not $IAcceptKnownRenderOnlyFailure) {
    throw ('Recusado: este SYS/perfil DIRECT_RENDERONLY ja produziu tela preta e Codigo 43. ' +
           'O 3.77 valida infraestrutura de recuperacao, nao uma correcao funcional. ' +
           'Para um ensaio deliberado do watchdog, acrescente -IAcceptKnownRenderOnlyFailure.')
}
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Bind exige PowerShell elevado.'
}

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-renderonly-safe-binding"
New-Item -ItemType Directory -Path $evidenceDir | Out-Null
$token = [guid]::NewGuid().ToString('D')
$taskName = 'BC250-SafeBindGuard'
$deadlineTaskName = 'BC250-SafeBindDeadline'
$guardDir = 'C:\BC250_SAFE_GUARD'
$guardScript = Join-Path $guardDir 'watch-bc250mcdm-safe-bind.ps1'
$configPath = Join-Path $guardDir 'armed.json'
$recoveryHash = (Get-FileHash -LiteralPath $recoveryPath -Algorithm SHA256).Hash
New-Item -ItemType Directory -Path $guardDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'watch-bc250mcdm-safe-bind.ps1') `
    -Destination $guardScript -Force

$armedAtUtc = [datetime]::UtcNow
$deadlineUtc = $armedAtUtc.AddSeconds($WatchdogTimeoutSeconds)
$guardConfig = [ordered]@{
    schema_version = 1
    task_name = $taskName
    deadline_task_name = $deadlineTaskName
    token = $token
    instance_id = [string]$device.PNPDeviceID
    timeout_seconds = $WatchdogTimeoutSeconds
    evidence_dir = $evidenceDir
    recovery_path = $recoveryPath
    recovery_sha256 = $recoveryHash
    expected_sys_sha256 = [string]$manifest.PackageSysSha256
    expected_inf_sha256 = [string]$manifest.PackageInfSha256
    expected_driver_version = [string]$manifest.DriverVer
    candidate_id = [string]$manifest.CandidateId
    armed_boot_ticks = [long](Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks
    armed_at_utc_ticks = [long]$armedAtUtc.Ticks
    deadline_utc_ticks = [long]$deadlineUtc.Ticks
    hardware_validated = $false
    automatic_recovery_reboot = $true
}
$guardConfig | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $configPath -Encoding UTF8
$guardConfig | ConvertTo-Json -Depth 5 | Set-Content `
    -LiteralPath (Join-Path $evidenceDir 'safe-bind-watchdog-config.json') -Encoding UTF8

Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
Unregister-ScheduledTask -TaskName $deadlineTaskName -Confirm:$false -ErrorAction SilentlyContinue
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument `
    "-NoProfile -ExecutionPolicy Bypass -File `"$guardScript`" -ConfigPath `"$configPath`" -Execute"
$trigger = New-ScheduledTaskTrigger -AtStartup
$principalTask = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 15) `
    -MultipleInstances IgnoreNew -StartWhenAvailable
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
    -Principal $principalTask -Settings $settings -Force | Out-Null
$deadlineAction = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument `
    "-NoProfile -ExecutionPolicy Bypass -File `"$guardScript`" -ConfigPath `"$configPath`" -Execute -DeadlineEnforcer"
$deadlineTrigger = New-ScheduledTaskTrigger -Once -At $deadlineUtc.ToLocalTime()
Register-ScheduledTask -TaskName $deadlineTaskName -Action $deadlineAction `
    -Trigger $deadlineTrigger -Principal $principalTask -Settings $settings -Force | Out-Null
Start-ScheduledTask -TaskName $taskName
$guardDeadline = (Get-Date).AddSeconds(10)
do {
    Start-Sleep -Milliseconds 250
    $guardStatePath = Join-Path $evidenceDir 'safe-bind-watchdog-state.json'
    $guardState = if (Test-Path -LiteralPath $guardStatePath) {
        Get-Content -LiteralPath $guardStatePath -Raw | ConvertFrom-Json
    } else { $null }
} until (($null -ne $guardState -and [string]$guardState.state -eq 'OBSERVING') -or
    (Get-Date) -ge $guardDeadline)
if ($null -eq $guardState -or [string]$guardState.state -ne 'OBSERVING') {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $deadlineTaskName -Confirm:$false -ErrorAction SilentlyContinue
    throw 'Recusado: watchdog SYSTEM nao entrou em OBSERVING antes do bind.'
}

Write-Host "[1/4] watchdog SYSTEM ativo antes do bind ($WatchdogTimeoutSeconds s)."
Write-Host '[2/4] importando o pacote no Driver Store...'
$pnputilOutput = (& pnputil.exe /add-driver $packageInf /install 2>&1) -join "`n"
$pnputilOutput | Tee-Object -FilePath (Join-Path $evidenceDir 'pnputil-add-driver.txt') | Write-Host
if ($LASTEXITCODE -ne 0) { throw "pnputil /add-driver falhou (exit $LASTEXITCODE)." }

Write-Host '[3/4] reenumerando e observando estabilidade por 60 s...'
& pnputil.exe /scan-devices | Out-Null

$publishedMatches = [regex]::Matches($pnputilOutput, '(?i)\boem\d+\.inf\b')
$published = if ($publishedMatches.Count -gt 0) {
    $publishedMatches[$publishedMatches.Count - 1].Value.ToLowerInvariant()
} else { $null }
if ([string]::IsNullOrWhiteSpace($published)) {
    throw 'Falha fechada: pnputil nao publicou identidade oemNN.inf interpretavel.'
}
Set-Content -LiteralPath $markerPath -Value $published -Encoding ascii

$samples = @()
$stable = $true
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 2
    $after = @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.PNPDeviceID -like "$hardwareId*" })
    $svc = if ($after.Count -eq 1) { [string]$after[0].Service } else { '<n/a>' }
    $code = if ($after.Count -eq 1) { [uint32]$after[0].ConfigManagerErrorCode } else { $null }
    $samples += [pscustomobject]@{ at=(Get-Date).ToString('o'); service=$svc; code=$code }
    if ($svc -ne 'bc250mcdm' -or $code -ne 0) { $stable = $false; break }
}
$samples | ConvertTo-Json -Depth 3 | Set-Content `
    -LiteralPath (Join-Path $evidenceDir 'pnp-stability-samples.json') -Encoding UTF8

[ordered]@{
    state              = $(if ($stable) { 'BIND_STABLE_UNCONFIRMED' } else { 'BIND_FAILED' })
    captured_at        = (Get-Date).ToString('o')
    evidence_dir       = $evidenceDir
    build_kind         = [string]$manifest.BuildKind
    driver_ver         = [string]$manifest.DriverVer
    schema_version     = [uint32]$manifest.TelemetrySchemaVersion
    package_sys_sha256 = [string]$manifest.PackageSysSha256
    candidate_id       = [string]$manifest.CandidateId
    instance_id        = [string]$device.PNPDeviceID
    published_inf      = $published
    service_after      = $svc
    code_after         = $code
    hardware_validated = $false
} | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 `
    -LiteralPath (Join-Path $evidenceDir 'renderonly-safe-binding.json')

Write-Host '[4/4] estado apos o bind:'
Write-Host "  service = $svc / Codigo $code"
Write-Host "  evidencia: $evidenceDir"
Write-Host ''
if ($stable) {
    Write-Host 'BIND: ESTAVEL POR 60 s, AINDA NAO CONFIRMADO.' -ForegroundColor Yellow
    Write-Host 'Com imagem visivel, confirme antes do timeout:'
    Write-Host ('powershell -NoProfile -ExecutionPolicy Bypass -File "{0}" ' +
        '-ConfigPath "{1}" -Token "{2}" -IConfirmDisplayIsVisible' -f
        (Join-Path $PSScriptRoot 'confirm-bc250mcdm-safe-bind.ps1'), $configPath, $token)
} else {
    Write-Host 'BIND: FALHOU durante a janela de estabilidade.' -ForegroundColor Red
    Write-Host 'Nao confirme. O watchdog executara a recuperacao automaticamente.'
}
Write-Host 'NAO VALIDADO EM HARDWARE'
