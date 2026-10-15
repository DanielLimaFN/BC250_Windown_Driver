@echo off
setlocal
cd /d "%~dp0"
echo ==========================================================
echo   DISPARANDO ESTRESSE SINTETICO DE VRAM BC-250 (500 MB)
echo   Alvo: 500 MB em blocos de 16 MB x 50 ciclos x 100 ms
echo ==========================================================
"%~dp0bc250-vram-paging-stress.exe" 500 50 100
echo.
echo Estresse concluido.
pause
