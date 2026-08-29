[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EvidenceDir,
    [Parameter(Mandatory = $true)][string]$InstanceId,
    [Parameter(Mandatory = $true)][string]$TargetInf,
    [Parameter(Mandatory = $true)][string]$Token,
    [ValidateRange(30,600)][int]$TimeoutSeconds = 180,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$rollbackScript = Join-Path $root 'tools\rollback-bc250mcdm-lab.ps1'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$statePath = Join-Path $EvidenceDir 'watchdog-state.json'
$disarmPath = Join-Path $EvidenceDir 'watchdog-disarm.json'

if (-not (Test-Path -LiteralPath $EvidenceDir -PathType Container)) {
    throw 'Watchdog recusado: diretorio de evidencia ausente.'
}
if ($InstanceId -notmatch '(?i)^PCI\\VEN_1002&DEV_13FE(?:&|\\)') {
    throw 'Watchdog recusado: instance ID nao pertence ao BC-250 exato.'
}

if (-not $Execute) {
    Write-Host "WATCHDOG LAB DRY-RUN: target=$TargetInf timeout=$TimeoutSeconds instance=$InstanceId"
    exit 0
}

function Write-WatchdogState([string]$State, [string]$Detail, [int]$ExitCode = 0) {
    [ordered]@{
        state = $State
        detail = $Detail
        target_inf = $TargetInf
        instance_id = $InstanceId
        timestamp = [datetime]::Now.ToString('o')
        exit_code = $ExitCode
        hardware_validated = $false
    } | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8
}

Write-WatchdogState 'ARMED' "automatic rollback in $TimeoutSeconds seconds"
$deadline = [datetime]::Now.AddSeconds($TimeoutSeconds)
do {
    if (Test-Path -LiteralPath $disarmPath) {
        try {
            $disarm = Get-Content -LiteralPath $disarmPath -Raw | ConvertFrom-Json
            if ([string]$disarm.token -eq $Token -and
                [string]$disarm.instance_id -eq $InstanceId -and
                [bool]$disarm.display_visible) {
                Write-WatchdogState 'DISARMED' 'visual confirmation received'
                exit 0
            }
        } catch {
        }
    }
    Start-Sleep -Milliseconds 250
} until ([datetime]::Now -ge $deadline)

Write-WatchdogState 'ROLLBACK_STARTED' 'visual confirmation timeout expired'
$rollbackOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $rollbackScript `
    -Execute -IAcceptDisplayInterruptionRisk 2>&1) -join "`n"
$rollbackExit = $LASTEXITCODE
$rollbackOutput | Set-Content -LiteralPath (Join-Path $EvidenceDir 'watchdog-rollback.txt') -Encoding UTF8

if ($rollbackExit -ne 0) {
    Write-WatchdogState 'ROLLBACK_FAILED' 'online rollback failed; safe-mode recovery required' $rollbackExit
    exit $rollbackExit
}
Write-WatchdogState 'ROLLED_BACK' 'BasicDisplay restoration completed'
exit 0
