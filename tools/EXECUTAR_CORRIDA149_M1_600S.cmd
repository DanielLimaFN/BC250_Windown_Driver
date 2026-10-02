@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 149 M1: recursos so-GPU na memoria do sistema; SEM depurador; harness atrasado 150 s.
echo Repita o roteiro da 148: desktop, Gerenciador de Tarefas e depois o navegador.
echo SE TRAVAR: anote a hora. QUALQUER Ctrl + Scroll Lock DUAS vezes; espere 3 minutos.
echo Se nada acontecer, reinicie pelo BOTAO de reset. NAO corte a energia.
echo Recuperando captura ETW pendente de corrida anterior, se houver...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bc250-hang-etw.ps1" -Recover
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida149-m1.ps1"
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
