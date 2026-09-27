@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 114 DX.70 (amostragem UMD), janela de 180 s. SE TRAVAR: reinicie a quente (reset), NAO corte a energia
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida114-dx70.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato ou auditoria divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=180"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
