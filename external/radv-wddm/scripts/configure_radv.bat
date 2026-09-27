@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo   Configurando Build do Mesa RADV (Vulkan AMD WDDM2)
echo ========================================================

set "SRCDIR=%~dp0"
set "TOOLSDIR=%SRCDIR%tools\bin"

if exist "%TOOLSDIR%\glslang.exe" (
    if not exist "%TOOLSDIR%\glslangValidator.exe" (
        copy /Y "%TOOLSDIR%\glslang.exe" "%TOOLSDIR%\glslangValidator.exe" >nul
    )
)

set "PATH=%TOOLSDIR%;C:\Users\Daniel\AppData\Local\Programs\Python\Python312;C:\Users\Daniel\AppData\Local\Programs\Python\Python312\Scripts;%PATH%"
set "PATH=C:\Users\Daniel\AppData\Local\Microsoft\WinGet\Packages\WinFlexBison.win_flex_bison_Microsoft.Winget.Source_8wekyb3d8bbwe;%PATH%"
set "PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"

call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul

cd "%SRCDIR%"
if exist "build" (
    echo Limpando diretorio build anterior...
    rmdir /s /q "build"
)

echo Executando meson setup...
meson setup build ^
  -Dvulkan-drivers=amd ^
  -Dgallium-drivers= ^
  -Dplatforms=windows ^
  -Dbuildtype=release ^
  -Db_vscrt=md ^
  -Degl=disabled ^
  -Dgles1=false ^
  -Dgles2=false ^
  -Dopengl=false ^
  -Dllvm=disabled ^
  -Dshared-llvm=disabled ^
  --backend=ninja

echo ========================================================
