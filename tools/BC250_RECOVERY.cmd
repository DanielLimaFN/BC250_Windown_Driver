@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "MODE=RUN"
if /I "%~1"=="/HELP" goto :help
if /I "%~1"=="/?" goto :help
if /I "%~1"=="/DRY-RUN" set "MODE=DRY-RUN"
if not "%~1"=="" if /I not "%~1"=="/DRY-RUN" goto :bad_argument

set "LOG=%SystemDrive%\BC250_RECOVERY.log"
if /I "%MODE%"=="DRY-RUN" set "LOG=NUL"
set "OEM_MARKER=%SystemDrive%\BC250_TEST_OEMINF.txt"
set "DRIVER_DIR=%SystemRoot%\System32\drivers"
set "INSTANCE_ID=PCI\VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00\4&19CAF403&0&0041"
rem Janela de espera por BasicDisplay. Ver :poll_basic_display.
set "POLL_TIMEOUT=90"
set "POLL_INTERVAL=2"
set "RESCAN_EVERY=20"

>"%LOG%" echo BC250 recovery - mode=%MODE% - %DATE% %TIME%
call :note ============================================================
call :note BC250 - RECUPERACAO DOS DRIVERS EXPERIMENTAIS
call :note Modo: %MODE%
call :note Windows: %SystemRoot%
call :note ============================================================

if /I "%MODE%"=="DRY-RUN" goto :dry_run

reg query "HKLM\SYSTEM\CurrentControlSet\Control\MiniNT" >nul 2>&1
if not errorlevel 1 (
    call :note Ambiente de Recuperacao [WinRE / WinPE] detectado.
) else (
    fltmc.exe >nul 2>&1
    if errorlevel 1 (
        call :note ERRO: abra o Prompt de Comando como Administrador.
        exit /b 2
    )
)

call :note Execucao com privilegios administrativos autorizada.
call :note O dispositivo BC-250 pode piscar ou perder imagem durante a troca.

set "RECOVERY_FAILED=0"
call :disable_bc250_device
call :remove_bc250_packages
if errorlevel 1 set "RECOVERY_FAILED=1"
call :remove_service bc250readonly
if errorlevel 1 set "RECOVERY_FAILED=1"
call :remove_service bc250mcdm
if errorlevel 1 set "RECOVERY_FAILED=1"
call :remove_service bc250dbg
if errorlevel 1 set "RECOVERY_FAILED=1"
call :delete_driver bc250readonly.sys
if errorlevel 1 set "RECOVERY_FAILED=1"
call :delete_driver bc250mcdm.sys
if errorlevel 1 set "RECOVERY_FAILED=1"
call :delete_driver bc250dbg.sys
if errorlevel 1 set "RECOVERY_FAILED=1"
if exist "%SystemDrive%\BC250_BOOT_FUSE.dat" (
    attrib.exe -R "%SystemDrive%\BC250_BOOT_FUSE.dat" >nul 2>&1
    del /f /q "%SystemDrive%\BC250_BOOT_FUSE.dat" >nul 2>&1
    call :note Fusivel persistente removido para rearmamento limpo.
)
call :enable_and_verify_basic_display
if errorlevel 1 set "RECOVERY_FAILED=1"

if "!RECOVERY_FAILED!"=="1" (
    call :note ------------------------------------------------------------
    call :note ERRO: recuperacao incompleta. Nao instale nem ative outro driver.
    call :note Se um binario estava carregado, reinicie normalmente e execute este script outra vez.
    call :note Consulte o log: %LOG%
    exit /b 5
)

call :note ------------------------------------------------------------
call :note Recuperacao concluida: BasicDisplay Codigo 0 confirmado.
call :note Os binarios experimentais encontrados foram apagados.
call :note Log: %LOG%
call :note Nao foram alterados drivers AMD, BasicDisplay ou bc250v4.
exit /b 0

:dry_run
call :note Nenhuma alteracao sera feita.
call :show_service bc250readonly
call :show_service bc250mcdm
call :show_service bc250dbg
call :show_service bc250v4
call :show_file bc250readonly.sys
call :show_file bc250mcdm.sys
call :show_file bc250dbg.sys
call :show_file bc250v4.sys
call :show_bc250_packages
call :read_oem_marker
if defined OEMINF (
    call :note Pacote OEM registrado para o teste: !OEMINF!
) else (
    call :note Pacote OEM do teste: marcador ausente ou invalido.
)
call :note DRY-RUN concluido; zero alteracoes.
exit /b 0

