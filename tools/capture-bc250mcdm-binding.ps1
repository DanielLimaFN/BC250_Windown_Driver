[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EvidenceDir,
    [Parameter(Mandatory = $true)][string]$InstanceId,
    [Parameter(Mandatory = $true)][string]$StartTimeIso
)

$ErrorActionPreference = 'Continue'
Start-Sleep -Seconds 30

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

    $umdLogCollected = $false
    $umdAggregate = Join-Path $EvidenceDir 'bc250_umd_log.txt'
    $umdSources = @('C:\Windows\Temp')
    if ($env:TEMP -and (Test-Path -LiteralPath $env:TEMP)) { $umdSources += $env:TEMP }
    $userTemps = @(Get-ChildItem -Path 'C:\Users\*\AppData\Local\Temp' -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName)
    foreach ($ut in $userTemps) { if ($umdSources -notcontains $ut) { $umdSources += $ut } }
    $umdLogs = @()
    foreach ($src in $umdSources) {
        $umdLogs += @(Get-ChildItem -LiteralPath $src -Filter 'BC250_UMD_LOG_*.txt' -File -ErrorAction SilentlyContinue)
    }
    $umdLogs = @($umdLogs | Sort-Object Name -Unique)
    foreach ($umdLog in $umdLogs) {
        Copy-Item -LiteralPath $umdLog.FullName `
            -Destination (Join-Path $EvidenceDir $umdLog.Name.ToLowerInvariant()) -Force
        ("===== {0} =====" -f $umdLog.Name),
            (Get-Content -LiteralPath $umdLog.FullName -ErrorAction SilentlyContinue) |
            Add-Content -LiteralPath $umdAggregate -Encoding UTF8
        $umdLogCollected = $true
    }
    if (-not $umdLogCollected) {
        'bc250umd.dll nunca escreveu: a DLL nao foi carregada por nenhum processo.' |
            Set-Content -LiteralPath (Join-Path $EvidenceDir 'bc250_umd_log.txt') -Encoding UTF8
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

    Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=$startTime} -ErrorAction SilentlyContinue |
        Where-Object {
            $_.ProviderName -match 'Application Error|Windows Error Reporting' -or
            $_.Message -match 'dwm\.exe|0xe0464645|bc250umd'
        } |
        Select-Object TimeCreated,ProviderName,Id,LevelDisplayName,Message |
        Format-List | Out-String -Width 240 |
        Set-Content -LiteralPath (Join-Path $EvidenceDir 'application-events-after.txt') -Encoding UTF8
} catch {
    $_ | Out-String | Set-Content -LiteralPath (Join-Path $EvidenceDir 'collector-error.txt') -Encoding UTF8
} finally {
    $restoreDwmCapture = Join-Path (Split-Path -Parent $PSScriptRoot) `
        'tools\restore-dwm-crash-capture.ps1'
    if (Test-Path -LiteralPath $restoreDwmCapture) {
        (& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $restoreDwmCapture `
            -EvidenceDir $EvidenceDir -Execute 2>&1) |
            Set-Content -LiteralPath (Join-Path $EvidenceDir 'dwm-localdumps-restore.txt') -Encoding UTF8
    }
    "collector_finished=$([datetime]::Now.ToString('o'))" |
        Set-Content -LiteralPath (Join-Path $EvidenceDir 'collector-finished.txt') -Encoding ASCII
}
