@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo   Compilando Diagnostic Tool: test_temperature.exe
echo ========================================================

set "SRCDIR=%~dp0"
set "BINDIR=%~dp0..\build"
set "PKGDIR=%~dp0..\package"

if not exist "%BINDIR%" mkdir "%BINDIR%"
if not exist "%PKGDIR%" mkdir "%PKGDIR%"

where cl.exe >nul 2>&1
if errorlevel 1 (
    for %%D in (C D E F G) do (
        for %%E in (Enterprise Professional Community BuildTools) do (
            if exist "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
                call "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
                goto :found_vs
            )
        )
    )
    echo ERRO: Visual Studio 2022 nao encontrado.
    exit /b 1
)

:found_vs
cl /nologo /O2 /W3 /I"%SRCDIR%." "%SRCDIR%test_temperature.c" /link /OUT:"%BINDIR%\test_temperature.exe" gdi32.lib user32.lib
if errorlevel 1 (
    echo ERRO na compilacao de test_temperature.exe
    exit /b 1
)

copy /Y "%BINDIR%\test_temperature.exe" "%PKGDIR%\test_temperature.exe" >nul

echo.
echo ========================================================
echo   test_temperature.exe COMPILADO COM SUCESSO!
echo   Binario gerado em: %BINDIR%\test_temperature.exe
echo   Copiado para:      %PKGDIR%\test_temperature.exe
echo ========================================================
exit /b 0
