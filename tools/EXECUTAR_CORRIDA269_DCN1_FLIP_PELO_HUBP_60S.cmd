@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 269 (doc 710): DCN.1, flip pelo HUBP0 do DCN 2.0.1. lab 3.418.1.0 = 3.416.1.0 + o HUBP0 passa a apontar para a primaria do DWM a cada flip, sem copia de 11 MB pelo SDMA0; VSYNC so' depois do latch; o HUBP0 volta ao GOP no fim. ESCREVE registrador de DCN pela primeira vez. Modo debug e carga T3 iguais aos da 268. CMOS em 256 MB.
echo PERGUNTA: o desktop aparece e ATUALIZA com o flip pelo HUBP0 (menu abrindo, janela mexendo, relogio)? Teste SO' do DCN: watchdog de 60 s.
echo Uma mudanca so': o flip pelo DCN. Mesmo modo debug da 268. SEM a carga T3 (nao cabe em 60 s).
echo Programas abertos sao PERMITIDOS (o preflight so' registra). Use a maquina normalmente.
echo ANTES: DESLIGUE a maquina (nao reinicie) e ligue de novo. O preflight recusa reinicio a quente e depurador de kernel.
echo ROTEIRO:
echo   1. Anote a hora em que o desktop aparecer. O monitor abrira automaticamente.
echo   2. Aperte a tecla Win (abre o menu; confira se ele APARECE). Espere 10 s. Feche com Esc.
echo   3. Mova uma janela pela tela e abra/feche o menu mais 2 vezes. Confira se a imagem ACOMPANHA.
echo      NAO rode a T3 (RODAR_PROCESSO_NOVO.cmd) nesta corrida.
echo   4. Em ~60 s o watchdog devolve o BasicDisplay sozinho. Anote se a imagem volta normal depois disso.
echo   5. ANOTE O QUE APARECE NA TELA: desktop normal? congelado? preto? listras/cores erradas? imagem tremendo? E a hora de qualquer travamento.
echo SE TRAVAR: anote a hora e aperte o BOTAO DE RESET em 10 a 15 segundos. NAO corte a energia.
echo DEPOIS DE UMA TRAVA: a coleta automatica roda no boot seguinte; espere ela terminar.
echo Recuperando captura ETW pendente de corrida anterior, se houver...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bc250-hang-etw.ps1" -Recover
echo Lendo a VRAM na CMOS e o MOTIVO DO ULTIMO RESET (le e limpa, como o Linux; doc 553). O preflight exige 256 MB e boot frio...
if exist "%~dp0vram-cmos\out\bc250-vram.exe" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0vram-cmos\bc250-vram.ps1" -Ler
    if errorlevel 1 echo [AVISO] Leitura da CMOS falhou; seguindo sem ela.
) else (
    echo [AVISO] tools\vram-cmos\out\bc250-vram.exe ausente; rode tools\vram-cmos\build.bat para ter a leitura.
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida269-dcn1-flip-pelo-hubp.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato, auditoria, energia ou boot frio divergiram. Nenhum bind foi iniciado.
    exit /b 1
)
sc query bc250compute 2>nul | find "RUNNING" >nul
if not errorlevel 1 (
    echo [RECUSADO] O servico bc250compute esta RODANDO. Ele escreve MMIO e usa o par SMN 0x38/0x3C do KMD. Pare com: sc stop bc250compute
    exit /b 1
)
echo Abrindo o MONITOR da corrida numa janela separada (so' leitura: KMD + Windows; CSV em C:\BC250_MONITOR.csv)...
start "BC-250 MONITOR" cmd /k python -u "%~dp0monitor-corrida.py"
rem Pedido do Daniel: ensaio so' do DCN, watchdog de 60 s (excecao explicita do piso de 120 s).
set "BC250_ALLOW_SHORT_WATCHDOG=1"
set "BC250_WATCHDOG_SECONDS=60"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
