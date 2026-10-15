@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 261 (doc 686): lab 3.407.1.0 = 260 + 5E (emissao incondicional de GCR GL2 rinse ao final de TransferVirtual, FillVirtual e FillByCopy no paging buffer) + 5D + 4C + C4 + C3 + C1 + MONITOR; P3.3 desligada + a MESMA RADV B4AE2E14 + ESTRESSE SINTETICO DE VRAM 500 MB MANUAL. CMOS em 256 MB.
echo PERGUNTA: com GCR (cache rinse GL2 write-back e invalidate) emitido incondicionalmente ao final de toda TransferVirtual, FillVirtual e FillByCopy, a eviccao de 16 MB do VidMm durante a 2a/3a bateria do estresse de 500 MB completa sem descompasso GL2/Data Fabric e sem paridade bit 30?
echo ATENCAO: a tela deve MOSTRAR o desktop e o menu. O estresse de 500 MB e' disparado MANUALMENTE quando desejar.
echo ANTES: feche Claude, Edge, Configuracoes, Gerenciador de Tarefas e o Microsoft 365 Copilot e DESLIGUE a maquina (nao reinicie). O preflight recusa reinicio e apps abertos.
echo ROTEIRO:
echo   1. Anote a hora em que o desktop aparecer. O monitor abrira automaticamente.
echo   2. Aperte a tecla Win (abre o menu; confira se ele APARECE na tela). Espere 10 s.
echo   3. Feche o menu com a tecla Esc. Espere 30 s.
echo   4. DISPARO MANUAL DO ESTRESSE (quando desejar): execute em outra janela:
echo        tools\RODAR_ESTRESSE_VRAM_500MB.cmd
echo   5. Acompanhe o estresse de 500 MB no console e continue interagindo ate o rollback (600 s).
echo   6. Anote a hora de qualquer tela preta, congelamento ou demora fora do normal.
echo SE TRAVAR: anote a hora e aperte o BOTAO DE RESET em 10 a 15 segundos. NAO corte a energia.
echo DEPOIS DE UMA TRAVA: a coleta automatica roda no boot seguinte; espere ela terminar antes de mexer.
echo Recuperando captura ETW pendente de corrida anterior, se houver...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bc250-hang-etw.ps1" -Recover
echo Lendo a VRAM na CMOS e o MOTIVO DO ULTIMO RESET (le e limpa, como o Linux; doc 553). O preflight exige 256 MB e boot frio...
if exist "%~dp0vram-cmos\out\bc250-vram.exe" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0vram-cmos\bc250-vram.ps1" -Ler
    if errorlevel 1 echo [AVISO] Leitura da CMOS falhou; seguindo sem ela.
) else (
    echo [AVISO] tools\vram-cmos\out\bc250-vram.exe ausente; rode tools\vram-cmos\build.bat para ter a leitura.
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida261-5e-transfer-gcr-stress-500mb.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato, auditoria, energia, boot frio ou apps abertos divergiram. Nenhum bind foi iniciado.
    exit /b 1
)
sc query bc250compute 2>nul | find "RUNNING" >nul
if not errorlevel 1 (
    echo [RECUSADO] O servico bc250compute esta RODANDO. Ele escreve MMIO e usa o par SMN 0x38/0x3C do KMD. Pare com: sc stop bc250compute
    exit /b 1
)
echo Abrindo o MONITOR da corrida numa janela separada (so' leitura: KMD + Windows; CSV em C:\BC250_MONITOR.csv)...
start "BC-250 MONITOR" cmd /k python -u "%~dp0monitor-corrida.py"
echo [ESTRESSE MANUAL]: O estresse nao foi disparado automaticamente.
echo Para executar o estresse de 500 MB manualmente quando desejar, rode:
echo   tools\RODAR_ESTRESSE_VRAM_500MB.cmd
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
