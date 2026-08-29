@echo off
setlocal
cd /d "%~dp0\.."

fltmc.exe >nul 2>&1
if errorlevel 1 (
    echo Solicitando privilegios de Administrador...
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell.exe -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File ""%~dp0run-lab-installation-and-test.ps1"" -Execute -IAcceptTheRisk' -Verb RunAs"
    exit /b
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-lab-installation-and-test.ps1" -Execute -IAcceptTheRisk
pause
