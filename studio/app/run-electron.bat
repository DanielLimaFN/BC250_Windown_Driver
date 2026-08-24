@echo off
cd /d "%~dp0"
echo Iniciando Painel BC-250 em modo Desktop Nativo (Electron)...
npx electron electron/main.cjs
