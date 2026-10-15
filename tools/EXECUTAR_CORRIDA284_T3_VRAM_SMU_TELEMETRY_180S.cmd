@echo off
setlocal
cd /d "%~dp0"
echo ===================================================================
echo   BC-250 CORRIDA 284 (doc 734): T3 COM VRAM REAL + TELEMETRIA SMU
echo   Pacote: lab 3.433.1.0 = DCN Basico + T3 VRAM 96MB + SMU GFXCLK
echo   Target: AMD BC-250 APU (1002:13FE) ^| Windows 10 22H2 x64
echo ===================================================================
echo.
echo NOVIDADES DESTA CORRIDA:
echo   - Telemetria SMU.1: Leitura periodica do GFXCLK (Msg 0x37) no KMD e no Monitor
echo   - Carga T3 Real   : 6 texturas x 16 MB = 96 MB em VRAM local (50%% do pool CMOS)
echo   - Pipeline 3D     : 30 frames de render/clear com ID3D11DeviceContext_Flush ativo
echo   - DCN Basico 274  : DcnFlip = 1, DcnExt = 125, D12/D19 DESATIVADOS
echo.
echo ANTES: DESLIGUE completamente a maquina (nao reinicie) e ligue de novo.
echo O preflight recusa reinicio a quente e depurador de kernel.
echo.
echo Recuperando captura ETW pendente de corrida anterior, se houver...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bc250-hang-etw.ps1" -Recover
echo Lendo a VRAM na CMOS e o MOTIVO DO ULTIMO RESET...
if exist "%~dp0vram-cmos\out\bc250-vram.exe" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0vram-cmos\bc250-vram.ps1" -Ler
    if errorlevel 1 echo [AVISO] Leitura da CMOS falhou; seguindo sem ela.
) else (
    echo [AVISO] tools\vram-cmos\out\bc250-vram.exe ausente; rode tools\vram-cmos\build.bat para ter a leitura.
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida284-t3-vram-smu-telemetry.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato, auditoria, energia ou boot frio divergiram. Nenhum bind foi iniciado.
    pause
    exit /b 1
)
sc query bc250compute 2>nul | find "RUNNING" >nul
if not errorlevel 1 (
    echo [RECUSADO] O servico bc250compute esta RODANDO. Pare com: sc stop bc250compute
    pause
    exit /b 1
)
echo Abrindo o MONITOR da corrida numa janela separada...
start "BC-250 MONITOR" cmd /k python -u "%~dp0monitor-corrida.py"
echo [T3] Apos a subida do desktop, rode COMO ADMINISTRADOR em outra janela:
echo      tools\RODAR_PROCESSO_NOVO.cmd
set "BC250_WATCHDOG_SECONDS=180"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
