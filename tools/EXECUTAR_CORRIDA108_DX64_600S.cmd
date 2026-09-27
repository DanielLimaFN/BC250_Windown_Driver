@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 108 DX.64 (mesmo pacote da 107), janela de 600 s: estabilidade e travamento duro
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida107-dx64.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato ou auditoria divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
