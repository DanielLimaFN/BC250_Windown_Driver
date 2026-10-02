@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 187: candidato T4.6+I.1 (3.332.1.0), recopia da superficie do menu + motivo das falhas da RADV no log.
echo SEM harness, ATIVIDADE MINIMA (mesmo roteiro da 185/186).
echo NAO abra NADA alem do menu Iniciar para testar.
echo Assim que o desktop aparecer, espere uns 20 s e clique no menu Iniciar (observe se os icones e textos aparecem no lugar dos blocos cinza).
echo SE TRAVAR: anote a hora e aperte o BOTAO DE RESET em 10 a 15 segundos (a memoria decai durante a trava; reset rapido preserva as migalhas).
echo Use o BOTAO de reset. NAO corte a energia nem segure o botao de ligar.
echo DEPOIS DE UMA TRAVA: copie C:\Windows\Temp\BC250_UMD_LOG_*.txt com PowerShell elevado (a coleta FINAL nao roda quando a maquina trava).
echo Recuperando captura ETW pendente de corrida anterior, se houver...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bc250-hang-etw.ps1" -Recover
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida187-t46i1.ps1"
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
