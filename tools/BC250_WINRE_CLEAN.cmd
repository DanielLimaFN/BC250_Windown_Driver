@echo off
setlocal EnableExtensions EnableDelayedExpansion
title BC-250 - Limpeza Offline (WinRE / midia externa)

rem =============================================================================
rem  BC250_WINRE_CLEAN.cmd
rem  Limpeza OFFLINE do driver BC-250 a partir do Ambiente de Recuperacao
rem  do Windows (WinRE) ou de qualquer WinPE, com midia externa.
rem
rem  QUANDO USAR
rem    A maquina ficou com tela preta ou em loop de boot depois de instalar o
rem    perfil WDDM do bc250mcdm, e nao da' para entrar no Windows.
rem
rem  COMO CHEGAR AQUI
rem    Ligar > 3 falhas de boot (ou pendrive de instalacao do Windows)
rem    > Reparar o computador > Solucao de problemas
rem    > Opcoes avancadas > Prompt de Comando
rem    Depois: D:\BC250_WINRE_CLEAN.cmd     (troque D: pela letra do pendrive)
rem
rem  AVISO SOBRE O METODO MANUAL
rem    NUNCA use  del C:\Windows\System32\drivers\bc*.sys
rem    O padrao bc*.sys casa com bcmfn2.sys, que e' driver INBOX da
rem    Microsoft e faz parte do Windows. Apagar quebra o sistema.
rem    Este script apaga apenas bc250*, amdbc250* e cyan_skillfish2*,
rem    e recusa por seguranca qualquer outro nome.
rem
rem  POR QUE NAO E' POWERSHELL
rem    O WinRE padrao NAO tem PowerShell. Este arquivo e' batch de proposito,
rem    para funcionar no Prompt de Comando cru da recuperacao.
rem
rem  O QUE ELE FAZ, EM ORDEM
rem    1. Descobre sozinho qual volume tem o Windows (nao assume C:)
rem    2. Descobre sozinho o oemNN.inf publicado do BC-250 (nao assume oem11)
rem    3. Mostra o plano e pede confirmacao
rem    4. dism /remove-driver em cada pacote encontrado
rem    5. Apaga bc*.sys, firmware cyan_skillfish2_*.bin e bc250umd.dll
rem    6. Desabilita (Start=4) os servicos residuais na hive SYSTEM offline
rem    7. Verifica o resultado e grava log na midia
rem
rem  PARAMETROS
rem    /force    nao pergunta nada (use quando ja' confia no plano)
rem    /list     apenas mostra o que faria; nao altera nada
rem    /purge    APAGA as chaves de servico em vez de so' desabilitar
rem    /win:D    forca a letra do volume do Windows
rem    -h        ajuda (ou barra-interrogacao)
rem =============================================================================

set "SCRIPTDIR=%~dp0"
set "OPT_FORCE=0"
set "OPT_LIST=0"
set "OPT_PURGE=0"
set "WINVOL="
set "RC=0"
set "FOUND=0"
set "REMOVED=0"
set "FAILED=0"

:PARSE
if "%~1"=="" goto PARSED
set "A=%~1"
if /i "%A%"=="/force" ( set "OPT_FORCE=1" & shift & goto PARSE )
if /i "%A%"=="/list"  ( set "OPT_LIST=1"  & shift & goto PARSE )
if /i "%A%"=="/purge" ( set "OPT_PURGE=1" & shift & goto PARSE )
if /i "%A%"=="-h"     goto HELP
if /i "%A%"=="/help"  goto HELP
if "%A%"=="/?"        goto HELP
set "PFX=%A:~0,5%"
if /i "%PFX%"=="/win:" ( set "WINVOL=%A:~5,1%" & shift & goto PARSE )
echo [ERRO] Parametro desconhecido: %A%
echo Use /? para ajuda.
exit /b 1
:PARSED

rem ---------------------------------------------------------------- log na midia
set "LOG=%SCRIPTDIR%BC250_WINRE_CLEAN.log"
> "%LOG%" echo === BC-250 WinRE Clean === %DATE% %TIME%
if not exist "%LOG%" set "LOG=%TEMP%\BC250_WINRE_CLEAN.log"
if not exist "%LOG%" set "LOG=nul"

call :SAY ""
call :SAY "==============================================================="
call :SAY "  BC-250 - LIMPEZA OFFLINE DO DRIVER (WinRE)"
call :SAY "==============================================================="
call :SAY ""
if "%OPT_LIST%"=="1" call :SAY "  MODO /list - nada sera' alterado"
if "%OPT_LIST%"=="1" call :SAY ""

rem ---------------------------------------------------- 1) achar o volume do Windows
call :SAY "[1/7] Localizando o volume do Windows..."
if not "%WINVOL%"=="" (
    if not exist "%WINVOL%:\Windows\System32\ntoskrnl.exe" (
        call :SAY "  [ERRO] %WINVOL%: nao contem uma instalacao do Windows."
        set "RC=1"
        goto FIM
    )
    call :SAY "  Volume forcado por parametro: %WINVOL%:"
    goto GOTVOL
)

