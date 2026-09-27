@echo off
cd /d "%~dp0"
if exist "dist\BC-250-Adrenaline-win32-x64\BC-250-Adrenaline.exe" (
  start "" "dist\BC-250-Adrenaline-win32-x64\BC-250-Adrenaline.exe"
  exit /b
)
if exist "node_modules\electron\dist\electron.exe" (
  start "" "node_modules\electron\dist\electron.exe" .
  exit /b
)
echo Execute npm ci nesta pasta para instalar as dependencias.
pause
