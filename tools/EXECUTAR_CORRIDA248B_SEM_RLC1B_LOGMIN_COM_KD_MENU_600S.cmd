@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 248b com KD (doc 651): pacote 3.393.1.0 (G.1 + RLC.1 + LOGMIN.1, sem RLC.1b) COM o depurador de kernel (KDNET) ativo por decisao do Daniel.
echo AVISO: cada break no WinDbg PARA a maquina inteira; de 'g' na hora. Feche o Microsoft 365 Copilot: ele relanca o WebView2, que gera breaks.
echo Conecte o WinDbg no host ANTES de seguir. Se travar: anote a hora, tente Ctrl+Break no WinDbg e depois reinicie.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0registrar-opcoes-de-boot.ps1" -Rotulo corrida248bkd
set "BC250_ALLOW_KERNEL_DEBUG=1"
call "%~dp0EXECUTAR_CORRIDA248B_SEM_RLC1B_LOGMIN_MENU_600S.cmd"
exit /b %ERRORLEVEL%
