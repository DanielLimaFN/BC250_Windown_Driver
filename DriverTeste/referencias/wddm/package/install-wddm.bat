@echo off
echo ========================================================
echo   Instalando Driver WDDM Nativo AMD BC-250...
echo ========================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-wddm.ps1" %*
set "INSTALL_RESULT=%errorlevel%"
echo.
echo Resultado do instalador: %INSTALL_RESULT% (BuildId esperado: 8211616)
pause
exit /b %INSTALL_RESULT%
