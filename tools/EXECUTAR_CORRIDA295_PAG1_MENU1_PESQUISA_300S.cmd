@echo off
setlocal
cd /d "%~dp0"
echo ===================================================================
echo   BC-250 CORRIDA 295 (doc 754): MENU MAIS RAPIDO E PESQUISA NA GPU (300S)
echo   Pacote: lab 3.449.1.0 = 3.448.1.0 + MENU.1 + PAG.1 + PSO.1b (UMD nova)
echo   Target: AMD BC-250 APU (1002:13FE) ^| Windows 10 22H2 x64
echo ===================================================================
echo.
echo NOVIDADE DESTA CORRIDA (sobre a 294: o menu recria o device a cada abertura,
echo e 0,9 a 1,6 s de cada abertura ficam dentro do CreateResource):
echo   - PAG.1: o CreateResource nao espera mais a paginacao na CPU para recurso com
echo            memoria Vulkan propria; quem precisa da alocacao (alias, Lock, Present,
echo            composicao pelo KMD) espera antes. Esperado: menu mais rapido.
echo   - MENU.1: uma linha [MENU1] por device com o tempo de cada fase ate o 1o Present
echo   - PSO.1b: o cache de pipelines so e regravado quando muda
echo   - PESQUISA: depois do bind, uma janela minimizada reinicia a Pesquisa sozinha
echo     (SearchApp + WebView2) para ela nascer no nosso driver. Na 294 ela nasceu antes
echo     do bind e nunca usou a nossa GPU. Log: C:\BC250_PESQUISA_REINICIO.log
echo.
echo ROTEIRO: espere a linha "PESQUISA_REINICIO: FEITO" na janela minimizada (cerca de
echo   1 min depois do bind). Depois abra o menu 3 vezes e a Pesquisa 3 vezes; anote se
echo   a Pesquisa ficou mais rapida e se o menu abre mais rapido que na 294.
echo   Procure no log: [MENU1], [PAG1], [PSO1], SHD1 no msedgewebview2
echo.
echo FAXINA (uma vez, elevado, se ainda nao fez): tools\armar-log-webview2.ps1 -Desarmar
echo.
echo MANTIDO: ZC.1/ZC.1b (zero copia), PSO.1, SHD.1, PISC.1, COMP.VA.2/3, APU.2b/2c,
echo   DCN.1 a DCN.3 (DcnFlip=1, DcnExt=125), GART de 32 MB, watchdog de 300 s.
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
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida295-pag1-menu1-pesquisa.ps1"
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
