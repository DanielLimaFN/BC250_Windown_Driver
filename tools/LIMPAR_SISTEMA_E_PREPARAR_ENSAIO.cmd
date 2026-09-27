@echo off
setlocal
cd /d "%~dp0"
echo ===================================================================
echo   BC-250: HIGIENE DE SISTEMA E PREPARACAO DO AMBIENTE DE ENSAIO
echo   Target: AMD BC-250 APU (1002:13FE) ^| Windows 10 22H2 x64
echo ===================================================================
echo.

echo 1. Removendo vulkan_radeon do registro global Khronos (evita crash do LogonUI)...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Remove-ItemProperty -Path 'HKLM:\SOFTWARE\Khronos\Vulkan\Drivers' -Name 'C:\Windows\system32\vulkan_radeon.json' -ErrorAction SilentlyContinue; Remove-ItemProperty -Path 'HKLM:\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers' -Name 'C:\Windows\system32\vulkan_radeon.json' -ErrorAction SilentlyContinue; if (Test-Path 'C:\Windows\System32\vulkan_radeon.dll') { Remove-Item 'C:\Windows\System32\vulkan_radeon.dll' -Force -ErrorAction SilentlyContinue }; Write-Host '  -> Registro Khronos e System32 limpos com sucesso.' -ForegroundColor Green"

echo.
echo 2. Assegurando vulkan_radeon.dll em C:\BC250_LIB...
if not exist "C:\BC250_LIB" mkdir "C:\BC250_LIB" >nul
copy /Y "C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll" "C:\BC250_LIB\vulkan_radeon.dll" >nul
echo [OK] vulkan_radeon.dll presente em C:\BC250_LIB.

echo.
echo 3. Executando BC250_RECOVERY.cmd para limpar Driver Store e garantir BasicDisplay...
call "%~dp0BC250_RECOVERY.cmd"
if %ERRORLEVEL% neq 0 (
    echo [ERRO] BC250_RECOVERY falhou com exit %ERRORLEVEL%.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo 4. Resetando fusivel de boot C:\BC250_BOOT_FUSE.dat para CLEAN (0)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0inspect-boot-fuse.ps1" -Reset
if %ERRORLEVEL% neq 0 (
    echo [ERRO] Falha ao resetar fusivel. Certifique-se de executar como Administrador!
    pause
    exit /b 1
)

echo.
echo ===================================================================
echo   AMBIENTE HIGIENIZADO COM SUCESSO!
echo   O display esta seguro no Microsoft BasicDisplay (Codigo 0).
echo   Voce agora pode executar tools\EXECUTAR_D3D11_NATIVO.cmd diretamente!
echo ===================================================================
pause
exit /b 0
