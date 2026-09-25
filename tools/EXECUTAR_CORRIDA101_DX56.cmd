@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 101 DX.56 (rastro duravel de paginacao; DX.55 mantida), janela de 600 s
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida101-dx56.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato ou auditoria divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
