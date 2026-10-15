@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 276 (docs 722/723/724): 180 s. lab 3.426.1.0 = 275 + D19b (instrumentacao: tempo real decorrido e QPC absoluto nos logs de D19, para isolar se os artefatos da 275 vieram do V_TOTAL ou do primeiro flip) + D12 ARMADO (pad AUX/DDC1 em modo hardware so' durante a leitura do EDID, autorizado pelo Daniel). CMOS em 256 MB.
echo PERGUNTAS: (1) a tela fica preta/com artefatos de novo no bind, como na 275? (2) se sim, por quanto tempo (anote a hora do inicio e do fim do efeito visual)? (3) o log mostra [DCN2_VTOTAL] DEVOLVIDO com qpc_abs e [DCN1_LATCH] #1 com qpc_abs -- a diferenca entre os dois bate com o que voce observou? (4) D12: aparece [DCN2_AUXPAD] no log, e o EDID muda de sintetizado para algo lido de verdade?
echo Programas abertos sao PERMITIDOS (o preflight so' registra). Use a maquina normalmente.
echo ANTES: DESLIGUE a maquina (nao reinicie) e ligue de novo. O preflight recusa reinicio a quente e depurador de kernel.
echo ROTEIRO (anote a hora de cada passo e o que a tela mostra):
echo   1. Anote a hora em que o bind comecar e a hora em que o desktop aparecer -- e se a tela ficou preta/com artefatos entre as duas, e por quanto tempo.
echo   2. CURSOR e MENU: mexa o mouse, abra o menu Iniciar (Win, Esc, 3x).
echo   3. Arraste uma janela e faca Alt+Tab 3 vezes (olhe se a tela preta transitoria da 274 se repete).
echo   4. Abra o Edge com um video (YouTube) ~15 s em janela e ~10 s em tela cheia (tecla F); saia e feche.
echo   5. Por volta de 1 min: em outra janela, COMO ADMINISTRADOR, rode tools\RODAR_PROCESSO_NOVO.cmd (T3) e deixe rodar.
echo   6. Em ~180 s o watchdog devolve o BasicDisplay sozinho. Anote se a imagem e o cursor voltam normais, e se precisou de reboot ou foi online direto.
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
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida276-d19b-d12-diag.ps1"
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
