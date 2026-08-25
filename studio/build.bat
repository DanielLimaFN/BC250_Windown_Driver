@echo off
setlocal enabledelayedexpansion
:: build.bat - compila o bc250ctl.exe (user mode).
:: Ligado estatico (/MT) de proposito: a maquina alvo e uma maquina de teste
:: crua, sem VC redist instalado.

set SRCDIR=%~dp0
set ODIR=%~dp0output
if not exist "%ODIR%" mkdir "%ODIR%"

:: Se o compilador ja esta no ambiente (EWDK, prompt de desenvolvedor), usa.
set HAVE_CL=
where cl.exe >nul 2>&1 && set HAVE_CL=1
if defined HAVE_CL goto :compila

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
  echo ERRO: vcvars64.bat do VS2022 nao encontrado.
  exit /b 1
)

call "%VCVARS%" >nul

:compila
echo [cc] bc250ctl.exe
cl /nologo /W4 /WX- /O2 /MT /Zi /D_CRT_SECURE_NO_WARNINGS ^
  /Fe"%ODIR%\bc250ctl.exe" /Fd"%ODIR%\ctl.pdb" /Fo"%ODIR%\\" ^
  "%SRCDIR%lib\bc250lib.c" "%SRCDIR%cli\bc250ctl.c" "%SRCDIR%cli\audit.c" "%SRCDIR%cli\stress.c" ^
  /link /SUBSYSTEM:CONSOLE kernel32.lib advapi32.lib
if errorlevel 1 (
  echo BUILD FALHOU ^(bc250ctl^)
  exit /b 1
)

echo [cc] BC250Studio.exe
cl /nologo /W4 /WX- /O2 /MT /Zi /D_CRT_SECURE_NO_WARNINGS ^
  /Fe"%ODIR%\BC250Studio.exe" /Fd"%ODIR%\gui.pdb" /Fo"%ODIR%\\" ^
  "%SRCDIR%lib\bc250lib.c" "%SRCDIR%gui\bc250studio.c" ^
  /link /SUBSYSTEM:WINDOWS ^
  kernel32.lib user32.lib gdi32.lib comctl32.lib comdlg32.lib advapi32.lib
if errorlevel 1 (
  echo BUILD FALHOU ^(BC250Studio^)
  exit /b 1
)

echo [cc] resource_contract_test.exe
cl /nologo /Brepro /W4 /WX /O2 /MT ^
  /Fe"%ODIR%\resource_contract_test.exe" /Fo"%ODIR%\\" ^
  "%SRCDIR%..\driver\core\bc250_resource_contract.c" ^
  "%SRCDIR%..\driver\tests\resource_contract_test.c" ^
  /link /Brepro /SUBSYSTEM:CONSOLE
if errorlevel 1 (
  echo BUILD FALHOU ^(resource_contract_test^)
  exit /b 1
)

"%ODIR%\resource_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(resource_contract_test^)
  exit /b 1
)

echo [cc] vram_allocator_test.exe
cl /nologo /Brepro /W4 /WX /O2 /MT ^
  /Fe"%ODIR%\vram_allocator_test.exe" /Fo"%ODIR%\\" ^
  "%SRCDIR%..\driver\core\bc250_vram_allocator.c" ^
  "%SRCDIR%..\driver\tests\vram_allocator_test.c" ^
  /link /Brepro /SUBSYSTEM:CONSOLE
if errorlevel 1 (
  echo BUILD FALHOU ^(vram_allocator_test^)
  exit /b 1
)

"%ODIR%\vram_allocator_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(vram_allocator_test^)
  exit /b 1
)

echo [cc] address_contract_test.exe
cl /nologo /Brepro /W4 /WX /O2 /MT ^
  /Fe"%ODIR%\address_contract_test.exe" /Fo"%ODIR%\\" ^
  "%SRCDIR%..\driver\core\bc250_vram_allocator.c" ^
  "%SRCDIR%..\driver\core\bc250_address_contract.c" ^
  "%SRCDIR%..\driver\tests\address_contract_test.c" ^
  /link /Brepro /SUBSYSTEM:CONSOLE
if errorlevel 1 (
  echo BUILD FALHOU ^(address_contract_test^)
  exit /b 1
)

"%ODIR%\address_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(address_contract_test^)
  exit /b 1
)

echo [cc] vram_topology_contract_test.exe
cl /nologo /Brepro /W4 /WX /O2 /MT ^
  /Fe"%ODIR%\vram_topology_contract_test.exe" /Fo"%ODIR%\\" ^
  "%SRCDIR%..\driver\core\bc250_vram_topology_contract.c" ^
  "%SRCDIR%..\driver\tests\vram_topology_contract_test.c" ^
  /link /Brepro /SUBSYSTEM:CONSOLE
if errorlevel 1 (
  echo BUILD FALHOU ^(vram_topology_contract_test^)
  exit /b 1
)

"%ODIR%\vram_topology_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(vram_topology_contract_test^)
  exit /b 1
)

