@echo off
setlocal enabledelayedexpansion

set ROOTDIR=%~dp0..\..
set ODIR=%ROOTDIR%\driver\umd\output
if not exist "%ODIR%" mkdir "%ODIR%"

set HAVE_CL=
where cl.exe >nul 2>&1 && set HAVE_CL=1

set VCVARS=
if not defined HAVE_CL (
  for %%D in (C D E F G) do (
    for %%E in (BuildTools Community Professional Enterprise) do (
      if not defined VCVARS (
        if exist "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
        if exist "%%D:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
      )
    )
  )
  if not defined VCVARS (
    echo ERRO: vcvars64.bat do VS2022 nao encontrado.
    exit /b 1
  )
  call "!VCVARS!" >nul
)

echo [UMD CC] bc250_runtime.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_runtime.obj" /c "%ROOTDIR%\driver\umd\bc250_runtime.c"
if errorlevel 1 exit /b 1

echo [LLM CC] bc250_llm_kernels.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_llm_kernels.obj" /c "%ROOTDIR%\driver\umd\bc250_llm_kernels.c"
if errorlevel 1 exit /b 1

echo [UMD LINK] bc250_runtime.dll
link /nologo /DLL /OUT:"%ODIR%\bc250_runtime.dll" /IMPLIB:"%ODIR%\bc250_runtime.lib" "%ODIR%\bc250_runtime.obj" "%ODIR%\bc250_llm_kernels.obj" kernel32.lib advapi32.lib
if errorlevel 1 exit /b 1

echo [D3D UMD CC] bc250_d3d_umd.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_d3d_umd.obj" /c "%ROOTDIR%\driver\umd\bc250_d3d_umd.c"
if errorlevel 1 exit /b 1

echo [D3D UMD LINK] bc250umd.dll
link /nologo /DLL /DEF:"%ROOTDIR%\driver\umd\bc250umd.def" /OUT:"%ODIR%\bc250umd.dll" /IMPLIB:"%ODIR%\bc250umd.lib" "%ODIR%\bc250_d3d_umd.obj" kernel32.lib
if errorlevel 1 exit /b 1

echo [D3D UMD TEST CC] bc250_d3d_umd_test.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_d3d_umd_test.obj" /c "%ROOTDIR%\driver\tests\bc250_d3d_umd_test.c"
if errorlevel 1 exit /b 1

echo [D3D UMD TEST LINK] bc250_d3d_umd_test.exe
link /nologo /OUT:"%ODIR%\bc250_d3d_umd_test.exe" "%ODIR%\bc250_d3d_umd_test.obj" "%ODIR%\bc250umd.lib" kernel32.lib
if errorlevel 1 exit /b 1

echo [VK ICD CC] bc250_vulkan_icd.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_vulkan_icd.obj" /c "%ROOTDIR%\driver\umd\bc250_vulkan_icd.c"
if errorlevel 1 exit /b 1

echo [VK ICD LINK] bc250_vulkan.dll
link /nologo /DLL /OUT:"%ODIR%\bc250_vulkan.dll" /IMPLIB:"%ODIR%\bc250_vulkan.lib" "%ODIR%\bc250_vulkan_icd.obj" "%ODIR%\bc250_runtime.lib" kernel32.lib
if errorlevel 1 exit /b 1

echo [UMD CONTRACT TEST CC] umd_runtime_contract_test.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\umd_runtime_contract_test.obj" /c "%ROOTDIR%\driver\tests\umd_runtime_contract_test.c"
if errorlevel 1 exit /b 1

echo [UMD CONTRACT TEST LINK] umd_runtime_contract_test.exe
link /nologo /OUT:"%ODIR%\umd_runtime_contract_test.exe" "%ODIR%\umd_runtime_contract_test.obj" "%ODIR%\bc250_runtime.lib" kernel32.lib
if errorlevel 1 exit /b 1

echo [UMD TEST CC] bc250-umd-test.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250-umd-test.obj" /c "%ROOTDIR%\tools\bc250-umd-test.c"
if errorlevel 1 exit /b 1

echo [UMD TEST LINK] bc250-umd-test.exe
link /nologo /OUT:"%ODIR%\bc250-umd-test.exe" "%ODIR%\bc250-umd-test.obj" "%ODIR%\bc250_runtime.lib" kernel32.lib
if errorlevel 1 exit /b 1

echo [VK TEST CC] bc250-vulkan-test.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250-vulkan-test.obj" /c "%ROOTDIR%\tools\bc250-vulkan-test.c"
if errorlevel 1 exit /b 1

echo [VK TEST LINK] bc250-vulkan-test.exe
link /nologo /OUT:"%ODIR%\bc250-vulkan-test.exe" "%ODIR%\bc250-vulkan-test.obj" "%ODIR%\bc250_vulkan.lib" kernel32.lib
if errorlevel 1 exit /b 1

echo [LLM BENCHMARK CC] bc250-llm-benchmark.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250-llm-benchmark.obj" /c "%ROOTDIR%\tools\bc250-llm-benchmark.c"
if errorlevel 1 exit /b 1

echo [LLM BENCHMARK LINK] bc250-llm-benchmark.exe
link /nologo /OUT:"%ODIR%\bc250-llm-benchmark.exe" "%ODIR%\bc250-llm-benchmark.obj" "%ODIR%\bc250_runtime.lib" kernel32.lib
if errorlevel 1 exit /b 1

copy /y "%ROOTDIR%\driver\umd\bc250_icd.json" "%ODIR%\bc250_icd.json" >nul

echo.
echo === EXECUTANDO TESTE DE CONTRATO UMD RUNTIME ===
"%ODIR%\umd_runtime_contract_test.exe"
if errorlevel 1 exit /b 1

echo.
echo === EXECUTANDO TESTE DE CONTRATO D3D UMD ===
"%ODIR%\bc250_d3d_umd_test.exe"
if errorlevel 1 exit /b 1

echo.
echo =======================================================================
echo D3D UMD:       %ODIR%\bc250umd.dll
echo UMD RUNTIME:   %ODIR%\bc250_runtime.dll
echo VULKAN ICD:    %ODIR%\bc250_vulkan.dll
echo ICD MANIFEST:  %ODIR%\bc250_icd.json
echo HARNESS D3D:   %ODIR%\bc250_d3d_umd_test.exe
echo HARNESS UMD:   %ODIR%\bc250-umd-test.exe
echo HARNESS VK:    %ODIR%\bc250-vulkan-test.exe
echo HARNESS LLM:   %ODIR%\bc250-llm-benchmark.exe
echo =======================================================================
