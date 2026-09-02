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
    throw 'Safe-bind watchdog recusado: configuracao ausente.'
}
$config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
foreach ($name in @('schema_version','task_name','token','instance_id','timeout_seconds',
        'evidence_dir','recovery_path','recovery_sha256','armed_boot_ticks',
        'automatic_recovery_reboot')) {
    if ($null -eq $config.PSObject.Properties[$name]) {
        throw "Safe-bind watchdog recusado: campo ausente: $name"
    }
}
if ([uint32]$config.schema_version -ne 1 -or
    [string]$config.instance_id -notmatch '(?i)^PCI\\VEN_1002&DEV_13FE(?:&|\\)' -or
    [int]$config.timeout_seconds -lt 120 -or [int]$config.timeout_seconds -gt 600) {
    throw 'Safe-bind watchdog recusado: contrato invalido.'
}
$parsedToken = [guid]::Empty
if (-not [guid]::TryParseExact([string]$config.token, 'D', [ref]$parsedToken)) {
    throw 'Safe-bind watchdog recusado: token invalido.'
}

$statePath = Join-Path ([string]$config.evidence_dir) 'safe-bind-watchdog-state.json'
$confirmationPath = Join-Path ([string]$config.evidence_dir) 'safe-bind-confirmation.json'
$attemptPath = Join-Path ([string]$config.evidence_dir) 'safe-bind-rollback-attempt.json'

