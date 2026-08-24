# Script de Validacao da Fase 5: Compute & GPGPU (1536 Shaders / 24 CUs)
# Executa a suite de testes de compute em modo usuario e le as metricas do kernel.

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host "   BC-250 WDDM - Teste de Validacao da Fase 5 (Compute & GPGPU)  " -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host ""

$regPath = "HKLM:\SYSTEM\CurrentControlSet\Services\bc250v2\Parameters"

if (Test-Path $regPath) {
    Set-ItemProperty -Path $regPath -Name "EnableDangerousEscapes" -Value 1 -ErrorAction SilentlyContinue
    Write-Host "  [+] Escape interface autorizada no Registro (EnableDangerousEscapes=1)" -ForegroundColor Green
}

$exePath = Join-Path $PSScriptRoot "test_phase5_compute.exe"
if (-not (Test-Path $exePath)) {
    $exePath = Join-Path $PSScriptRoot "..\build\test_phase5_compute.exe"
}

if (Test-Path $exePath) {
    Write-Host "  [+] Executando aplicacao de teste GPGPU: $exePath" -ForegroundColor Green
    & $exePath
} else {
    Write-Host "  [!] Executavel test_phase5_compute.exe nao encontrado. Execute compile_tests.bat primeiro." -ForegroundColor Yellow
}
