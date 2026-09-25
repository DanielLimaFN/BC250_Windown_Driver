@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 107 DX.64 (copia de imagem com layout rastreado), janela de 180 s
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida107-dx64.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato ou auditoria divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=180"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
