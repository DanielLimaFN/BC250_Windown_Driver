@echo off
setlocal EnableExtensions

fltmc.exe >nul 2>&1
if errorlevel 1 (
  echo ERRO: abra o CMD ou PowerShell como Administrador e execute novamente.
  exit /b 2
)

set "SOURCE=%~dp0BC250_RECOVERY.cmd"
set "TARGET=%SystemDrive%\BC250_RECOVERY.cmd"

if not exist "%SOURCE%" (
  echo ERRO: fonte ausente: %SOURCE%
  exit /b 3
)

copy /Y "%SOURCE%" "%TARGET%" >nul
if errorlevel 1 (
  echo ERRO: nao foi possivel copiar para %TARGET%.
  exit /b 4
)

fc.exe /B "%SOURCE%" "%TARGET%" >nul
if errorlevel 1 (
  echo ERRO: a copia em %TARGET% diverge da fonte.
  exit /b 5
)

echo OK: recuperacao instalada em %TARGET%.
call "%TARGET%" /DRY-RUN
exit /b %ERRORLEVEL%
