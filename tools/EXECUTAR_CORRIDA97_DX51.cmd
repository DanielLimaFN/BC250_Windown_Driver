@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 97 DX.51 (lote de draws desligado por bisseccao; dirty rects seguem desligados), janela de 300 s - candidato fixado por hash
rem Evidencia da Corrida 95/96: o log residual do dwm.exe veio com a ACL de C:\Windows\Temp.
rem Reset de ACL (heranca da pasta de evidencia) para a sessao de analise conseguir ler.
icacls "%~dp0..\docs\evidence\2026-09-23_23-52-12-DESKTOP-HQAG6T4-lab-binding\umd-logs-residuais-do-bind-anterior" /reset /T /Q
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida97-dx51.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato ou auditoria divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=300"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%