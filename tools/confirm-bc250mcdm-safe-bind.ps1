[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ConfigPath,
    [Parameter(Mandatory = $true)][string]$Token,
    [switch]$IConfirmDisplayIsVisible
)

$ErrorActionPreference = 'Stop'
if (-not $IConfirmDisplayIsVisible) { throw 'Falta -IConfirmDisplayIsVisible.' }
$config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
if ($Token -ne [string]$config.token) { throw 'Token de confirmacao invalido.' }
$absoluteDeadlineUtc = [datetime]::new(
    [long]$config.deadline_utc_ticks, [DateTimeKind]::Utc)
if (($absoluteDeadlineUtc - [datetime]::UtcNow).TotalSeconds -lt 70) {
    throw 'Confirmacao recusada: restam menos de 70 s; deixe o rollback acontecer.'
}
$devices = @(Get-CimInstance Win32_PnPEntity -OperationTimeoutSec 2 -ErrorAction Stop |
    Where-Object { $_.PNPDeviceID -eq [string]$config.instance_id })
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'bc250mcdm' -or
    [uint32]$devices[0].ConfigManagerErrorCode -ne 0) {
    throw 'Confirmacao recusada: BC-250 nao esta em bc250mcdm Codigo 0.'
}
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
if (-not (Test-Path -LiteralPath $driverPath) -or
    (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash -ne [string]$config.expected_sys_sha256) {
    throw 'Confirmacao recusada: hash do SYS ativo diverge.'
}

# Evita aceitar o Codigo 0 transitorio observado nas corridas anteriores.
$deadline = (Get-Date).AddSeconds(60)
do {
    Start-Sleep -Seconds 2
    if ([datetime]::UtcNow.AddSeconds(5) -ge $absoluteDeadlineUtc) {
        throw 'Confirmacao recusada: prazo absoluto alcancado durante a estabilidade.'
    }
    $sample = @(Get-CimInstance Win32_PnPEntity -OperationTimeoutSec 2 -ErrorAction Stop |
        Where-Object { $_.PNPDeviceID -eq [string]$config.instance_id })
    if ($sample.Count -ne 1 -or $sample[0].Service -ne 'bc250mcdm' -or
        [uint32]$sample[0].ConfigManagerErrorCode -ne 0) {
        throw 'Confirmacao recusada: estado deixou de ser bc250mcdm Codigo 0 durante 60 s.'
    }
} until ((Get-Date) -ge $deadline)

$confirmationPath = Join-Path ([string]$config.evidence_dir) 'safe-bind-confirmation.json'
$temporary = "$confirmationPath.$([guid]::NewGuid().ToString('N')).tmp"
[ordered]@{
    token = [string]$config.token
    instance_id = [string]$config.instance_id
    boot_ticks = [long](Get-CimInstance Win32_OperatingSystem `
        -OperationTimeoutSec 2 -ErrorAction Stop).LastBootUpTime.ToUniversalTime().Ticks
    display_visible = $true
    stable_seconds = 60
    confirmed_at = (Get-Date).ToString('o')
    sys_sha256 = (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash
    hardware_validated = $false
} | ConvertTo-Json | Set-Content -LiteralPath $temporary -Encoding UTF8
Move-Item -LiteralPath $temporary -Destination $confirmationPath -Force
Write-Host 'Confirmacao publicada. O watchdog desarmara em seguida.'
Write-Host 'Isto valida somente display visivel + PnP estavel; nao valida render nem compute.'
