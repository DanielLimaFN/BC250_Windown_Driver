@echo off
setlocal
title BC250 WinDV - Control Center

echo =========================================================================
echo       BC250 WinDV - GESTAO DO DRIVER BC-250 (GC 10.1.3)
echo =========================================================================
echo.

cd /d "%~dp0"

:: Compilar Launcher caso o executavel nao exista
if not exist "BC250_Adrenalin.exe" (
    echo [*] Compilando host desktop nativo com System Tray...
    "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:winexe /win32icon:logo.ico /out:BC250_Adrenalin.exe /reference:System.dll,System.Windows.Forms.dll,System.Drawing.dll BC250_Adrenalin_Host.cs
)

echo [*] Iniciando BC-250 Adrenalin...
start "" "BC250_Adrenalin.exe"

exit /b 0
