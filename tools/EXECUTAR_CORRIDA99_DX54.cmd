@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 99 DX.54 (velocidade: pool de descritores persistente, lote religado com anel de 4 MB, inspecao amostrada; DX.53 mantida), janela de 600 s - candidato fixado por hash
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida99-dx54.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato ou auditoria divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
