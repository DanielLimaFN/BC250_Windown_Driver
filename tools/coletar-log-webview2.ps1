# SHD.1 (doc 752): copia os chrome_debug.log do WebView2 escritos na ultima hora para a pasta
# de evidencia mais recente, e resume as linhas de GPU (o que o Chromium decidiu sobre a GPU).
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$ev = Get-ChildItem (Join-Path $root 'docs\evidence') -Directory -Filter '*lab-binding' |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $ev) { Write-Host '[WEBVIEW2] pasta de evidencia nao encontrada'; exit 0 }
$dest = Join-Path $ev.FullName 'webview2-logs'
New-Item -ItemType Directory -Force $dest | Out-Null
$since = (Get-Date).AddHours(-1)
$pkgs = Join-Path $env:LOCALAPPDATA 'Packages'
$found = Get-ChildItem $pkgs -Recurse -Filter 'chrome_debug.log' -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -ge $since }
$i = 0
foreach ($f in $found) {
    $i++
    $pkg = ($f.FullName.Substring($pkgs.Length + 1) -split '\')[0]
    $out = Join-Path $dest ("{0}_{1}_chrome_debug.log" -f $i, $pkg)
    Copy-Item -LiteralPath $f.FullName -Destination $out -Force
    Write-Host "[WEBVIEW2] copiado: $($f.FullName)"
}
if ($i -eq 0) { Write-Host '[WEBVIEW2] nenhum chrome_debug.log na ultima hora (o log estava armado? tools\armar-log-webview2.ps1)'; exit 0 }
$resumo = Join-Path $dest 'resumo-gpu.txt'
Get-ChildItem $dest -Filter '*chrome_debug.log' | ForEach-Object {
    "=== $($_.Name)"
    Select-String -LiteralPath $_.FullName -Pattern 'gpu|GPU|D3D11|ANGLE|SwiftShader|software|blocklist|feature level|FeatureLevel|DXGI|disabled' |
        Select-Object -First 200 | ForEach-Object { $_.Line }
} | Set-Content -Encoding utf8 $resumo
Write-Host "[WEBVIEW2] resumo das linhas de GPU em $resumo"
