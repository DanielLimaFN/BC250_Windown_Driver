[CmdletBinding()]
param(
    [int]$Minutos = 45,
    [int]$MbTotal = 8192
)

# W1 (PLANO_MESTRE Fase 1.2, doc 625): a plataforma trava SEM o nosso driver?
# Roda tools\bc250-platform-stress.exe com o BasicDisplay ativo. Nao toca em driver,
# registrador nem configuracao: so' le o estado e arquiva a saida.
# Desfechos: trava -> plataforma (fonte, memoria, calor); erro de dados -> memoria;
# 45 min limpos -> a trava precisa da GPU trabalhando (com o nosso driver).

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $PSScriptRoot 'bc250-platform-stress.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "Ausente: $exe" }

# Preflight 1: o adaptador da BC-250 tem de estar no BasicDisplay, nao no bc250mcdm.
$gpus = Get-CimInstance Win32_VideoController
$gpus | ForEach-Object { Write-Host ("Adaptador: {0} | status={1} | driver={2}" -f $_.Name, $_.Status, $_.InstalledDisplayDrivers) }
if ($gpus | Where-Object { $_.InstalledDisplayDrivers -match 'bc250' -or $_.Name -match 'BC-?250' }) {
    throw 'O nosso driver esta ativo. Rode tools\BC250_RECOVERY.cmd elevado, reinicie e repita.'
}
if (-not ($gpus | Where-Object { $_.Name -match 'Basic Display|Video Basico|Adaptador de V' })) {
    throw 'BasicDisplay nao encontrado como adaptador ativo; o W1 exige o adaptador basico da Microsoft.'
}
$svc = Get-Service -Name 'bc250mcdm' -ErrorAction SilentlyContinue
if ($svc -and $svc.Status -eq 'Running') { throw 'Servico bc250mcdm em execucao.' }

# Preflight 2: boot frio recente (estado de engine de corrida anterior nao cai com desbind).
$boot = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
Write-Host "Ultimo boot: $boot"

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$ev = Join-Path $root "docs\evidence\$stamp-$env:COMPUTERNAME-W1-plataforma"
New-Item -ItemType Directory -Force $ev | Out-Null
$gpus | Select-Object Name, Status, InstalledDisplayDrivers, DriverVersion |
    Format-List | Out-File -LiteralPath (Join-Path $ev 'adaptadores.txt') -Encoding UTF8
@{
    inicio = (Get-Date).ToString('o'); ultimo_boot = $boot.ToString('o')
    minutos = $Minutos; mb_total = $MbTotal
    exe_sha256 = (Get-FileHash $exe -Algorithm SHA256).Hash
    ram_bytes = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ev 'w1-manifesto.json') -Encoding UTF8

Write-Host ''
Write-Host "W1: $Minutos min, $MbTotal MB, todos os processadores. Evidencia: $ev" -ForegroundColor Cyan
Write-Host 'NAO use a maquina durante o teste. Se TRAVAR: anote a hora e use o BOTAO de reset.'
Write-Host 'Depois de uma trava, rode elevado tools\vram-cmos\bc250-vram.ps1 -Ler antes de tudo.'
& $exe $Minutos $MbTotal 0 $ev
$code = $LASTEXITCODE
Add-Content -LiteralPath (Join-Path $ev 'w1-manifesto.json') -Value "`r`n// exit=$code fim=$((Get-Date).ToString('o'))"
switch ($code) {
    0 { Write-Host 'W1: SEM TRAVA E SEM ERRO DE DADOS no tempo pedido.' -ForegroundColor Green }
    3 { Write-Host 'W1: ERRO DE DADOS na memoria (ver o arquivo bc250-platform-stress_*.txt).' -ForegroundColor Red }
    default { Write-Host "W1: falha de preparacao (codigo $code)." -ForegroundColor Yellow }
}
exit $code
