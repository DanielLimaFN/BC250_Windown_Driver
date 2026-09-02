<#
.SYNOPSIS
    setup-bc250-kernel-debugging.ps1 — Prepara o ambiente local/remoto de Kernel Debugging para diagnóstico do RenderNode.
#>

[CmdletBinding()]
param(
    [switch]$ConfigureLocalKd,
    [string]$HostIp,
    [int]$KdPort = 50000,
    [string]$KdKey = "1.2.3.4",
    [switch]$CheckOnly
)

$ErrorActionPreference = "Stop"

Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host " BC-250 KERNEL DEBUGGING (KD/WINDBG) ENVIRONMENT SETUP" -ForegroundColor Cyan
Write-Host "================================================================================" -ForegroundColor Cyan

# 1. Verificar privilégios de Administrador
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Warning "[AVISO] Execute este script em um PowerShell elevado (Administrador) para configurar BCD / KD."
}

# 2. Verificar PDB do driver
$pdbLab = "C:\Users\Daniel\Desktop\NamixWDDM\driver\wdk\output_lab\bc250mcdm.pdb"
$pdbRelease = "C:\Users\Daniel\Desktop\NamixWDDM\driver\wdk\output\bc250mcdm.pdb"
$pdbFound = $false

if (Test-Path $pdbLab) {
    $item = Get-Item $pdbLab
    Write-Host "[OK] PDB de Laboratório encontrado: $pdbLab ($($item.Length) bytes, Modificado: $($item.LastWriteTime))" -ForegroundColor Green
    $pdbFound = $true
} elseif (Test-Path $pdbRelease) {
    $item = Get-Item $pdbRelease
    Write-Host "[OK] PDB Release encontrado: $pdbRelease ($($item.Length) bytes)" -ForegroundColor Green
    $pdbFound = $true
} else {
    Write-Warning "[ALERTA] PDB bc250mcdm.pdb não encontrado em output_lab/ nem output/. Execute o build antes de iniciar o KD."
}

# 3. Verificar diretório de símbolos locais
$symDir = "C:\symbols"
if (-not (Test-Path $symDir)) {
    Write-Host "[INFO] Criando diretório de símbolos locais em $symDir..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $symDir -Force | Out-Null
}
Write-Host "[OK] Diretório de cache de símbolos: $symDir" -ForegroundColor Green

# 4. Verificar Script WinDbg
$wdsScript = "C:\Users\Daniel\Desktop\NamixWDDM\tools\windbg_diagnose_rendernode.wds"
if (Test-Path $wdsScript) {
    Write-Host "[OK] Script de diagnóstico WinDbg: $wdsScript" -ForegroundColor Green
} else {
    Write-Warning "[ALERTA] Script WinDbg $wdsScript não encontrado."
}

# 5. Configuração BCD (se solicitado)
if ($ConfigureLocalKd) {
    Write-Host "`n[CONFIG] Habilitando Local Kernel Debugging via bcdedit..." -ForegroundColor Yellow
    cmd.exe /c "bcdedit /debug on"
    cmd.exe /c "bcdedit /bootdebug on"
    Write-Host "[OK] Depuração local configurada. O Windows pode requerer reboot para ativar live KD." -ForegroundColor Green
}

if ($HostIp -ne "") {
    Write-Host "`n[CONFIG] Configurando KDNET para Host $HostIp (Porta $KdPort, Chave $KdKey)..." -ForegroundColor Yellow
    cmd.exe /c "bcdedit /debug on"
    cmd.exe /c "bcdedit /dbgsettings net hostip:$HostIp port:$KdPort key:$KdKey"
    Write-Host "[OK] KDNET configurado com sucesso." -ForegroundColor Green
}

# 6. Informar comandos para execução
Write-Host "`n--------------------------------------------------------------------------------" -ForegroundColor Cyan
Write-Host " COMANDOS DE INICIALIZAÇÃO DO WINDBG" -ForegroundColor Cyan
Write-Host "--------------------------------------------------------------------------------" -ForegroundColor Cyan
Write-Host "Modo Live Kernel Debug (Local):" -ForegroundColor White
Write-Host '  windbg.exe -kl -c "$$< C:\Users\Daniel\Desktop\NamixWDDM\tools\windbg_diagnose_rendernode.wds"' -ForegroundColor Yellow

Write-Host "`nModo KDNET (No Host conectado ao Target):" -ForegroundColor White
Write-Host "  windbg.exe -k net:port=$KdPort,key=$KdKey" -ForegroundColor Yellow
Write-Host '  (Dentro do WinDbg: $$< C:\Users\Daniel\Desktop\NamixWDDM\tools\windbg_diagnose_rendernode.wds)' -ForegroundColor Yellow
Write-Host "================================================================================" -ForegroundColor Cyan
