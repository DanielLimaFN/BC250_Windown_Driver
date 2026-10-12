@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 246b (doc 646): REPETICAO da 246 com o MESMO pacote 3.390.1.0 (G.1), agora COM o depurador de kernel (KDNET) ativo por decisao do Daniel.
echo AVISO: com o KD ligado o tempo ate a trava NAO e' comparavel com a 246; esta corrida nao julga a G.1 sozinha.
echo Conecte o WinDbg no host ANTES de seguir. Se travar: anote a hora, tente Ctrl+Break no WinDbg e depois reinicie.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0registrar-opcoes-de-boot.ps1" -Rotulo corrida246b
set "BC250_ALLOW_KERNEL_DEBUG=1"
call "%~dp0EXECUTAR_CORRIDA246_G1_GOLDEN_MENU_600S.cmd"
exit /b %ERRORLEVEL%
