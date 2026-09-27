@echo off
setlocal enabledelayedexpansion
rem Faixa DX.26: autoteste grafico em Vulkan puro (controle positivo), docs/337 Fase B.
rem Os shaders ja estao embutidos em driver\tests\gfx_selftest\selftest_spv.inl (glslc).

set "ROOTDIR=%~dp0.."
set "OUTDIR=%ROOTDIR%\driver\tests\output_d3d11_native"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

set VCVARS=
if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS (
    echo ERRO: vcvars64.bat nao encontrado.
    exit /b 1
)
call "%VCVARS%" >nul
if %ERRORLEVEL% neq 0 exit /b 1

echo Compilando bc250-gfx-selftest.exe (Vulkan puro, ICD direto)...
cl.exe /nologo /O2 /W4 /WX /I "%ROOTDIR%\inc" /I "%ROOTDIR%\driver\tests\gfx_selftest" "%ROOTDIR%\driver\tests\gfx_selftest\bc250_gfx_selftest.c" /Fe:"%OUTDIR%\bc250-gfx-selftest.exe" /Fo:"%OUTDIR%\bc250_gfx_selftest.obj" kernel32.lib
if %ERRORLEVEL% neq 0 (
    echo ERRO na compilacao de bc250-gfx-selftest.exe.
    exit /b 1
)
echo OK: %OUTDIR%\bc250-gfx-selftest.exe
exit /b 0
