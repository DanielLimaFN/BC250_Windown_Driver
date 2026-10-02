@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 189: candidato T4.7+I.1c (3.334.1.0), tabela de objetos do KMD 4x maior + RADV sem redirecionamento em AppContainer.
echo SEM harness, ATIVIDADE MINIMA. NAO abra NADA alem do menu Iniciar.
echo ROTEIRO (IMPORTANTE - a 188 ficou sem resposta porque o menu nao foi aberto):
echo   1. Quando o desktop aparecer, espere uns 20 s.
echo   2. Clique no menu Iniciar. Anote a hora e se icones e textos aparecem (e em quantos segundos).
echo   3. Feche o menu, espere 30 s, abra de novo. Repita umas 5 vezes ao longo da corrida.
echo   4. Uma vez, digite algumas letras na Pesquisa com o menu aberto.
echo SE TRAVAR: anote a hora e aperte o BOTAO DE RESET em 10 a 15 segundos.
echo Use o BOTAO de reset. NAO corte a energia nem segure o botao de ligar.
echo DEPOIS DE UMA TRAVA: copie C:\Windows\Temp\BC250_UMD_LOG_*.txt com PowerShell elevado (a coleta FINAL nao roda quando a maquina trava).
echo Recuperando captura ETW pendente de corrida anterior, se houver...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bc250-hang-etw.ps1" -Recover
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida189-t47i1c.ps1"
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