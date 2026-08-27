@echo off
cd /d "%~dp0app"
echo ========================================================
echo   BC-250 Advanced Control - Painel de Gestao e Driver
echo ========================================================
echo Iniciando aplicativo nativo Windows (.exe)...
if not exist "%~dp0app\BC250Studio.exe" (
    call npm run package
)
start "" "%~dp0app\BC250Studio.exe"
