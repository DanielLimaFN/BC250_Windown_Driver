@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo   Configurando Ambiente de Build Mesa RADV (WDDM2 / ACO)
echo ========================================================

:: 1. Adiciona Python e Meson ao PATH
set "PATH=C:\Users\Daniel\AppData\Local\Programs\Python\Python312;C:\Users\Daniel\AppData\Local\Programs\Python\Python312\Scripts;%PATH%"

:: 2. Adiciona WinFlexBison ao PATH
set "PATH=C:\Users\Daniel\AppData\Local\Microsoft\WinGet\Packages\WinFlexBison.win_flex_bison_Microsoft.Winget.Source_8wekyb3d8bbwe;%PATH%"

:: 3. Adiciona Ninja e CMake do Visual Studio 2022 ao PATH
set "PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"

:: 4. Inicializa o ambiente MSVC x64
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"

echo.
echo === Verificacao de Ferramentas ===
python --version
meson --version
ninja --version
win_flex --version
win_bison --version
echo ========================================================
