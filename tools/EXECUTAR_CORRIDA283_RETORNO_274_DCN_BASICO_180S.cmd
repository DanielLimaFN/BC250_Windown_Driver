@echo off
setlocal
cd /d "%~dp0"
echo ===================================================================
echo   BC-250 CORRIDA 283 (doc 732): RETORNO A CONFIGURACAO DA 274
echo   Pacote: lab 3.432.1.0 = DCN Basico Provado (HUBP0 Flip + Cursor HW)
echo   Target: AMD BC-250 APU (1002:13FE) ^| Windows 10 22H2 x64
echo ===================================================================
echo.
echo PARAMETROS DESTA CORRIDA:
echo   - DcnFlip = 1  : Flip direto pelo HUBP0 (zero copia de 11 MB pelo SDMA0)
echo   - DcnExt  = 125: DCN basico comprovado da Corrida 274 (0x7D: IRQ + Cursor HW + DPMS + Timing nativo no VidPn)
echo   - D19     = DESATIVADO (V_TOTAL intocado; sem mexer no refresh)
echo   - D12     = DESATIVADO (sem chaveamento de hardware no pad AUX)
echo.
echo EXPECTATIVA DESTA CORRIDA:
echo   (1) O video volta a acender PERFEITO e limpo, sem tela preta e sem artefatos?
echo   (2) O cursor por hardware continua respondendo liso?
echo   (3) O flip direto pelo HUBP0 ocorre a 60 FPS com zero copia de 11 MB?
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
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida283-retorno-274-dcn-basico.ps1"
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
echo [T3] Por volta de 1 min, rode COMO ADMINISTRADOR: tools\RODAR_PROCESSO_NOVO.cmd
set "BC250_WATCHDOG_SECONDS=180"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
