[CmdletBinding()]
param(
    [ValidateRange(180,600)][int]$TimeoutSeconds = 300,
    [switch]$Execute,
    [switch]$IAcceptPersistentRebootGuard,
    [switch]$LabCandidate,
    [switch]$RequireRenderNode
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$taskName = 'BC250-RebootGuard'
$guardRoot = 'C:\BC250_REBOOT_GUARD'
$installedWatchdog = Join-Path $guardRoot 'watch-bc250mcdm-reboot.ps1'
$installedConfirm = Join-Path $guardRoot 'confirm-bc250mcdm-reboot.ps1'
$installedConfig = Join-Path $guardRoot 'armed.json'
$sourceWatchdog = Join-Path $root 'tools\watch-bc250mcdm-reboot.ps1'
$sourceConfirm = Join-Path $root 'tools\confirm-bc250mcdm-reboot.ps1'
$rollbackPath = Join-Path $root $(if ($LabCandidate) {
    'tools\rollback-bc250mcdm-lab.ps1'
} else { 'tools\rollback-bc250mcdm-online.ps1' })
$recoveryAudit = Join-Path $root 'tools\audit-bc250-recovery-readiness.ps1'
$manifestPath = Join-Path $root $(if ($LabCandidate) {
    'driver\wdk\package\output_lab.manifest.json'
} else { 'driver\wdk\package\output_active.manifest.json' })
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$instancePrefix = 'PCI\VEN_1002&DEV_13FE'

function Get-CompletedPredecessorGuard {
    if (-not (Test-Path -LiteralPath $guardRoot -PathType Container)) { return $null }
    $oldConfigPath = Join-Path $guardRoot 'armed.json'
    if (-not (Test-Path -LiteralPath $oldConfigPath -PathType Leaf)) {
        throw "Armamento recusado: residuo sem configuracao em $guardRoot."
    }
    $oldConfig = Get-Content -LiteralPath $oldConfigPath -Raw | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace([string]$oldConfig.task_name) -or
        [string]::IsNullOrWhiteSpace([string]$oldConfig.evidence_dir)) {
        throw 'Armamento recusado: configuracao do guard predecessor invalida.'
    }
    if (Get-ScheduledTask -TaskName ([string]$oldConfig.task_name) -ErrorAction SilentlyContinue) {
        throw 'Armamento recusado: tarefa do guard predecessor ainda esta registrada.'
    }
    $oldStatePath = Join-Path ([string]$oldConfig.evidence_dir) 'reboot-watchdog-state.json'
    $oldConfirmationPath = Join-Path ([string]$oldConfig.evidence_dir) 'reboot-confirmation.json'
    if (-not (Test-Path -LiteralPath $oldStatePath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $oldConfirmationPath -PathType Leaf)) {
        throw 'Armamento recusado: predecessor nao possui estado/confirmacao finais.'
    }
    $oldState = Get-Content -LiteralPath $oldStatePath -Raw | ConvertFrom-Json
    $oldConfirmation = Get-Content -LiteralPath $oldConfirmationPath -Raw | ConvertFrom-Json
    if ([string]$oldState.state -ne 'DISARMED' -or
        -not [bool]$oldConfirmation.display_visible -or
        [long]$oldState.boot_ticks -ne [long]$oldConfirmation.boot_ticks) {
        throw 'Armamento recusado: guard predecessor nao esta DISARMED no boot confirmado.'
    }
    return [ordered]@{
        task_name = [string]$oldConfig.task_name
        driver_version = [string]$oldConfig.expected_driver_version
        evidence_dir = [string]$oldConfig.evidence_dir
        boot_ticks = [long]$oldState.boot_ticks
    }
}

foreach ($path in @(
    $sourceWatchdog,$sourceConfirm,$rollbackPath,$recoveryAudit,$manifestPath,
    $markerPath,'C:\BC250_RECOVERY.cmd',$driverPath
)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Armamento recusado: artefato ausente: $path" }
}
if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
    throw "Armamento recusado: tarefa $taskName ja existe."
}
$completedPredecessor = Get-CompletedPredecessorGuard

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $recoveryAudit
if ($LASTEXITCODE -ne 0) { throw 'Armamento recusado: recovery readiness falhou.' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$expectedDriverVersion = ([string]$manifest.DriverVer -split ',', 2)[1]
if ([string]::IsNullOrWhiteSpace($expectedDriverVersion) -or
    $expectedDriverVersion -notmatch '^\d+\.\d+\.\d+\.\d+$' -or
    [string]$manifest.Verdict -ne 'NAO VALIDADO EM HARDWARE' -or
    [bool]$manifest.HardwareValidated) {
    throw 'Armamento recusado: manifesto nao identifica candidato fail-closed valido.'
}
$devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
    Where-Object { $_.PNPDeviceID -like "$instancePrefix*" })
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'bc250mcdm' -or
    $devices[0].ConfigManagerErrorCode -ne 0) {
    throw 'Armamento recusado: BC-250 nao esta em bc250mcdm Codigo 0.'
}
$video = @(Get-CimInstance Win32_VideoController -ErrorAction Stop |
    Where-Object { $_.PNPDeviceID -eq $devices[0].PNPDeviceID })
