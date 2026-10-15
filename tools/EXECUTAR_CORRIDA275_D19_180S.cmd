@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 275 (docs 720/722): 180 s. lab 3.425.1.0 = 274 + D19 (prova autocontida de troca de refresh pelo V_TOTAL, so' diminui, muda e devolve ANTES do desktop aparecer) + diagnostico novo de recovery (so' leitura). D12 (pad AUX) NAO esta armado. CMOS em 256 MB.
echo PERGUNTAS: (1) o log mostra [DCN2_VTOTAL] APLICADO / MEDIDO / DEVOLVIDO logo no inicio, ANTES do desktop aparecer? (2) o Hz medido depois de aplicar bate com o pedido (-10 Hz)? (3) o Hz medido depois de devolver volta ao original? (4) o StopDevice loga [DX48_LIFECYCLE] desta vez, ou a pendencia da 274 se repete?
echo Programas abertos sao PERMITIDOS (o preflight so' registra). Use a maquina normalmente.
echo ANTES: DESLIGUE a maquina (nao reinicie) e ligue de novo. O preflight recusa reinicio a quente e depurador de kernel.
echo ROTEIRO (anote a hora de cada passo e o que a tela mostra):
echo   1. Anote a hora em que o desktop aparecer (a prova do V_TOTAL ja aconteceu ANTES disso, durante o StartDevice).
echo   2. CURSOR e MENU: mexa o mouse, abra o menu Iniciar (Win, Esc, 3x). So' para confirmar que nada regrediu.
echo   3. Arraste uma janela e faca Alt+Tab 3 vezes (olhe se a tela preta transitoria da 274 se repete).
echo   4. Abra o Edge com um video (YouTube) ~15 s em janela e ~10 s em tela cheia (tecla F); saia e feche.
echo   5. Por volta de 1 min: em outra janela, COMO ADMINISTRADOR, rode tools\RODAR_PROCESSO_NOVO.cmd (T3) e deixe rodar.
echo   6. Em ~180 s o watchdog devolve o BasicDisplay sozinho. Anote se a imagem e o cursor voltam normais, e se precisou de reboot (como na 274) ou foi online direto.
echo SE TRAVAR: anote a hora e ESPERE 3 MINUTOS antes do reset. Se o Windows der tela azul e reiniciar sozinho nesse tempo, deixe; o dump diz onde travou.
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
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida275-d19-recovery-diag.ps1"
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
echo [T3] Por volta de 1 min, rode COMO ADMINISTRADOR: tools\RODAR_PROCESSO_NOVO.cmd
set "BC250_WATCHDOG_SECONDS=180"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
