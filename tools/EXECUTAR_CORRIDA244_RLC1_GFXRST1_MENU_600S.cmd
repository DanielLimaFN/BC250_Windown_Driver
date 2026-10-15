@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 244 (doc 642): lab 3.388.1.0 = P3.3 desligada + RLC.1 (RLC rodando o firmware, como o amdgpu) + GFXRST.1 (reset do GFX como o amdgpu) + a MESMA RADV B4AE2E14, SO o menu Iniciar por 600 s. CMOS em 256 MB.
echo PERGUNTAS: sem a P3.3 a trava volta aos minutos? Com o RLC rodando a trava dura muda? Se houver TDR do GFX, o reset do amdgpu acaba com os erros do SQ?
echo ANTES: feche Claude, Edge, Configuracoes, Gerenciador de Tarefas e o Microsoft 365 Copilot e DESLIGUE a maquina (nao reinicie). O preflight recusa reinicio e apps abertos.
echo ROTEIRO (SO O MENU, NADA MAIS):
echo   1. Anote a hora em que o desktop aparecer. Espere 20 s.
echo   2. Clique no botao Iniciar. Espere o menu desenhar (ou 10 s). Anote se icones e textos apareceram.
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
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida244-rlc1-gfxrst1-menu-600s.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato, auditoria, energia, boot frio ou apps abertos divergiram. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
