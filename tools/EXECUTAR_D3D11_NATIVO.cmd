@echo off
setlocal
cd /d "%~dp0"
set "FORCE_BOOT_CHECK=0"
set "DWM_CAPTURE_ARG="
for %%A in (%*) do (
    if /i "%%~A"=="-Force" set "FORCE_BOOT_CHECK=1"
    if /i "%%~A"=="-CaptureDwmDump" set "DWM_CAPTURE_ARG=-EnableDwmCrashCapture"
)
echo ===================================================================
echo   BC-250: ENSAIO DE CONFORMIDADE DIRECT3D 11 NATIVO (bc250umd.dll)
echo   Target: AMD BC-250 APU (1002:13FE) ^| Windows 10 22H2 x64
echo ===================================================================
echo.
echo 0. Verificando se o rollback anterior ja foi seguido por boot frio...
if "%FORCE_BOOT_CHECK%"=="1" (
    echo [FORCE] Marcador de boot frio ignorado por solicitacao do operador.
    del /f /q C:\BC250_REQUIRE_CLEAN_BOOT.json 2>nul
    goto skip_boot_check
)
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
:skip_boot_check
echo [OK] Preflight de boot frio aprovado.
echo.
echo 1. Resetando fusivel de boot C:\BC250_BOOT_FUSE.dat...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0inspect-boot-fuse.ps1" -Reset
if %ERRORLEVEL% neq 0 (
    echo [ERRO] Falha ao resetar o fusivel. Certifique-se de executar como Administrador!
    pause
    exit /b 1
)
echo 1b. Isolando vulkan_radeon.dll em C:\BC250_LIB (sem contaminar System32 ou LogonUI)...
if not exist "C:\BC250_LIB" mkdir "C:\BC250_LIB" >nul
copy /Y "C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll" "C:\BC250_LIB\vulkan_radeon.dll" >nul
powershell -NoProfile -ExecutionPolicy Bypass -Command "Remove-ItemProperty -Path 'HKLM:\SOFTWARE\Khronos\Vulkan\Drivers' -Name 'C:\Windows\system32\vulkan_radeon.json' -ErrorAction SilentlyContinue; Remove-ItemProperty -Path 'HKLM:\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers' -Name 'C:\Windows\system32\vulkan_radeon.json' -ErrorAction SilentlyContinue; if (Test-Path 'C:\Windows\System32\vulkan_radeon.dll') { Remove-Item 'C:\Windows\System32\vulkan_radeon.dll' -Force -ErrorAction SilentlyContinue }"
echo [OK] vulkan_radeon.dll atualizada exclusivamente em C:\BC250_LIB.
echo.
echo 2. Iniciando vinculacao WDDM com ensaio D3D11 Nativo (bc250umd.dll)...
if defined DWM_CAPTURE_ARG echo [ATENCAO] WER LocalDumps do dwm.exe sera armado somente durante esta janela.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bind-bc250mcdm-lab.ps1" -Execute -IAcceptBlackScreenRisk -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -EnableNode1JournalGatePerProcess -EnableD3D11LabPromotion -RunD3D11Native -D3D11Frames 60 -WatchdogTimeoutSeconds 120 -IAcceptAutomaticRecoveryReboot %DWM_CAPTURE_ARG%
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
powershell -NoProfile -ExecutionPolicy Bypass -Command "if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Stop-Process -Name sihost -Force -ErrorAction SilentlyContinue; Start-Process explorer.exe }"
echo.
echo ===================================================================
echo   RESUMO DO ENSAIO D3D11 EXTRAIDO DA EVIDENCIA
echo ===================================================================
powershell -NoProfile -ExecutionPolicy Bypass -Command "$last=Join-Path '%~dp0' '_last_lab_binding.json'; if (Test-Path $last) { $j=Get-Content $last -Raw | ConvertFrom-Json; $d3dLog=Join-Path $j.evidence_dir 'd3d11-native-conformance-console.txt'; if (Test-Path $d3dLog) { Get-Content $d3dLog | ForEach-Object { Write-Host ('  ' + $_) -ForegroundColor Green } } $umdLog=Join-Path $j.evidence_dir 'bc250_umd_log-FINAL.txt'; if (Test-Path $umdLog) { Select-String -Path $umdLog -Pattern 'DRAW EXECUTADO|OpenAdapter10|CreateDevice|INSPECAO LINEAR_BUF' | Select-Object -First 20 | ForEach-Object { Write-Host ('  ' + $_.Line.Trim()) -ForegroundColor Cyan } } }"
echo ===================================================================
echo.
echo ===================================================================
echo   ENSAIO FINALIZADO COM EXIT CODE: %BIND_EXIT%
echo ===================================================================
pause
exit /b %BIND_EXIT%
