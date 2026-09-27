<#
.SYNOPSIS
    Ponte de Telemetria e Watchdog para o ESP32 (Out-of-Band BMC).
    Envia batimento cardíaco a cada 2 segundos via Wi-Fi (HTTP) ou Serial (COM).
    Permite acionar rollback imediato do driver ao receber comando do ESP32.

.PARAMETER Esp32Host
    Endereço IP ou hostname mDNS do ESP32 (padrão: "bc250.local").

.PARAMETER ComPort
    Nome da porta serial opcional (ex: "COM3") caso o driver CP2102 esteja instalado.

.PARAMETER IntervalSeconds
    Intervalo do ping em segundos (padrão: 2).
#>
[CmdletBinding()]
param(
    [string]$Esp32Host = "bc250.local",
    [string]$ComPort = "",
    [int]$IntervalSeconds = 2
)

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$rollbackScript = Join-Path $PSScriptRoot 'rollback-bc250mcdm-lab.ps1'
$fuseScript = Join-Path $PSScriptRoot 'inspect-boot-fuse.ps1'
$fuseFile = 'C:\BC250_BOOT_FUSE.dat'
$kmdLogFile = 'C:\BC250_DISPLAY_LOG.txt'
$instancePrefix = 'PCI\VEN_1002&DEV_13FE'

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host "  BC-250 ESP32 TELEMETRY & HARDWARE WATCHDOG COMPANION" -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host "Target Host Wi-Fi: http://$Esp32Host" -ForegroundColor Gray
if ($ComPort) { Write-Host "Target Serial Port: $ComPort" -ForegroundColor Gray }
Write-Host "Ping Interval: $IntervalSeconds s" -ForegroundColor Gray
Write-Host "Pressione Ctrl+C para encerrar o monitor.`n" -ForegroundColor Yellow

function Get-DriverStatus {
    try {
        $dev = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
            Where-Object { $_.PNPDeviceID -like "$instancePrefix*" } | Select-Object -First 1
        if ($null -eq $dev) { return "Nao Detectado" }
        $svc = if ($dev.Service) { $dev.Service } else { "Sem Servico" }
        $err = $dev.ConfigManagerErrorCode
        return "$svc (Code $err)"
    } catch {
        return "Erro Consulta"
    }
}

function Get-BootFuseStatus {
    if (-not (Test-Path -LiteralPath $fuseFile)) { return "AUSENTE" }
    try {
        $bytes = [System.IO.File]::ReadAllBytes($fuseFile)
        if ($bytes.Length -ge 1) {
            $state = $bytes[0]
            $boots = if ($bytes.Length -ge 2) { $bytes[1] } else { 0 }
            switch ($state) {
                0 { return "CLEAN (0)" }
                1 { return "ARMED (1)" }
                2 { return "TRIPPED (2)" }
                default { return "State $state" }
            }
        }
        return "DESCONHECIDO"
    } catch {
        return "ERRO LEITURA"
    }
}

function Get-WatchdogInfo {
    $wdProc = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -like '*watch-bc250mcdm-lab.ps1*' -and $_.ProcessId -ne $PID } |
        Select-Object -First 1

    if ($null -ne $wdProc) {
        $timeout = 180
        if ($wdProc.CommandLine -match '-TimeoutSeconds\s+(\d+)') {
            $timeout = [int]$matches[1]
        }
        $creationTime = $wdProc.CreationDate
        $elapsed = ((Get-Date) - $creationTime).TotalSeconds
        $rem = [Math]::Max(0, [int]($timeout - $elapsed))
        return @{
            State = "ARMED"
            Remaining = $rem
        }
    }

    # Procura estado recente em docs/evidence
    $latestState = Get-ChildItem -Path (Join-Path $root 'docs\evidence') -Filter 'watchdog-state.json' -Recurse -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($null -ne $latestState) {
        try {
            $json = Get-Content -LiteralPath $latestState.FullName -Raw | ConvertFrom-Json
            return @{
                State = $json.state
                Remaining = 0
            }
        } catch {}
    }

    return @{
        State = "IDLE"
        Remaining = 0
    }
}

