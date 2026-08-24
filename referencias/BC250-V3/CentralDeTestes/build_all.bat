@echo off
setlocal enabledelayedexpansion
echo ===============================================================================
echo   Compilando Central de Testes AMD BC-250 (BC250-V3) — T1 a T12 + Fases
echo ===============================================================================

set ROOT=%~dp0
set SRCDIR=%ROOT%src
set BINDIR=%ROOT%bin

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

echo Inicializando ambiente MSVC (vcvars64.bat)...
call "%VCVARS%" >nul

:compile
set TOOLS=t1_scratch_mmio.c t2_pm4_scratch.c t3_write_stick.c t4_fill_pattern.c t5_sdma_fence_poison.c t6_telemetry_correlation.c t7_psp_protocol_validate.c t8_fallback_audit.c t9_bringup_bisect.c t10_baseline_plausibility.c t11_vram_memtest.c t12_smu_mailbox_probe.c phase_audit.c

for %%T in (%TOOLS%) do (
  echo [CC] %%T -^> bin\%%~nT.exe
  cl /nologo /O2 /W3 /I"%SRCDIR%" /Fe:"%BINDIR%\%%~nT.exe" /Fo"%BINDIR%\%%~nT.obj" "%SRCDIR%\%%T" gdi32.lib user32.lib advapi32.lib
  if errorlevel 1 (
    echo [-] ERRO ao compilar %%T
    exit /b 1
  )
  if exist "%BINDIR%\%%~nT.obj" del /Q "%BINDIR%\%%~nT.obj"
)

echo.
echo ===============================================================================
echo   [+] TODOS OS BINARIOS DA CENTRAL DE TESTES (T1..T12) FORAM COMPILADOS!
echo   Destino: %BINDIR%
echo ===============================================================================
exit /b 0
