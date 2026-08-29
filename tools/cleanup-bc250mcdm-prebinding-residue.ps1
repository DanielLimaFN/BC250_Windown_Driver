[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptOrphanCleanup
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$instancePrefix = 'PCI\VEN_1002&DEV_13FE'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$serviceRegistryPath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm'
$serviceRegistryNative = 'HKLM\SYSTEM\CurrentControlSet\Services\bc250mcdm'

$devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
    Where-Object { $_.PNPDeviceID -like "$instancePrefix*" })
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'BasicDisplay' -or
    $devices[0].ConfigManagerErrorCode -ne 0) {
    throw 'Cleanup recusado: BC-250 nao esta exclusivamente em BasicDisplay Codigo 0.'
}
$service = Get-Service -Name bc250mcdm -ErrorAction SilentlyContinue
if ($null -eq $service -or $service.Status -ne 'Stopped') {
    throw 'Cleanup recusado: servico bc250mcdm nao existe ou nao esta parado.'
}
if (Test-Path -LiteralPath $driverPath) {
    throw 'Cleanup recusado: bc250mcdm.sys ainda existe.'
}
if (-not (Test-Path -LiteralPath $serviceRegistryPath)) {
    throw 'Cleanup recusado: chave exata do servico ausente.'
}
$serviceConfig = Get-ItemProperty -LiteralPath $serviceRegistryPath
if ([string]$serviceConfig.ImagePath -ne '\SystemRoot\System32\drivers\bc250mcdm.sys') {
    throw 'Cleanup recusado: ImagePath do servico diverge do binario experimental exato.'
}

$matchingPackages = @(Get-ChildItem -LiteralPath (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' |
    Where-Object {
        $text = Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue
        $null -ne $text -and $text -match '(?i)PCI\\VEN_1002&DEV_13FE' -and
        $text -match '(?im)^AddService=bc250mcdm,' -and
        $text -match '(?im)^ServiceBinary=%12%\\bc250mcdm\.sys\s*$'
    })
if ($matchingPackages.Count -ne 0) {
    throw "Cleanup recusado: ainda existem pacotes MCDM: $($matchingPackages.Name -join ', ')."
}
if (-not (Test-Path -LiteralPath $markerPath)) {
    throw 'Cleanup recusado: marcador residual ausente; estado nao corresponde ao caso auditado.'
}
$markerBytes = [System.IO.File]::ReadAllBytes($markerPath)
$markerText = [System.Text.Encoding]::ASCII.GetString($markerBytes).Trim()
$markerIsValid = $markerText -match '^oem\d+\.inf$'
if ($markerIsValid -and
    (Test-Path -LiteralPath (Join-Path $env:SystemRoot "INF\$markerText"))) {
    throw 'Cleanup recusado: marcador aponta para um INF ainda existente.'
}

Write-Host 'BC250 PREBINDING ORPHAN CLEANUP PREFLIGHT: PASS'
Write-Host "  device: $($devices[0].Service) / $($devices[0].ConfigManagerErrorCode)"
Write-Host "  service: $($service.Status); SYS absent"
Write-Host "  matching MCDM packages: $($matchingPackages.Count)"
Write-Host "  marker bytes: $($markerBytes.Length); valid OEM name: $markerIsValid"
if (-not $Execute) {
    if ($IAcceptOrphanCleanup) {
        throw '-IAcceptOrphanCleanup nao e valido sem -Execute.'
    }
    Write-Host 'DRY-RUN: zero escrita.'
    exit 0
}
if (-not $IAcceptOrphanCleanup) {
    throw 'Cleanup recusado: falta -IAcceptOrphanCleanup.'
}
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Cleanup exige PowerShell elevado.'
}

# Second gate immediately before the two exact mutations.
$secondDevice = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
    Where-Object { $_.PNPDeviceID -like "$instancePrefix*" })
$secondService = Get-Service -Name bc250mcdm -ErrorAction SilentlyContinue
if ($secondDevice.Count -ne 1 -or $secondDevice[0].Service -ne 'BasicDisplay' -or
    $secondDevice[0].ConfigManagerErrorCode -ne 0 -or $null -eq $secondService -or
    $secondService.Status -ne 'Stopped' -or (Test-Path -LiteralPath $driverPath) -or
    -not (Test-Path -LiteralPath $markerPath)) {
    throw 'Cleanup abortado: estado mudou depois do preflight.'
}

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-mcdm-orphan-cleanup"
New-Item -ItemType Directory -Path $evidenceDir | Out-Null
$registryBackup = Join-Path $evidenceDir 'bc250mcdm-service.reg'
& reg.exe export $serviceRegistryNative $registryBackup /y | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $registryBackup)) {
    throw 'Cleanup abortado: export recuperavel da chave do servico falhou.'
}
$quarantineDir = 'C:\BC250_DRIVER_QUARANTINE'
if (-not (Test-Path -LiteralPath $quarantineDir)) {
    New-Item -ItemType Directory -Path $quarantineDir | Out-Null
}
$quarantinedMarker = Join-Path $quarantineDir "BC250_TEST_OEMINF-$stamp.txt.orphaned"
Move-Item -LiteralPath $markerPath -Destination $quarantinedMarker
if (Test-Path -LiteralPath $markerPath) {
    throw 'Cleanup incompleto: marcador nao foi movido para quarentena.'
}

$deleteOutput = (& sc.exe delete bc250mcdm 2>&1) -join "`n"
$deleteExit = $LASTEXITCODE
$deleteOutput | Set-Content -LiteralPath (Join-Path $evidenceDir 'sc-delete.txt') -Encoding UTF8
if ($deleteExit -ne 0) {
    Move-Item -LiteralPath $quarantinedMarker -Destination $markerPath
    throw "Cleanup falhou removendo servico; marcador restaurado (exit=$deleteExit)."
}
Start-Sleep -Seconds 2
if (Get-Service -Name bc250mcdm -ErrorAction SilentlyContinue) {
    throw 'Cleanup parcial: servico ficou marcado para remocao; reinicie antes do stage.'
}

$auditScript = Join-Path $root 'tools\audit-bc250mcdm-prebinding.ps1'
$auditOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $auditScript 2>&1) -join "`n"
$auditExit = $LASTEXITCODE
$auditOutput | Set-Content -LiteralPath (Join-Path $evidenceDir 'prebinding-after.txt') -Encoding UTF8
if ($auditExit -ne 0) {
    throw 'Cleanup executado, mas o prebinding final bloqueou; consulte a evidencia.'
}
Write-Host 'BC250 PREBINDING ORPHAN CLEANUP: PASS'
Write-Host "  registry backup: $registryBackup"
Write-Host "  marker quarantine: $quarantinedMarker"
Write-Host "  evidence: $evidenceDir"
exit 0
