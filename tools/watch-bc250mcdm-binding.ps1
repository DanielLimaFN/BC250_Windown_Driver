[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EvidenceDir,
    [Parameter(Mandatory = $true)][string]$InstanceId,
    [Parameter(Mandatory = $true)][string]$TargetInf,
    [Parameter(Mandatory = $true)][string]$Token,
    [ValidateRange(30,600)][int]$TimeoutSeconds = 120,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $root 'driver\wdk\package\output_active.manifest.json'
$rollbackScript = Join-Path $root 'tools\rollback-bc250mcdm-online.ps1'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$statePath = Join-Path $EvidenceDir 'watchdog-state.json'
$disarmPath = Join-Path $EvidenceDir 'watchdog-disarm.json'

if (-not (Test-Path -LiteralPath $EvidenceDir -PathType Container)) {
    throw 'Watchdog recusado: diretorio de evidencia ausente.'
}
foreach ($path in @($manifestPath,$rollbackScript)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Watchdog recusado: artefato ausente: $path" }
}
if ($InstanceId -notmatch '(?i)^PCI\\VEN_1002&DEV_13FE(?:&|\\)') {
    throw 'Watchdog recusado: instance ID nao pertence ao BC-250 exato.'
}
if ($TargetInf -notmatch '^oem\d+\.inf$') {
    throw 'Watchdog recusado: nome OEM invalido.'
}
$parsedToken = [guid]::Empty
if (-not [guid]::TryParseExact($Token, 'D', [ref]$parsedToken)) {
    throw 'Watchdog recusado: token invalido.'
}

if (-not $Execute) {
    Write-Host "WATCHDOG DRY-RUN: target=$TargetInf timeout=$TimeoutSeconds instance=$InstanceId"
    Write-Host 'Zero espera, zero binding e zero rollback.'
    exit 0
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Watchdog exige PowerShell elevado.'
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
            # A partial or foreign file never disarms the watchdog.
        }
    }
    Start-Sleep -Milliseconds 250
} until ([datetime]::Now -ge $deadline)

$devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
    Where-Object { $_.PNPDeviceID -eq $InstanceId })
if ($devices.Count -eq 1 -and $devices[0].Service -eq 'BasicDisplay' -and
    $devices[0].ConfigManagerErrorCode -eq 0) {
    Write-WatchdogState 'NO_ACTION' 'binding did not replace BasicDisplay'
    exit 0
}
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'bc250mcdm' -or
    $devices[0].ConfigManagerErrorCode -notin @(0,43)) {
    Write-WatchdogState 'BLOCKED' 'unexpected device identity/state; safe-mode recovery required' 2
    exit 2
}

$markerLines = @(Get-Content -LiteralPath $markerPath -ErrorAction SilentlyContinue)
if ($markerLines.Count -ne 1 -or $markerLines[0].ToLowerInvariant() -ne $TargetInf.ToLowerInvariant()) {
    Write-WatchdogState 'BLOCKED' 'OEM marker changed; refusing broad rollback' 3
    exit 3
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$installedInf = Join-Path $env:SystemRoot "INF\$TargetInf"
if (-not (Test-Path -LiteralPath $installedInf) -or
    (Get-FileHash -LiteralPath $installedInf -Algorithm SHA256).Hash -ne [string]$manifest.PackageInfSha256 -or
    -not (Test-Path -LiteralPath $driverPath) -or
    (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash -ne [string]$manifest.PackageSysSha256) {
    Write-WatchdogState 'BLOCKED' 'installed package hashes changed; safe-mode recovery required' 4
    exit 4
}

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
