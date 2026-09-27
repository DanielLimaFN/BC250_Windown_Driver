# _bind_runner.ps1
$ErrorActionPreference = "Continue"
Start-Transcript -Path "C:\Users\Daniel\Desktop\NamixWDDM\tools\_elevated_bind_latest.log" -Force

Write-Host "=== INICIANDO FLUXO DE BINDING WDDM (BC-250 CANDIDATO P5) ===" -ForegroundColor Cyan

# 1. Verificar estado atual da BC-250 e residuos
$devices = @(Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -like "PCI\VEN_1002&DEV_13FE*" })
$hasResidue = (Test-Path "C:\BC250_TEST_OEMINF.txt") -or (Test-Path "$env:SystemRoot\System32\drivers\bc250mcdm.sys")
$needsRecovery = ($devices.Count -eq 0 -or $devices[0].Service -ne 'BasicDisplay' -or $devices[0].ConfigManagerErrorCode -ne 0 -or $hasResidue)

if ($needsRecovery) {
    Write-Host "`nDispositivo em estado nao-limpo ou residuo detectado: Service=$($devices[0].Service), Code=$($devices[0].ConfigManagerErrorCode), Residuo=$hasResidue" -ForegroundColor Yellow
    Write-Host "Executando C:\BC250_RECOVERY.cmd para restaurar BasicDisplay Codigo 0 limpo..." -ForegroundColor Cyan
    & cmd.exe /c "C:\BC250_RECOVERY.cmd"
    Start-Sleep -Seconds 3
}

# 2. Verificar se o BasicDisplay Codigo 0 foi alcancado
$devices = @(Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -like "PCI\VEN_1002&DEV_13FE*" })
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'BasicDisplay' -or $devices[0].ConfigManagerErrorCode -ne 0) {
    Write-Host "`nERRO FATAL: BC-250 nao retornou para BasicDisplay Codigo 0." -ForegroundColor Red
} else {
    Write-Host "`nPreflight OK: BC-250 em BasicDisplay Codigo 0." -ForegroundColor Green

    # 2.5 Se o driver de diagnostico bc250dbg ainda estiver ativo do ensaio de lab, descarregar
    if (Get-Service bc250dbg -ErrorAction SilentlyContinue) {
        Write-Host "`nDescarregando driver de diagnostico bc250dbg..." -ForegroundColor Cyan
        & cmd.exe /c "C:\Users\Daniel\Desktop\NamixWDDM\dbg\uninstall.bat"
        Start-Sleep -Seconds 1
    }

    # 3. Limpar residuo de marcador antigo, se existir
    if (Test-Path "C:\BC250_TEST_OEMINF.txt") {
        Remove-Item "C:\BC250_TEST_OEMINF.txt" -Force -ErrorAction SilentlyContinue
    }

    # 4. Estagiar pacote de producao (output_active)
    Write-Host "`n[1/2] Estagiando pacote WDDM ativo (output_active)..." -ForegroundColor Cyan
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Daniel\Desktop\NamixWDDM\tools\stage-bc250mcdm-package.ps1" -Execute

    if ($LASTEXITCODE -eq 0) {
        Write-Host "`n[2/2] Vinculando driver WDDM ativo com Watchdog fail-closed..." -ForegroundColor Cyan
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Daniel\Desktop\NamixWDDM\tools\bind-bc250mcdm-failclosed.ps1" -Execute -IAcceptBlackScreenRisk
    } else {
        Write-Host "`nERRO NO STAGE: Codigo $LASTEXITCODE" -ForegroundColor Red
    }
}

Write-Host "`n============================================================" -ForegroundColor Cyan
Write-Host "EXECUCAO CONCLUIDA. O terminal permanecera aberto." -ForegroundColor Cyan
Write-Host "Pressione ENTER para encerrar esta janela..." -ForegroundColor Yellow
Write-Host "============================================================" -ForegroundColor Cyan

Stop-Transcript
Read-Host

