@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "MODE=RUN"
if /I "%~1"=="/HELP" goto :help
if /I "%~1"=="/?" goto :help
if /I "%~1"=="/DRY-RUN" set "MODE=DRY-RUN"
if not "%~1"=="" if /I not "%~1"=="/DRY-RUN" goto :bad_argument

set "LOG=%SystemDrive%\BC250_RECOVERY.log"
if /I "%MODE%"=="DRY-RUN" set "LOG=NUL"
set "QUARANTINE=%SystemDrive%\BC250_DRIVER_QUARANTINE"
set "OEM_MARKER=%SystemDrive%\BC250_TEST_OEMINF.txt"
set "DRIVER_DIR=%SystemRoot%\System32\drivers"

>"%LOG%" echo BC250 recovery - mode=%MODE% - %DATE% %TIME%
call :note ============================================================
call :note BC250 - RECUPERACAO DOS DRIVERS EXPERIMENTAIS
call :note Modo: %MODE%
call :note Windows: %SystemRoot%
call :note ============================================================

if /I "%MODE%"=="DRY-RUN" goto :dry_run

fltmc.exe >nul 2>&1
if errorlevel 1 (
    call :note ERRO: abra o Prompt de Comando como Administrador.
    exit /b 2
)

reg.exe query "HKLM\SYSTEM\CurrentControlSet\Control\SafeBoot\Option" /v OptionValue >nul 2>&1
if errorlevel 1 (
    call :note RECUSADO: a remocao real so e permitida no Modo de Seguranca.
    call :note Entre em Recuperacao, Opcoes avancadas, Configuracoes de Inicializacao.
    call :note Escolha Modo de Seguranca com Prompt de Comando e execute novamente.
    exit /b 3
)

if not exist "%QUARANTINE%\" mkdir "%QUARANTINE%" >nul 2>&1
if not exist "%QUARANTINE%\" (
    call :note ERRO: nao foi possivel criar %QUARANTINE%.
    exit /b 4
)

set "RECOVERY_FAILED=0"
call :remove_bc250_packages
if errorlevel 1 set "RECOVERY_FAILED=1"
call :remove_service bc250readonly
if errorlevel 1 set "RECOVERY_FAILED=1"
call :remove_service bc250dbg
if errorlevel 1 set "RECOVERY_FAILED=1"
call :quarantine_driver bc250readonly.sys
if errorlevel 1 set "RECOVERY_FAILED=1"
call :quarantine_driver bc250dbg.sys
if errorlevel 1 set "RECOVERY_FAILED=1"

if "!RECOVERY_FAILED!"=="1" (
    call :note ------------------------------------------------------------
    call :note ERRO: recuperacao incompleta. Nao instale nem ative outro driver.
    call :note Consulte o log: %LOG%
    exit /b 5
)

call :note ------------------------------------------------------------
call :note Recuperacao concluida. Reinicie o Windows.
call :note Os binarios encontrados foram movidos para %QUARANTINE%.
call :note Log: %LOG%
call :note Nao foram alterados drivers AMD, BasicDisplay ou bc250v4.
exit /b 0

:dry_run
call :note Nenhuma alteracao sera feita.
call :show_service bc250readonly
call :show_service bc250dbg
call :show_service bc250v4
call :show_file bc250readonly.sys
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
        call :note Removendo pacote BC250 readonly identificado pelo conteudo: %%~nxF
        pnputil.exe /delete-driver "%%~nxF" /uninstall /force >>"%LOG%" 2>&1
        if errorlevel 1 (
            call :note AVISO: pnputil nao removeu %%~nxF. Consulte o log.
            set "PACKAGE_FAILURE=1"
        ) else (
            call :note Pacote %%~nxF removido.
        )
    )
)
if "!PACKAGE_COUNT!"=="0" call :note Nenhum pacote BC250 readonly foi encontrado no Driver Store.
if "!PACKAGE_FAILURE!"=="1" exit /b 1
call :delete_oem_marker
exit /b !ERRORLEVEL!

:show_bc250_packages
set "PACKAGE_COUNT=0"
for %%F in ("%SystemRoot%\INF\oem*.inf") do if exist "%%~fF" (
    call :is_bc250_package "%%~fF"
    if not errorlevel 1 (
        set /a PACKAGE_COUNT+=1 >nul
        call :note Pacote BC250 readonly detectado: %%~nxF
    )
)
if "!PACKAGE_COUNT!"=="0" call :note Pacotes BC250 readonly detectados: nenhum.
exit /b 0

:is_bc250_package
findstr.exe /I /C:"Class=ComputeAccelerator" "%~1" >nul 2>&1
if errorlevel 1 exit /b 1
findstr.exe /I /C:"PCI\VEN_1002&DEV_13FE" "%~1" >nul 2>&1
if errorlevel 1 exit /b 1
findstr.exe /I /C:"AddService=bc250readonly," "%~1" >nul 2>&1
if errorlevel 1 exit /b 1
findstr.exe /I /C:"ServiceBinary=%%12%%\bc250readonly.sys" "%~1" >nul 2>&1
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

:quarantine_driver
set "SOURCE=%DRIVER_DIR%\%~1"
set "TARGET=%QUARANTINE%\%~1.disabled"
if not exist "!SOURCE!" (
    call :note Binario %~1: ausente.
    exit /b 0
)
move /Y "!SOURCE!" "!TARGET!" >>"%LOG%" 2>&1
if errorlevel 1 (
    call :note Primeiro isolamento de !SOURCE! falhou; aplicando ACL somente ao binario experimental exato.
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
    move /Y "!SOURCE!" "!TARGET!" >>"%LOG%" 2>&1
    if errorlevel 1 (
        call :note ERRO: !SOURCE! continuou presente apos correcao de ACL.
        exit /b 1
    )
)
if exist "!SOURCE!" (
    call :note ERRO: !SOURCE! ainda existe depois do isolamento.
    exit /b 1
)
call :note Binario %~1 isolado em !TARGET!.
exit /b 0

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
echo   %~nx0            Remove o teste, somente no Modo de Seguranca elevado.
echo.
echo O script trata exclusivamente bc250readonly e bc250dbg. Pacotes OEM sao
echo identificados pelo conjunto: classe ComputeAccelerator, hardware ID
echo PCI\VEN_1002^&DEV_13FE, servico e binario bc250readonly. O marcador continua
echo sendo auditado, mas nao e a unica fonte para localizar pacotes orfaos.
exit /b 0
