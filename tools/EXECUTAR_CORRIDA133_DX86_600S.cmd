@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 133 DX.86: janela de 600 s (10 minutos).
echo DX.86: sem DbgPrint no log do KMD e depurador de kernel DESLIGADO no boot.
echo O resto e' igual a Corrida 132 (DX.85, ETW de travamento ligado).
echo SE TRAVAR: registre a hora e reinicie pelo botao; NAO corte a energia.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida133-dx86.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato, auditoria ou configuracao de boot divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
