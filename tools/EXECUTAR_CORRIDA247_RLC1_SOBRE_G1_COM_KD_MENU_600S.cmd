@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 247 com KD (doc 648): pacote 3.391.1.0 (G.1 + RLC.1 rodando) COM o depurador de kernel (KDNET) ativo por decisao do Daniel.
echo AVISO: cada break no WinDbg PARA a maquina inteira; de 'g' na hora. Feche o Microsoft 365 Copilot: ele relanca o WebView2, que gera breaks.
echo Conecte o WinDbg no host ANTES de seguir. Se travar: anote a hora, tente Ctrl+Break no WinDbg e depois reinicie.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0registrar-opcoes-de-boot.ps1" -Rotulo corrida247kd
set "BC250_ALLOW_KERNEL_DEBUG=1"
call "%~dp0EXECUTAR_CORRIDA247_RLC1_SOBRE_G1_MENU_600S.cmd"
exit /b %ERRORLEVEL%
