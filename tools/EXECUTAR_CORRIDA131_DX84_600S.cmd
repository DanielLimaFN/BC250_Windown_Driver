@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 131 DX.84: validacao de resistencia em janela longa de 600 s (10 minutos).
echo Topologia do monitor alinhada com UEFI GOP (UID0), EDID sintetico ativado (128 bytes) e wait de paging com async event.
echo SE TRAVAR: use Break no depurador remoto, se estiver conectado.
echo Se nao houver resposta do host, registre a hora e reinicie pelo botao; NAO corte a energia.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida131-dx84.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato ou auditoria divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
