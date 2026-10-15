@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 249 com KD (doc 658): pacote 3.394.1.0 (TMR.1 + correcoes + monitor) COM o depurador de kernel (KDNET) ativo por decisao do Daniel.
echo AVISO: cada break no WinDbg PARA a maquina inteira; de 'g' na hora. Feche o Microsoft 365 Copilot: ele relanca o WebView2, que gera breaks.
echo Conecte o WinDbg no host ANTES de seguir. Se travar: anote a hora, tente Ctrl+Break no WinDbg e depois reinicie.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0registrar-opcoes-de-boot.ps1" -Rotulo corrida249kd
set "BC250_ALLOW_KERNEL_DEBUG=1"
call "%~dp0EXECUTAR_CORRIDA249_TMR1_PACOTE_MENU_600S.cmd"
exit /b %ERRORLEVEL%
