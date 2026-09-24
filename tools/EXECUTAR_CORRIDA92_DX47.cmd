@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 92 DX.47 (conclusao da preempcao), janela de 300 s - candidato fixado por hash
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida92-dx47.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato ou auditoria divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=300"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
