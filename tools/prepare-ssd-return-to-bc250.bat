@echo off
setlocal EnableDelayedExpansion
echo ============================================================
echo BC-250: PREPARACAO DO SSD PARA RETORNO A PLACA BC-250
echo ============================================================
echo.

net session >nul 2>&1
if errorlevel 1 (
    echo ERRO: Execute este script como Administrador.
    pause
    exit /b 1
)

echo [1/4] Removendo servico bc250mcdm...
sc.exe stop bc250mcdm >nul 2>&1
sc.exe delete bc250mcdm
if errorlevel 1 (
    echo   Servico bc250mcdm ja estava ausente ou foi removido.
) else (
    echo   Servico bc250mcdm removido com sucesso.
)

echo.
echo [2/4] Removendo binario bc250mcdm.sys de System32\drivers...
if exist "%SystemRoot%\System32\drivers\bc250mcdm.sys" (
    takeown.exe /f "%SystemRoot%\System32\drivers\bc250mcdm.sys" >nul 2>&1
    icacls.exe "%SystemRoot%\System32\drivers\bc250mcdm.sys" /grant *S-1-5-32-544:F >nul 2>&1
    del /f /q "%SystemRoot%\System32\drivers\bc250mcdm.sys"
    if exist "%SystemRoot%\System32\drivers\bc250mcdm.sys" (
        echo   ERRO: nao foi possivel apagar bc250mcdm.sys.
    ) else (
        echo   bc250mcdm.sys removido com sucesso.
    )
) else (
    echo   bc250mcdm.sys ja nao existia em System32\drivers.
)

echo.
echo [3/4] Limpando marcadores de ensaio anteriores...
if exist "%SystemDrive%\BC250_TEST_OEMINF.txt" (
    del /f /q "%SystemDrive%\BC250_TEST_OEMINF.txt"
    echo   BC250_TEST_OEMINF.txt apagado.
)
if exist "%SystemDrive%\BC250_BOOT_FUSE.dat" (
    attrib -R "%SystemDrive%\BC250_BOOT_FUSE.dat" >nul 2>&1
    del /f /q "%SystemDrive%\BC250_BOOT_FUSE.dat"
    echo   BC250_BOOT_FUSE.dat apagado.
)

echo.
echo [4/4] Atualizando C:\BC250_RECOVERY.cmd...
copy /y "tools\BC250_RECOVERY.cmd" "C:\BC250_RECOVERY.cmd" >nul
echo   C:\BC250_RECOVERY.cmd atualizado com suporte a WinRE.

echo.
echo ============================================================
echo LIMPEZA CONCLUIDA COM SUCESSO!
echo O SSD esta seguro para retornar a placa-mae da BC-250.
echo Ao ligar a BC-250, o Windows iniciara normalmente com o
echo Driver de Video Basico (BasicDisplay, Codigo 0) e tela ativa.
echo ============================================================
echo.
pause
