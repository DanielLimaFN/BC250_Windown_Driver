#===============================================================================
# Test-PM4-DirectRender.ps1
# Script de Homologacao Automatizada de Renderizacao PM4 & Indirect Buffers
# Conforme diretrizes anti-ilusao de BC250_HARDWARE_RULES.md
#===============================================================================

param(
    [switch]$SkipBuild = $false
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoDir = Split-Path -Parent $ScriptDir
$DiagDir = Join-Path $RepoDir "package\diagnostics"

if (!(Test-Path $DiagDir)) {
    New-Item -ItemType Directory -Path $DiagDir -Force | Out-Null
}

Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host "   AMD BC-250 NATIVE DRIVER - HOMOLOGACAO DE RENDERIZACAO DIRETA PM4 (IB)     " -ForegroundColor Cyan
Write-Host "   Arquitetura: 16 GB GDDR6 UMA Compartilhada (Dinamica / BIOS Configurada)   " -ForegroundColor Cyan
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host ""

# 1. Compilacao do teste se necessario
if (!$SkipBuild) {
    Write-Host "[1/3] Compilando suite de testes C (test_pm4_direct_render.c)..." -ForegroundColor Yellow
    $CompileBat = Join-Path $ScriptDir "compile_pm4_render_test.bat"
    & cmd.exe /c $CompileBat
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[-] ERRO: Falha ao compilar test_pm4_direct_render.exe" -ForegroundColor Red
        exit 1
    }
}

$ExePath = Join-Path $ScriptDir "test_pm4_direct_render.exe"
if (!(Test-Path $ExePath)) {
    Write-Host "[-] ERRO: Executavel $ExePath nao encontrado!" -ForegroundColor Red
    exit 1
}

# 2. Execucao do teste de silicio
Write-Host "[2/3] Executando validacao no silicio real da BC-250..." -ForegroundColor Yellow
$TestOutput = & $ExePath
$TestExitCode = $LASTEXITCODE

Write-Host $TestOutput

# 3. Consolidacao do Log de Evidencias (Anti-Ilusao)
Write-Host "[3/3] Registrando evidencias fisicas em package/diagnostics/pm4-render-tests.json..." -ForegroundColor Yellow

$Timestamp = (Get-Date).ToString("yyyy-MM-ddTHH:mm:sszzz")
$LogObj = [PSCustomObject]@{
    test_id              = "REQ-23-PM4-DIRECT-RENDER-IB"
    timestamp            = $Timestamp
    hardware_target      = "AMD BC-250 (Cyan Skillfish / GFX1013 / 1536 Shaders / 16 GB GDDR6 UMA)"
    memory_architecture  = "16 GB GDDR6 UMA Compartilhada Dinamicamente entre CPU e GPU (Ajustavel na BIOS)"
    execution_engine     = "CP PM4 Ring 0 via PACKET3_INDIRECT_BUFFER (Opcode 0x3F)"
    status               = if ($TestExitCode -eq 0) { "COMPROVADO" } else { "FALHOU" }
    exit_code            = $TestExitCode
}

$LogJson = $LogObj | ConvertTo-Json -Depth 5
$LogPath = Join-Path $DiagDir "pm4-render-tests.json"
$LogJson | Out-File -FilePath $LogPath -Encoding utf8 -Force

Write-Host ""
if ($TestExitCode -eq 0) {
    Write-Host ">>> SUCESSO: RENDERIZACAO GRAFICA DIRETA PM4 COMPROVADA EM SILICIO! <<<" -ForegroundColor Green
} else {
    Write-Host ">>> FALHA NA VALIDACAO EM SILICIO: VERIFIQUE O LOG DE REGISTRADORES <<<" -ForegroundColor Red
}
Write-Host "Log gravado em: $LogPath" -ForegroundColor Cyan
Write-Host ""
