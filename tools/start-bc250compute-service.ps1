# Script para registrar e iniciar o servico de kernel bc250compute
$ErrorActionPreference = "Stop"

$sysPath = "C:\Users\Daniel\Desktop\NamixWDDM\driver\wdk\output\bc250compute_pkg.sys"

Write-Host "Registrando e iniciando servico bc250compute..."

# 1. Fechar ferramentas de diagnostico/monitor que possam estar segurando o handle do driver
Get-Process | Where-Object { $_.Name -like "bc250_monitor*" -or $_.Name -like "bc250_silicon*" -or $_.Name -like "bc250-hardware*" } | ForEach-Object {
    Write-Host "[*] Encerrando processo: $($_.ProcessName) (PID $($_.Id))..."
    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 600

# 2. Parar servico anterior
& sc.exe stop bc250compute 2>$null | Out-Null
& sc.exe delete bc250compute 2>$null | Out-Null

# Aguardar o Service Control Manager liberar a exclusao
for ($i = 0; $i -lt 10; $i++) {
    $q = & sc.exe query bc250compute 2>&1
    if ($q -match "1060" -or $q -match "O servi") {
        break
    }
    Start-Sleep -Milliseconds 500
}

# 3. Criar servico de kernel
$createRes = & sc.exe create bc250compute type= kernel binPath= $sysPath 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "[*] CreateService retornou $LASTEXITCODE, tentando atualizar configuracao em memoria..."
    & sc.exe config bc250compute binPath= $sysPath
}

# 4. Iniciar servico
& sc.exe start bc250compute
if ($LASTEXITCODE -ne 0) {
    throw "Falha ao iniciar servico bc250compute (codigo $LASTEXITCODE)"
}

Write-Host "[+] Servico bc250compute iniciado com sucesso!"
Write-Host "    Dispositivo de kernel \\.\Bc250Compute pronto para diagnostico e telemetria."
