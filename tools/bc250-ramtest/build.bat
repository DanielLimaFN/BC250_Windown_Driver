@echo off
setlocal
rem bc250_ramtest (doc 595): compila o teste de RAM em modo usuario com o MSVC do VS2026.
set "VCVARS="
for %%D in (C D E) do (
  for %%E in (Community Professional Enterprise BuildTools) do (
    if exist "%%D:\Program Files\Microsoft Visual Studio\18\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\18\%%E\VC\Auxiliary\Build\vcvars64.bat"
    if exist "%%D:\Program Files\Microsoft Visual Studio\2026\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\2026\%%E\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if not defined VCVARS ( echo ERRO: vcvars64.bat do VS2026 nao encontrado. & exit /b 1 )
call "%VCVARS%" >nul
if not exist "%~dp0out" mkdir "%~dp0out"
cl /nologo /O2 /W4 /WX /Fe"%~dp0out\bc250_ramtest.exe" /Fo"%~dp0out\\" "%~dp0bc250_ramtest.c"
exit /b %ERRORLEVEL%
