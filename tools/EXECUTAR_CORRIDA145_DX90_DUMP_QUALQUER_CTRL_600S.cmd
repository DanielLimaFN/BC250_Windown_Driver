@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 145 DX.90: repeticao da 143 (harness atrasado 150 s), atalho de dump novo.
echo Use o desktop desde o inicio (abra pastas, menu Iniciar) para o Explorer crescer antes do harness.
echo SE TRAVAR: anote a hora. SEGURE QUALQUER Ctrl e aperte Scroll Lock DUAS vezes.
echo Isso gera o dump da tela azul. Espere 3 minutos SEM mexer: a maquina deve reiniciar sozinha.
echo So se nada acontecer em 3 minutos, reinicie pelo BOTAO de reset. NAO corte a energia.
echo Recuperando captura ETW pendente de corrida anterior, se houver...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bc250-hang-etw.ps1" -Recover
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida145-dx90.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato, auditoria ou configuracao de boot divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_D3D11_START_DELAY=150"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
