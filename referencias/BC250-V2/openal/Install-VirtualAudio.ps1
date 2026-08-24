<#
.SYNOPSIS
    Assistente de Dispositivo de Áudio Virtual para AMD BC-250 (VB-Audio CABLE)
.DESCRIPTION
    Fornece um endpoint de áudio de reprodução virtual para a placa AMD BC-250 (Cyan Skillfish).
    Isso permite que jogos (Minecraft, emuladores, etc.) enviem som real para transmissões via Discord,
    Moonlight/Sunshine, OBS Studio ou fones Bluetooth.
#>

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "   AMD BC-250: INSTALADOR DE DISPOSITIVO DE AUDIO VIRTUAL" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "`n[ERRO] Execute este script como Administrador!" -ForegroundColor Red
    Pause
    return
}

$workDir = Join-Path $env:TEMP "VBAudio_BC250"
if (-not (Test-Path $workDir)) { New-Item -Path $workDir -ItemType Directory -Force | Out-Null }

$zipPath = Join-Path $workDir "VBCABLE_Driver_Pack43.zip"
$downloadUrl = "https://download.vb-audio.com/Download_CAB/VBCABLE_Driver_Pack43.zip"

Write-Host "`n[1/3] Baixando driver de cabo de audio virtual..." -ForegroundColor Yellow
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 -bor [Net.SecurityProtocolType]::Tls13
    Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath -UseBasicParsing
    Write-Host "  • Download concluido com sucesso! [OK]" -ForegroundColor Green
} catch {
    Write-Host "  • Falha no download automatico. Voce pode baixar manualmente em: https://vb-audio.com/Cable/" -ForegroundColor DarkYellow
    Write-Host "  • Erro: $($_.Exception.Message)" -ForegroundColor Red
    return
}

Write-Host "`n[2/3] Extraindo instalador..." -ForegroundColor Yellow
Expand-Archive -Path $zipPath -DestinationPath $workDir -Force

$setupExe = Join-Path $workDir "VBCABLE_Setup_x64.exe"
if (-not (Test-Path $setupExe)) {
    $setupExe = Join-Path $workDir "VBCABLE_Setup.exe"
}

if (Test-Path $setupExe) {
    Write-Host "`n[3/3] Executando instalador do driver de audio virtual..." -ForegroundColor Yellow
    Write-Host "  • Siga as instrucoes na tela e clique em 'Install Driver'." -ForegroundColor Cyan
    Start-Process -FilePath $setupExe -Wait
    Write-Host "  • Concluido! Reinicie o computador se solicitado." -ForegroundColor Green
} else {
    Write-Host "  • Nao foi possivel encontrar o executavel de instalacao." -ForegroundColor Red
}

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "  Apos a instalacao do cabo virtual, a BC-250 tera som" -ForegroundColor Green
Write-Host "  ativo para jogos e streaming (Moonlight/Sunshine/Discord)." -ForegroundColor Green
Write-Host "========================================================`n" -ForegroundColor Cyan
