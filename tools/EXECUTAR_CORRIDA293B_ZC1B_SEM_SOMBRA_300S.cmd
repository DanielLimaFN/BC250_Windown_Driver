@echo off
setlocal
cd /d "%~dp0"
echo ===================================================================
echo   BC-250 CORRIDA 293b (doc 750): ZERO COPIA SEM APAGAR A TELA (300S)
echo   Pacote: lab 3.446.1.0 = 3.445.1.0 + ZC.1b (UMD nova)
echo   Target: AMD BC-250 APU (1002:13FE) ^| Windows 10 22H2 x64
echo ===================================================================
echo.
echo NOVIDADE DESTA CORRIDA (sobre a 293: tela preta fora do que era redesenhado):
echo   - ZC.1b: a copia de regiao do DWM nao sobe mais a sombra de CPU (zeros)
echo            por cima da tela; so a copia de GPU escreve
echo   Procure no log: [ZC1B], [ZC1_PRESENT]
echo.
echo ANTERIOR (293):
echo   - ZC.1: o DWM desenha direto na primaria que o DCN mostra (e o app na superficie
echo           compartilhada): sem copia imagem-^>buffer linear-^>primaria no Present
echo   - PISC.1: latch medido no registrador do OTG; conta o erro antigo e os tardios reais
echo   Procure no log: [ZC1_PRESENT], FRENTE_B SIM_ZC1_PRIMARIA, [PISC1_LATCH]
echo   OLHE A TELA: se partes do desktop ficarem pretas fora do que e redesenhado,
echo   e a Corrida 190 voltando (anote).
echo.
echo ANTERIOR (292c): COMP.VA.3c tabela da GART no alocador da MMU global
echo HISTORICO (292b):
echo   - COMP.VA.3b: a tabela de 64 KB e reservada na primeira fatia livre a partir de 0x0F3F0000
echo               para baixo (292: offset fixo e TOP_DOWN deram NO_SPACE, GART desligada)
echo.
echo MANTIDO DA 292:
echo   - COMP.VA.2: a janela da GART espera a vez em vez de recusar (291: 5 recusas = 5 Present E_FAIL)
echo   - COMP.VA.3: GART de 32 MB; a tela inteira cabe na janela: 1 copia por quadro em vez de 22,
echo               e a janela fica mapeada enquanto a tabela do DWM nao muda (sem reler PTE)
echo   Procure no log: [D2_GART] TABELA RESERVADA, COMPOSITE_BLT OK VIA_VA ... pedacos=1, total reuso=
echo.
echo ANTERIOR (291):
echo   - APU.2b: buffer de comando fica no Seg 1; a DX.43 volta a ler o DW0 do IB e emitir o
echo             IB de aquecimento na troca de VMID (289: 21/21 CABECA_ILEGIVEL e GFX travado)
echo   - APU.2c: superficie da UMD sem GPU_VA_ONLY prefere a VRAM; a composicao do KMD
echo             volta a resolver a origem (290: 0 COMPOSITE_BLT OK, 160 recusados)
echo   - COMP.VA: se a superficie estiver fora da VRAM, o KMD copia as PTEs do processo
echo             para uma janela da GART e o SDMA copia por ela (como o amdgpu)
echo   Procure no log: [APU_ALLOC] UMD_COMPOSICAO_VRAM_PRIMEIRO, COMPOSITE_BLT OK e OK VIA_VA
echo.
echo MANTIDO DA 289:
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
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida293b-zc1b-sem-sombra.ps1"
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
