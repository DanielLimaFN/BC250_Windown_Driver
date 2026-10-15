@echo off
setlocal enabledelayedexpansion
rem T3 (doc 691): compila bc250-processo-novo.exe (gatilho de processo novo).

set "OUTDIR=%~dp0"

set VCVARS=
if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS (
    echo ERRO: vcvars64.bat nao encontrado.
    exit /b 1
)
call "%VCVARS%" >nul
if %ERRORLEVEL% neq 0 (
    echo ERRO ao carregar vcvars64.
    exit /b 1
)

echo Compilando bc250-processo-novo.exe...
cl.exe /nologo /O2 /W4 /WX /wd4201 /DUNICODE /D_UNICODE "%OUTDIR%bc250-processo-novo.c" /Fe:"%OUTDIR%bc250-processo-novo.exe" /Fo:"%OUTDIR%bc250-processo-novo.obj" d3d11.lib dxgi.lib dxguid.lib user32.lib gdi32.lib
if %ERRORLEVEL% neq 0 (
    echo ERRO na compilacao de bc250-processo-novo.exe.
    exit /b 1
)
del "%OUTDIR%bc250-processo-novo.obj" >nul 2>&1
echo [OK] bc250-processo-novo.exe compilado.
