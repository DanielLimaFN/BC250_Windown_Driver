@echo off
setlocal
cd /d "%~dp0"
echo ===================================================================
echo   BC-250 CORRIDA 289 (doc 740): FLUIDEZ TOTAL DO DESKTOP (300S)
echo   Pacote: lab 3.438.1.0 = APU.2 + DCN.4 + GPUVA.4 + LOG.2
echo   Target: AMD BC-250 APU (1002:13FE) ^| Windows 10 22H2 x64
echo ===================================================================
echo.
echo NOVIDADES DESTA CORRIDA:
echo   - APU.2 (Inversao PreferredSegment) : Nao-primarias preferem Seg 2 (RAM 12 GB) e Seg 1 (VRAM 256 MB)
echo                                         Derruba transferencias SDMA0 de 2.683 para ^< 50 (Copy ^< 5%%)
echo   - DCN.4 (Formatos Compativeis 32bpp): Blt Present aceita 21, 22 e 28 + Swapchain B8G8R8A8 no 3D
echo                                         Elimina 100%% das 2.992 recusas de Present
echo   - GPUVA.4 (Capacidade + Desacoplamento): 8.192 slots no journal + FlushTlb retorna STATUS_SUCCESS
echo                                         Elimina 2.590 erros STATUS_IO_DEVICE_ERROR e piscadas do DWM
echo   - LOG.2 (Fim do I/O Sincrono de Log): ZwFlushBuffersFile removido do caminho em regime
echo                                         Elimina congelamentos de nucleos de CPU por flush no SSD
echo   - DCN Scanout Protegido             : Primaria estritamente travada no Seg 1 com Prioridade MAXIMA
echo   - DCN Zero-Stall Mantido            : 99,94%% dos flips com latch ^< 16,7 ms
echo   - SMU.4 Mantido                     : GFXCLK 1500 MHz, Target 1500 MHz, VDDGFX 100 mV
echo   - Carga T3 Animada 3D               : Janela visivel (480x320) + 64 MB VRAM + 180 Frames (60 FPS)
echo   - Janela Estendida                  : Watchdog configurado para 300 s (5 minutos)
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
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida289-fluidez-desktop.ps1"
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
