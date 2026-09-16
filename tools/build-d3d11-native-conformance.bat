@echo off
setlocal enabledelayedexpansion

set "ROOTDIR=%~dp0.."
set "OUTDIR=%ROOTDIR%\driver\tests\output_d3d11_native"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

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

echo Compilando bc250-d3d11-conformance.exe (D3D11 Nativo via bc250umd.dll)...
cl.exe /nologo /O2 /W3 /EHsc "%ROOTDIR%\driver\tests\d3d11_native_conformance_test.cpp" /Fe:"%OUTDIR%\bc250-d3d11-conformance.exe" /Fo:"%OUTDIR%\d3d11_native_conformance.obj" d3d11.lib dxgi.lib d3dcompiler.lib user32.lib
if %ERRORLEVEL% neq 0 (
    echo ERRO na compilacao de bc250-d3d11-conformance.exe.
    exit /b 1
)

echo [OK] bc250-d3d11-conformance.exe compilado com sucesso em:
echo      %OUTDIR%\bc250-d3d11-conformance.exe
