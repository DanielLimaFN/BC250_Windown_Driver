@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 175: candidato T3.5 (3.320.1.0), Eliminacao do RingLock no polling sincrono do SDMA + protecao contra I/O de disco sob pushlocks.
echo SEM harness, ATIVIDADE MINIMA (mesmo roteiro da 174).
echo NAO abra NADA: nem Gerenciador de Tarefas, nem navegador, nem outro programa.
echo Assim que o desktop aparecer, espere uns 20 s e clique no menu Iniciar algumas vezes (abre e fecha).
echo SE TRAVAR: anote a hora e aperte o BOTAO DE RESET em 10 a 15 segundos (a memoria decai durante a trava; reset rapido preserva as migalhas).
echo Use o BOTAO de reset. NAO corte a energia nem segure o botao de ligar.
echo Recuperando captura ETW pendente de corrida anterior, se houver...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bc250-hang-etw.ps1" -Recover
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida175-t35.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato, auditoria ou configuracao de boot divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
