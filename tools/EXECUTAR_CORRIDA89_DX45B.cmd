@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 89 DX.45b (detector em duas fases + guardiao duravel, com DX.43/44) - candidato fixado por hash
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida89-dx45b.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato ou auditoria divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
