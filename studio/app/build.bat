@echo off
setlocal enabledelayedexpansion
echo =======================================================
echo Compilando BC-250 Control App Nativo (Windows 11 Fluent)
echo =======================================================

taskkill /F /IM BC250Control.exe >nul 2>&1

set "CSC=C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\Roslyn\csc.exe"
if not exist "%CSC%" set "CSC=C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"

set "NET_DIR=C:\Windows\Microsoft.NET\Framework64\v4.0.30319"

if not exist "%~dp0bin" mkdir "%~dp0bin"

set REFS=/r:"%NET_DIR%\System.dll" /r:"%NET_DIR%\System.Core.dll" /r:"%NET_DIR%\System.Xaml.dll" /r:"%NET_DIR%\WPF\WindowsBase.dll" /r:"%NET_DIR%\WPF\PresentationCore.dll" /r:"%NET_DIR%\WPF\PresentationFramework.dll"

set "SRCS="
for /r "%~dp0src" %%f in (*.cs) do (
    set "SRCS=!SRCS! "%%f""
)

"%CSC%" /target:winexe /optimize+ /platform:x64 /out:"%~dp0bin\BC250Control.exe" %REFS% !SRCS!

if %ERRORLEVEL% equ 0 (
    echo.
    echo =======================================================
    echo [SUCESSO] Aplicativo compilado: "%~dp0bin\BC250Control.exe"
    echo =======================================================
) else (
    echo.
    echo =======================================================
    echo [ERRO] Falha na compilacao.
    echo =======================================================
    exit /b %ERRORLEVEL%
)
