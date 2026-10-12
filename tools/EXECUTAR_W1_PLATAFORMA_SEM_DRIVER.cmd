@echo off
setlocal
cd /d "%~dp0"
echo BC-250 W1 (doc 625): a plataforma trava SEM o nosso driver? 45 min de estresse de CPU e memoria com o BasicDisplay.
echo PRE-REQUISITO: BasicDisplay ativo (BC250_RECOVERY.cmd ja rodado) e boot frio.
echo NAO use a maquina durante o teste. Deixe a tela ligada.
echo SE TRAVAR: anote a hora e use o BOTAO de reset. Depois, elevado: tools\vram-cmos\bc250-vram.ps1 -Ler
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0executar-w1-plataforma-sem-driver.ps1" %*
exit /b %ERRORLEVEL%
