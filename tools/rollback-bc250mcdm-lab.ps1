<#
.SYNOPSIS
    Rollback online para desinstalar o driver de LABORATORIO do BC-250 e restaurar o BasicDisplay.
#>
[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptDisplayInterruptionRisk
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$cleanBootMarkerPath = 'C:\BC250_REQUIRE_CLEAN_BOOT.json'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$instancePrefix = 'PCI\VEN_1002&DEV_13FE'

if (-not (Test-Path -LiteralPath 'C:\BC250_RECOVERY.cmd')) {
    throw 'C:\BC250_RECOVERY.cmd ausente.'
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Rollback exige PowerShell elevado (Administrador).'
}

Write-Host "Iniciando Rollback de Laboratorio..." -ForegroundColor Cyan

$devicesBefore = @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue | Where-Object {
    $_.PNPDeviceID -like "$instancePrefix*"
})
$requiresCleanBoot = -not ($devicesBefore.Count -eq 1 -and
    $devicesBefore[0].Service -eq 'BasicDisplay' -and
    $devicesBefore[0].ConfigManagerErrorCode -eq 0)

$markerLines = @(Get-Content -LiteralPath $markerPath -ErrorAction SilentlyContinue)
$targetInf = $null
if ($markerLines.Count -eq 1 -and $markerLines[0] -match '^oem\d+\.inf$') {
    $targetInf = $markerLines[0].ToLowerInvariant()
}

foreach ($dev in $devicesBefore) {
    if ($dev.Service -ne 'BasicDisplay') {
        Write-Host "Desabilitando dispositivo antes do rollback: $($dev.PNPDeviceID)"
        & pnputil.exe /disable-device $dev.PNPDeviceID /force | Out-Null
    }
}

if ($null -ne $targetInf) {
    Write-Host "Removendo pacote OEM: $targetInf"
    & pnputil.exe /delete-driver $targetInf /uninstall /force
} else {
    Write-Host "Marcador OEM nao encontrado, varrendo pacotes de terceiros..."
    $rawDrivers = pnputil /enum-drivers
    $currentOem = ""
    $isMatch = $false
    foreach ($line in $rawDrivers) {
        if ($line -match "Nome Publicado:\s+(oem\d+\.inf)") {
            if ($currentOem -ne "" -and $isMatch) {
                Write-Host "Removendo $currentOem..."
                & pnputil /delete-driver $currentOem /uninstall /force | Out-Null
            }
            $currentOem = $matches[1]
            $isMatch = $false
        }
        if ($line -match "(bc250|BC250)") { $isMatch = $true }
    }
    if ($currentOem -ne "" -and $isMatch) {
        Write-Host "Removendo $currentOem..."
        & pnputil /delete-driver $currentOem /uninstall /force | Out-Null
    }
}
foreach ($dev in $devicesBefore) {
    & pnputil.exe /enable-device $dev.PNPDeviceID | Out-Null
}

& pnputil.exe /scan-devices | Out-Null

$deadline = (Get-Date).AddSeconds(15)
$restored = $false
do {
    Start-Sleep -Milliseconds 500
    $devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue | Where-Object {
        $_.PNPDeviceID -like "$instancePrefix*"
    })
    $restored = ($devices.Count -eq 1 -and $devices[0].Service -eq 'BasicDisplay' -and
        $devices[0].ConfigManagerErrorCode -eq 0)
} until ($restored -or (Get-Date) -ge $deadline)

if (-not $restored -and $devices.Count -gt 0) {
    Write-Host "Reiniciando dispositivo PnP..."
    & pnputil.exe /restart-device $devices[0].PNPDeviceID | Out-Null
    $deadline = (Get-Date).AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 500
        $devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue | Where-Object {
            $_.PNPDeviceID -like "$instancePrefix*"
        })
        $restored = ($devices.Count -eq 1 -and $devices[0].Service -eq 'BasicDisplay' -and
            $devices[0].ConfigManagerErrorCode -eq 0)
    } until ($restored -or (Get-Date) -ge $deadline)
}

if (-not $restored) {
    $observed = if ($devices.Count -eq 1) {
        "service=$($devices[0].Service), code=$($devices[0].ConfigManagerErrorCode)"
    } else {
        "device_count=$($devices.Count)"
    }
    throw "Rollback incompleto: BasicDisplay Codigo 0 nao foi confirmado ($observed). Use C:\BC250_RECOVERY.cmd."
}

& sc.exe stop bc250mcdm 2>&1 | Out-Null
& sc.exe delete bc250mcdm 2>&1 | Out-Null

if (Test-Path -LiteralPath $markerPath) {
    Remove-Item -LiteralPath $markerPath -Force -ErrorAction SilentlyContinue
}

if (Test-Path -LiteralPath 'HKLM:\SOFTWARE\BC250\Umd') {
    Remove-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\BC250\Umd' -Name 'D3D11LabPromotion' -ErrorAction SilentlyContinue
}

if ($requiresCleanBoot) {
    [ordered]@{
        state = 'CLEAN_BOOT_REQUIRED'
        rollback_boot_ticks = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.
            ToUniversalTime().Ticks
        rollback_completed_at = (Get-Date).ToString('o')
        restored_service = 'BasicDisplay'
        restored_code = 0
        hardware_validated = $false
    } | ConvertTo-Json | Set-Content -LiteralPath $cleanBootMarkerPath -Encoding UTF8
}

Write-Host "Rollback de Laboratorio Concluido e verificado: BasicDisplay Codigo 0." -ForegroundColor Green
if ($requiresCleanBoot) {
    Write-Host "BOOT FRIO obrigatorio antes do proximo binding: $cleanBootMarkerPath" -ForegroundColor Yellow
} else {
    Write-Host 'Nenhum binding ocorreu; novo boot nao e necessario.' -ForegroundColor Green
}
exit 0
