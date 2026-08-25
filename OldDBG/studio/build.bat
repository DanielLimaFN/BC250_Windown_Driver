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

echo.
echo OK: %ODIR%\bc250ctl.exe
echo OK: %ODIR%\BC250Studio.exe
endlocal
