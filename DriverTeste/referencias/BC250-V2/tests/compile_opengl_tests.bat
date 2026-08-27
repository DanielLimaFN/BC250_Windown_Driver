@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo   Compilando Suites de Teste OpenGL 4.0+ da AMD BC-250
echo   (Cyan Skillfish / Navi12 Lite - Fases 1 a 6)
echo ========================================================

set "SRCDIR=%~dp0"
cd /d "%SRCDIR%"

call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul

echo.
echo [1/6] Compilando test_opengl_phase1_wgl_context.c...
cl /nologo /W3 /O2 /MD /D_CRT_SECURE_NO_WARNINGS test_opengl_phase1_wgl_context.c /link /out:test_opengl_phase1_wgl_context.exe opengl32.lib gdi32.lib user32.lib
if %ERRORLEVEL% neq 0 (
    echo [ERRO] Falha na compilacao de test_opengl_phase1_wgl_context.exe.
    exit /b %ERRORLEVEL%
)
echo [OK] test_opengl_phase1_wgl_context.exe compilado com sucesso!

echo.
echo [2/6] Compilando test_opengl_phase1_icd_direct.c...
cl /nologo /W3 /O2 /MD /D_CRT_SECURE_NO_WARNINGS test_opengl_phase1_icd_direct.c /link /out:test_opengl_phase1_icd_direct.exe gdi32.lib user32.lib
if %ERRORLEVEL% neq 0 (
    echo [ERRO] Falha na compilacao de test_opengl_phase1_icd_direct.exe.
    exit /b %ERRORLEVEL%
)
echo [OK] test_opengl_phase1_icd_direct.exe compilado com sucesso!

echo.
echo [3/6] Compilando test_opengl_phase2_zink_bridge.c...
cl /nologo /W3 /O2 /MD /D_CRT_SECURE_NO_WARNINGS test_opengl_phase2_zink_bridge.c /link /out:test_opengl_phase2_zink_bridge.exe gdi32.lib user32.lib
if %ERRORLEVEL% neq 0 (
    echo [ERRO] Falha na compilacao de test_opengl_phase2_zink_bridge.exe.
    exit /b %ERRORLEVEL%
)
echo [OK] test_opengl_phase2_zink_bridge.exe compilado com sucesso!

echo.
echo [4/6] Compilando test_opengl_phase3_glsl_shaders.c...
cl /nologo /W3 /O2 /MD /D_CRT_SECURE_NO_WARNINGS test_opengl_phase3_glsl_shaders.c /link /out:test_opengl_phase3_glsl_shaders.exe gdi32.lib user32.lib
if %ERRORLEVEL% neq 0 (
    echo [ERRO] Falha na compilacao de test_opengl_phase3_glsl_shaders.exe.
    exit /b %ERRORLEVEL%
)
echo [OK] test_opengl_phase3_glsl_shaders.exe compilado com sucesso!

echo.
echo [5/6] Compilando test_opengl_phase4_fbo_textures.c e test_opengl_phase5_3d_render.c...
cl /nologo /W3 /O2 /MD /D_CRT_SECURE_NO_WARNINGS test_opengl_phase4_fbo_textures.c /link /out:test_opengl_phase4_fbo_textures.exe gdi32.lib user32.lib
if %ERRORLEVEL% neq 0 (
    echo [ERRO] Falha na compilacao de test_opengl_phase4_fbo_textures.exe.
    exit /b %ERRORLEVEL%
)

cl /nologo /W3 /O2 /MD /D_CRT_SECURE_NO_WARNINGS test_opengl_phase5_3d_render.c /link /out:test_opengl_phase5_3d_render.exe gdi32.lib user32.lib
if %ERRORLEVEL% neq 0 (
    echo [ERRO] Falha na compilacao de test_opengl_phase5_3d_render.exe.
    exit /b %ERRORLEVEL%
)
echo [OK] test_opengl_phase4_fbo_textures.exe e test_opengl_phase5_3d_render.exe compilados com sucesso!

echo.
echo [6/6] Compilando test_opengl_phase6_stress_benchmark.c...
cl /nologo /W3 /O2 /MD /D_CRT_SECURE_NO_WARNINGS test_opengl_phase6_stress_benchmark.c /link /out:test_opengl_phase6_stress_benchmark.exe gdi32.lib user32.lib
if %ERRORLEVEL% neq 0 (
    echo [ERRO] Falha na compilacao de test_opengl_phase6_stress_benchmark.exe.
    exit /b %ERRORLEVEL%
)
echo [OK] test_opengl_phase6_stress_benchmark.exe compilado com sucesso!

echo.
echo ========================================================
echo   Compilacao Concluida com Sucesso (100%% das Fases)!
echo ========================================================
