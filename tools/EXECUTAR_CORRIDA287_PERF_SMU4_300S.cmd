@echo off
setlocal
cd /d "%~dp0"
echo ===================================================================
echo   BC-250 CORRIDA 287 (doc 738): PERF SUBMIT + ZERO-STALL + SMU.4 (300S)
echo   Pacote: lab 3.436.1.0 = Zero-Stall + Perf Submit + SMU.4 + DCN Basico
echo   Target: AMD BC-250 APU (1002:13FE) ^| Windows 10 22H2 x64
echo ===================================================================
echo.
echo NOVIDADES DESTA CORRIDA:
echo   - Opcao 1 (Perf Submit) : Amortizacao de log repetitivo em DISPATCH_LEVEL (GATE/FENCE no Node1 GFX)
echo   - Opcao 4 (Zero-Stall)  : Notificacao VSYNC instantanea no DCN MPO3 ao hardware confirmar latch
echo   - Opcao 3 (SMU.4)       : Telemetria de P-State (Msg 0x0C) e Target GFXCLK (Msg 0x0F)
echo   - Carga T3 Animada      : Janela 3D visivel (480x320) + 64 MB VRAM + 180 Frames (60 FPS, 3.0s de tela)
echo   - Monitor Atualizado    : Exibe clock, target clk, tensao, P-State e features do SMU
echo   - Janela Estendida      : Watchdog configurado para 300 s (5 minutos)
echo   - DCN Basico 274        : DcnFlip = 1, DcnExt = 125, D12/D19 DESATIVADOS
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
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida287-perf-smu4-zerostall.ps1"
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
set "BC250_WATCHDOG_SECONDS=300"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
