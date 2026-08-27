@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0\.."

echo Compilando benchmark_vulkan_suite.exe...
cl /nologo /O2 /W3 /I umd tests\benchmark_vulkan_suite.c /Fe:build\benchmark_vulkan_suite.exe gdi32.lib user32.lib
if %ERRORLEVEL% equ 0 (
    echo [OK] build\benchmark_vulkan_suite.exe gerado com sucesso!
) else (
    echo [FALHA] Erro na compilacao!
)
