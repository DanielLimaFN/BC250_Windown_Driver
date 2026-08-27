<#
.SYNOPSIS
    Test Runner Completo do Roadmap OpenGL 4.0+ da AMD BC-250 (Fases 1 a 6)
.DESCRIPTION
    Executa a suite completa de 6 Fases do Roadmap OpenGL:
    - FASE 1: Driver WGL ICD e Negociação de Contextos Modernos
    - FASE 2: Integração Mesa Zink Gallium + Vulkan RADV
    - FASE 3: Pipeline GLSL 4.00+ & Compute Shaders GLSL 4.30 NIR
    - FASE 4: VAO, VBO, ReBAR Memory Mapping, Texturas HDR e FBO 1080p
    - FASE 5: Renderização 3D, DrawCalls Instanciadas e WGL SwapBuffers
    - FASE 6: Teste de Estresse Massivo, Zero Memory Leaks e Homologação Final
#>

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $ScriptDir

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "   AMD BC-250: SUITE COMPLETA DE TESTES OPENGL 4.60     " -ForegroundColor Cyan
Write-Host "   (Cyan Skillfish / Navi12 Lite - 1536 Shaders / 16GB) " -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

# Compilar testes se necessário
Write-Host "`n[*] Verificando e compilando suites de teste (Fases 1 a 6)..." -ForegroundColor Yellow
cmd.exe /c "compile_opengl_tests.bat"

$Test1Direct = Join-Path $ScriptDir "test_opengl_phase1_icd_direct.exe"
$Test1Wgl    = Join-Path $ScriptDir "test_opengl_phase1_wgl_context.exe"
$Test2Zink   = Join-Path $ScriptDir "test_opengl_phase2_zink_bridge.exe"
$Test3Shader = Join-Path $ScriptDir "test_opengl_phase3_glsl_shaders.exe"
$Test4Fbo    = Join-Path $ScriptDir "test_opengl_phase4_fbo_textures.exe"
$Test5Render = Join-Path $ScriptDir "test_opengl_phase5_3d_render.exe"
$Test6Stress = Join-Path $ScriptDir "test_opengl_phase6_stress_benchmark.exe"

Write-Host "`n>>> [FASE 1.1] Teste Direto do Driver WGL ICD..." -ForegroundColor Green
$p1 = Start-Process -FilePath $Test1Direct -NoNewWindow -PassThru -Wait

Write-Host "`n>>> [FASE 1.2] Teste de Contexto GDI/WGL Win32..." -ForegroundColor Green
$p2 = Start-Process -FilePath $Test1Wgl -NoNewWindow -PassThru -Wait

Write-Host "`n>>> [FASE 2] Teste da Ponte Mesa Zink (OpenGL 4.6 -> Vulkan RADV)..." -ForegroundColor Green
$p3 = Start-Process -FilePath $Test2Zink -NoNewWindow -PassThru -Wait

Write-Host "`n>>> [FASE 3] Teste do Pipeline de Shaders GLSL 4.00+ & Compute GLSL 4.30..." -ForegroundColor Green
$p4 = Start-Process -FilePath $Test3Shader -NoNewWindow -PassThru -Wait

Write-Host "`n>>> [FASE 4] Teste de VAO, Texturas, Mapeamento ReBAR & FBOs 1080p..." -ForegroundColor Green
$p5 = Start-Process -FilePath $Test4Fbo -NoNewWindow -PassThru -Wait

Write-Host "`n>>> [FASE 5] Teste de Renderização 3D, DrawCalls & WGL SwapBuffers..." -ForegroundColor Green
$p6 = Start-Process -FilePath $Test5Render -NoNewWindow -PassThru -Wait

Write-Host "`n>>> [FASE 6] Teste de Estresse Massivo, VRAM Leaks & Homologação Final..." -ForegroundColor Green
$p7 = Start-Process -FilePath $Test6Stress -NoNewWindow -PassThru -Wait

Write-Host "`n========================================================" -ForegroundColor Cyan
if ($p1.ExitCode -eq 0 -and $p2.ExitCode -eq 0 -and $p3.ExitCode -eq 0 -and 
    $p4.ExitCode -eq 0 -and $p5.ExitCode -eq 0 -and $p6.ExitCode -eq 0 -and $p7.ExitCode -eq 0) {
    Write-Host "  [SUCESSO] 100% DAS 6 FASES DO ROADMAP OPENGL 4.60 HOMOLOGADAS!" -ForegroundColor Green
} else {
    Write-Host "  [FALHA] Erros detectados em uma ou mais fases." -ForegroundColor Red
}
Write-Host "========================================================`n" -ForegroundColor Cyan
