@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 134 DX.87: janela de 600 s (10 minutos), depurador de kernel DESLIGADO.
echo DX.87: migalhas MMIO espelhadas em RAM e gravadas em disco pelo escritor do rastro.
echo SE TRAVAR: anote a hora. Tente Ctrl direito + Scroll Lock duas vezes.
echo Espere 1 minuto e reinicie pelo BOTAO de reset. NAO corte a energia.
echo Recuperando captura ETW pendente de corrida anterior, se houver...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bc250-hang-etw.ps1" -Recover
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida134-dx87.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato, auditoria ou configuracao de boot divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
