@echo off
setlocal enabledelayedexpansion
echo Compilando Suite de Ferramentas AMD BC-250 (BC250-V3 Tools)
echo ==========================================================

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
set CLI_TOOLS=bc250-smn-tool.c bc250-psp-tool.c bc250-studio.c

for %%T in (%CLI_TOOLS%) do (
  echo [cc-cli] %%T -^> bin\%%~nT.exe
  cl /nologo /O2 /W3 /I"%SRCDIR%." /Fe:"%BINDIR%\%%~nT.exe" /Fo"%BINDIR%\%%~nT.obj" "%SRCDIR%%%T" gdi32.lib user32.lib
  if errorlevel 1 (
    echo ERRO ao compilar %%T
    exit /b 1
  )
  if exist "%BINDIR%\%%~nT.obj" del /Q "%BINDIR%\%%~nT.obj"
)

echo [cc-gui] bc250-studio-gui.c -^> bin\bc250-studio-gui.exe
cl /nologo /O2 /W3 /I"%SRCDIR%." /Fe:"%BINDIR%\bc250-studio-gui.exe" /Fo"%BINDIR%\bc250-studio-gui.obj" "%SRCDIR%bc250-studio-gui.c" /link /SUBSYSTEM:WINDOWS gdi32.lib user32.lib
if errorlevel 1 (
  echo ERRO ao compilar bc250-studio-gui.c
  exit /b 1
)
if exist "%BINDIR%\bc250-studio-gui.obj" del /Q "%BINDIR%\bc250-studio-gui.obj"

echo.
echo ==========================================================
echo   TODAS AS FERRAMENTAS COMPILADAS COM SUCESSO!
echo   Executaveis em: %BINDIR%
echo ==========================================================
exit /b 0
