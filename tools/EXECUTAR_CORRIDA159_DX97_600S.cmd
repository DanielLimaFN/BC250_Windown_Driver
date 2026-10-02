@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 159 DX.97: validacao de SRC/DEST so nas pontas (-99%% de leitura de tabelas pela BAR) (+DX.96-HDP/FPS.3b/DX.95/DX.94); SEM depurador; harness aos 30 s.
echo Repita o roteiro da 148: desktop, Gerenciador de Tarefas e depois o navegador.
echo SE TRAVAR: anote a hora e aperte o BOTAO DE RESET em 10 a 15 segundos (a memoria decai durante a trava; reset rapido preserva as migalhas).
echo Use o BOTAO de reset. NAO corte a energia nem segure o botao de ligar.
echo Recuperando captura ETW pendente de corrida anterior, se houver...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bc250-hang-etw.ps1" -Recover
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida159-dx97.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato, auditoria ou configuracao de boot divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_D3D11_START_DELAY=30"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
