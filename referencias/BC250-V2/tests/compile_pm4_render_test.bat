@echo off
setlocal
echo ===============================================================================
echo   Compilando Suite de Validacao de Silicio: Renderizacao PM4 (IB) BC-250
echo ===============================================================================

for %%D in (C D E F G) do (
  for %%E in (BuildTools Community Professional Enterprise) do (
    if exist "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
      call "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" >nul
      goto :compile
    )
  )
)

:compile
where cl.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo [-] ERRO: Compilador MSVC x64 nao localizado!
    exit /b 1
)

cl.exe /nologo /O2 /MD /utf-8 "%~dp0test_pm4_direct_render.c" /Fe:"%~dp0test_pm4_direct_render.exe" /link /OUT:"%~dp0test_pm4_direct_render.exe" gdi32.lib user32.lib
if %errorlevel% equ 0 (
    copy /y "%~dp0test_pm4_direct_render.exe" "%~dp0..\build\test_pm4_direct_render.exe" >nul
    echo [+] Sucesso: test_pm4_direct_render.exe compilado com sucesso!
) else (
    echo [-] Falha na compilacao de test_pm4_direct_render.exe
    exit /b 1
)
