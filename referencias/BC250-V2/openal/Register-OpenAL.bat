@echo off
title AMD BC-250 OpenAL Installer
:: Request admin elevation
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo Solicitando privilegios de Administrador...
    powershell -Command "Start-Process cmd -ArgumentList '/c \"\"%~dp0Register-OpenAL.bat\"\"' -Verb RunAs"
    exit /b
)

pushd "%~dp0"
powershell -ExecutionPolicy Bypass -File "%~dp0Install-OpenAL.ps1"
popd
pause
