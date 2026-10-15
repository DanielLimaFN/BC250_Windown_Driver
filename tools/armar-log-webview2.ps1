# SHD.1 (doc 752): liga o log do Chromium (WebView2) para provar se a Pesquisa usa a nossa GPU.
# Rodar ELEVADO antes de desligar a maquina. -Desarmar remove.
# O WebView2 le WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS do ambiente; com --enable-logging
# ele grava chrome_debug.log na pasta de dados do app (ex.: Packages\Microsoft.Windows.Search_*\...\EBWebView).
param([switch]$Desarmar)
$ErrorActionPreference = 'Stop'
$name = 'WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'
if ($Desarmar) {
    [Environment]::SetEnvironmentVariable($name, $null, 'Machine')
    Write-Host "Log do WebView2 DESARMADO ($name removida). Vale a partir do proximo logon." -ForegroundColor Yellow
    exit 0
}
$args2 = '--enable-logging --v=0 --log-level=0'
[Environment]::SetEnvironmentVariable($name, $args2, 'Machine')
Write-Host "Log do WebView2 ARMADO: $name=$args2" -ForegroundColor Green
Write-Host "Agora desligue (nao reinicie) e ligue a maquina; o log aparece na proxima corrida."
