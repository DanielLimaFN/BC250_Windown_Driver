# Script de Instalacao e Diagnostico Fisico Real (AMD BC-250)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$pkgPath = "$root\driver\wdk\output\bc250compute_pkg.sys"
$sysPath = "$root\driver\wdk\output\bc250compute.sys"
$diagPath = "$root\driver\wdk\output\bc250-hardware-diag.exe"

Write-Host "=== REINICIANDO DRIVER COM SCAN PCI HARDWARE REAL ===" -ForegroundColor Cyan

# 1. Parar e remover servico anterior se existir
sc.exe stop bc250compute 2>$null | Out-Null
sc.exe delete bc250compute 2>$null | Out-Null
Start-Sleep -Seconds 1

# 2. Atualizar binario do driver
if (Test-Path $pkgPath) {
    Copy-Item -Path $pkgPath -Destination $sysPath -Force
}

# 3. Criar o servico de kernel
Write-Host "[+] Registrando servico de kernel bc250compute..."
$createResult = & sc.exe create bc250compute binPath= "$sysPath" type= kernel start= demand
Write-Host $createResult

# 4. Iniciar o servico de kernel
Write-Host "[+] Iniciando servico bc250compute..."
$startResult = & sc.exe start bc250compute
Write-Host $startResult

Start-Sleep -Seconds 1

# 5. Executar o Diagnostico Fisico Direto
Write-Host "`n=== LENDO REGISTRADORES FISICOS NO BARRAMENTO PCIe ===" -ForegroundColor Yellow
& $diagPath
