@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 198: candidato M2 completo (3.344.1.0), imagens da RADV sem CPU no segmento 3. CMOS em 4096 MB (doc 548 secao 11).
echo SEM harness, ATIVIDADE MINIMA. NAO abra NADA alem do menu Iniciar.
echo ROTEIRO (IMPORTANTE - abra o menu desde cedo):
echo   1. Quando o desktop aparecer, espere uns 20 s.
echo   2. Clique no menu Iniciar. Anote a hora e se icones e textos aparecem (e em quantos segundos).
echo   3. Feche o menu, espere 30 s, abra de novo. Repita umas 5 vezes ao longo da corrida.
echo   4. Uma vez, digite algumas letras na Pesquisa com o menu aberto.
echo SE TRAVAR: anote a hora e aperte o BOTAO DE RESET em 10 a 15 segundos.
echo Use o BOTAO de reset. NAO corte a energia nem segure o botao de ligar.
echo DEPOIS DE UMA TRAVA: copie C:\Windows\Temp\BC250_UMD_LOG_*.txt com PowerShell elevado (a coleta FINAL nao roda quando a maquina trava).
echo Recuperando captura ETW pendente de corrida anterior, se houver...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bc250-hang-etw.ps1" -Recover
echo Lendo o tamanho da VRAM na CMOS (so leitura, doc 550; nao bloqueia se falhar)...
if exist "%~dp0vram-cmos\out\bc250-vram.exe" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0vram-cmos\bc250-vram.ps1" -Ler
    if errorlevel 1 echo [AVISO] Leitura da CMOS falhou; seguindo sem ela.
) else (
    echo [AVISO] tools\vram-cmos\out\bc250-vram.exe ausente; rode tools\vram-cmos\build.bat para ter a leitura.
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida198-m2-segmento3-ligado.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato, auditoria ou configuracao de boot divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%