# Script para registrar, assinar e iniciar o servico de kernel bc250compute
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$sysPath = "$root\driver\wdk\output\bc250compute_pkg.sys"
$srcSys = "$root\driver\wdk\output\bc250compute.sys"
$certificateThumbprint = "8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B"

Write-Host "Registrando e iniciando servico bc250compute..."

# 1. Fechar ferramentas de diagnostico/monitor que possam estar segurando o handle do driver
Get-Process | Where-Object { $_.Name -like "bc250_monitor*" -or $_.Name -like "bc250_silicon*" -or $_.Name -like "bc250-hardware*" -or $_.Name -like "bc250-umd*" -or $_.Name -like "bc250-vulkan*" -or $_.Name -like "bc250-llm*" } | ForEach-Object {
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

# Atualizar binario com retry seguro
if (Test-Path $srcSys) {
    $copied = $false
    for ($attempt = 0; $attempt -lt 10; $attempt++) {
        try {
            Copy-Item -LiteralPath $srcSys -Destination $sysPath -Force -ErrorAction Stop
            $copied = $true
            break
        } catch {
            Start-Sleep -Milliseconds 600
        }
    }
}

# Assinar digitalmente o binario de kernel com o certificado de teste
$signtoolPaths = @(
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe",
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe",
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe"
)
$signtool = $null
foreach ($p in $signtoolPaths) {
    if (Test-Path $p) { $signtool = $p; break }
}

if ($signtool -and (Test-Path $sysPath)) {
    Write-Host "[*] Assinando digitalmente $sysPath..."
    & $signtool sign /sha1 $certificateThumbprint /fd SHA256 $sysPath
    & $signtool verify /pa /v $sysPath | Out-Null
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
