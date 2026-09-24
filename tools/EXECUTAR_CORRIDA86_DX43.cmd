@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 86 DX.43 (IB de aquecimento na troca de VMID) - candidato fixado por hash
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida86-dx43.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato ou auditoria divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
