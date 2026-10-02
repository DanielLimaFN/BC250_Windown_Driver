@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 164 KD: mesmo pacote 3.310.1.0, COM o harness D3D11 aos 30 s e COM o WinDbg (KDNET) conectado.
echo Objetivo: se travar, o WinDbg consegue parar a maquina? Clique no menu Iniciar algumas vezes depois de 1 minuto.
echo SE A TELA FICAR PRETA: anote a hora e, NO PC DO WINDBG, aperte Ctrl+Break e espere ate 30 s.
echo   Se aparecer o prompt kd^>, rode, nesta ordem:
echo     .logopen C:\bc250_kd164.log
echo     !running -it
echo     !stacks 2 bc250
echo     .dump /f C:\bc250_kd164.dmp
echo     .logclose
echo   Depois aperte o BOTAO DE RESET na BC-250.
echo   Se o WinDbg NAO responder em 30 s: anote isso e aperte o BOTAO DE RESET na hora.
echo NAO corte a energia nem segure o botao de ligar.
set "BC250_ALLOW_KERNEL_DEBUG=1"
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
