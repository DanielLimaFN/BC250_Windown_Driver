@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 116 DX.71: MESMO PACOTE da 115, janela de 180 s, captura de dump se travar.
echo SE TRAVAR: segure CTRL DIREITO e pressione SCROLL LOCK duas vezes. Aguarde a tela azul e a gravacao do dump.
echo Se o teclado nao responder, registre isso e reinicie pelo botao; NAO corte a energia.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida116-dx71-dump.ps1"
if errorlevel 1 (
    echo [RECUSADO] Preflight de candidato/dump falhou. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=180"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
