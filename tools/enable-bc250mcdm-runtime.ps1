[CmdletBinding()]
param(
    [switch]$Disable
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$regPath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters'
$hardwareId = 'PCI\VEN_1002&DEV_13FE'

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em PowerShell como Administrador.'
}

if (-not (Test-Path -LiteralPath $regPath)) {
    New-Item -ItemType Directory -Path $regPath -Force | Out-Null
}

if ($Disable) {
    Set-ItemProperty -LiteralPath $regPath -Name 'OperationalMode' -Value 0 -Type DWord
    Write-Host "Modo Operacional DESATIVADO (Modo Fail-Closed de Diagnostico restaurado)."
} else {
    Set-ItemProperty -LiteralPath $regPath -Name 'OperationalMode' -Value 1 -Type DWord
    Write-Host "Modo Operacional ATIVADO (OperationalMode = 1)."
}

# Reinicia o dispositivo PnP para aplicar a inicializacao
Write-Host "Reiniciando dispositivo BC-250 no PnP..."
$dev = Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -like "$hardwareId*" }
if ($dev) {
    & pnputil.exe /restart-device $dev.PNPDeviceID
}

Start-Sleep -Seconds 5

$device = Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like "$hardwareId*"
}

Write-Host "========================== STATUS ATUAL DO DISPOSITIVO =========================="
Write-Host "Nome:           $($device.Name)"
Write-Host "Servico:        $($device.Service)"
Write-Host "Status:         $($device.Status)"
Write-Host "Codigo PnP:     $($device.ConfigManagerErrorCode)"
if ($device.ConfigManagerErrorCode -eq 0) {
    Write-Host "VEREDITO: DISPOSITIVO OPERACIONAL (CODIGO 0 - FUNCIONANDO CORRETAMENTE)!" -ForegroundColor Green
} else {
    Write-Host "VEREDITO: DISPOSITIVO EM MODO FAIL-CLOSED (CODIGO $($device.ConfigManagerErrorCode))" -ForegroundColor Yellow
}
Write-Host "================================================================================="
