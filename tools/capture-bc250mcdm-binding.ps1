[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EvidenceDir,
    [Parameter(Mandatory = $true)][string]$InstanceId,
    [Parameter(Mandatory = $true)][string]$StartTimeIso
)

$ErrorActionPreference = 'Continue'
Start-Sleep -Seconds 12

$startTime = [datetime]::Parse($StartTimeIso).ToLocalTime()
$probeKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
$setupApiLog = Join-Path $env:SystemRoot 'INF\setupapi.dev.log'

try {
    $device = Get-CimInstance Win32_PnPEntity | Where-Object {
        $_.PNPDeviceID -eq $InstanceId
    } | Select-Object Name,PNPDeviceID,Service,Status,ConfigManagerErrorCode,Manufacturer
    $device | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $EvidenceDir 'device-after.json') -Encoding UTF8

    (& sc.exe query bc250mcdm 2>&1) |
        Set-Content -LiteralPath (Join-Path $EvidenceDir 'service-bc250mcdm-after.txt') -Encoding UTF8
    (& pnputil.exe /enum-devices /instanceid $InstanceId 2>&1) |
        Set-Content -LiteralPath (Join-Path $EvidenceDir 'pnputil-device-after.txt') -Encoding UTF8

    if (Test-Path -LiteralPath $probeKey) {
        Get-ItemProperty -LiteralPath $probeKey |
            Select-Object * -ExcludeProperty PSPath,PSParentPath,PSChildName,PSDrive,PSProvider |
            ConvertTo-Json -Depth 4 |
            Set-Content -LiteralPath (Join-Path $EvidenceDir 'probe-registry.json') -Encoding UTF8
        (& reg.exe query 'HKLM\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe' /s 2>&1) |
            Set-Content -LiteralPath (Join-Path $EvidenceDir 'probe-registry.txt') -Encoding UTF8
    } else {
        'Probe registry key absent.' |
            Set-Content -LiteralPath (Join-Path $EvidenceDir 'probe-registry.txt') -Encoding ASCII
    }

    $displayLog = 'C:\BC250_DISPLAY_LOG.txt'
    if (Test-Path -LiteralPath $displayLog) {
        Copy-Item -LiteralPath $displayLog -Destination (Join-Path $EvidenceDir 'bc250_display_log.txt') -Force
    }

    if (Test-Path -LiteralPath $setupApiLog) {
        Get-Content -LiteralPath $setupApiLog -Tail 1600 |
            Set-Content -LiteralPath (Join-Path $EvidenceDir 'setupapi-after-tail.txt') -Encoding UTF8
    }
    Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=$startTime} -ErrorAction SilentlyContinue |
        Where-Object { $_.ProviderName -match 'Kernel-PnP|DxgKrnl|Display|UserPnp|DriverFrameworks' } |
        Select-Object TimeCreated,ProviderName,Id,LevelDisplayName,Message |
        Format-List | Out-String -Width 240 |
        Set-Content -LiteralPath (Join-Path $EvidenceDir 'system-events-after.txt') -Encoding UTF8
} catch {
    $_ | Out-String | Set-Content -LiteralPath (Join-Path $EvidenceDir 'collector-error.txt') -Encoding UTF8
} finally {
    "collector_finished=$([datetime]::Now.ToString('o'))" |
        Set-Content -LiteralPath (Join-Path $EvidenceDir 'collector-finished.txt') -Encoding ASCII
}
