@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo   Compilando Driver WGL ICD da AMD BC-250
echo   (Cyan Skillfish / Navi12 Lite - OpenGL 4.0+)
echo ========================================================

set "SRCDIR=%~dp0"
cd /d "%SRCDIR%"

call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul

echo.
echo [1/2] Compilando bc250_wgl_icd.c -> bc250_opengl32.dll...
cl /nologo /W3 /O2 /MD /LD /D_CRT_SECURE_NO_WARNINGS bc250_wgl_icd.c /link /DEF:bc250_wgl_icd.def /out:bc250_opengl32.dll gdi32.lib user32.lib

if %ERRORLEVEL% equ 0 (
    echo [OK] bc250_opengl32.dll compilado com sucesso!
) else (
    echo [ERRO] Falha na compilacao de bc250_opengl32.dll.
    exit /b %ERRORLEVEL%
)

echo.
echo [2/2] Gerando alias atio6xxx.dll (AMD OpenGL ICD)...
copy /Y bc250_opengl32.dll atio6xxx.dll >nul
copy /Y bc250_opengl32.dll ..\build\bc250_opengl32.dll >nul
copy /Y bc250_opengl32.dll ..\build\atio6xxx.dll >nul
copy /Y bc250_opengl32.dll ..\tests\bc250_opengl32.dll >nul
copy /Y bc250_opengl32.dll ..\tests\atio6xxx.dll >nul

echo [OK] DLLs instaladas com sucesso nos diretorios build e tests:
echo      - build\bc250_opengl32.dll
echo      - build\atio6xxx.dll
echo      - tests\bc250_opengl32.dll
echo      - tests\atio6xxx.dll

echo.
echo ========================================================
echo   Compilacao do WGL ICD Concluida com Sucesso!
echo ========================================================
