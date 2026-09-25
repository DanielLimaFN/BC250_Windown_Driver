@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 118 DX.73: verificar pixel central da copia, janela de 180 s.
echo SE TRAVAR: segure CTRL DIREITO e pressione SCROLL LOCK duas vezes para tentar gravar dump.
echo Se o teclado nao responder, registre isso e reinicie pelo botao; NAO corte a energia.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida118-dx73.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato ou auditoria divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=180"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
