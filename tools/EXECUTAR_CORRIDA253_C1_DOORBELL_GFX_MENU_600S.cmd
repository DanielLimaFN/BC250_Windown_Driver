@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 253 (doc 672): lab 3.399.1.0 = 252 + C1 (o anel grafico recebe o WPTR por DOORBELL, como o amdgpu; sem consulta de WPTR escolhida por nos) + temperatura no MONITOR; P3.3 desligada + a MESMA RADV B4AE2E14, SO o menu Iniciar por 600 s. CMOS em 256 MB.
echo PERGUNTA: com o anel grafico publicado por doorbell como no Linux, a trava dura some?
echo ATENCAO: a tela deve MOSTRAR o desktop e o menu (a sombra saiu). Se ficar preta ou congelada, ANOTE a hora: isso e' resultado.
echo ANTES: feche Claude, Edge, Configuracoes, Gerenciador de Tarefas e o Microsoft 365 Copilot e DESLIGUE a maquina (nao reinicie). O preflight recusa reinicio e apps abertos.
echo ROTEIRO (SO O MENU, NADA MAIS):
echo   1. Anote a hora em que o desktop aparecer. Espere 20 s.
echo   2. Aperte a tecla Win (abre o menu; confira se ele APARECE na tela). Espere 10 s.
echo   3. Feche o menu com a tecla Esc. Espere 30 s.
echo   4. Repita 2 e 3 ATE O ROLLBACK, sem parar no meio (uns 15 ciclos). NAO digite no menu (isso abre a Pesquisa), NAO abra app nenhum.
echo   5. Anote a hora de qualquer tela preta, congelamento ou demora fora do normal.
echo SE TRAVAR: anote a hora e o numero do ciclo, e aperte o BOTAO DE RESET em 10 a 15 segundos. NAO corte a energia.
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
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida253-c1-doorbell-gfx-menu-600s.ps1"
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
