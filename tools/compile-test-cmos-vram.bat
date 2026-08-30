@echo off
setlocal enabledelayedexpansion

set SRCDIR=%~dp0
set ROOTDIR=%~dp0..
set ODIR=%ROOTDIR%\output
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

set CFLAGS=/nologo /W4 /O2 /Oi /I "%ROOTDIR%\driver\core"

echo [cc] bc250_cmos_vram_contract.c + cmos_vram_contract_test.c
cl %CFLAGS% "%ROOTDIR%\driver\core\bc250_cmos_vram_contract.c" "%ROOTDIR%\driver\tests\cmos_vram_contract_test.c" /Fe"%ODIR%\cmos_vram_contract_test.exe" /link /SUBSYSTEM:CONSOLE
if errorlevel 1 (
  echo ERRO de compilacao do teste unitario!
  exit /b 1
)

echo [cc] bc250-vram-bios-tool.c
cl %CFLAGS% "%ROOTDIR%\driver\core\bc250_cmos_vram_contract.c" "%ROOTDIR%\tools\bc250-vram-bios-tool.c" /Fe"%ODIR%\bc250-vram-bios-tool.exe" /link /SUBSYSTEM:CONSOLE cfgmgr32.lib advapi32.lib
if errorlevel 1 (
  echo ERRO de compilacao da ferramenta de diagnostico!
  exit /b 1
)

echo [run] cmos_vram_contract_test.exe
"%ODIR%\cmos_vram_contract_test.exe"
if errorlevel 1 (
  echo TESTE UNITARIO FALHOU!
  exit /b 1
)

echo.
echo [run] bc250-vram-bios-tool.exe
"%ODIR%\bc250-vram-bios-tool.exe"
if errorlevel 1 (
  echo FERRAMENTA FALHOU!
  exit /b 1
)

echo.
echo [ok] Modulo e Ferramenta CMOS VRAM compilados e validados com sucesso!
