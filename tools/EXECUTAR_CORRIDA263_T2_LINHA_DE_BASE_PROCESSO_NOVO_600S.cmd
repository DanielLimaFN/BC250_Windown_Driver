@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 263 (doc 692): LINHA DE BASE. lab 3.409.1.0 = 3.408.1.0 + T2 (folha de PTE MTYPE NC por padrao, como o amdgpu; doc 691) + a MESMA UMD B73F9BDA e RADV B4AE2E14. CMOS em 256 MB.
echo PERGUNTA: com uma carga fixa de PROCESSOS NOVOS (T3: um processo D3D11 a cada ~5 s), quantos processos passam ate a trava dura, e a trava cai dentro de um processo novo?
echo Esta corrida NAO testa mudanca: ela mede a taxa que as proximas mudancas vao ter de bater.
echo Programas abertos sao PERMITIDOS (o preflight so' registra). Use a maquina normalmente.
echo ANTES: DESLIGUE a maquina (nao reinicie) e ligue de novo. O preflight recusa reinicio a quente e depurador de kernel.
echo ROTEIRO:
echo   1. Anote a hora em que o desktop aparecer. O monitor abrira automaticamente.
echo   2. Aperte a tecla Win (abre o menu; confira se ele APARECE). Espere 10 s. Feche com Esc.
echo   3. Em outra janela, COMO ADMINISTRADOR, rode: tools\RODAR_PROCESSO_NOVO.cmd
echo      (90 processos, ~8 min; log em C:\BC250_PROCESSO_NOVO.log)
echo   4. Deixe a T3 rodar ate o fim ou ate a trava. Pode usar a maquina normalmente.
echo   5. Anote a hora de qualquer tela preta, congelamento, imagem corrompida ou demora fora do normal.
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
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida263-t2-linha-de-base-processo-novo.ps1"
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
echo [T3] Depois do passo 2, rode COMO ADMINISTRADOR: tools\RODAR_PROCESSO_NOVO.cmd
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
