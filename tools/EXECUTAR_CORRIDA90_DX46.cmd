@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 90 DX.46 (anel grafico no tamanho inteiro, com DX.43/44/45b) - candidato fixado por hash
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida90-dx46.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato ou auditoria divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
