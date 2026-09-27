@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 105 DX.61-63 (Blt para o destino + seguimento de alocacao + guardiao), janela de 180 s
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida105-dx61-63.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato ou auditoria divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=180"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