set "CANDS="
set "NCAND=0"
for %%d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do call :TESTVOL %%d
if "%NCAND%"=="0" (
    call :SAY "  [ERRO] Nenhuma instalacao do Windows encontrada."
    call :SAY "         Rode 'diskpart' -^> 'list volume' e repita com /win:LETRA"
    set "RC=1"
    goto FIM
)
if not "%NCAND%"=="1" (
    call :SAY "  [!] Mais de uma instalacao encontrada:%CANDS%"
    call :SAY "      Repita informando qual, por exemplo:  %~nx0 /win:D"
    set "RC=1"
    goto FIM
)
for %%v in (%CANDS%) do set "WINVOL=%%v"
:GOTVOL
set "WINDIR_OFF=%WINVOL%:\Windows"
call :SAY "  Windows offline em: %WINDIR_OFF%"
call :SAY ""

rem ------------------------------------------- 2) achar os pacotes oemNN.inf do BC-250
rem  Nao parseamos a saida do DISM: ela e' traduzida ("Nome Publicado" em pt-BR)
rem  e quebra facil. Buscamos direto nos INF publicados em \Windows\INF, que e'
rem  independente de idioma.
call :SAY "[2/7] Procurando pacotes do BC-250 no DriverStore offline..."
set "OEMLIST="
if exist "%WINDIR_OFF%\INF\" (
    for /f "delims=" %%f in ('dir /b "%WINDIR_OFF%\INF\oem*.inf" 2^>nul') do call :MATCHINF "%%f"
)
if "%FOUND%"=="0" (
    call :SAY "  Nenhum pacote do BC-250 publicado. Nada a remover do DriverStore."
) else (
    call :SAY "  Encontrado(s) %FOUND% pacote(s):%OEMLIST%"
)
call :SAY ""

rem ------------------------------------------------------ 3) inventario de arquivos
call :SAY "[3/7] Arquivos residuais do BC-250:"
set "NFILES=0"
for %%p in (
    "%WINDIR_OFF%\System32\drivers\bc250*.sys"
    "%WINDIR_OFF%\System32\drivers\amdbc250*.sys"
    "%WINDIR_OFF%\System32\drivers\cyan_skillfish2_*.bin"
    "%WINDIR_OFF%\System32\bc250umd.dll"
    "%WINDIR_OFF%\SysWOW64\bc250umd.dll"
) do call :LISTFILES %%p
if "%NFILES%"=="0" call :SAY "  (nenhum)"
call :SAY ""

if "%OPT_LIST%"=="1" (
    call :SAY "==============================================================="
    call :SAY "  MODO /list - encerrando sem alterar nada."
    call :SAY "==============================================================="
    set "RC=0"
    goto FIM
)

