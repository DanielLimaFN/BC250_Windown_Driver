@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo   Compilando ADL Bridges (atiadlxx.dll x64 e atiadlxy.dll x86)
echo ========================================================

set "SRCDIR=%~dp0"
set "BINDIR=%~dp0..\build"
set "PKGDIR=%~dp0..\package"

if not exist "%BINDIR%" mkdir "%BINDIR%"
if not exist "%PKGDIR%" mkdir "%PKGDIR%"

set "VS_DIR="
for %%D in (C D E F G) do (
    for %%E in (Enterprise Professional Community BuildTools) do (
        if exist "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
            set "VS_DIR=%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build"
            goto :found_vs
        )
    )
)

echo ERRO: Visual Studio 2022 nao encontrado.
exit /b 1

:found_vs

:: --------------------------------------------------------------------------
:: 1. Compila atiadlxx.dll (64-bit x64)
:: --------------------------------------------------------------------------
echo.
echo [1/2] Compilando atiadlxx.dll (x64)...
call "%VS_DIR%\vcvars64.bat" >nul 2>&1
cl /nologo /O2 /MD /W3 /I"%SRCDIR%." "%SRCDIR%atiadlxx.c" /link /DLL /MACHINE:X64 /DEF:"%SRCDIR%atiadlxx.def" /OUT:"%BINDIR%\atiadlxx.dll" gdi32.lib user32.lib
if errorlevel 1 (
    echo ERRO ao compilar atiadlxx.dll x64
    exit /b 1
)
copy /Y "%BINDIR%\atiadlxx.dll" "%PKGDIR%\atiadlxx.dll" >nul

:: --------------------------------------------------------------------------
:: 2. Compila atiadlxy.dll (32-bit x86 para GPU-Z 32-bit)
:: --------------------------------------------------------------------------
echo.
echo [2/2] Compilando atiadlxy.dll (x86 32-bit para GPU-Z)...
call "%VS_DIR%\vcvars32.bat" >nul 2>&1
cl /nologo /O2 /MD /W3 /I"%SRCDIR%." "%SRCDIR%atiadlxx.c" /link /DLL /MACHINE:X86 /DEF:"%SRCDIR%atiadlxx.def" /OUT:"%BINDIR%\atiadlxy.dll" gdi32.lib user32.lib
if errorlevel 1 (
    echo ERRO ao compilar atiadlxy.dll x86
    exit /b 1
)
copy /Y "%BINDIR%\atiadlxy.dll" "%PKGDIR%\atiadlxy.dll" >nul

:: Também cria cópia atiadlxx.dll 32-bit como fallback
copy /Y "%BINDIR%\atiadlxy.dll" "%BINDIR%\atiadlxx_x86.dll" >nul

echo.
echo ========================================================
echo   AMBAS AS DLLs FORAM COMPILADAS COM SUCESSO!
echo   64-bit: %PKGDIR%\atiadlxx.dll
echo   32-bit: %PKGDIR%\atiadlxy.dll (O GPU-Z usa esta!)
echo ========================================================
exit /b 0
