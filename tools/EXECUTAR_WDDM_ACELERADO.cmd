@echo off
echo ========================================================
echo   BC-250: WDDM ACELERADO POR GPU (DWM + SHADER BRIDGE + SDMA)
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
echo 2. Iniciando vinculacao WDDM com DWM acelerado por GPU...
echo    - RenderNode: ATIVO (D3D10/11)
echo    - MEC/Node 1: ATIVO (Persistent Submission)
echo    - Shaders: ATIVO (TokenStream -> DXBC -> SPIR-V via vkd3d-shader)
echo    - Composicao: ATIVO (Sub-rect Blt 1:1 no SDMA0 da GPU)
echo    - Watchdog: 180s com recuperacao automatica
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bind-bc250mcdm-lab.ps1" -Execute -IAcceptBlackScreenRisk -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -EnableNode1JournalGatePerProcess -EnableD3D11LabPromotion -EnableD3D11DrawBltSubstitution -EnableD3D11ShaderTranslator -D3D11ShaderTranslatorDllPath "C:\BC250_LIB\libvkd3d-shader-1.dll" -WatchdogTimeoutSeconds 180 -IAcceptAutomaticRecoveryReboot
set "BIND_EXIT=%ERRORLEVEL%"
echo.
if %BIND_EXIT% equ 0 (
    echo ========================================================
    echo   DRIVER WDDM ACELERADO VINCULADO COM SUCESSO!
    echo   Area de trabalho (DWM) ativa na GPU BC-250.
    echo   Watchdog ativo em segundo plano (timeout 180s).
    echo ========================================================
    echo.
    echo Janela de observacao do Desktop iniciada (60 segundos).
    echo Pressione qualquer tecla para encerrar o ensaio e restaurar...
    timeout /t 60
    echo.
)

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
