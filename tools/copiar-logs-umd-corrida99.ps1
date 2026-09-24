# Copia os logs da UMD que ficaram em C:\Windows\Temp depois do travamento da
# Corrida 99 para a pasta de evidencia, com hash, e reseta a ACL para a sessao
# de analise (nao elevada) conseguir ler. Executar em PowerShell ELEVADO.
# Uso em outra corrida: -Pasta <pasta de evidencia da corrida>.
param([string]$Pasta = 'C:\Users\Daniel\Desktop\NamixWDDM\docs\evidence\2026-09-24_10-15-52-DESKTOP-HQAG6T4-lab-binding')
$ErrorActionPreference = 'Stop'
$dest = Join-Path $Pasta 'umd-logs-pos-travamento'
if (-not (Test-Path -LiteralPath $dest)) { New-Item -ItemType Directory -Path $dest | Out-Null }
$logs = @(Get-ChildItem -LiteralPath 'C:\Windows\Temp' -Filter 'BC250_UMD_LOG_*.txt' -File)
if ($logs.Count -eq 0) { Write-Host 'Nenhum BC250_UMD_LOG_*.txt em C:\Windows\Temp.'; exit 1 }
$sums = foreach ($log in $logs) {
    Copy-Item -LiteralPath $log.FullName -Destination $dest
    $h = (Get-FileHash -LiteralPath (Join-Path $dest $log.Name) -Algorithm SHA256).Hash
    "$h  $($log.Name)  $($log.Length) bytes  $($log.LastWriteTime.ToString('s'))"
}
$sums | Set-Content -LiteralPath (Join-Path $dest 'SHA256SUMS.txt') -Encoding ASCII
icacls $dest /reset /T /Q | Out-Null
Write-Host "Copiados $($logs.Count) logs para $dest"
$sums | ForEach-Object { Write-Host $_ }