if "%FOUND%"=="0" if "%NFILES%"=="0" (
    call :SAY "Nada do BC-250 encontrado neste volume. Sistema ja' esta' limpo."
    set "RC=2"
    goto FIM
)

rem ------------------------------------------------------------- 4) confirmacao
if "%OPT_FORCE%"=="1" goto DOIT
call :SAY "---------------------------------------------------------------"
call :SAY " Isto vai REMOVER o driver BC-250 de %WINVOL%: permanentemente."
call :SAY " No proximo boot o Windows volta para o Microsoft Basic Display."
call :SAY "---------------------------------------------------------------"
set "ANS="
set /p "ANS=Confirmar? (S para continuar, qualquer outra tecla aborta): "
if /i not "%ANS%"=="S" (
    call :SAY "Abortado pelo usuario. Nada foi alterado."
    set "RC=1"
    goto FIM
)
call :SAY ""

:DOIT
rem --------------------------------------------------- 5) remover pacotes via DISM
call :SAY "[4/7] Removendo pacotes do DriverStore com DISM..."
if "%FOUND%"=="0" (
    call :SAY "  (nada a remover)"
) else (
    for %%o in (%OEMLIST%) do call :REMOVEDRV "%%o"
)
call :SAY ""

rem ------------------------------------------------------ 6) apagar arquivos soltos
call :SAY "[5/7] Apagando arquivos residuais..."
call :KILLFILE "%WINDIR_OFF%\System32\drivers\bc250*.sys"
call :KILLFILE "%WINDIR_OFF%\System32\drivers\amdbc250*.sys"
call :KILLFILE "%WINDIR_OFF%\System32\drivers\cyan_skillfish2_*.bin"
call :KILLFILE "%WINDIR_OFF%\System32\bc250umd.dll"
call :KILLFILE "%WINDIR_OFF%\SysWOW64\bc250umd.dll"
call :SAY ""

rem ------------------------------------- 7) neutralizar servicos na hive offline
rem  Por padrao DESABILITAMOS (Start=4) em vez de apagar a chave.
rem
rem  Motivo, registrado em tools/clean_all_drivers.ps1: apagar a chave de
rem  servico deixou o SCM com referencia orfa e a reinstalacao seguinte falhou
rem  em AddService com 0x00000002, sem pista do motivo. Custou um ciclo inteiro
rem  de depuracao em 2026-08-22. Offline o risco e' menor, mas Start=4 resolve o
rem  problema (o servico nao carrega) e nao atrapalha uma reinstalacao futura,
rem  porque o AddService do INF sobrescreve o valor.
rem
rem  Quem quiser a chave apagada de fato usa /purge.
call :SAY "[6/7] Neutralizando servicos na hive SYSTEM offline..."
set "HIVE=%WINDIR_OFF%\System32\config\SYSTEM"
if not exist "%HIVE%" (
    call :SAY "  [!] Hive SYSTEM nao encontrada em %HIVE% - etapa ignorada"
    goto AFTERHIVE
)
reg load HKLM\BC250OFF "%HIVE%" >nul 2>&1
if errorlevel 1 (
    call :SAY "  [!] Nao foi possivel carregar a hive (em uso?) - etapa ignorada"
    goto AFTERHIVE
)
for /f "delims=" %%k in ('reg query HKLM\BC250OFF 2^>nul ^| findstr /i "ControlSet0"') do call :DOSET "%%k"
reg unload HKLM\BC250OFF >nul 2>&1
:AFTERHIVE
call :SAY ""

rem -------------------------------------------------------------- 8) verificacao
call :SAY "[7/7] Verificando..."
set "LEFT=0"
if exist "%WINDIR_OFF%\INF\" (
    for /f "delims=" %%f in ('dir /b "%WINDIR_OFF%\INF\oem*.inf" 2^>nul') do call :RECHECK "%%f"
)
for %%p in (
    "%WINDIR_OFF%\System32\drivers\bc250*.sys"
    "%WINDIR_OFF%\System32\drivers\amdbc250*.sys"
    "%WINDIR_OFF%\System32\drivers\cyan_skillfish2_*.bin"
    "%WINDIR_OFF%\System32\bc250umd.dll"
) do if exist %%p ( set /a LEFT+=1 & call :SAY "  [!] AINDA EXISTE: %%p" )

