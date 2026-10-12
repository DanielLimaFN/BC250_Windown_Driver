<#
.SYNOPSIS
    Monitor de Telemetria do AMD BC-250 (Cadencia de 2 segundos)
.DESCRIPTION
    Exibe em tempo real no console a cada 2 segundos:
    - Clock da CPU (media aritmetica de todos os nucleos/threads)
    - Uso de memoria RAM (GB e %)
    - GPU GFX Clock (MHz via SMU)
    - Tensao VDDGFX (mV)
    - Uso de memoria de video VRAM (MB e %)
    - Temperatura da GPU (Junction e Edge)
    - Temperatura das memorias GDDR6 (SMN / UMC)
.PARAMETER LineMode
    Exibe uma linha de log continuo por ciclo de 2 segundos (ideal para pipes ou logs).
#>
[CmdletBinding()]
param(
    [switch]$LineMode
)

$ErrorActionPreference = "Continue"
$rootDir = Split-Path -Parent $PSScriptRoot
$exePath = Join-Path $rootDir "tools\monitor\bc250_monitor.exe"

if (-not (Test-Path $exePath)) {
    Write-Host "[*] Compilando bc250_monitor.exe..." -ForegroundColor Yellow
    $buildBat = Join-Path $rootDir "tools\monitor\build.bat"
    cmd.exe /c "cd /d `"$rootDir\tools\monitor`" && call build.bat"
}

if (Test-Path $exePath) {
    $argsList = @()
    if ($LineMode) {
        $argsList += "--line"
    }
    Write-Host "[*] Executando $exePath (Cadencia: 2s)..." -ForegroundColor Cyan
    & $exePath $argsList
} else {
    Write-Error "[-] Nao foi possivel localizar nem compilar $exePath."
}
