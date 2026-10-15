@echo off
setlocal
cd /d "%~dp0"
echo ===================================================================
echo   BC-250 CORRIDA 302 (doc 764): O QUE COPIAMOS DO AMDGPU PELO RASTRO DO NAGI (600S)
echo   Pacote: lab 3.456.1.0 = 3.455.1.0 + C01 + C03 + C05 + C06 + C07
echo   Target: AMD BC-250 APU (1002:13FE) ^| Windows 10 22H2 x64
echo ===================================================================
echo.
echo NOVIDADES DESTA CORRIDA:
echo   - C01: registradores que o Linux programa nesta placa e nos nao (SH_MEM, GDS, SDMA, TLB);
echo   - C03: RLC.1b religada (handshake RLC/SMU desligado, como o Nagi e o Linux sem GFXOFF);
echo   - C05: se a GPU travar, as ondas sao mortas antes do reset (soft recovery do Linux);
echo   - C06: VSYNC so pela VUPDATE sem lock, na sequencia do Linux; erros do CP contados;
echo   - C07: VMID nova escolhida pela menos usada (LRU), como o Linux.
echo   - Mantido da 301: SUBALOC.1, FRAME1, RES.1, TDRLOG.1, CLK.1 a 1800 MHz.
echo.
echo ROTEIRO: o mesmo da 301. Espere "VULKAN_VERIFICADO" na janela minimizada "BC-250 VULKAN". Depois:
echo   1. abra o menu Iniciar 3 vezes; 2. vkcube ~30 s; 3. TFM ~30 s;
echo   4. GravityMark com as mesmas opcoes da 300; 5. NAO rode o memtest_vulkan;
echo   6. desktop normal ate o fim.
echo   Se travar: anote a hora; depois do reboot copie C:\BC250_TDR_PASSOS.txt para mim.
echo.
echo MANTIDO: Bloco 1 do journal, APU.2/2b/2c, DCN.1 a DCN.3 (DcnFlip=1, DcnExt=125),
echo   ZC.1/1b, PSO.1/1b, PAG.1, MENU.1, GART de 32 MB. Watchdog de 600 s.
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

if exist "%SystemRoot%\System32\drivers\ps5_vcn.bin" (
    echo Removendo o residuo da 296: System32\drivers\ps5_vcn.bin
    del /f /q "%SystemRoot%\System32\drivers\ps5_vcn.bin"
)

echo Conferindo o runtime do Vulkan e o registro do ICD...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0instalar-vulkan-runtime.ps1"
if errorlevel 1 (
    echo [RECUSADO] O runtime do Vulkan nao ficou pronto. Nenhum bind foi iniciado.
    pause
    exit /b 1
)

if exist C:\BC250_TDR_PASSOS.txt move /y C:\BC250_TDR_PASSOS.txt C:\BC250_TDR_PASSOS_anterior.txt >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida302-nagi-c01-c03-c05-c06-c07.ps1"
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
echo Armando a verificacao do Vulkan depois do bind (janela minimizada)...
start "BC-250 VULKAN" /min powershell -NoProfile -ExecutionPolicy Bypass -NoExit -File "%~dp0verificar-vulkan-apos-bind.ps1"
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
set "BC250_RC=%ERRORLEVEL%"
echo Copiando o log da verificacao do Vulkan para a evidencia...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$d = Get-ChildItem -LiteralPath '%~dp0..\docs\evidence' -Directory -Filter '*-lab-binding' | Sort-Object Name | Select-Object -Last 1; if ($d -and (Test-Path 'C:\BC250_VULKAN_APOS_BIND.log')) { Copy-Item 'C:\BC250_VULKAN_APOS_BIND.log' (Join-Path $d.FullName 'vulkan-apos-bind.log') -Force; Write-Host ('copiado para ' + $d.Name) } else { Write-Host 'log da verificacao do Vulkan ausente' }"
exit /b %BC250_RC%
