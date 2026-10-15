@echo off
setlocal enabledelayedexpansion
set "ROOTDIR=%~dp0.."

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

echo Compilando tools\d3d11_triangle.exe...
cl.exe /nologo /O2 "%ROOTDIR%\driver\tests\d3d11_triangle.cpp" /Fe:"%ROOTDIR%\tools\d3d11_triangle.exe" /Fo:"%ROOTDIR%\tools\d3d11_triangle.obj" /link d3d11.lib dxgi.lib d3dcompiler.lib user32.lib
if %ERRORLEVEL% neq 0 (
    echo ERRO na compilacao de d3d11_triangle.exe.
    exit /b 1
)
if exist "%ROOTDIR%\tools\d3d11_triangle.obj" del "%ROOTDIR%\tools\d3d11_triangle.obj" >nul 2>&1
echo [OK] d3d11_triangle.exe compilado com sucesso.