:remove_bc250_packages
set "PACKAGE_COUNT=0"
set "PACKAGE_FAILURE=0"
for %%F in ("%SystemRoot%\INF\oem*.inf") do if exist "%%~fF" (
    call :is_bc250_package "%%~fF"
    if not errorlevel 1 (
        set /a PACKAGE_COUNT+=1 >nul
        call :note Removendo pacote experimental BC250 identificado pelo conteudo: %%~nxF
        pnputil.exe /delete-driver "%%~nxF" /uninstall /force >>"%LOG%" 2>&1
        if errorlevel 1 (
            call :note AVISO: pnputil nao removeu %%~nxF. Consulte o log.
            set "PACKAGE_FAILURE=1"
        ) else (
            call :note Pacote %%~nxF removido.
        )
    )
)
if "!PACKAGE_COUNT!"=="0" call :note Nenhum pacote experimental BC250 foi encontrado no Driver Store.
if "!PACKAGE_FAILURE!"=="1" exit /b 1
call :delete_oem_marker
exit /b !ERRORLEVEL!

:show_bc250_packages
set "PACKAGE_COUNT=0"
for %%F in ("%SystemRoot%\INF\oem*.inf") do if exist "%%~fF" (
    call :is_bc250_package "%%~fF"
    if not errorlevel 1 (
        set /a PACKAGE_COUNT+=1 >nul
        call :note Pacote experimental BC250 detectado: %%~nxF
    )
)
if "!PACKAGE_COUNT!"=="0" call :note Pacotes experimentais BC250 detectados: nenhum.
exit /b 0

:disable_bc250_device
call :note Desabilitando temporariamente o dispositivo BC-250 exato.
pnputil.exe /disable-device "%INSTANCE_ID%" /force >>"%LOG%" 2>&1
if errorlevel 1 call :note AVISO: disable-device falhou; a remocao do pacote ainda sera tentada.
exit /b 0

:is_bc250_package
findstr.exe /I /C:"Class=ComputeAccelerator" /C:"Class=Display" "%~1" >nul 2>&1
if errorlevel 1 exit /b 1
findstr.exe /I /C:"PCI\VEN_1002&DEV_13FE" "%~1" >nul 2>&1
if errorlevel 1 exit /b 1
findstr.exe /I /C:"AddService=bc250readonly," "%~1" >nul 2>&1
if not errorlevel 1 (
    findstr.exe /I /C:"ServiceBinary=%%12%%\bc250readonly.sys" "%~1" >nul 2>&1
    if not errorlevel 1 exit /b 0
)
findstr.exe /I /C:"AddService=bc250mcdm," "%~1" >nul 2>&1
if errorlevel 1 exit /b 1
findstr.exe /I /C:"ServiceBinary=%%12%%\bc250mcdm.sys" "%~1" >nul 2>&1
if errorlevel 1 exit /b 1
exit /b 0

:delete_oem_marker
if not exist "%OEM_MARKER%" exit /b 0
attrib.exe -R "%OEM_MARKER%" >nul 2>&1
del /f /q "%OEM_MARKER%" >nul 2>&1
if exist "%OEM_MARKER%" (
    call :note ERRO: nao foi possivel remover o marcador %OEM_MARKER%.
    exit /b 1
)
call :note Marcador OEM removido.
exit /b 0

:read_oem_marker
set "OEMINF="
if not exist "%OEM_MARKER%" exit /b 0
for /f "delims=" %%I in ('findstr.exe /R /I /X "oem[0-9][0-9]*[.]inf" "%OEM_MARKER%"') do if not defined OEMINF set "OEMINF=%%I"
exit /b 0

:remove_service
sc.exe query "%~1" >nul 2>&1
if errorlevel 1 (
    call :note Servico %~1: ausente.
    exit /b 0
)
call :note Servico %~1: parando e removendo.
sc.exe stop "%~1" >>"%LOG%" 2>&1
sc.exe delete "%~1" >>"%LOG%" 2>&1
if errorlevel 1 (
    call :note AVISO: sc delete %~1 falhou. Consulte o log.
    exit /b 1
) else (
    call :note Servico %~1 removido ou marcado para remocao.
)
exit /b 0

