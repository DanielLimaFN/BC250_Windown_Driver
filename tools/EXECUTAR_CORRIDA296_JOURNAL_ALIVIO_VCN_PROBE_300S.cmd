@echo off
setlocal
cd /d "%~dp0"
echo ===================================================================
echo   BC-250 CORRIDA 296 (doc 756): ALIVIO JOURNAL GPUVA + SONDA VCN 2.0 (300S)
echo   Pacote: lab 3.450.1.0 = 3.449.1.0 + BLOCO 1 (JOURNAL 4X/64X) + VCN.1 PROBE
echo   Target: AMD BC-250 APU (1002:13FE) ^| Windows 10 22H2 x64
echo ===================================================================
echo.
echo NOVIDADES DESTA CORRIDA:
echo   1. BLOCO 1 - ALIVIO DO JOURNAL GPUVA E TAINT:
echo      - BC250_ACTIVE_GPUVA_JOURNAL_SLOTS expandido de 4 para 16 (4x capacidade).
echo      - BC250_ACTIVE_JOURNAL_TAINT_SLOTS expandido de 16 para 64 (4x capacidade).
echo      - Cura da histerese de JournalTaintAll (resetado quando tabelas esvaziam).
echo      - Descarte limpo de journals e taints orfaos no teardown do processo.
echo      - Esperado: fim definitivo do falso NO_SPACE por exaustao de slots.
echo.
echo   2. VCN.1 - SONDA EXPERIMENTAL DE ATIVACAO DO VCN 2.0 (PS5 LINUX):
echo      - Firmware ps5_vcn.bin integrado e copiado para System32\drivers.
echo      - Sonda de registradores do bloco UVD/VCN (0x1E000..0x22000).
echo      - Sequencia de reset e boot canonico do PS5 (VcnProbe=2).
echo      - Polling de prontidao em mmUVD_STATUS (bit 1 / 0x2).
echo      - Esperado: verificacao no silicio se a VCPU do VCN acorda com o firmware do PS5.
echo.
echo MANTIDO: APU.2/2b/2c, DCN.1 a DCN.3 (DcnFlip=1, DcnExt=125), ZC.1/1b (zero copia),
echo   PSO.1/1b, PAG.1, MENU.1, GART de 32 MB, watchdog de 300 s.
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

if not exist "%SystemRoot%\System32\drivers\ps5_vcn.bin" (
    copy /y "%~dp0..\firmware\ps5_vcn.bin" "%SystemRoot%\System32\drivers\ps5_vcn.bin" >nul 2>&1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida296-journal-alivio-vcn-probe.ps1"
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
echo Armando o reinicio da Pesquisa depois do bind (janela minimizada)...
start "BC-250 PESQUISA" /min powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0reiniciar-pesquisa-apos-bind.ps1"
echo [T3] Apos a subida do desktop, rode COMO ADMINISTRADOR em outra janela:
echo      tools\RODAR_PROCESSO_NOVO.cmd
set "BC250_WATCHDOG_SECONDS=300"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
set "BC250_RC=%ERRORLEVEL%"
echo Copiando o log do reinicio da Pesquisa para a evidencia...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$d = Get-ChildItem -LiteralPath '%~dp0..\docs\evidence' -Directory -Filter '*-lab-binding' | Sort-Object Name | Select-Object -Last 1; if ($d -and (Test-Path 'C:\BC250_PESQUISA_REINICIO.log')) { Copy-Item 'C:\BC250_PESQUISA_REINICIO.log' (Join-Path $d.FullName 'pesquisa-reinicio.log') -Force; Write-Host ('copiado para ' + $d.Name) } else { Write-Host 'log do reinicio da Pesquisa ausente' }"
exit /b %BC250_RC%
