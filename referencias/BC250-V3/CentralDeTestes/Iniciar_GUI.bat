@echo off
title AMD BC-250 — Central de Testes (GUI)
set "ROOT=%~dp0"

echo ===============================================================================
echo   Iniciando Central de Testes AMD BC-250 (GUI)...
echo ===============================================================================

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%gui\CentralDeTestes_WPF.ps1"

if %errorlevel% neq 0 (
    echo.
    echo [-] A interface grafica foi encerrada com codigo de erro %errorlevel%.
    pause
)
