<#
.SYNOPSIS
    Executa o ciclo completo de instalação de laboratório com Watchdog e ensaio F4d (ENG17/PFP).
#>
[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptTheRisk,
    [ValidateRange(30,600)][int]$WatchdogTimeoutSeconds = 180,
    [int]$Iterations = 40
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

Write-Host "==========================================================================" -ForegroundColor Cyan
Write-Host "  BC-250: CICLO COMPLETO DE LABORATORIO (ENG17 + PFP DIRECT LOAD)" -ForegroundColor Cyan
Write-Host "==========================================================================" -ForegroundColor Cyan
Write-Host ""

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "ERRO: Este script DEVE ser executado em uma janela do PowerShell como Administrador." -ForegroundColor Red
    Write-Host "Por favor, abra o PowerShell como Administrador e execute este comando." -ForegroundColor Yellow
    exit 1
}

# 1. Empacotamento
Write-Host "`n>>> [Passo 1/3] Empacotando e assinando a imagem de laboratorio..." -ForegroundColor Yellow
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\package-bc250mcdm-lab.ps1')
if ($LASTEXITCODE -ne 0) { throw "Empacotamento de laboratorio falhou." }

# 2. Binding com Watchdog
Write-Host "`n>>> [Passo 2/3] Vinculando driver de laboratorio com Watchdog ($WatchdogTimeoutSeconds s)..." -ForegroundColor Yellow
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\bind-bc250mcdm-lab.ps1') `
    -Execute -IAcceptBlackScreenRisk -WatchdogTimeoutSeconds $WatchdogTimeoutSeconds
if ($LASTEXITCODE -ne 0) { throw "Binding de laboratorio falhou." }

# 3. Execução do Ensaio F4d
Write-Host "`n>>> [Passo 3/3] Executando Ensaio de Intermitencia PFP / ENG17 ($Iterations ciclos)..." -ForegroundColor Yellow
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\ensaio-pfp-intermitencia.ps1') `
    -Iterations $Iterations -StopOnFirstFailure -Execute -IAcceptTheRisk -AllowDriverMismatch

$ensaioExit = $LASTEXITCODE

Write-Host "`n==========================================================================" -ForegroundColor Cyan
Write-Host "  CICLO CONCLUIDO (Exit: $ensaioExit)" -ForegroundColor Cyan
Write-Host "==========================================================================" -ForegroundColor Cyan

if (Test-Path -LiteralPath (Join-Path $root 'tools\_last_lab_binding.json')) {
    $lastBinding = Get-Content -LiteralPath (Join-Path $root 'tools\_last_lab_binding.json') -Raw | ConvertFrom-Json
    Write-Host "`nPara desarmar o watchdog e manter o driver se a tela estiver OK:" -ForegroundColor Green
    Write-Host "powershell -ExecutionPolicy Bypass -File tools\confirm-bc250mcdm-lab.ps1 -EvidenceDir `"$($lastBinding.evidence_dir)`" -InstanceId `"$($lastBinding.instance_id)`" -Token `"$($lastBinding.watchdog_token)`" -IConfirmDisplayIsVisible"
    Write-Host "`nOu para reverter imediatamente para o BasicDisplay:" -ForegroundColor Yellow
    Write-Host "powershell -ExecutionPolicy Bypass -File tools\rollback-bc250mcdm-lab.ps1 -Execute -IAcceptDisplayInterruptionRisk"
}
