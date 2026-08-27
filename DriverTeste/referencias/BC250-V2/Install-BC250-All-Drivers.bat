@echo off
setlocal
cd /d "%~dp0"
echo ========================================================
echo   AMD BC-250: INSTALADOR VULKAN 1.2, OPENGL 4.6 & OPENAL
echo ========================================================
echo.
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [SOLICITANDO ELEVACAO DE ADMINISTRADOR...]
    powershell -Command "Start-Process cmd -ArgumentList '/k \"\"%~f0\"\"' -Verb RunAs"
    exit /b
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-BC250-All-Drivers.ps1"
pause
