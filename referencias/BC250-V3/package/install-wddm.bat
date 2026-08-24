@echo off
echo ========================================================
echo   Instalando Driver WDDM 2.0+ Nativo AMD BC-250 (V3)...
echo ========================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-wddm.ps1" %*
set "INSTALL_RESULT=%errorlevel%"
echo.
echo Resultado do instalador: %INSTALL_RESULT% (BuildId esperado: 8220859)
pause
exit /b %INSTALL_RESULT%
