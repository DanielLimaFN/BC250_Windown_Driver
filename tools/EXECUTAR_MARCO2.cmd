@echo off
echo ========================================================
echo   BC-250: RESET DE FUSIVEL E ENSAIO MARCO 2 (RADV PRESENT)
echo ========================================================
echo.
echo 1. Resetando fusivel de boot C:\BC250_BOOT_FUSE.dat...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0inspect-boot-fuse.ps1" -Reset
if %ERRORLEVEL% neq 0 (
    echo [ERRO] Falha ao resetar o fusivel. Certifique-se de executar como Administrador!
    pause
    exit /b 1
)
echo.
echo 2. Iniciando vinculacao WDDM de laboratorio com ensaio do Marco 2...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bind-bc250mcdm-lab.ps1" -Execute -IAcceptBlackScreenRisk -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunRadvPresentTripod -WatchdogTimeoutSeconds 180
echo.
echo Concluido. Pressione qualquer tecla para sair...
pause
