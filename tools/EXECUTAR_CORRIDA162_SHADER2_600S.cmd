@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 162 SHADER.2: SRV de buffer tipado (o shader do menu Iniciar); KMD igual a 3.308 (DX.97); harness aos 30 s. CLIQUE NO MENU INICIAR algumas vezes, a partir de 1 minuto.
echo Repita o roteiro da 148: desktop, Gerenciador de Tarefas e depois o navegador.
echo SE TRAVAR: anote a hora e aperte o BOTAO DE RESET em 10 a 15 segundos (a memoria decai durante a trava; reset rapido preserva as migalhas).
echo Use o BOTAO de reset. NAO corte a energia nem segure o botao de ligar.
echo Recuperando captura ETW pendente de corrida anterior, se houver...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bc250-hang-etw.ps1" -Recover
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida162-shader2.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato, auditoria ou configuracao de boot divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_D3D11_START_DELAY=30"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
