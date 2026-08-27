@echo off
setlocal enabledelayedexpansion

set SRCDIR=%~dp0
set ROOTDIR=%~dp0..
set ODIR=%ROOTDIR%\build
set PKGDIR=%ROOTDIR%\package

if not exist "%ODIR%" mkdir "%ODIR%"

:: 1. Localiza Visual Studio vcvars64.bat
set VCVARS=
for %%D in (C D E F G) do (
  if not defined VCVARS (
    for %%E in (BuildTools Community Professional Enterprise) do (
      if not defined VCVARS (
        if exist "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
        if exist "%%D:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
      )
    )
  )
)

call "%VCVARS%" >nul

:: 2. Localiza Windows SDK
set WDK=
for %%D in (C D E F G) do (
  for %%P in ("Program Files (x86)" "Program Files") do (
    if not defined WDK (
      if exist "%%D:\%%~P\Windows Kits\10\Include" set "WDK=%%D:\%%~P\Windows Kits\10"
    )
  )
)

set WDKVER=
for %%V in (10.0.26100.0 10.0.22621.0 10.0.19041.0) do (
  if not defined WDKVER (
    if exist "%WDK%\Include\%%V\um\windows.h" set "WDKVER=%%V"
  )
)

set "INCS=/I"%WDK%\Include\%WDKVER%\um" /I"%WDK%\Include\%WDKVER%\shared" /I"%WDK%\Include\%WDKVER%\ucrt" /I"%ROOTDIR%\umd""
set "LIBS=/LIBPATH:"%WDK%\Lib\%WDKVER%\um\x64" /LIBPATH:"%WDK%\Lib\%WDKVER%\ucrt\x64" gdi32.lib user32.lib"

echo [cc] benchmark_vulkan_suite.c
cl.exe /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS %INCS% "%SRCDIR%benchmark_vulkan_suite.c" /Fe:"%ODIR%\benchmark_vulkan_suite.exe" /link %LIBS%

if errorlevel 1 (
  echo ERRO na compilacao de benchmark_vulkan_suite.exe
  exit /b 1
)

copy /Y "%ODIR%\benchmark_vulkan_suite.exe" "%PKGDIR%\benchmark_vulkan_suite.exe" >nul
copy /Y "%ODIR%\benchmark_vulkan_suite.exe" "%SRCDIR%\benchmark_vulkan_suite.exe" >nul

echo Compilado com sucesso: %ODIR%\benchmark_vulkan_suite.exe
