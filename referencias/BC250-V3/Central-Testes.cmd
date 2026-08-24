@echo off
setlocal enabledelayedexpansion
title Central de Testes - AMD BC-250 (BC250-V3 WDDM 2.0+)
color 0B

set "ROOT=%~dp0"
set "BIN=%ROOT%tests\bin\"

:menu
cls
echo ===============================================================================
echo            CENTRAL DE TESTES E VALIDACAO - AMD BC-250 (BC250-V3)
echo                   Driver Nativo WDDM 2.0+ de Silicio Cru
echo ===============================================================================
echo.
echo   [ GERENCIAMENTO DO DRIVER ]
echo   1. Instalar / Atualizar Driver BC250-V3 no Windows (Elevado)
echo   2. Diagnostico PnP, VidPN e Telemetria do Sistema (diag.ps1)
echo   3. Recompilar Driver e Suite de Testes
echo.
echo   [ TESTES DE SILICIO E HARDWARE ]
echo   4. Teste 1: Hardware Bring-up, BARs, Aneis e Shaders
echo   5. Teste 2: Telemetria SMU (Temperaturas, Tensoes, Clocks e Potencia)
echo   6. Teste 2.1: Monitor Termico em Tempo Real (Loop continuo)
echo   7. Teste 3: Leitura e Escrita Direta na GDDR6 (VRAM R/W)
echo   8. Teste 4: Engine DMA Fisica (SDMA0 Copy & Diagnostico CONST_FILL)
echo   9. Teste 5: Command Processor PM4 Ring 0 (Submissao de Pacotes e WPTR/RPTR)
echo   10. Teste 6: Disparo dos 1536 Stream Processors (GPGPU Compute Dispatch)
echo   11. Teste 7: Gerenciador de Energia, Power Limit e Overclock (SMU Clocks/TDP)
echo   12. Teste 8: Camada OpenCL (Verificacao de falha honesta e VRAM BAR)
echo   13. Instalar/Registrar OpenCL 2.0 Global no Windows (GPU-Z, Blender, IA)
echo.
echo   [ AUTOMACAO ]
echo   A. Executar Suite Completa de Validacao (11 Testes + Matriz Consolidada)
echo   0. Sair
echo.
echo ===============================================================================
set /p op="Selecione uma opcao [0-13, A]: "

if "%op%"=="1" goto :install_driver
if "%op%"=="2" goto :run_diag
if "%op%"=="3" goto :rebuild_all
if "%op%"=="4" goto :test1
if "%op%"=="5" goto :test2
if "%op%"=="6" goto :test2_loop
if "%op%"=="7" goto :test3
if "%op%"=="8" goto :test4
if "%op%"=="9" goto :test5
if "%op%"=="10" goto :test6
if "%op%"=="11" goto :test7
if "%op%"=="12" goto :test8
if "%op%"=="13" goto :install_opencl_global
if /i "%op%"=="A" goto :test_all
if "%op%"=="0" goto :exit

echo Opcao invalida.
timeout /t 2 >nul
goto :menu

:install_driver
cls
echo ===============================================================================
echo   Instalando Driver BC250-V3 (WDDM 2.0+)...
echo ===============================================================================
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Solicitando elevacao de Administrador para instalacao...
    powershell -NoProfile -Command "Start-Process -FilePath powershell.exe -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File \"%ROOT%package\install-wddm.ps1\"' -Verb RunAs -Wait"
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%package\install-wddm.ps1"
)
echo.
pause
goto :menu

:run_diag
cls
echo ===============================================================================
echo   Executando Diagnostico PnP e Telemetria do Sistema...
echo ===============================================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%diag.ps1"
echo.
pause
goto :menu

:rebuild_all
cls
echo ===============================================================================
echo   Recompilando Driver BC250-V3 e Testes...
echo ===============================================================================
call "%ROOT%build.bat"
if errorlevel 1 (
  echo ERRO no build do driver.
  pause
  goto :menu
)
call "%ROOT%tests\build_tests.bat"
echo.
pause
goto :menu

:test1
cls
if not exist "%BIN%test_hw_info.exe" call "%ROOT%tests\build_tests.bat"
"%BIN%test_hw_info.exe"
echo.
pause
goto :menu

:test2
cls
if not exist "%BIN%test_telemetry.exe" call "%ROOT%tests\build_tests.bat"
"%BIN%test_telemetry.exe"
echo.
pause
goto :menu

:test2_loop
cls
if not exist "%BIN%test_telemetry.exe" call "%ROOT%tests\build_tests.bat"
"%BIN%test_telemetry.exe" --loop
echo.
pause
goto :menu

:test3
cls
if not exist "%BIN%test_vram_rw.exe" call "%ROOT%tests\build_tests.bat"
"%BIN%test_vram_rw.exe"
echo.
pause
goto :menu

:test4
cls
if not exist "%BIN%test_sdma.exe" call "%ROOT%tests\build_tests.bat"
"%BIN%test_sdma.exe"
echo.
pause
goto :menu

:test5
cls
if not exist "%BIN%test_pm4_ring.exe" call "%ROOT%tests\build_tests.bat"
"%BIN%test_pm4_ring.exe"
echo.
pause
goto :menu

:test6
cls
if not exist "%BIN%test_compute.exe" call "%ROOT%tests\build_tests.bat"
"%BIN%test_compute.exe"
echo.
pause
goto :menu

:test7
cls
if not exist "%BIN%test_power_profile.exe" call "%ROOT%tests\build_tests.bat"
"%BIN%test_power_profile.exe"
echo.
pause
goto :menu

:test8
cls
if not exist "%BIN%test_opencl.exe" call "%ROOT%tests\build_tests.bat"
"%BIN%test_opencl.exe"
echo.
pause
goto :menu

:test_all
cls
echo ===============================================================================
echo   Suite Completa de Validacao de Silicio (AMD BC-250 / BC250-V3)
echo ===============================================================================
echo.
call "%ROOT%tests\run_suite.cmd"
echo.
pause
goto :menu

:install_opencl_global
cls
echo ===============================================================================
echo   Instalando e Registrando OpenCL 2.0 Globalmente no Windows...
echo ===============================================================================
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Solicitando elevacao de Administrador...
    powershell -NoProfile -Command "Start-Process -FilePath powershell.exe -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File \"%ROOT%opencl\install_opencl.ps1\"' -Verb RunAs -Wait"
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%opencl\install_opencl.ps1"
)
echo.
pause
goto :menu

:exit
cls
exit /b 0