call :SAY ""
call :SAY "==============================================================="
if "%LEFT%"=="0" (
    if "%FAILED%"=="0" (
        call :SAY "  LIMPEZA CONCLUIDA - nada do BC-250 restou em %WINVOL%:"
        set "RC=0"
    ) else (
        call :SAY "  LIMPEZA PARCIAL - %FAILED% remocao(oes) do DISM falhou(aram)"
        set "RC=3"
    )
) else (
    call :SAY "  LIMPEZA PARCIAL - %LEFT% residuo(s) permanecem"
    set "RC=3"
)
call :SAY "  Pacotes removidos: %REMOVED% de %FOUND%"
call :SAY "==============================================================="
call :SAY ""
call :SAY "  PROXIMOS PASSOS"
call :SAY "   1. Feche o prompt e escolha 'Continuar' para iniciar o Windows"
call :SAY "   2. O video volta pelo Microsoft Basic Display Adapter"
call :SAY "   3. Ja' no Windows, rode tools\clean_all_drivers.ps1 elevado"
call :SAY "      para limpar o que so' da' para limpar online (SCM)"
call :SAY "   4. BOOT FRIO antes do proximo bind - desligar, nao reiniciar"
call :SAY ""
if not "%LOG%"=="nul" call :SAY "  Log salvo em: %LOG%"
call :SAY ""

goto FIM

rem =============================================================================
rem  Sub-rotinas
rem =============================================================================

:TESTVOL
rem  %1 = letra. Ignora X: (WinRE) e volumes sem instalacao completa.
if /i "%~1"=="X" exit /b 0
if not exist "%~1:\Windows\System32\ntoskrnl.exe" exit /b 0
if not exist "%~1:\Windows\explorer.exe" exit /b 0
set "CANDS=%CANDS% %~1"
set /a NCAND+=1
exit /b 0

:MATCHINF
rem  %1 = nome do oemNN.inf. Marca se o conteudo mencionar o BC-250.
findstr /m /i /c:"bc250" /c:"BC-250" /c:"amdbc250" /c:"DEV_13FE" "%WINDIR_OFF%\INF\%~1" >nul 2>&1
if errorlevel 1 exit /b 0
set "OEMLIST=%OEMLIST% %~1"
set /a FOUND+=1
for /f "tokens=1,* delims==" %%a in ('findstr /i "^DeviceName" "%WINDIR_OFF%\INF\%~1" 2^>nul') do (
    call :SAY "    %~1  ->  %%b"
    goto :MATCHDONE
)
call :SAY "    %~1"
:MATCHDONE
exit /b 0

:LISTFILES
if not exist "%~1" exit /b 0
for %%f in ("%~1") do call :LISTONE "%%~ff" "%%~nxf" "%%~zf"
exit /b 0

:LISTONE
set "NM=%~2"
set "OK=0"
if /i "%NM:~0,5%"=="bc250"          set "OK=1"
if /i "%NM:~0,8%"=="amdbc250"       set "OK=1"
if /i "%NM:~0,14%"=="cyan_skillfish" set "OK=1"
if "%OK%"=="0" exit /b 0
call :SAY "  %~1  (%~3 bytes)"
set /a NFILES+=1
exit /b 0

:REMOVEDRV
call :SAY "  dism /remove-driver /driver:%~1"
dism /image:%WINVOL%:\ /remove-driver /driver:%~1 >>"%LOG%" 2>&1
if errorlevel 1 (
    call :SAY "    [FALHOU] veja o log para a saida completa do DISM"
    set /a FAILED+=1
) else (
    call :SAY "    [OK] removido"
    set /a REMOVED+=1
)
exit /b 0

