@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 270 (doc 711): DCN.1 ampliada, 180 s. lab 3.420.1.0 = 3.418.1.0 (flip pelo HUBP0 da 269) + contabilidade do latch (latchado/substituido/pendente, conta conferida) + [DCN1_RESUMO]: rotas, latch em quadros e em ms, VSYNC contra os quadros do OTG, primaria fora do segmento 1. Nenhuma escrita nova em registrador. CMOS em 256 MB.
echo PERGUNTAS: (1) menu, Alt+Tab, video e tela cheia funcionam com o flip pelo HUBP0? (2) a conta dos flips fecha? (3) o VSYNC acompanha o OTG? (4) o flip aguenta a carga T3 ate os 180 s?
echo Mudancas contra a 269: so' medicao no driver; roteiro maior; watchdog de 180 s; T3 depois do primeiro minuto.
echo Programas abertos sao PERMITIDOS (o preflight so' registra). Use a maquina normalmente.
echo ANTES: DESLIGUE a maquina (nao reinicie) e ligue de novo. O preflight recusa reinicio a quente e depurador de kernel.
echo ROTEIRO (anote a hora de cada passo):
echo   1. Anote a hora em que o desktop aparecer. O monitor abrira automaticamente.
echo   2. MENU (obrigatorio): Win, confira se o menu e os itens aparecem, espere 5 s, Esc. Repita 3 vezes.
echo   3. Arraste uma janela pela tela. Depois Alt+Tab entre duas janelas 3 vezes.
echo   4. Abra o Edge com um video (YouTube) em janela por ~15 s; depois tela cheia (tecla F) por ~10 s; saia com Esc e feche o Edge.
echo   5. Por volta de 1 min: em outra janela, COMO ADMINISTRADOR, rode tools\RODAR_PROCESSO_NOVO.cmd (T3) e deixe rodar.
echo   6. Em ~180 s o watchdog devolve o BasicDisplay sozinho. Anote se a imagem volta normal.
echo   ANOTE O QUE APARECE NA TELA em cada passo: normal? congelado? preto? listras/cores erradas? tremendo? E a hora de qualquer travamento.
echo SE TRAVAR: anote a hora e aperte o BOTAO DE RESET em 10 a 15 segundos. NAO corte a energia.
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
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida270-dcn1-ampliada.ps1"
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
