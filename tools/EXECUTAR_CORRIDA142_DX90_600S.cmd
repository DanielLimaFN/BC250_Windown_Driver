@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 142 DX.90: janela de 600 s COM o harness D3D11, depurador de kernel DESLIGADO.
echo DX.90: batimento do GPU a cada 100 ms no rastro. Use o desktop igual as 139/140.
echo SE TRAVAR: anote a hora. Tente Ctrl direito + Scroll Lock duas vezes.
echo Espere 1 minuto e reinicie pelo BOTAO de reset. NAO corte a energia.
echo Recuperando captura ETW pendente de corrida anterior, se houver...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bc250-hang-etw.ps1" -Recover
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida142-dx90.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato, auditoria ou configuracao de boot divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
