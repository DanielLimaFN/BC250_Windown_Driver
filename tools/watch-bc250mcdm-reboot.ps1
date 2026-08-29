[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ConfigPath,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'

function Write-AtomicJson([string]$Path, [object]$Value) {
    $temporary = "$Path.$([guid]::NewGuid().ToString('N')).tmp"
    $Value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temporary -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
    throw 'Reboot watchdog recusado: configuracao ausente.'
}
$config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
$required = @(
    'schema_version','task_name','token','instance_id','target_inf',
    'expected_driver_version','expected_sys_sha256','expected_inf_sha256',
    'armed_boot_ticks','timeout_seconds','evidence_dir','rollback_path',
    'rollback_sha256'
)
foreach ($name in $required) {
    if ($null -eq $config.PSObject.Properties[$name]) {
        throw "Reboot watchdog recusado: campo ausente: $name"
    }
}
if ([uint32]$config.schema_version -ne 1 -or
    [string]$config.instance_id -notmatch '(?i)^PCI\\VEN_1002&DEV_13FE(?:&|\\)' -or
    [string]$config.target_inf -notmatch '^oem\d+\.inf$' -or
    [string]$config.expected_driver_version -ne '3.24.0.0' -or
    [int]$config.timeout_seconds -lt 180 -or [int]$config.timeout_seconds -gt 600) {
    throw 'Reboot watchdog recusado: contrato de configuracao invalido.'
}
$parsedToken = [guid]::Empty
if (-not [guid]::TryParseExact([string]$config.token, 'D', [ref]$parsedToken)) {
    throw 'Reboot watchdog recusado: token invalido.'
}
if (-not (Test-Path -LiteralPath ([string]$config.evidence_dir) -PathType Container)) {
    throw 'Reboot watchdog recusado: evidencia ausente.'
}
$statePath = Join-Path ([string]$config.evidence_dir) 'reboot-watchdog-state.json'
$confirmationPath = Join-Path ([string]$config.evidence_dir) 'reboot-confirmation.json'
$attemptPath = Join-Path ([string]$config.evidence_dir) 'reboot-rollback-attempt.json'

function Write-State([string]$State, [string]$Detail, [int]$ExitCode = 0) {
    Write-AtomicJson $statePath ([ordered]@{
        state = $State
        detail = $Detail
        task_name = [string]$config.task_name
        target_inf = [string]$config.target_inf
        instance_id = [string]$config.instance_id
        boot_ticks = [long](Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks
        timestamp = [datetime]::Now.ToString('o')
        exit_code = $ExitCode
        hardware_validated = $false
    })
}

if (-not $Execute) {
    Write-Host "REBOOT WATCHDOG DRY-RUN: task=$($config.task_name) timeout=$($config.timeout_seconds)"
    Write-Host 'Zero espera, zero rollback e zero alteracao de tarefa.'
    exit 0
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Reboot watchdog exige contexto elevado/SYSTEM.'
}
$currentBootTicks = [long](Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks
if ($currentBootTicks -le [long]$config.armed_boot_ticks) {
    Write-State 'WAITING_FOR_REBOOT' 'task started in the arming boot; no action taken' 10
    exit 10
}
if (Test-Path -LiteralPath $attemptPath) {
    Write-State 'BLOCKED' 'rollback was already attempted; safe-mode recovery required' 11
    Unregister-ScheduledTask -TaskName ([string]$config.task_name) -Confirm:$false -ErrorAction SilentlyContinue
    exit 11
}
if (-not (Test-Path -LiteralPath ([string]$config.rollback_path) -PathType Leaf) -or
    (Get-FileHash -LiteralPath ([string]$config.rollback_path) -Algorithm SHA256).Hash -ne
        [string]$config.rollback_sha256) {
    Write-State 'BLOCKED' 'authorized rollback script missing or changed; safe-mode recovery required' 12
    Unregister-ScheduledTask -TaskName ([string]$config.task_name) -Confirm:$false -ErrorAction SilentlyContinue
    exit 12
}

Write-State 'OBSERVING' "waiting $($config.timeout_seconds) seconds for current-boot visual confirmation"
$deadline = [datetime]::Now.AddSeconds([int]$config.timeout_seconds)
do {
    if (Test-Path -LiteralPath $confirmationPath -PathType Leaf) {
        try {
            $confirmation = Get-Content -LiteralPath $confirmationPath -Raw | ConvertFrom-Json
            if ([string]$confirmation.token -eq [string]$config.token -and
                [string]$confirmation.instance_id -eq [string]$config.instance_id -and
                [long]$confirmation.boot_ticks -eq $currentBootTicks -and
                [bool]$confirmation.display_visible) {
                Write-State 'DISARMED' 'current-boot visual confirmation accepted'
                Unregister-ScheduledTask -TaskName ([string]$config.task_name) -Confirm:$false -ErrorAction SilentlyContinue
                exit 0
            }
        } catch {
            # Arquivo parcial ou estrangeiro nunca desarma o watchdog.
        }
    }
    Start-Sleep -Milliseconds 250
} until ([datetime]::Now -ge $deadline)

$devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
    Where-Object { $_.PNPDeviceID -eq [string]$config.instance_id })
if ($devices.Count -eq 1 -and $devices[0].Service -eq 'BasicDisplay' -and
    $devices[0].ConfigManagerErrorCode -eq 0) {
    Write-State 'NO_ACTION' 'BasicDisplay Code 0 already active at timeout'
    Unregister-ScheduledTask -TaskName ([string]$config.task_name) -Confirm:$false -ErrorAction SilentlyContinue
    exit 0
}
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'bc250mcdm' -or
    $devices[0].ConfigManagerErrorCode -notin @(0,43)) {
    Write-State 'BLOCKED' 'unexpected PnP state; safe-mode recovery required' 13
    Unregister-ScheduledTask -TaskName ([string]$config.task_name) -Confirm:$false -ErrorAction SilentlyContinue
    exit 13
}

$markerLines = @(Get-Content -LiteralPath 'C:\BC250_TEST_OEMINF.txt' -ErrorAction SilentlyContinue)
$installedInf = Join-Path $env:SystemRoot "INF\$($config.target_inf)"
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
if ($markerLines.Count -ne 1 -or
    $markerLines[0].ToLowerInvariant() -ne ([string]$config.target_inf).ToLowerInvariant() -or
    -not (Test-Path -LiteralPath $installedInf) -or
    (Get-FileHash -LiteralPath $installedInf -Algorithm SHA256).Hash -ne [string]$config.expected_inf_sha256 -or
    -not (Test-Path -LiteralPath $driverPath) -or
    (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash -ne [string]$config.expected_sys_sha256) {
    Write-State 'BLOCKED' 'package identity changed; safe-mode recovery required' 14
    Unregister-ScheduledTask -TaskName ([string]$config.task_name) -Confirm:$false -ErrorAction SilentlyContinue
    exit 14
}

Write-AtomicJson $attemptPath ([ordered]@{
    attempted_at = [datetime]::Now.ToString('o')
    boot_ticks = $currentBootTicks
    target_inf = [string]$config.target_inf
    reason = 'visual confirmation timeout'
})
Write-State 'ROLLBACK_STARTED' 'current-boot visual confirmation timeout expired'
$rollbackOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    ([string]$config.rollback_path) -Execute -IAcceptDisplayInterruptionRisk 2>&1) -join "`n"
$rollbackExit = $LASTEXITCODE
$rollbackOutput | Set-Content -LiteralPath `
    (Join-Path ([string]$config.evidence_dir) 'reboot-watchdog-rollback.txt') -Encoding UTF8
if ($rollbackExit -ne 0) {
    Write-State 'ROLLBACK_FAILED' 'online rollback failed; safe-mode recovery required' $rollbackExit
    Unregister-ScheduledTask -TaskName ([string]$config.task_name) -Confirm:$false -ErrorAction SilentlyContinue
    exit $rollbackExit
}
Write-State 'ROLLED_BACK' 'BasicDisplay restoration completed after reboot confirmation timeout'
Unregister-ScheduledTask -TaskName ([string]$config.task_name) -Confirm:$false -ErrorAction SilentlyContinue
exit 0
