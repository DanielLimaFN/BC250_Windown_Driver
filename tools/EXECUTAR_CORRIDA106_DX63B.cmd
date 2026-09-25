@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 106 DX.63b (geometria das alocacoes padrao para o Blt), janela de 180 s
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida106-dx63b.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato ou auditoria divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=180"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
