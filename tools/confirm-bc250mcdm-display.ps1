[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EvidenceDir,
    [Parameter(Mandatory = $true)][string]$InstanceId,
    [Parameter(Mandatory = $true)][string]$Token,
    [switch]$IConfirmDisplayIsVisible
)

$ErrorActionPreference = 'Stop'
if (-not $IConfirmDisplayIsVisible) {
    throw 'Confirmacao recusada: falta -IConfirmDisplayIsVisible.'
}
if (-not (Test-Path -LiteralPath $EvidenceDir -PathType Container)) {
    throw 'Confirmacao recusada: diretorio de evidencia ausente.'
}
$parsedToken = [guid]::Empty
if (-not [guid]::TryParseExact($Token, 'D', [ref]$parsedToken)) {
    throw 'Confirmacao recusada: token invalido.'
}
$devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
    Where-Object { $_.PNPDeviceID -eq $InstanceId })
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'bc250mcdm' -or
    $devices[0].ConfigManagerErrorCode -ne 0) {
    throw 'Confirmacao recusada: BC-250 nao esta em bc250mcdm Codigo 0.'
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
    throw 'Confirmacao recusada: VSync real seguido por Present nao foi comprovado.'
}
if ($null -eq $probe.PSObject.Properties['SkipHwBringup'] -or
    [uint32]$probe.SkipHwBringup -ne 1) {
    throw 'Confirmacao recusada: SkipHwBringup=1 nao foi comprovado.'
}
$requiredDisplayProbe = [ordered]@{
    SchemaVersion = 98
    DmmIsSupportedStatus = 0
    DmmIsSupportedResult = 1
    DmmCommitStatus = 0
    DmmCommitHasPath = 1
    DmmCommitSupported = 1
    DmmPresentEntered = 1
    DmmPresentStatus = 0
}
foreach ($entry in $requiredDisplayProbe.GetEnumerator()) {
    $property = $probe.PSObject.Properties[$entry.Key]
    if ($null -eq $property -or [uint32]$property.Value -ne [uint32]$entry.Value) {
        throw "Confirmacao recusada: telemetria $($entry.Key)=$($entry.Value) nao foi comprovada."
    }
}
foreach ($counterName in @(
        'DmmIsSupportedCount','DmmCommitCount',
        'DmmControlInterruptCount')) {
    $property = $probe.PSObject.Properties[$counterName]
    if ($null -eq $property -or [uint32]$property.Value -lt 1) {
        throw "Confirmacao recusada: contador $counterName nao avancou."
    }
}

$finalPath = Join-Path $EvidenceDir 'watchdog-disarm.json'
$temporaryPath = Join-Path $EvidenceDir ("watchdog-disarm-{0}.tmp" -f [guid]::NewGuid().ToString('N'))
[ordered]@{
    token = $Token
    instance_id = $InstanceId
    display_visible = $true
    confirmed_at = [datetime]::Now.ToString('o')
    skip_hw_bringup = 1
    schema_version = 98
    is_supported_count = [uint32]$probe.DmmIsSupportedCount
    commit_count = [uint32]$probe.DmmCommitCount
    control_interrupt_count = [uint32]$probe.DmmControlInterruptCount
    present_entered = [uint32]$probe.DmmPresentEntered
    present_status = [uint32]$probe.DmmPresentStatus
    vsync_count_at_first_present = if (
        $null -ne $probe.PSObject.Properties['DmmVsyncCount']) {
        [uint32]$probe.DmmVsyncCount
    } else { $null }
    hardware_validated = $false
} | ConvertTo-Json | Set-Content -LiteralPath $temporaryPath -Encoding UTF8
Move-Item -LiteralPath $temporaryPath -Destination $finalPath
Write-Host 'Watchdog desarmado por confirmacao visual explicita.'
Write-Host 'Display presente nao valida compute nem aceleracao.'