:delete_driver
set "SOURCE=%DRIVER_DIR%\%~1"
if not exist "!SOURCE!" (
    call :note Binario %~1: ausente.
    exit /b 0
)
call :note Apagando binario experimental exato: !SOURCE!
attrib.exe -R -S -H "!SOURCE!" >nul 2>&1
del /F /Q "!SOURCE!" >>"%LOG%" 2>&1
if exist "!SOURCE!" (
    call :note Primeira remocao de !SOURCE! falhou; aplicando ACL somente ao binario experimental exato.
    takeown.exe /F "!SOURCE!" /A >>"%LOG%" 2>&1
    if errorlevel 1 (
        call :note ERRO: nao foi possivel assumir propriedade de !SOURCE!.
        exit /b 1
    )
    icacls.exe "!SOURCE!" /grant *S-1-5-32-544:F >>"%LOG%" 2>&1
    if errorlevel 1 (
        call :note ERRO: nao foi possivel conceder acesso ao Administrators em !SOURCE!.
        exit /b 1
    )
    attrib.exe -R -S -H "!SOURCE!" >nul 2>&1
    del /F /Q "!SOURCE!" >>"%LOG%" 2>&1
    if errorlevel 1 (
        call :note ERRO: !SOURCE! esta carregado ou continuou presente apos correcao de ACL.
        exit /b 1
    )
)
if exist "!SOURCE!" (
    call :note ERRO: !SOURCE! ainda existe depois da exclusao forcada.
    exit /b 1
)
call :note Binario %~1 apagado.
exit /b 0

:enable_and_verify_basic_display
call :note Reabilitando o dispositivo e solicitando nova enumeracao PnP.
pnputil.exe /enable-device "%INSTANCE_ID%" >>"%LOG%" 2>&1
pnputil.exe /scan-devices >>"%LOG%" 2>&1
call :note Aguardando BasicDisplay por ate %POLL_TIMEOUT% s (amostra a cada %POLL_INTERVAL% s).
call :poll_basic_display %POLL_TIMEOUT%
if not errorlevel 1 (
    call :note BasicDisplay Codigo 0 confirmado por leitura CIM independente.
    exit /b 0
)

rem ---------------------------------------------------------------------
rem No PnP orfao.
rem
rem Remover servico, binario e pacote do Driver Store NAO desfaz o vinculo do
rem no de dispositivo. A chave Enum continua com Service=bc250mcdm e
rem DeviceDesc=@oemNN.inf, apontando para o que acabou de ser apagado, e toda
rem reenumeracao para em Codigo 19 (CM_PROB_REGISTRY). Reiniciar nao resolve:
rem o vinculo e' persistente.
rem
rem Ate o 3.75 a recuperacao terminava aqui pedindo mais um reboot, e o
rem operador ficava em laco. Removendo o no, o PnP o recria do zero na
rem varredura seguinte e vincula BasicDisplay.
rem ---------------------------------------------------------------------
call :note AVISO: BasicDisplay nao confirmado; removendo o no PnP orfao.
pnputil.exe /remove-device "%INSTANCE_ID%" >>"%LOG%" 2>&1
if errorlevel 1 call :note AVISO: remove-device falhou; pode nao existir nesta versao do pnputil.
pnputil.exe /scan-devices >>"%LOG%" 2>&1
call :note Aguardando reenumeracao por ate %POLL_TIMEOUT% s apos remover o no.
call :poll_basic_display %POLL_TIMEOUT%
if not errorlevel 1 (
    call :note BasicDisplay Codigo 0 confirmado apos remocao do no PnP.
    exit /b 0
)

call :note ERRO: BasicDisplay Codigo 0 ainda nao foi confirmado. Consulte o log e reinicie.
call :note Se persistir, remova o no manualmente e reenumere:
call :note   pnputil /remove-device "%INSTANCE_ID%"
call :note   pnputil /scan-devices
exit /b 1