echo [cc] vram_topology_reader_test.exe
cl /nologo /Brepro /W4 /WX /O2 /MT ^
  /Fe"%ODIR%\vram_topology_reader_test.exe" /Fo"%ODIR%\\" ^
  "%SRCDIR%..\driver\core\bc250_vram_topology_contract.c" ^
  "%SRCDIR%..\driver\core\bc250_vram_topology_reader.c" ^
  "%SRCDIR%..\driver\tests\vram_topology_reader_test.c" ^
  /link /Brepro /SUBSYSTEM:CONSOLE
if errorlevel 1 (
  echo BUILD FALHOU ^(vram_topology_reader_test^)
  exit /b 1
)

"%ODIR%\vram_topology_reader_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(vram_topology_reader_test^)
  exit /b 1
)

echo [cc] ip_discovery_contract_test.exe
cl /nologo /Brepro /W4 /WX /O2 /MT ^
  /Fe"%ODIR%\ip_discovery_contract_test.exe" /Fo"%ODIR%\\" ^
  "%SRCDIR%..\driver\core\bc250_ip_discovery_contract.c" ^
  "%SRCDIR%..\driver\tests\ip_discovery_contract_test.c" ^
  /link /Brepro /SUBSYSTEM:CONSOLE
if errorlevel 1 (
  echo BUILD FALHOU ^(ip_discovery_contract_test^)
  exit /b 1
)

"%ODIR%\ip_discovery_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(ip_discovery_contract_test^)
  exit /b 1
)

echo [cc] psp_contract_test.exe
cl /nologo /Brepro /W4 /WX /O2 /MT ^
  /Fe"%ODIR%\psp_contract_test.exe" /Fo"%ODIR%\\" ^
  "%SRCDIR%..\driver\core\bc250_vram_allocator.c" ^
  "%SRCDIR%..\driver\core\bc250_address_contract.c" ^
  "%SRCDIR%..\driver\core\bc250_psp_contract.c" ^
  "%SRCDIR%..\driver\tests\psp_contract_test.c" ^
  /link /Brepro /SUBSYSTEM:CONSOLE
if errorlevel 1 (
  echo BUILD FALHOU ^(psp_contract_test^)
  exit /b 1
)

"%ODIR%\psp_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(psp_contract_test^)
  exit /b 1
)

echo [cc] compute_bootstrap_test.exe
cl /nologo /Brepro /W4 /WX /O2 /MT ^
  /Fe"%ODIR%\compute_bootstrap_test.exe" /Fo"%ODIR%\\" ^
  "%SRCDIR%..\driver\core\bc250_vram_allocator.c" ^
  "%SRCDIR%..\driver\core\bc250_address_contract.c" ^
  "%SRCDIR%..\driver\core\bc250_psp_contract.c" ^
  "%SRCDIR%..\driver\core\bc250_compute_bootstrap.c" ^
  "%SRCDIR%..\driver\tests\compute_bootstrap_test.c" ^
  /link /Brepro /SUBSYSTEM:CONSOLE
if errorlevel 1 (
  echo BUILD FALHOU ^(compute_bootstrap_test^)
  exit /b 1
)

"%ODIR%\compute_bootstrap_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(compute_bootstrap_test^)
  exit /b 1
)

echo [cc] owned_adapter_contract_test.exe
cl /nologo /Brepro /W4 /WX /O2 /MT ^
  /Fe"%ODIR%\owned_adapter_contract_test.exe" /Fo"%ODIR%\\" ^
  "%SRCDIR%..\driver\core\bc250_resource_contract.c" ^
  "%SRCDIR%..\driver\core\bc250_vram_allocator.c" ^
  "%SRCDIR%..\driver\core\bc250_address_contract.c" ^
  "%SRCDIR%..\driver\core\bc250_psp_contract.c" ^
  "%SRCDIR%..\driver\core\bc250_compute_bootstrap.c" ^
  "%SRCDIR%..\driver\core\bc250_owned_adapter_contract.c" ^
  "%SRCDIR%..\driver\tests\owned_adapter_contract_test.c" ^
  /link /Brepro /SUBSYSTEM:CONSOLE
if errorlevel 1 (
  echo BUILD FALHOU ^(owned_adapter_contract_test^)
  exit /b 1
)

"%ODIR%\owned_adapter_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(owned_adapter_contract_test^)
  exit /b 1
)

echo.
echo OK: %ODIR%\bc250ctl.exe
echo OK: %ODIR%\BC250Studio.exe
echo OK: %ODIR%\resource_contract_test.exe
echo OK: %ODIR%\vram_allocator_test.exe
echo OK: %ODIR%\address_contract_test.exe
echo OK: %ODIR%\vram_topology_contract_test.exe
echo OK: %ODIR%\vram_topology_reader_test.exe
echo OK: %ODIR%\ip_discovery_contract_test.exe
echo OK: %ODIR%\psp_contract_test.exe
echo OK: %ODIR%\compute_bootstrap_test.exe
echo OK: %ODIR%\owned_adapter_contract_test.exe
endlocal
