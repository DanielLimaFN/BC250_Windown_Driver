@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0.."
set "OUT=%ROOT%\driver\wdk\output_compute"
if not exist "%OUT%" mkdir "%OUT%"

where cl.exe >nul 2>&1
if errorlevel 1 (
  set "VCVARS="
  for %%D in (C D E F G) do (
    for %%E in (BuildTools Community Professional Enterprise) do (
      if not defined VCVARS if exist "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
      if not defined VCVARS if exist "%%D:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
    )
  )
  if not defined VCVARS (
    echo ERRO: Visual Studio 2022 nao encontrado.
    exit /b 1
  )
  call "!VCVARS!" >nul
)

set "WDK=C:\Program Files (x86)\Windows Kits\10"
if defined WindowsSdkDir set "WDK=%WindowsSdkDir:~0,-1%"
set "WDKVER="
for %%V in (10.0.26100.0 10.0.22621.0) do if not defined WDKVER if exist "%WDK%\Include\%%V\km\ntddk.h" set "WDKVER=%%V"
if not defined WDKVER (
  echo ERRO: WDK 10.0.22621 ou superior nao encontrado.
  exit /b 1
)

set "CFLAGS=/nologo /c /Brepro /W4 /WX /O2 /Oi /kernel /GS- /X /D_AMD64_ /DAMD64 /D_WIN64 /DBC250_KERNEL_MODE /I "%ROOT%\driver\core" /I "%WDK%\Include\%WDKVER%\km" /I "%WDK%\Include\%WDKVER%\km\crt" /I "%WDK%\Include\%WDKVER%\shared""

echo [cc] gpuva_sdma broker and executor contracts ^(isolated^)
cl %CFLAGS% /Fo"%OUT%\bc250_mcdm_object_contract.isolated.obj" "%ROOT%\driver\core\bc250_mcdm_object_contract.c" || exit /b 1
cl %CFLAGS% /Fo"%OUT%\bc250_gpuva_sdma_broker_contract.isolated.obj" "%ROOT%\driver\core\bc250_gpuva_sdma_broker_contract.c" || exit /b 1
cl %CFLAGS% /Fo"%OUT%\bc250_gpuva_sdma_executor_contract.isolated.obj" "%ROOT%\driver\core\bc250_gpuva_sdma_executor_contract.c" || exit /b 1
cl %CFLAGS% /Fo"%OUT%\gpuva_sdma_broker_contract_test.isolated.obj" "%ROOT%\driver\tests\gpuva_sdma_broker_contract_test.c" || exit /b 1
cl %CFLAGS% /Fo"%OUT%\gpuva_sdma_executor_contract_test.isolated.obj" "%ROOT%\driver\tests\gpuva_sdma_executor_contract_test.c" || exit /b 1

link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup /OUT:"%OUT%\gpuva_sdma_broker_contract_test.isolated.exe" "%OUT%\bc250_mcdm_object_contract.isolated.obj" "%OUT%\bc250_gpuva_sdma_broker_contract.isolated.obj" "%OUT%\gpuva_sdma_broker_contract_test.isolated.obj" kernel32.lib || exit /b 1
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup /OUT:"%OUT%\gpuva_sdma_executor_contract_test.isolated.exe" "%OUT%\bc250_mcdm_object_contract.isolated.obj" "%OUT%\bc250_gpuva_sdma_broker_contract.isolated.obj" "%OUT%\bc250_gpuva_sdma_executor_contract.isolated.obj" "%OUT%\gpuva_sdma_executor_contract_test.isolated.obj" kernel32.lib || exit /b 1

"%OUT%\gpuva_sdma_broker_contract_test.isolated.exe" || exit /b 1
"%OUT%\gpuva_sdma_executor_contract_test.isolated.exe" || exit /b 1
echo BC250 COMPUTE CONTRACTS ISOLATED: PASS
echo Nenhum pacote Display, INF ou output_active foi alterado.
endlocal
