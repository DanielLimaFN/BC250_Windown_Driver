# Grava as opcoes de boot (SystemStartOptions) junto da evidencia, sem a chave do KDNET.
# Motivo: o binder nao registra se o depurador de kernel estava ligado (memoria
# bc250-kd-e-dbgprint-derrubam-a-paginacao), e isso muda o tempo do experimento.
param([string]$Rotulo = 'corrida')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$opts = (Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control').SystemStartOptions
$opts = $opts -replace 'ENCRYPTION_KEY=\S+', 'ENCRYPTION_KEY=***'
$dir = Join-Path $root 'docs\evidence\vram-cmos'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$file = Join-Path $dir ((Get-Date -Format 'yyyy-MM-dd_HH-mm-ss') + "-$Rotulo-SystemStartOptions.txt")
Set-Content -LiteralPath $file -Value $opts
Write-Host "SystemStartOptions: $opts"
Write-Host "Gravado em $file"