:KILLFILE
if not exist "%~1" exit /b 0
rem  Guarda dura: expande o padrao e recusa qualquer nome que nao seja
rem  inequivocamente nosso. Isto existe porque "bc*.sys" casa com bcmfn2.sys,
rem  driver INBOX da Microsoft. Ver BUG 8 no historico deste arquivo.
for %%k in ("%~1") do call :KILLONE "%%~fk" "%%~nxk"
exit /b 0

:KILLONE
rem  %1 = caminho completo   %2 = nome do arquivo
set "NM=%~2"
set "OK=0"
if /i "%NM:~0,5%"=="bc250"          set "OK=1"
if /i "%NM:~0,8%"=="amdbc250"       set "OK=1"
if /i "%NM:~0,14%"=="cyan_skillfish" set "OK=1"
if "%OK%"=="0" (
    call :SAY "  [RECUSADO por seguranca] %~2 nao e' arquivo do BC-250"
    exit /b 0
)
attrib -r -s -h "%~1" >nul 2>&1
del /f /q "%~1" >nul 2>&1
if exist "%~1" (
    call :SAY "  [FALHOU] %~1"
) else (
    call :SAY "  [OK] apagado: %~1"
)
exit /b 0

:DOSET
rem  %1 = HKLM\BC250OFF\ControlSetNNN
for %%s in (bc250mcdm bc250dbg bc250v4 bc250v3 bc250v2 bc250wddm amdbc250kmd bc250compute) do call :DOSVC "%~1" "%%s"
exit /b 0

:DOSVC
reg query "%~1\Services\%~2" >nul 2>&1
if errorlevel 1 exit /b 0
if "%OPT_PURGE%"=="1" (
    reg delete "%~1\Services\%~2" /f >nul 2>&1
    if errorlevel 1 (
        call :SAY "  [FALHOU] apagar %~2"
    ) else (
        call :SAY "  [OK] chave apagada: %~2"
    )
) else (
    reg add "%~1\Services\%~2" /v Start /t REG_DWORD /d 4 /f >nul 2>&1
    if errorlevel 1 (
        call :SAY "  [FALHOU] desabilitar %~2"
    ) else (
        call :SAY "  [OK] desabilitado (Start=4): %~2"
    )
)
exit /b 0

:RECHECK
findstr /m /i /c:"bc250" /c:"BC-250" /c:"amdbc250" /c:"DEV_13FE" "%WINDIR_OFF%\INF\%~1" >nul 2>&1
if errorlevel 1 exit /b 0
set /a LEFT+=1
call :SAY "  [!] AINDA PUBLICADO: %~1"
exit /b 0

:SAY
echo(%~1
if not "%LOG%"=="nul" >>"%LOG%" echo %~1
exit /b 0

:HELP
echo.
echo  BC250_WINRE_CLEAN.cmd - limpeza offline do driver BC-250
echo.
echo  USO (no Prompt de Comando do WinRE):
echo    D:\BC250_WINRE_CLEAN.cmd              limpeza guiada, com confirmacao
echo    D:\BC250_WINRE_CLEAN.cmd /list        so' mostra o que faria
echo    D:\BC250_WINRE_CLEAN.cmd /force       sem perguntar
echo    D:\BC250_WINRE_CLEAN.cmd /win:D       forca a letra do Windows
echo    D:\BC250_WINRE_CLEAN.cmd /purge       apaga as chaves de servico
echo    D:\BC250_WINRE_CLEAN.cmd -h           esta ajuda
echo.
echo  Descobre sozinho o volume do Windows e o oemNN.inf do BC-250.
echo  Nao assume C: nem oem11.inf.
echo.
echo  Codigos de saida: 0 limpo  1 erro/abortado  2 nada encontrado  3 parcial
echo.
exit /b 0

:FIM
endlocal & exit /b %RC%
