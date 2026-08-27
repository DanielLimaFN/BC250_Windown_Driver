<#
.SYNOPSIS
    Test Runner da FASE 1 do Roadmap OpenGL 4.0+ da AMD BC-250
.DESCRIPTION
    Executa a suite completa de testes da FASE 1:
    1. Teste de Sistema WGL Padrão (Win32 Context Negotiation)
    2. Teste Direto do Driver WGL ICD (atio6xxx.dll / bc250_opengl32.dll)
#>

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $ScriptDir

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "  AMD BC-250: Teste Automatizado OpenGL 4.0+ (FASE 1)   " -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

# Compilar WGL ICD se necessário
$IcdPath = Join-Path $ScriptDir "bc250_opengl32.dll"
if (-not (Test-Path $IcdPath)) {
    Write-Host "[*] Compilando WGL ICD..." -ForegroundColor Yellow
    $WglCompileBat = Join-Path (Join-Path (Split-Path -Parent $ScriptDir) "opengl") "compile_wgl_icd.bat"
    cmd.exe /c $WglCompileBat
}

# Compilar testes se necessário
$TestDirectExe = Join-Path $ScriptDir "test_opengl_phase1_icd_direct.exe"
$TestWglExe = Join-Path $ScriptDir "test_opengl_phase1_wgl_context.exe"

if ((-not (Test-Path $TestDirectExe)) -or (-not (Test-Path $TestWglExe))) {
    Write-Host "[*] Compilando executaveis de teste..." -ForegroundColor Yellow
    cmd.exe /c "compile_opengl_tests.bat"
}

Write-Host "`n>>> [1/2] Executando Teste Direto do WGL ICD (bc250_opengl32.dll / atio6xxx.dll)..." -ForegroundColor Yellow
$p1 = Start-Process -FilePath $TestDirectExe -NoNewWindow -PassThru -Wait

Write-Host "`n>>> [2/2] Executando Teste de Contexto GDI/WGL Win32..." -ForegroundColor Yellow
$p2 = Start-Process -FilePath $TestWglExe -NoNewWindow -PassThru -Wait

Write-Host "`n========================================================" -ForegroundColor Cyan
if ($p1.ExitCode -eq 0 -and $p2.ExitCode -eq 0) {
    Write-Host "  [SUCESSO] 100% DOS TESTES DA FASE 1 HOMOLOGADOS!      " -ForegroundColor Green
} else {
    Write-Host "  [FALHA] Codigos de saida: Teste Direct=$($p1.ExitCode), Teste WGL=$($p2.ExitCode)" -ForegroundColor Red
}
Write-Host "========================================================`n" -ForegroundColor Cyan
