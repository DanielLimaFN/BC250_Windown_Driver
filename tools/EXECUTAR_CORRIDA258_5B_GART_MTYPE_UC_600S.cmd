@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 258 (doc 683): lab 3.404.1.0 = 257 + 5B (MTYPE_UC nas paginas de sistema/GART nao-coerentes dos Segmentos 0 e 2 em Bc250McdmEncodeVidMmPteWithLevelEx; alinhado com amdgpu gmc_v10_0.c:741 gart_pte_flags = MTYPE_UC; elimina colisoes de paridade GL2/CPU na DRAM) + 4C + C4 + C3 + C1 + MONITOR; P3.3 desligada + a MESMA RADV B4AE2E14, SO o menu Iniciar por 600 s. CMOS em 256 MB.
echo PERGUNTA: com MTYPE_UC aplicado em todas as paginas de sistema/GART nao-coerentes (eliminando cache GL2 da GPU sobre memoria do host), o erro de paridade do FCH/Data Fabric (RESET_REASON bit 30) desaparece e a corrida sustenta os 600 s?
echo ATENCAO: a tela deve MOSTRAR o desktop e o menu. Se ficar preta ou congelada, ANOTE a hora: isso e' resultado.
echo ANTES: feche Claude, Edge, Configuracoes, Gerenciador de Tarefas e o Microsoft 365 Copilot e DESLIGUE a maquina (nao reinicie). O preflight recusa reinicio e apps abertos.
echo ROTEIRO:
echo   1. Anote a hora em que o desktop aparecer. Espere 20 s.
echo   2. Aperte a tecla Win (abre o menu; confira se ele APARECE na tela). Espere 10 s.
echo   3. Feche o menu com a tecla Esc. Espere 30 s.
echo   4. Repita 2 e 3 ATE O ROLLBACK, sem parar no meio (uns 15 ciclos). NAO digite no menu, NAO abra app nenhum.
echo   5. Anote a hora de qualquer tela preta, congelamento ou demora fora do normal.
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
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida258-5b-gart-mtype-uc.ps1"
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
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
