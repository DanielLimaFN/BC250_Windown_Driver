@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 273 (doc 714): 180 s. lab 3.423.1.0 = 272 + correcao da tela azul 0x133 (log e copia do SDMA0 fora de DIRQL) + VSYNC por 3 interrupcoes do DCN em paralelo + diagnostico do AUX nos dois canais + cursor em I/mascarado por hardware. CRC desligado. ESCREVE registradores de DCN. CMOS em 256 MB.
echo PERGUNTAS: (1) sem tela azul 0x133? (2) o cursor segue normal, inclusive o I de texto? (3) o monitor apaga e volta? (4) a T3 roda ate os 180 s?
echo Programas abertos sao PERMITIDOS (o preflight so' registra). Use a maquina normalmente.
echo ANTES: DESLIGUE a maquina (nao reinicie) e ligue de novo. O preflight recusa reinicio a quente e depurador de kernel.
echo ROTEIRO (anote a hora de cada passo e o que a tela mostra):
echo   1. Anote a hora em que o desktop aparecer. O monitor abrira automaticamente.
echo   2. CURSOR: mexa o mouse pela tela inteira e pelos 4 cantos. Passe sobre um campo de texto (cursor em I) e sobre a borda de uma janela (seta dupla).
echo      Anote se algum cursor pisca, some, aparece duplicado ou com cor errada. Se o cursor SUMIR, use o teclado.
echo   3. MENU: Win, confira o menu e os itens, Esc. Repita 3 vezes. Arraste uma janela e faca Alt+Tab 3 vezes.
echo   4. MONITOR: rode tools\DESLIGAR_MONITOR.cmd. A tela deve apagar; espere ~5 s e mexa o mouse. Ela volta normal?
echo   5. Abra o Edge com um video (YouTube) ~15 s em janela e ~10 s em tela cheia (tecla F); saia e feche.
echo   6. Por volta de 1 min: em outra janela, COMO ADMINISTRADOR, rode tools\RODAR_PROCESSO_NOVO.cmd (T3) e deixe rodar.
echo   7. Em ~180 s o watchdog devolve o BasicDisplay sozinho. Anote se a imagem e o cursor voltam normais.
echo SE TRAVAR (MUDOU NESTA CORRIDA): anote a hora e ESPERE 3 MINUTOS antes do reset. Se o Windows der tela azul e reiniciar sozinho nesse tempo, deixe; o dump diz onde travou.
echo   Passados 3 minutos sem reiniciar, aperte o BOTAO DE RESET. NAO corte a energia.
echo DEPOIS DE UMA TRAVA: a coleta automatica roda no boot seguinte (ela copia tambem o log da T3); espere ela terminar.
echo Recuperando captura ETW pendente de corrida anterior, se houver...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bc250-hang-etw.ps1" -Recover
echo Lendo a VRAM na CMOS e o MOTIVO DO ULTIMO RESET (le e limpa, como o Linux; doc 553). O preflight exige 256 MB e boot frio...
if exist "%~dp0vram-cmos\out\bc250-vram.exe" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0vram-cmos\bc250-vram.ps1" -Ler
    if errorlevel 1 echo [AVISO] Leitura da CMOS falhou; seguindo sem ela.
) else (
    echo [AVISO] tools\vram-cmos\out\bc250-vram.exe ausente; rode tools\vram-cmos\build.bat para ter a leitura.
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida273-dirql1-dcn3.ps1"
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
rem Pedido do Daniel: 180 s (dentro do piso normal de 120 s; sem excecao de watchdog curto).
echo [T3] Por volta de 1 min, rode COMO ADMINISTRADOR: tools\RODAR_PROCESSO_NOVO.cmd
set "BC250_WATCHDOG_SECONDS=180"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
