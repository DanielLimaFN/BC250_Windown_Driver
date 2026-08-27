@echo off
cd /d "%~dp0"
echo Iniciando servidor de desenvolvimento da interface BC-250...
start http://localhost:5173
npm run dev
