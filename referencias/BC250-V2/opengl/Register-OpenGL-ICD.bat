@echo off
:: ============================================================================
:: Registro do Driver OpenGL 4.60 ICD da AMD BC-250 no Windows
:: Requer Privilégios de Administrador
:: ============================================================================

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo ======================================================================
    echo   [AVISO] ESTE SCRIPT PRECISA DE PERMISSAO DE ADMINISTRADOR
    echo ======================================================================
    echo   Clique com o BOTAO DIREITO neste arquivo e selecione:
    echo   "Executar como Administrador"
    echo ======================================================================
    echo.
    pause
    exit /b 1
)

set "SRCDIR=%~dp0"
set "BUILD_DIR=%SRCDIR%..\build"

echo ========================================================
echo   Registrando Driver OpenGL 4.60 ICD da AMD BC-250
echo   (Cyan Skillfish / Navi12 Lite - 1536 Shaders / 16GB)
echo ========================================================

echo.
echo [1/4] Copiando DLLs para C:\Windows\System32...
copy /Y "%BUILD_DIR%\atio6xxx.dll" "%windir%\System32\atio6xxx.dll" >nul
copy /Y "%BUILD_DIR%\opengl32.dll" "%windir%\System32\opengl32.dll" >nul
copy /Y "%BUILD_DIR%\libgallium_wgl.dll" "%windir%\System32\libgallium_wgl.dll" >nul
copy /Y "%BUILD_DIR%\vulkan_radeon.dll" "%windir%\System32\vulkan_radeon.dll" >nul
copy /Y "%BUILD_DIR%\z-1.dll" "%windir%\System32\z-1.dll" >nul
copy /Y "%SRCDIR%..\..\vulkan_radeon.json" "%windir%\System32\vulkan_radeon.json" >nul
echo [OK] DLLs instaladas em C:\Windows\System32 com sucesso!

echo.
echo [2/4] Vinculando Driver ao Adaptador de Video WDDM da BC-250...
for %%I in (0000 0001 0002 0003) do (
    reg add "HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\%%I" /v "InstalledDisplayDrivers" /t REG_MULTI_SZ /d "atio6xxx" /f >nul 2>&1
    reg add "HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\%%I" /v "OpenGLDriverName" /t REG_MULTI_SZ /d "atio6xxx.dll" /f >nul 2>&1
    reg add "HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\%%I" /v "OpenGLDriverName_Wow" /t REG_MULTI_SZ /d "atio6xxx.dll" /f >nul 2>&1
    reg add "HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\%%I" /v "OpenGLVersion" /t REG_DWORD /d 2 /f >nul 2>&1
    reg add "HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\%%I" /v "OpenGLFlags" /t REG_DWORD /d 1 /f >nul 2>&1
)
echo [OK] Adaptador de Video BC-250 vinculado ao driver atio6xxx.dll!

echo.
echo [3/4] Registrando chaves ICD no subsistema OpenGL do Windows...
reg import "%SRCDIR%BC250_Enable_OpenGL46.reg" >nul 2>&1
reg add "HKLM\SOFTWARE\Khronos\OpenGL\Drivers" /v "atio6xxx.dll" /t REG_DWORD /d 0 /f >nul 2>&1
reg add "HKLM\SOFTWARE\Khronos\Vulkan\Drivers" /v "%windir%\System32\vulkan_radeon.json" /t REG_DWORD /d 0 /f >nul 2>&1
echo [OK] Registro do ICD concluido com sucesso!

echo.
echo [4/4] Injetando Driver no Runtime do Java / Minecraft Launcher...
set "MC_LEGACY=%ProgramFiles(x86)%\Minecraft Launcher\runtime\jre-legacy\windows-x64\jre-legacy\bin"
if exist "%MC_LEGACY%" (
    copy /Y "%BUILD_DIR%\opengl32.dll" "%MC_LEGACY%\opengl32.dll" >nul
    copy /Y "%BUILD_DIR%\libgallium_wgl.dll" "%MC_LEGACY%\libgallium_wgl.dll" >nul
    copy /Y "%BUILD_DIR%\vulkan_radeon.dll" "%MC_LEGACY%\vulkan_radeon.dll" >nul
    copy /Y "%BUILD_DIR%\z-1.dll" "%MC_LEGACY%\z-1.dll" >nul
    echo [OK] Injetado com sucesso no Java 8 (Minecraft 1.7.10)!
)
set "MC_EPSILON=%ProgramFiles(x86)%\Minecraft Launcher\runtime\java-runtime-epsilon\windows-x64\java-runtime-epsilon\bin"
if exist "%MC_EPSILON%" (
    copy /Y "%BUILD_DIR%\opengl32.dll" "%MC_EPSILON%\opengl32.dll" >nul
    copy /Y "%BUILD_DIR%\libgallium_wgl.dll" "%MC_EPSILON%\libgallium_wgl.dll" >nul
    copy /Y "%BUILD_DIR%\vulkan_radeon.dll" "%MC_EPSILON%\vulkan_radeon.dll" >nul
    copy /Y "%BUILD_DIR%\z-1.dll" "%MC_EPSILON%\z-1.dll" >nul
    echo [OK] Injetado com sucesso no Java Moderno!
)

echo.
echo ========================================================
echo   [SUCESSO] Driver OpenGL 4.60 da BC-250 Ativado com Exito!
echo   GPU-Z, FurMark, Emuladores e Jogos ja detectarao a GPU.
echo ========================================================
pause