if ($video.Count -ne 1 -or [string]$video[0].DriverVersion -ne $expectedDriverVersion) {
    throw "Armamento recusado: versao ativa nao e $expectedDriverVersion."
}
$markerLines = @(Get-Content -LiteralPath $markerPath)
if ($markerLines.Count -ne 1 -or $markerLines[0] -notmatch '^oem\d+\.inf$') {
    throw 'Armamento recusado: marcador OEM invalido.'
}
$targetInf = $markerLines[0].ToLowerInvariant()
$installedInf = Join-Path $env:SystemRoot "INF\$targetInf"
if (-not (Test-Path -LiteralPath $installedInf) -or
    (Get-FileHash -LiteralPath $installedInf -Algorithm SHA256).Hash -ne [string]$manifest.PackageInfSha256 -or
    (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash -ne [string]$manifest.PackageSysSha256) {
    throw 'Armamento recusado: hashes do pacote instalado divergem do manifesto.'
}
if ((Get-FileHash -LiteralPath 'C:\BC250_RECOVERY.cmd' -Algorithm SHA256).Hash -ne
    [string]$manifest.RecoverySha256) {
    throw 'Armamento recusado: recovery instalado diverge do manifesto.'
}
if ($RequireRenderNode) {
    if (-not $LabCandidate) {
        throw 'Armamento recusado: RequireRenderNode exige LabCandidate.'
    }
    $parameters = Get-ItemProperty -LiteralPath `
        'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters' -ErrorAction Stop
    $renderProperty = $parameters.PSObject.Properties['RenderNode']
    if ($null -eq $renderProperty -or [uint32]$renderProperty.Value -ne 1) {
        throw 'Armamento recusado: RenderNode=1 nao foi aplicado e relido.'
    }
    $probe = Get-ItemProperty -LiteralPath `
        'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe' -ErrorAction Stop
    $renderPreflight = [ordered]@{
        V5RegistrationMode = 0
        HqdActivePhysicalReady = 1
        ShaderPhysicalReady = 1
        P1EngineState = 1
        P1SelfTestPhysicalReady = 1
        P2PhysicalReady = 1
    }
    foreach ($entry in $renderPreflight.GetEnumerator()) {
        $property = $probe.PSObject.Properties[$entry.Key]
        if ($null -eq $property -or [uint32]$property.Value -ne [uint32]$entry.Value) {
            throw "Armamento recusado: preflight RenderNode sem $($entry.Key)=$($entry.Value)."
        }
    }
}

Write-Host "BC250 REBOOT WATCHDOG PREFLIGHT: PASS"
Write-Host "  candidate: $expectedDriverVersion"
Write-Host "  device service/code: $($devices[0].Service) / $($devices[0].ConfigManagerErrorCode)"
Write-Host "  target INF: $targetInf"
Write-Host "  timeout after next boot: $TimeoutSeconds seconds"
if ($null -ne $completedPredecessor) {
    Write-Host "  completed predecessor: $($completedPredecessor.task_name) / DISARMED"
}
if (-not $Execute) {
    if ($IAcceptPersistentRebootGuard) {
        throw '-IAcceptPersistentRebootGuard nao e valido sem -Execute.'
    }
    Write-Host 'DRY-RUN: nenhuma pasta ou tarefa criada; nenhum reboot executado.'
    exit 0
}
if (-not $IAcceptPersistentRebootGuard) {
    throw 'Armamento recusado: falta -IAcceptPersistentRebootGuard.'
}
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Armamento exige PowerShell elevado.'
}
if (-not [Environment]::Is64BitProcess) { throw 'Armamento exige PowerShell 64-bit.' }

