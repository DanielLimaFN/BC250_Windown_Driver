@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 281 (doc 729): 600 s. lab 3.430.1.0 = DCN reduzido ao basico ja provado (D01-D05 flip e cursor e DPMS e refresh real, D33 DIRQL-safe) -- D12 e D19 FORA (decisao do Daniel apos 5 corridas com artefatos: 276-280). DcnExt=125, igual as Corridas 273/274 que nao tiveram esse problema. Volta a investigacao da TRAVA DURA (nao mais o DCN): DebugTrace=1 ligado (validador de PTE). CMOS em 256 MB.
echo PERGUNTAS: (1) a tela fica normal (sem artefatos), como nas 273/274? (2) a trava dura historica acontece, com qual assinatura (GPU ociosa, RPTR==WPTR, sem fault/TDR)? (3) quanto tempo ate travar, se travar?
echo Programas abertos sao PERMITIDOS (o preflight so' registra). Use a maquina normalmente.
echo ANTES: DESLIGUE a maquina (nao reinicie) e ligue de novo. O preflight recusa reinicio a quente e depurador de kernel.
echo ROTEIRO (anote a hora de cada passo e o que a tela mostra):
echo   1. Confirme que o desktop aparece normal, sem artefatos.
echo   2. Por volta de 1 min: em outra janela, COMO ADMINISTRADOR, rode tools\RODAR_PROCESSO_NOVO.cmd (T3) e deixe rodar os 90 ciclos.
echo   3. Use a maquina normalmente enquanto isso: menu Iniciar, Alt+Tab, Edge com video.
echo   4. Em ~600 s o watchdog devolve o BasicDisplay sozinho, SE nao travar antes. Anote o tempo total se travar.
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
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida281-dcn-basico-trava.ps1"
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
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
