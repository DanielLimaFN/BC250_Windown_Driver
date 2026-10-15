@echo off
setlocal enabledelayedexpansion

set "ROOTDIR=%~dp0.."
set "OUTDIR=%ROOTDIR%\tools"

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

echo Compilando bc250-vram-paging-stress.exe...
cl.exe /nologo /O2 /W3 "%ROOTDIR%\tools\bc250-vram-paging-stress.c" /Fe:"%OUTDIR%\bc250-vram-paging-stress.exe" /Fo:"%OUTDIR%\bc250-vram-paging-stress.obj" d3d11.lib dxgi.lib
if %ERRORLEVEL% neq 0 (
    echo ERRO na compilacao de bc250-vram-paging-stress.exe.
    exit /b 1
)

echo [OK] bc250-vram-paging-stress.exe compilado com sucesso.
del "%OUTDIR%\bc250-vram-paging-stress.obj" >nul 2>&1