$completedPredecessor = Get-CompletedPredecessorGuard

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-mcdm-reboot-watchdog"
New-Item -ItemType Directory -Path $evidenceDir | Out-Null
if ($null -ne $completedPredecessor) {
    $quarantineRoot = 'C:\BC250_DRIVER_QUARANTINE'
    if (-not (Test-Path -LiteralPath $quarantineRoot -PathType Container)) {
        New-Item -ItemType Directory -Path $quarantineRoot | Out-Null
    }
    $quarantinePath = Join-Path $quarantineRoot "BC250_REBOOT_GUARD-$stamp-completed"
    $resolvedGuard = [IO.Path]::GetFullPath($guardRoot)
    $resolvedQuarantine = [IO.Path]::GetFullPath($quarantinePath)
    if ($resolvedGuard -ne 'C:\BC250_REBOOT_GUARD' -or
        -not $resolvedQuarantine.StartsWith('C:\BC250_DRIVER_QUARANTINE\')) {
        throw 'Armamento recusado: caminhos de quarentena inesperados.'
    }
    Move-Item -LiteralPath $resolvedGuard -Destination $resolvedQuarantine
    [ordered]@{
        source = $resolvedGuard
        quarantine = $resolvedQuarantine
        predecessor = $completedPredecessor
        moved_at = [datetime]::Now.ToString('o')
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath `
        (Join-Path $evidenceDir 'completed-predecessor-quarantine.json') -Encoding UTF8
}
New-Item -ItemType Directory -Path $guardRoot | Out-Null
Copy-Item -LiteralPath $sourceWatchdog -Destination $installedWatchdog
Copy-Item -LiteralPath $sourceConfirm -Destination $installedConfirm
$token = [guid]::NewGuid().ToString('D')
$bootTicks = [long](Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks
$config = [ordered]@{
    schema_version = 1
    armed_at = [datetime]::Now.ToString('o')
    armed_boot_ticks = $bootTicks
    task_name = $taskName
    token = $token
    instance_id = [string]$devices[0].PNPDeviceID
    target_inf = $targetInf
    expected_driver_version = $expectedDriverVersion
    expected_schema_version = [uint32]$manifest.TelemetrySchemaVersion
    expected_skip_hw_bringup = [uint32]$manifest.SkipHwBringup
    expected_render_node = $(if ($RequireRenderNode) { 1 } else { 0 })
    lab_candidate = [bool]$LabCandidate
    expected_sys_sha256 = [string]$manifest.PackageSysSha256
    expected_inf_sha256 = [string]$manifest.PackageInfSha256
    expected_recovery_sha256 = [string]$manifest.RecoverySha256
    timeout_seconds = $TimeoutSeconds
    evidence_dir = $evidenceDir
    rollback_path = $rollbackPath
    rollback_sha256 = (Get-FileHash -LiteralPath $rollbackPath -Algorithm SHA256).Hash
    hardware_validated = $false
}
$config | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $installedConfig -Encoding UTF8
$config | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath `
    (Join-Path $evidenceDir 'reboot-watchdog-manifest.json') -Encoding UTF8

$powerShellPath = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$arguments = "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File `"$installedWatchdog`" -ConfigPath `"$installedConfig`" -Execute"
$action = New-ScheduledTaskAction -Execute $powerShellPath -Argument $arguments
$trigger = New-ScheduledTaskTrigger -AtStartup
$taskPrincipal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 15) `
    -MultipleInstances IgnoreNew -StartWhenAvailable
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
    -Principal $taskPrincipal -Settings $settings | Out-Null

$task = Get-ScheduledTask -TaskName $taskName -ErrorAction Stop
$taskInfo = Get-ScheduledTaskInfo -TaskName $taskName -ErrorAction Stop
$taskEvidence = [ordered]@{
    task_name = $task.TaskName
    state = [string]$task.State
    enabled = [bool]$task.Settings.Enabled
    execute = [string]$task.Actions.Execute
    arguments = [string]$task.Actions.Arguments
    last_run_time = $taskInfo.LastRunTime.ToString('o')
    last_task_result = $taskInfo.LastTaskResult
    reboot_executed = $false
}
$taskEvidence | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath `
    (Join-Path $evidenceDir 'scheduled-task-after-arm.json') -Encoding UTF8
Export-ScheduledTask -TaskName $taskName | Set-Content -LiteralPath `
    (Join-Path $evidenceDir 'scheduled-task.xml') -Encoding UTF8
@"
SE A TELA POS-REBOOT ESTIVER VISIVEL, execute como Administrador:
powershell -NoProfile -ExecutionPolicy Bypass -File "$installedConfirm" -ConfigPath "$installedConfig" -IConfirmDisplayIsVisible

SE A TELA FICAR PRETA:
1. Aguarde pelo menos $TimeoutSeconds segundos para o rollback online automatico.
2. Se nao recuperar, inicie em Modo de Seguranca com Prompt de Comando.
3. Execute C:\BC250_RECOVERY.cmd e reinicie.
"@ | Set-Content -LiteralPath (Join-Path $evidenceDir 'RECOVERY-REBOOT.txt') -Encoding UTF8

Write-Host 'BC250 REBOOT WATCHDOG: ARMED'
Write-Host "  task: $taskName ($($task.State))"
Write-Host "  evidence: $evidenceDir"
Write-Host "  timeout after next boot: $TimeoutSeconds seconds"
Write-Host '  reboot executed: NO'
Write-Host 'Depois do reboot, com imagem visivel, confirme usando:'
Write-Host "powershell -NoProfile -ExecutionPolicy Bypass -File `"$installedConfirm`" -ConfigPath `"$installedConfig`" -IConfirmDisplayIsVisible"
Write-Host 'NAO VALIDADO EM HARDWARE'
