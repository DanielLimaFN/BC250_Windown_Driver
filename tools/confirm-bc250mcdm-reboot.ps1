[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ConfigPath,
    [switch]$IConfirmDisplayIsVisible
)

$ErrorActionPreference = 'Stop'
if (-not $IConfirmDisplayIsVisible) {
    throw 'Confirmacao de reboot recusada: falta -IConfirmDisplayIsVisible.'
}
if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
    throw 'Confirmacao de reboot recusada: configuracao ausente.'
}
$config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
$currentBootTicks = [long](Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks
if ($currentBootTicks -le [long]$config.armed_boot_ticks) {
    throw 'Confirmacao de reboot recusada: a maquina ainda esta no boot de armamento.'
}
$statePath = Join-Path ([string]$config.evidence_dir) 'reboot-watchdog-state.json'
if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
    throw 'Confirmacao de reboot recusada: watchdog ainda nao publicou estado.'
}
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
if ([string]$state.state -ne 'OBSERVING' -or [long]$state.boot_ticks -ne $currentBootTicks) {
    throw "Confirmacao de reboot recusada: estado do watchdog=$($state.state)."
}
$devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
    Where-Object { $_.PNPDeviceID -eq [string]$config.instance_id })
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'bc250mcdm' -or
    $devices[0].ConfigManagerErrorCode -ne 0) {
    throw 'Confirmacao de reboot recusada: BC-250 nao esta em bc250mcdm Codigo 0.'
}
$video = @(Get-CimInstance Win32_VideoController -ErrorAction Stop |
    Where-Object { $_.PNPDeviceID -eq [string]$config.instance_id })
if ($video.Count -ne 1 -or [string]$video[0].DriverVersion -ne
    [string]$config.expected_driver_version) {
    throw 'Confirmacao de reboot recusada: versao ativa nao corresponde ao candidato autorizado.'
}
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
if (-not (Test-Path -LiteralPath $driverPath) -or
    (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash -ne
        [string]$config.expected_sys_sha256) {
    throw 'Confirmacao de reboot recusada: hash do SYS ativo diverge.'
}
$probeKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
$vsyncDeadline = (Get-Date).AddSeconds(20)
do {
    $probe = Get-ItemProperty -LiteralPath $probeKey -ErrorAction Stop
    $vsyncProperty = $probe.PSObject.Properties['DmmVsyncCount']
    $vsyncProved = ($null -ne $vsyncProperty -and [uint32]$vsyncProperty.Value -ge 1)
    if (-not $vsyncProved) { Start-Sleep -Milliseconds 250 }
} until ($vsyncProved -or (Get-Date) -ge $vsyncDeadline)
if (-not $vsyncProved) {
    throw 'Confirmacao de reboot recusada: VSync real seguido por Present nao foi comprovado.'
}
$requiredProbe = [ordered]@{
    SchemaVersion = 96
    SkipHwBringup = 1
    StartDeviceEntered = 1
    DmmIsSupportedStatus = 0
    DmmIsSupportedResult = 1
    DmmCommitStatus = 0
    DmmCommitHasPath = 1
    DmmCommitSupported = 1
    DmmPresentEntered = 1
    DmmPresentStatus = 0
}
foreach ($entry in $requiredProbe.GetEnumerator()) {
    $property = $probe.PSObject.Properties[$entry.Key]
    if ($null -eq $property -or [uint32]$property.Value -ne [uint32]$entry.Value) {
        throw "Confirmacao de reboot recusada: $($entry.Key)=$($entry.Value) nao comprovado."
    }
}
foreach ($counterName in @('DmmIsSupportedCount','DmmCommitCount','DmmControlInterruptCount')) {
    $property = $probe.PSObject.Properties[$counterName]
    if ($null -eq $property -or [uint32]$property.Value -lt 1) {
        throw "Confirmacao de reboot recusada: $counterName nao avancou."
    }
}

$events = @(Get-WinEvent -FilterHashtable @{
    LogName='System'; StartTime=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime; Level=1,2,3
} -ErrorAction SilentlyContinue | Where-Object {
    $_.ProviderName -match 'Display|Kernel-PnP|DriverFrameworks|WHEA|BugCheck|dxgkrnl' -or
    $_.Message -match 'bc250|13FE|display driver|video'
} | Select-Object TimeCreated,Id,LevelDisplayName,ProviderName,Message)
if ($events.Count -ne 0) {
    $events | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath `
        (Join-Path ([string]$config.evidence_dir) 'reboot-warning-error-events.json') -Encoding UTF8
    throw 'Confirmacao de reboot recusada: ha warning/error relevante no log do Sistema.'
}

$confirmationPath = Join-Path ([string]$config.evidence_dir) 'reboot-confirmation.json'
$temporaryPath = "$confirmationPath.$([guid]::NewGuid().ToString('N')).tmp"
[ordered]@{
    token = [string]$config.token
    instance_id = [string]$config.instance_id
    boot_ticks = $currentBootTicks
    display_visible = $true
    confirmed_at = [datetime]::Now.ToString('o')
    driver_version = [string]$video[0].DriverVersion
    sys_sha256 = (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash
    schema_version = [uint32]$probe.SchemaVersion
    skip_hw_bringup = [uint32]$probe.SkipHwBringup
    is_supported_count = [uint32]$probe.DmmIsSupportedCount
    commit_count = [uint32]$probe.DmmCommitCount
    control_interrupt_count = [uint32]$probe.DmmControlInterruptCount
    present_status = [uint32]$probe.DmmPresentStatus
    hardware_validated = $false
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $temporaryPath -Encoding UTF8
Move-Item -LiteralPath $temporaryPath -Destination $confirmationPath -Force
Write-Host 'Confirmacao visual pos-reboot publicada; aguarde o watchdog desarmar.'
Write-Host 'Display presente nao valida compute nem aceleracao.'
