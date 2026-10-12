@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 237 (doc 625): MESMO pacote 3.384.1.0 da 236, maquina PARADA no desktop por 600 s. CMOS em 256 MB.
echo PERGUNTA: a trava dura precisa de uso (menu, Pesquisa, apps) ou vem so com o tempo ligado?
echo ROTEIRO (IMPORTANTE - NAO MEXA):
echo   1. Rode este arquivo elevado, confirme o que o binder pedir e ANOTE a hora em que o desktop aparecer.
echo   2. Depois disso NAO toque em mouse nem teclado. Nao abra menu, Pesquisa, Gerenciador nem app nenhum.
echo   3. So' olhe a tela de longe. Anote a hora se ela congelar (relogio da barra parado) ou ficar preta.
echo   4. Aos 600 s o watchdog faz o rollback sozinho. Nao interrompa.
echo   Se precisar digitar algo para o binder (ex. DISPLAY_OK), faca so' isso e anote a hora: e' o unico toque permitido.
echo SE TRAVAR: anote a hora e aperte o BOTAO DE RESET em 10 a 15 segundos. NAO corte a energia nem segure o botao de ligar.
echo DEPOIS DE UMA TRAVA: a coleta automatica roda no boot seguinte; espere ela terminar antes de mexer.
echo Recuperando captura ETW pendente de corrida anterior, se houver...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bc250-hang-etw.ps1" -Recover
echo Lendo a VRAM na CMOS e o MOTIVO DO ULTIMO RESET (le e limpa, como o Linux; doc 553). O preflight exige 256 MB aplicados...
if exist "%~dp0vram-cmos\out\bc250-vram.exe" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0vram-cmos\bc250-vram.ps1" -Ler
    if errorlevel 1 echo [AVISO] Leitura da CMOS falhou; seguindo sem ela.
) else (
    echo [AVISO] tools\vram-cmos\out\bc250-vram.exe ausente; rode tools\vram-cmos\build.bat para ter a leitura.
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida237-parado-600s.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato, auditoria, energia ou configuracao de boot divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%
