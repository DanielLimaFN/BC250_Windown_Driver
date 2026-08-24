@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo   Compilando Suite de Testes Compute & IA - BC-250
echo ========================================================

set SRCDIR=%~dp0
set ROOTDIR=%~dp0..
set ODIR=%ROOTDIR%\build
set PKGDIR=%ROOTDIR%\package

if not exist "%ODIR%" mkdir "%ODIR%"
if not exist "%PKGDIR%" mkdir "%PKGDIR%"

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

if not defined VCVARS (
  echo ERRO: vcvars64.bat nao encontrado.
  exit /b 1
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

echo === VS   : %VCVARS%
echo === SDK  : %WDK% (%WDKVER%)

:: 3. Compila test_phase5_compute.c
set "INCS=/I"%WDK%\Include\%WDKVER%\um" /I"%WDK%\Include\%WDKVER%\shared" /I"%WDK%\Include\%WDKVER%\ucrt" /I"%ROOTDIR%""
set "LIBS=/LIBPATH:"%WDK%\Lib\%WDKVER%\um\x64" /LIBPATH:"%WDK%\Lib\%WDKVER%\ucrt\x64" gdi32.lib user32.lib"

echo [cc] test_phase5_compute.c
cl.exe /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS %INCS% "%SRCDIR%test_phase5_compute.c" /Fe:"%ODIR%\test_phase5_compute.exe" /link %LIBS%

if errorlevel 1 (
  echo ERRO na compilacao de test_phase5_compute.exe
  exit /b 1
)

copy /Y "%ODIR%\test_phase5_compute.exe" "%PKGDIR%\test_phase5_compute.exe" >nul
copy /Y "%ODIR%\test_phase5_compute.exe" "%SRCDIR%\test_phase5_compute.exe" >nul

:: 4. Compila test_llm_gemm.c
echo [cc] test_llm_gemm.c
cl.exe /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS %INCS% "%SRCDIR%test_llm_gemm.c" /Fe:"%ODIR%\test_llm_gemm.exe" /link %LIBS%

if errorlevel 1 (
  echo ERRO na compilacao de test_llm_gemm.exe
  exit /b 1
)

:: 5. Compila test_smu_probe.c
echo [cc] test_smu_probe.c
cl.exe /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS %INCS% "%SRCDIR%test_smu_probe.c" /Fe:"%ODIR%\test_smu_probe.exe" /link %LIBS%

if errorlevel 1 (
  echo ERRO na compilacao de test_smu_probe.exe
  exit /b 1
)

copy /Y "%ODIR%\test_smu_probe.exe" "%PKGDIR%\test_smu_probe.exe" >nul
copy /Y "%ODIR%\test_smu_probe.exe" "%SRCDIR%\test_smu_probe.exe" >nul

:: 6. Compila test_wddm2_adapter_info.c
echo [cc] test_wddm2_adapter_info.c
cl.exe /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS %INCS% "%SRCDIR%test_wddm2_adapter_info.c" /Fe:"%ODIR%\test_wddm2_adapter_info.exe" /link %LIBS%

if errorlevel 1 (
  echo ERRO na compilacao de test_wddm2_adapter_info.exe
  exit /b 1
)

copy /Y "%ODIR%\test_wddm2_adapter_info.exe" "%PKGDIR%\test_wddm2_adapter_info.exe" >nul
copy /Y "%ODIR%\test_wddm2_adapter_info.exe" "%SRCDIR%\test_wddm2_adapter_info.exe" >nul

:: 7. Compila test_wddm2_gpuvm_paging.c
echo [cc] test_wddm2_gpuvm_paging.c
cl.exe /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS %INCS% "%SRCDIR%test_wddm2_gpuvm_paging.c" /Fe:"%ODIR%\test_wddm2_gpuvm_paging.exe" /link %LIBS%

if errorlevel 1 (
  echo ERRO na compilacao de test_wddm2_gpuvm_paging.exe
  exit /b 1
)

copy /Y "%ODIR%\test_wddm2_gpuvm_paging.exe" "%PKGDIR%\test_wddm2_gpuvm_paging.exe" >nul
copy /Y "%ODIR%\test_wddm2_gpuvm_paging.exe" "%SRCDIR%\test_wddm2_gpuvm_paging.exe" >nul

:: 8. Compila test_wddm2_submit_ib.c
echo [cc] test_wddm2_submit_ib.c
cl.exe /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS %INCS% "%SRCDIR%test_wddm2_submit_ib.c" /Fe:"%ODIR%\test_wddm2_submit_ib.exe" /link %LIBS%

if errorlevel 1 (
  echo ERRO na compilacao de test_wddm2_submit_ib.exe
  exit /b 1
)

copy /Y "%ODIR%\test_wddm2_submit_ib.exe" "%PKGDIR%\test_wddm2_submit_ib.exe" >nul
copy /Y "%ODIR%\test_wddm2_submit_ib.exe" "%SRCDIR%\test_wddm2_submit_ib.exe" >nul

echo ========================================================
echo   TESTES COMPILADOS COM SUCESSO:
echo   - %ODIR%\test_phase5_compute.exe
echo   - %ODIR%\test_llm_gemm.exe
echo   - %ODIR%\test_smu_probe.exe
echo   - %ODIR%\test_wddm2_adapter_info.exe
echo   - %ODIR%\test_wddm2_gpuvm_paging.exe
echo   - %ODIR%\test_wddm2_submit_ib.exe
echo ========================================================
