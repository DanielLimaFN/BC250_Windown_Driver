@echo off
setlocal
cd /d "%~dp0\.."

echo ==========================================================================
echo   AMD BC-250: TESTE DA ETAPA 1 (MEC1 / HQD COMPUTE QUEUE)
echo ==========================================================================
echo.

echo [1/2] Reiniciando servico bc250compute com novo binario assinado...
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\start-bc250compute-service.ps1"
if errorlevel 1 (
    echo [!] Erro ao iniciar servico bc250compute.
    pause
    exit /b 1
)

echo.
echo [2/2] Executando teste fisico de SDMA e MEC1/HQD...
driver\wdk\output\bc250-compute-cli.exe

echo.
echo ==========================================================================
