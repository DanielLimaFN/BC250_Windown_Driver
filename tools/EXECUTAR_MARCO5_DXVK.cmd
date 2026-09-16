@echo off
echo ========================================================
echo   BC-250: RESET DE FUSIVEL E ENSAIO MARCO 5 (DIRECTX 11 VIA DXVK)
echo ========================================================
echo.
echo 0. Verificando se o rollback anterior ja foi seguido por boot frio...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$marker='C:\BC250_REQUIRE_CLEAN_BOOT.json'; if (-not (Test-Path -LiteralPath $marker)) { exit 0 }; $m=Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json; $boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks; if ([int64]$m.rollback_boot_ticks -eq [int64]$boot) { exit 42 }; exit 0"
if %ERRORLEVEL% equ 42 (
    echo [RECUSADO] O rollback anterior exige BOOT FRIO antes de outro binding.
    echo Desligue completamente a maquina, ligue novamente e execute este arquivo outra vez.
    echo O fusivel nao foi alterado e o adaptador nao foi vinculado.
    pause
    exit /b 42
)
if %ERRORLEVEL% neq 0 (
    echo [ERRO] Nao foi possivel verificar o marcador de boot limpo.
    pause
    exit /b 1
)
echo [OK] Preflight de boot frio aprovado.
echo.
echo 1. Resetando fusivel de boot C:\BC250_BOOT_FUSE.dat...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0inspect-boot-fuse.ps1" -Reset
if %ERRORLEVEL% neq 0 (
    echo [ERRO] Falha ao resetar o fusivel. Certifique-se de executar como Administrador!
    pause
    exit /b 1
)
echo.
echo 1b. Atualizando vulkan_radeon.dll em System32 para o build com WSI nativo...
copy /Y "C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll" "%SystemRoot%\System32\vulkan_radeon.dll" >nul
if %ERRORLEVEL% neq 0 (
    echo [AVISO] Nao foi possivel atualizar vulkan_radeon.dll em System32.
) else (
    echo [OK] vulkan_radeon.dll atualizada com sucesso em System32.
)
echo.
echo 2. Iniciando vinculacao WDDM de laboratorio com ensaio do Marco 5 (DirectX 11 via DXVK)...
echo.
echo ========================================================
echo   DEMO DIRECTX 11 NATIVA VIA DXVK (D3D11Triangle):
echo     - Janela D3D11 sendo traduzida para Vulkan SPIR-V em tempo real
echo     - Renderizacao nas 24 CUs via Mesa RADV + ACO
echo     - Apresentacao no DisplayPort via SDMA HW Blt
echo     - Pressione ESC a qualquer momento para sair com exito
echo ========================================================
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bind-bc250mcdm-lab.ps1" -Execute -IAcceptBlackScreenRisk -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -EnableNode1JournalGatePerProcess -EnableD3D11LabPromotion -RunDxvkD3D11 -DxvkFrames 1800 -WatchdogTimeoutSeconds 300 -IAcceptAutomaticRecoveryReboot
set "BIND_EXIT=%ERRORLEVEL%"
echo.
echo 3. Executando rollback incondicional do ensaio automatizado...
call "%~dp0BC250_RECOVERY.cmd"
set "RECOVERY_EXIT=%ERRORLEVEL%"
if not "%RECOVERY_EXIT%"=="0" (
    echo [ERRO] Rollback incompleto, exit=%RECOVERY_EXIT%. Nao execute outro ensaio.
    pause
    exit /b %RECOVERY_EXIT%
)
powershell -NoProfile -ExecutionPolicy Bypass -Command "$m=[ordered]@{state='CLEAN_BOOT_REQUIRED';rollback_boot_ticks=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks;rollback_completed_at=(Get-Date).ToString('o');restored_service='BasicDisplay';restored_code=0;hardware_validated=$false}; $m | ConvertTo-Json | Set-Content -LiteralPath 'C:\BC250_REQUIRE_CLEAN_BOOT.json' -Encoding UTF8"
if %ERRORLEVEL% neq 0 (
    echo [ERRO] BasicDisplay voltou, mas o marcador de boot frio nao foi gravado.
    pause
    exit /b 1
)
echo [OK] BasicDisplay Codigo 0 restaurado. Boot frio obrigatorio antes de outro ensaio.
if not "%BIND_EXIT%"=="0" echo [AVISO] O ensaio terminou com exit=%BIND_EXIT%; a recuperacao foi concluida.
echo.
echo Concluido. Pressione qualquer tecla para sair...
pause
exit /b %BIND_EXIT%
