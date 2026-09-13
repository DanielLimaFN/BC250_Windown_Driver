[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EvidenceDir,
    [ValidateRange(1,600)][int]$RequiredRemainingSeconds = 30
)

$ErrorActionPreference = 'Stop'
$taskName = 'BC250-SafeBindGuard'
$deadlineTaskName = 'BC250-SafeBindDeadline'
$configPath = Join-Path $EvidenceDir 'safe-bind-watchdog-config.json'
$statePath = Join-Path $EvidenceDir 'safe-bind-watchdog-state.json'

if (-not (Test-Path -LiteralPath $configPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
    throw 'Guardiao SYSTEM recusado: configuracao ou estado ausente na evidencia.'
}
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$expectedEvidence = [IO.Path]::GetFullPath($EvidenceDir).TrimEnd('\')
$declaredEvidence = [IO.Path]::GetFullPath([string]$config.evidence_dir).TrimEnd('\')
if (-not [string]::Equals($expectedEvidence, $declaredEvidence,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Guardiao SYSTEM recusado: configuracao pertence a outra evidencia.'
}
if ([string]$config.task_name -ne $taskName -or
    [string]$state.state -ne 'OBSERVING') {
    throw "Guardiao SYSTEM recusado: estado=$($state.state)."
}
$task = Get-ScheduledTask -TaskName $taskName -ErrorAction Stop
if ([string]$task.State -ne 'Running') {
    throw "Guardiao SYSTEM recusado: tarefa esta $($task.State), nao Running."
}
$deadlineTask = Get-ScheduledTask -TaskName $deadlineTaskName -ErrorAction Stop
if ([string]$deadlineTask.State -notin @('Ready','Running')) {
    throw "Guardiao SYSTEM recusado: tarefa de prazo esta $($deadlineTask.State)."
}
$guardProcesses = @(Get-CimInstance Win32_Process -ErrorAction Stop |
    Where-Object {
        [string]$_.CommandLine -match 'watch-bc250mcdm-safe-bind\.ps1' -and
        [string]$_.CommandLine -match [regex]::Escape('C:\BC250_SAFE_GUARD\armed.json')
    })
if ($guardProcesses.Count -ne 1) {
    throw 'Guardiao SYSTEM recusado: processo SYSTEM exato nao esta vivo.'
}
$deadlineUtc = [datetime]::new([long]$config.deadline_utc_ticks, [DateTimeKind]::Utc)
$remaining = ($deadlineUtc - [datetime]::UtcNow).TotalSeconds
if ($remaining -lt $RequiredRemainingSeconds) {
    throw ("Guardiao SYSTEM recusado: restam {0:N1}s; exigidos {1}s." -f
        $remaining, $RequiredRemainingSeconds)
}

[ordered]@{
    state = [string]$state.state
    task_name = $taskName
    task_state = [string]$task.State
    deadline_task_name = $deadlineTaskName
    deadline_task_state = [string]$deadlineTask.State
    process_id = [int]$guardProcesses[0].ProcessId
    token = [string]$config.token
    instance_id = [string]$config.instance_id
    remaining_seconds = [math]::Round($remaining, 1)
    evidence_dir = $declaredEvidence
    hardware_validated = $false
} | ConvertTo-Json -Compress
