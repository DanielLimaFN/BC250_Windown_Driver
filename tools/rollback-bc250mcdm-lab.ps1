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

$markerLines = @(Get-Content -LiteralPath $markerPath -ErrorAction SilentlyContinue)
$targetInf = $null
if ($markerLines.Count -eq 1 -and $markerLines[0] -match '^oem\d+\.inf$') {
    $targetInf = $markerLines[0].ToLowerInvariant()
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
    Start-Sleep -Seconds 2
}

& sc.exe stop bc250mcdm 2>&1 | Out-Null
& sc.exe delete bc250mcdm 2>&1 | Out-Null

if (Test-Path -LiteralPath $markerPath) {
    Remove-Item -LiteralPath $markerPath -Force -ErrorAction SilentlyContinue
}

Write-Host "Rollback de Laboratorio Concluido. Dispositivo restaurado para BasicDisplay." -ForegroundColor Green
exit 0
