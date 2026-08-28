# Script para registrar e iniciar o servico de kernel bc250compute
$ErrorActionPreference = "Stop"

$sysPath = "C:\Users\Daniel\Desktop\NamixWDDM\driver\wdk\output\bc250compute_pkg.sys"

if (!(Test-Path $sysPath)) {
    throw "Binario bc250compute.sys nao encontrado em $sysPath"
}

Write-Host "Registrando e iniciando servico bc250compute..."

# Parar e remover instancia anterior se existente
& sc.exe stop bc250compute 2>$null | Out-Null
& sc.exe delete bc250compute 2>$null | Out-Null
Start-Sleep -Milliseconds 500

# Criar servico de kernel
& sc.exe create bc250compute type= kernel binPath= $sysPath
if ($LASTEXITCODE -ne 0) {
    throw "Falha ao criar servico bc250compute (codigo $LASTEXITCODE)"
}

# Iniciar servico
& sc.exe start bc250compute
if ($LASTEXITCODE -ne 0) {
    throw "Falha ao iniciar servico bc250compute (codigo $LASTEXITCODE)"
}

Write-Host "[+] Servico bc250compute iniciado com sucesso!"
Write-Host "    Dispositivo de kernel \\.\Bc250Compute pronto para diagnostico e telemetria."