function Write-State([string]$State, [string]$Detail, [int]$ExitCode = 0) {
    Write-AtomicJson $statePath ([ordered]@{
        state = $State
        detail = $Detail
        timestamp = (Get-Date).ToString('o')
        boot_ticks = [long](Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks
        exit_code = $ExitCode
        hardware_validated = $false
    })
}

if (-not $Execute) {
    Write-Host "SAFE-BIND WATCHDOG DRY-RUN: timeout=$($config.timeout_seconds)s"
    exit 0
}
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Safe-bind watchdog exige Administrador/SYSTEM.'
}
if (-not (Test-Path -LiteralPath ([string]$config.recovery_path) -PathType Leaf) -or
    (Get-FileHash -LiteralPath ([string]$config.recovery_path) -Algorithm SHA256).Hash -ne
        [string]$config.recovery_sha256) {
    Write-State 'BLOCKED' 'recovery ausente ou alterada' 12
    exit 12
}
$currentBootTicks = [long](Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks
$attemptNumber = 1
$retryAfterBoot = $false
if (Test-Path -LiteralPath $attemptPath) {
    $previousAttempt = Get-Content -LiteralPath $attemptPath -Raw | ConvertFrom-Json
    if ([long]$previousAttempt.boot_ticks -eq $currentBootTicks -or
        [uint32]$previousAttempt.attempt_number -ge 2) {
        Write-State 'BLOCKED' 'rollback ja foi tentado neste boot ou atingiu o limite de 2 boots' 11
        exit 11
    }
    $attemptNumber = [uint32]$previousAttempt.attempt_number + 1
    $retryAfterBoot = $true
}

Write-State 'OBSERVING' $(if ($retryAfterBoot) {
    'boot posterior ao rollback incompleto; verificacao imediata'
} else {
    'guardiao ativo antes do bind; aguardando confirmacao explicita'
})
$deadline = $(if ($retryAfterBoot) { Get-Date } else {
    (Get-Date).AddSeconds([int]$config.timeout_seconds)
})
do {
    if (Test-Path -LiteralPath $confirmationPath -PathType Leaf) {
        try {
            $confirmation = Get-Content -LiteralPath $confirmationPath -Raw | ConvertFrom-Json
            $currentBoot = [long](Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks
            if ([string]$confirmation.token -eq [string]$config.token -and
                [string]$confirmation.instance_id -eq [string]$config.instance_id -and
                [long]$confirmation.boot_ticks -eq $currentBoot -and
                [bool]$confirmation.display_visible) {
                Write-State 'DISARMED' 'confirmacao visual e estado PnP estavel aceitos'
                Unregister-ScheduledTask -TaskName ([string]$config.task_name) -Confirm:$false -ErrorAction SilentlyContinue
                exit 0
            }
        } catch { }
    }
    Start-Sleep -Milliseconds 250
} until ((Get-Date) -ge $deadline)

$devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
    Where-Object { $_.PNPDeviceID -eq [string]$config.instance_id })
if ($devices.Count -eq 1 -and $devices[0].Service -eq 'BasicDisplay' -and
    [uint32]$devices[0].ConfigManagerErrorCode -eq 0) {
    Write-State 'NO_ACTION' 'BasicDisplay Codigo 0 ja estava restaurado no timeout'
    Unregister-ScheduledTask -TaskName ([string]$config.task_name) -Confirm:$false -ErrorAction SilentlyContinue
    exit 0
}

# SNAPSHOT antes do rollback: preserva a causa mesmo quando a tela esta preta
# e o operador nao consegue executar o inspetor manualmente.
$devices | Select-Object PNPDeviceID,Service,Status,ConfigManagerErrorCode |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath `
    (Join-Path ([string]$config.evidence_dir) 'safe-bind-pnp-before-rollback.json') -Encoding UTF8
$probeKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
if (Test-Path -LiteralPath $probeKey) {
    $probe = Get-ItemProperty -LiteralPath $probeKey -ErrorAction SilentlyContinue
    if ($null -ne $probe) {
        $probe | Select-Object * -ExcludeProperty PSPath,PSParentPath,PSChildName,PSDrive,PSProvider |
            ConvertTo-Json -Depth 6 | Set-Content -LiteralPath `
            (Join-Path ([string]$config.evidence_dir) 'safe-bind-probe-before-rollback.json') -Encoding UTF8
    }
}
$bootTime = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
$events = @(Get-WinEvent -FilterHashtable @{ LogName='System'; StartTime=$bootTime } `
    -ErrorAction SilentlyContinue | Where-Object {
        $_.ProviderName -match 'Display|Kernel-PnP|DriverFrameworks|WHEA|BugCheck|dxgkrnl' -or
        $_.Message -match 'bc250|13FE|display driver|video'
    } | Select-Object TimeCreated,Id,LevelDisplayName,ProviderName,Message)
$events | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath `
    (Join-Path ([string]$config.evidence_dir) 'safe-bind-events-before-rollback.json') -Encoding UTF8

Write-AtomicJson $attemptPath ([ordered]@{
    attempted_at = (Get-Date).ToString('o')
    attempt_number = $attemptNumber
    boot_ticks = $currentBootTicks
    observed_service = $(if ($devices.Count -eq 1) { [string]$devices[0].Service } else { '<count-ne-1>' })
    observed_code = $(if ($devices.Count -eq 1) { [uint32]$devices[0].ConfigManagerErrorCode } else { $null })
    reason = 'ausencia de confirmacao explicita; qualquer Codigo, inclusive 39/43, exige rollback'
})
Write-State 'ROLLBACK_STARTED' 'timeout sem confirmacao; executando recovery autorizada'
$output = (& cmd.exe /d /c ([string]$config.recovery_path) 2>&1) -join "`n"
$exitCode = $LASTEXITCODE
$output | Set-Content -LiteralPath (Join-Path ([string]$config.evidence_dir) 'safe-bind-recovery.txt') -Encoding UTF8
if ($exitCode -ne 0) {
    if ([bool]$config.automatic_recovery_reboot -and $attemptNumber -eq 1) {
        Write-State 'RECOVERY_REBOOT_REQUESTED' 'recovery online incompleta; reboot automatico em 10 s' $exitCode
        & shutdown.exe /r /t 10 /f /d p:2:4 /c "BC250 safe-bind recovery"
        exit $exitCode
    }
    Write-State 'ROLLBACK_FAILED' 'recovery falhou apos dois boots; execute C:\BC250_RECOVERY.cmd' $exitCode
    exit $exitCode
}
Write-State 'ROLLED_BACK' 'BasicDisplay restaurado pela recovery'
Unregister-ScheduledTask -TaskName ([string]$config.task_name) -Confirm:$false -ErrorAction SilentlyContinue
exit 0
