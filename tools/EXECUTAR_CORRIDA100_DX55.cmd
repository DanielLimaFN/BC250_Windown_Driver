@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 100 DX.55 (bisseccao: lote de draws DESLIGADO; pool de descritores persistente e inspecao amostrada mantidos; DX.53 mantida), janela de 600 s - candidato fixado por hash
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida100-dx55.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato ou auditoria divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