function Get-LastKmdLogLine {
    if (-not (Test-Path -LiteralPath $kmdLogFile)) { return "Log KMD ausente" }
    try {
        $stream = [System.IO.File]::Open($kmdLogFile,
            [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite)
        try {
            $len = $stream.Length
            if ($len -eq 0) { return "Log vazio" }
            $seekPos = [Math]::Max(0L, $len - 1024)
            [void]$stream.Seek($seekPos, [System.IO.SeekOrigin]::Begin)
            $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
            $tail = $reader.ReadToEnd()
            $lines = $tail.Split("`n") | Where-Object { $_.Trim().Length -gt 0 }
            if ($lines.Count -gt 0) {
                return $lines[-1].Trim()
            }
            return "Sem linhas novas"
        } finally {
            $stream.Dispose()
        }
    } catch {
        return "Erro ao ler log KMD"
    }
}

# Inicialização Serial opcional
$serialPortObj = $null
if ($ComPort -ne "") {
    try {
        $serialPortObj = New-Object System.IO.Ports.SerialPort $ComPort, 115200, None, 8, One
        $serialPortObj.Open()
        $serialPortObj.ReadTimeout = 200
        Write-Host "Porta serial $ComPort aberta a 115200 baud." -ForegroundColor Green
    } catch {
        Write-Warning "Nao foi possivel abrir a porta serial ${ComPort}: $($_.Exception.Message)"
        $serialPortObj = $null
    }
}

# Laço Principal de Heartbeat
while ($true) {
    $now = Get-Date -Format "HH:mm:ss"
    $drv = Get-DriverStatus
    $fuse = Get-BootFuseStatus
    $wd = Get-WatchdogInfo
    $logLine = Get-LastKmdLogLine
    if ($logLine.Length -gt 80) { $logLine = $logLine.Substring($logLine.Length - 80) }

    $state = $wd.State
    $rem = $wd.Remaining

    Write-Host "[$now] PING -> ESP32 | State: $state | Rem: ${rem}s | Drv: $drv | Fuse: $fuse" -ForegroundColor DarkGray

    # 1. Envia via Wi-Fi HTTP
    $cmdToExec = ""
    try {
        $uri = "http://${Esp32Host}/api/heartbeat?state=$state&rem=$rem&drv=$([Uri]::EscapeDataString($drv))&fuse=$([Uri]::EscapeDataString($fuse))&log=$([Uri]::EscapeDataString($logLine))"
        $resp = Invoke-RestMethod -Uri $uri -Method Get -TimeoutSec 1 -ErrorAction Stop
        if ($resp -match 'CMD:(.+)') {
            $cmdToExec = $matches[1].Trim()
        }
    } catch {
        # Se bc250.local falhar por DNS local, tenta manter vivo sem quebrar o loop
    }

    # 2. Envia via Serial se estiver ativo
    if ($null -ne $serialPortObj -and $serialPortObj.IsOpen) {
        try {
            $serialMsg = "PING|state=$state|rem=$rem|drv=$drv|fuse=$fuse|log=$logLine`n"
            $serialPortObj.Write($serialMsg)
            
            # Verifica se há comandos chegando da serial
            while ($serialPortObj.BytesToRead -gt 0) {
                $line = $serialPortObj.ReadLine().Trim()
                if ($line -match 'CMD:(.+)') {
                    $cmdToExec = $matches[1].Trim()
                }
            }
        } catch {}
    }

    # 3. Execução de comandos recebidos do ESP32
    if ($cmdToExec -eq "FORCE_ROLLBACK") {
        Write-Host "`n[ESP32] >>> COMANDO RECEBIDO DO DASHBOARD: FORCAR ROLLBACK IMEDIATO! <<<" -ForegroundColor Yellow
        Write-Host "Disparando $rollbackScript..." -ForegroundColor Cyan
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $rollbackScript -Execute -IAcceptDisplayInterruptionRisk
        Write-Host "[ESP32] Rollback concluido com codigo de saida $LASTEXITCODE." -ForegroundColor Green
    } elseif ($cmdToExec -eq "RESET_FUSE") {
        Write-Host "`n[ESP32] >>> COMANDO RECEBIDO: RESETAR FUSIVEL DE BOOT! <<<" -ForegroundColor Cyan
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $fuseScript -Reset
        Write-Host "[ESP32] Fusivel resetado." -ForegroundColor Green
    }

    Start-Sleep -Seconds $IntervalSeconds
}
