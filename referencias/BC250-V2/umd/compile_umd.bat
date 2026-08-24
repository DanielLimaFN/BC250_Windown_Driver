@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo   Compilando AMD BC-250 Vulkan ICD (x64 e x86 32-bit)
echo ========================================================

set SRCDIR=%~dp0
set ROOTDIR=%~dp0..
set ODIR=%ROOTDIR%\build
set PKGDIR=%ROOTDIR%\package

if not exist "%ODIR%" mkdir "%ODIR%"
if not exist "%PKGDIR%" mkdir "%PKGDIR%"

:: 1. Compila x64
echo [1/2] Compilando x64 (amdbc250vulkan.dll)...
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cl.exe /nologo /O2 /W3 /LD /Zi /DEBUG /D_CRT_SECURE_NO_WARNINGS /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um" /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared" /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt" /I"%SRCDIR:~0,-1%" /I"%ROOTDIR%" "%SRCDIR%bc250_vulkan_icd.c" /Fe:"%ODIR%\amdbc250vulkan.dll" /DEF:"%SRCDIR%bc250_vulkan.def" /link /MAP:"%ODIR%\amdbc250vulkan.map" /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64" /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64" gdi32.lib user32.lib

copy /Y "%ODIR%\amdbc250vulkan.dll" "%PKGDIR%\amdbc250vulkan.dll" >nul
copy /Y "%SRCDIR%amdbc250vulkan.json" "%PKGDIR%\amdbc250vulkan.json" >nul
copy /Y "%SRCDIR%amdbc250vulkan.json" "%ODIR%\amdbc250vulkan.json" >nul

:: 2. Compila x86 (32-bit)
echo [2/2] Compilando x86 32-bit (amdbc250vulkan32.dll)...
call "%SRCDIR%compile_x86.bat" >nul
copy /Y "%ODIR%\amdbc250vulkan32.dll" "%PKGDIR%\amdbc250vulkan32.dll" >nul
copy /Y "%SRCDIR%amdbc250vulkan32.json" "%PKGDIR%\amdbc250vulkan32.json" >nul
copy /Y "%SRCDIR%amdbc250vulkan32.json" "%ODIR%\amdbc250vulkan32.json" >nul

echo ========================================================
echo   UMD VULKAN ICD COMPILADO COM SUCESSO (x64 e x86 32-bit)
echo   x64: %ODIR%\amdbc250vulkan.dll
echo   x86: %ODIR%\amdbc250vulkan32.dll
echo ========================================================