:poll_basic_display
rem ---------------------------------------------------------------------
rem Espera ATIVA por BasicDisplay/Codigo 0. Recebe o timeout em segundos.
rem
rem Ate o 3.79 havia aqui "timeout /t 3" e "timeout /t 5" fixos. Reenumerar um
rem no de classe Display recem-removido nao cabe em 5 s: em 01/09/2026 21:57 a
rem recuperacao declarou fracasso cedo demais, o watchdog escalou para reboot
rem e a imagem so' voltou depois de reiniciar a maquina. Evidencia em
rem docs\evidence\2026-09-01_21-53-07-DESKTOP-HQAG6T4-lab-binding.
rem
rem O laco inteiro roda em UM processo PowerShell, de proposito:
rem   1. 45 chamadas a :check_basic_display seriam 45 inicializacoes de
rem      powershell.exe (0,5 a 1,5 s cada) e a cadencia derraparia;
rem   2. timeout.exe aborta com "Input redirection is not supported" quando
rem      este script e' chamado com stdin redirecionado, que e' exatamente
rem      como o watchdog SYSTEM e os auditores o invocam.
rem
rem Cada amostra vai para o log. Um fracasso passa a ser uma serie temporal
rem auditavel, e nao mais uma unica leitura sem contexto.
rem ---------------------------------------------------------------------
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$id='%INSTANCE_ID%'; $start=Get-Date; $deadline=$start.AddSeconds([int]'%~1'); $lastScan=$start; $i=0; $d=@(); while($true){ $i++; $d=@(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue | Where-Object {$_.PNPDeviceID -eq $id}); if($d.Count -eq 1){ $svc=[string]$d[0].Service; $code=[string][uint32]$d[0].ConfigManagerErrorCode } else { $svc='count=' + $d.Count; $code='n/a' }; Write-Host ('  amostra {0} t={1}s Service={2} Codigo={3}' -f $i,[int]((Get-Date)-$start).TotalSeconds,$svc,$code); if($d.Count -eq 1 -and $d[0].Service -eq 'BasicDisplay' -and [uint32]$d[0].ConfigManagerErrorCode -eq 0){ exit 0 }; if((Get-Date) -ge $deadline){ break }; if(((Get-Date)-$lastScan).TotalSeconds -ge %RESCAN_EVERY%){ Write-Host '  reenumerando: pnputil /scan-devices'; pnputil.exe /scan-devices | Out-Null; $lastScan=Get-Date }; Start-Sleep -Seconds %POLL_INTERVAL% }; $d | Format-List PNPDeviceID,Service,Status,ConfigManagerErrorCode | Out-String | Write-Host; exit 1" >>"%LOG%" 2>&1
exit /b %errorlevel%

:check_basic_display
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$d=@(Get-CimInstance Win32_PnPEntity | Where-Object {$_.PNPDeviceID -eq '%INSTANCE_ID%'}); if($d.Count -eq 1 -and $d[0].Service -eq 'BasicDisplay' -and [uint32]$d[0].ConfigManagerErrorCode -eq 0){exit 0}; $d | Format-List PNPDeviceID,Service,Status,ConfigManagerErrorCode | Out-String | Write-Host; exit 1" >>"%LOG%" 2>&1
exit /b %errorlevel%

:show_service
sc.exe query "%~1" >nul 2>&1
if errorlevel 1 (call :note Servico %~1: ausente.) else (call :note Servico %~1: presente.)
exit /b 0

:show_file
if exist "%DRIVER_DIR%\%~1" (call :note Binario %~1: presente.) else (call :note Binario %~1: ausente.)
exit /b 0

:note
echo %*
>>"%LOG%" echo %*
exit /b 0

:bad_argument
echo ERRO: argumento desconhecido: %~1
echo Use %~nx0 /HELP
exit /b 1

:help
echo Uso:
echo   %~nx0 /DRY-RUN   Audita sem modificar o sistema.
echo   %~nx0            Remove o teste no Windows normal, em terminal elevado.
echo.
echo O script trata exclusivamente bc250readonly, bc250mcdm e bc250dbg. Pacotes OEM sao
echo identificados pelo conjunto: classe ComputeAccelerator, hardware ID
echo PCI\VEN_1002^&DEV_13FE e o par exato servico/binario bc250readonly ou
echo bc250mcdm. O marcador continua
echo sendo auditado, mas nao e a unica fonte para localizar pacotes orfaos.
echo Os tres binarios experimentais sao apagados, nao movidos para quarentena.
exit /b 0
