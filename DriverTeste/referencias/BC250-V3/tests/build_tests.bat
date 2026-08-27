@echo off
setlocal enabledelayedexpansion
echo Compilando Suite de Testes AMD BC-250 (BC250-V3)
echo =================================================

set SRCDIR=%~dp0
set BINDIR=%SRCDIR%bin

if not exist "%BINDIR%" mkdir "%BINDIR%"

set HAVE_CL=
where cl.exe >nul 2>&1 && set HAVE_CL=1
if defined HAVE_CL goto :compile

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

:compile
rem Ordem importa: test_regmap valida o mapa de registradores e deve rodar primeiro.
set TESTS=test_regmap.c test_gfxoff.c test_hw_info.c test_telemetry.c test_fw_status.c test_vram_rw.c test_sdma.c test_pm4_ring.c test_cp_execution.c test_compute.c test_power_profile.c test_opencl.c

for %%T in (%TESTS%) do (
  echo [cc] %%T -^> bin\%%~nT.exe
  cl /nologo /O2 /W3 /I"%SRCDIR%." /I"%SRCDIR%..\opencl\include" /Fe:"%BINDIR%\%%~nT.exe" /Fo"%BINDIR%\%%~nT.obj" "%SRCDIR%%%T" gdi32.lib user32.lib advapi32.lib
  if errorlevel 1 (
    echo ERRO ao compilar %%T
    exit /b 1
  )
  if exist "%BINDIR%\%%~nT.obj" del /Q "%BINDIR%\%%~nT.obj"
)

echo.
echo =================================================
echo   TODOS OS TESTES COMPILADOS COM SUCESSO!
echo   Executaveis em: %BINDIR%
echo =================================================
exit /b 0
