@echo off
setlocal enabledelayedexpansion

set "SRCDIR=C:\Users\Daniel\.gemini\antigravity\brain\6ac9fd8d-42ba-4854-ba8c-7bdc815b756e\scratch"
set "OUTDIR=C:\Users\Daniel\Desktop\NamixWDDM\driver\tests\output_dxvk"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

copy /Y "%SRCDIR%\d3d11_triangle.cpp" "C:\Users\Daniel\Desktop\NamixWDDM\driver\tests\d3d11_triangle.cpp" >nul

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

echo Compilando D3D11Triangle.exe...
cl.exe /nologo /O2 /W3 /EHsc "C:\Users\Daniel\Desktop\NamixWDDM\driver\tests\d3d11_triangle.cpp" /Fe:"%OUTDIR%\D3D11Triangle.exe" /Fo:"%OUTDIR%\d3d11_triangle.obj" d3d11.lib dxgi.lib d3dcompiler.lib user32.lib
if %ERRORLEVEL% neq 0 (
    echo ERRO na compilacao de D3D11Triangle.exe.
    exit /b 1
)

echo [OK] D3D11Triangle.exe compilado com sucesso!
echo Copiando DLLs do DXVK x64 para %OUTDIR%...
copy /Y "C:\Users\Daniel\Desktop\NamixWDDM\driver\tests\dxvk_x64\dxvk-2.4.1\x64\d3d11.dll" "%OUTDIR%\d3d11.dll" >nul
copy /Y "C:\Users\Daniel\Desktop\NamixWDDM\driver\tests\dxvk_x64\dxvk-2.4.1\x64\dxgi.dll" "%OUTDIR%\dxgi.dll" >nul

echo [OK] Ambiente DXVK pronto em %OUTDIR%!
dir "%OUTDIR%"
