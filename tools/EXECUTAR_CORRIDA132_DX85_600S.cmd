@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 132 DX.85: janela de 600 s (10 minutos).
echo DX.85: log da DX.83 fora da thread de paginacao, espera de fence fail-closed,
echo topologia de volta a ChildUid=1/EDID 0 (Corridas 118-128) e GPUVA de volta a 1024.
echo SE TRAVAR: tente Ctrl direito + Scroll Lock duas vezes (dump manual 0xE2) antes do reset.
echo Se nao houver resposta, registre a hora e reinicie pelo botao; NAO corte a energia.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida132-dx85.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato ou auditoria divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
