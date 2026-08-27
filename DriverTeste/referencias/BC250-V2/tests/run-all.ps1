# =============================================================================
#  run-all.ps1 - executa a suite completa e imprime um sumario consolidado
#
#  Uso:
#    powershell -ExecutionPolicy Bypass -File run-all.ps1
#    powershell -ExecutionPolicy Bypass -File run-all.ps1 -Phases 1,2
#    powershell -ExecutionPolicy Bypass -File run-all.ps1 -Quick
#    powershell -ExecutionPolicy Bypass -File run-all.ps1 -Json resultado.json
#
#  Codigo de saida: 0 se nenhum FAIL, 1 caso contrario. XFAIL nao derruba a
#  suite (bloqueio conhecido e documentado); XPASS derruba de proposito, para
#  forcar a revisao de uma expectativa que ficou obsoleta.
# =============================================================================
[CmdletBinding()]
param(
    # String em vez de int[]: com "powershell -File", argumentos de array sao
    # entregues como um unico token ("1,2,3" chega como "123"), o que fazia a
    # validacao de conjunto rejeitar. Fazemos o parse aqui.
    [string]$Phases = '1,2,3,4,5',
    [switch]$Quick,
    [string]$Json = ''
)

. (Join-Path $PSScriptRoot 'lib\Bc250TestLib.ps1')

$selected = New-Object System.Collections.ArrayList
foreach ($tok in ($Phases -split '[,;\s]+')) {
    if ($tok -eq '') { continue }
    $n = 0
    if (-not [int]::TryParse($tok, [ref]$n)) {
        Write-Host ("  [ERRO ] fase invalida: '" + $tok + "'") -ForegroundColor Red
        exit 2
    }
    if ($n -lt 1 -or $n -gt 5) {
        Write-Host ("  [ERRO ] fase fora da faixa 1-5: " + $n) -ForegroundColor Red
        exit 2
    }
    if (-not $selected.Contains($n)) { [void]$selected.Add($n) }
}
if ($selected.Count -eq 0) {
    Write-Host '  [ERRO ] nenhuma fase selecionada' -ForegroundColor Red
    exit 2
}

$loadSeconds = 5.0
$vsyncSamples = 40
if ($Quick) { $loadSeconds = 1.5; $vsyncSamples = 15 }

$plan = @(
    @{ N=1; Script='Test-Fase1-LinhaDeBase.ps1'; Args=@{ VsyncSamples=$vsyncSamples; LoadSeconds=$loadSeconds } }
    @{ N=2; Script='Test-Fase2-Blit2D.ps1';      Args=@{ LoadSeconds=$loadSeconds } }
    @{ N=3; Script='Test-Fase3-Sdma0.ps1';       Args=@{} }
    @{ N=4; Script='Test-Fase4-Compute.ps1';     Args=@{} }
    @{ N=4; Script='Test-Fase4-Vulkan.ps1';      Args=@{} }
    @{ N=5; Script='Test-Fase5-Resolucao.ps1';   Args=@{} }
)

Write-Host ''
Write-Host '################################################################################' -ForegroundColor White
Write-Host '  Suite de testes do driver AMD BC-250 (BC250-V2)' -ForegroundColor White
Write-Host ('  ' + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss') + '   host: ' + $env:COMPUTERNAME) -ForegroundColor Gray
$verNow = Get-Bc250DriverVersion
if ($null -ne $verNow) { Write-Host ("  driver instalado: " + $verNow) -ForegroundColor Gray }
Write-Host '################################################################################' -ForegroundColor White

$results = New-Object System.Collections.ArrayList

foreach ($p in $plan) {
    if (-not $selected.Contains($p.N)) { continue }
    $path = Join-Path $PSScriptRoot $p.Script
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host ("  [ERRO ] script ausente: " + $p.Script) -ForegroundColor Red
        continue
    }
    $a = $p.Args.Clone()
    $a['PassThru'] = $true
    try {
        $r = & $path @a
        if ($null -ne $r) { [void]$results.Add($r) }
    } catch {
        Write-Host ("  [ERRO ] " + $p.Script + ": " + $_.Exception.Message) -ForegroundColor Red
        [void]$results.Add([pscustomobject]@{
            Phase=("FASE " + $p.N); Title='(erro de execucao)'
            Pass=0; Fail=1; XFail=0; XPass=0; Skip=0; Seconds=0
            Bench=@(); Checks=@()
        })
    }
}

# --- Sumario consolidado ----------------------------------------------------
Write-Host ''
Write-Host '################################################################################' -ForegroundColor White
Write-Host '  SUMARIO' -ForegroundColor White
Write-Host '################################################################################' -ForegroundColor White
Write-Host ''
Write-Host ('  {0,-8} {1,-46} {2,5} {3,5} {4,6} {5,6} {6,5}' -f 'FASE','TITULO','PASS','FAIL','XFAIL','XPASS','SKIP') -ForegroundColor Gray
Write-Host ('  ' + ('-' * 84)) -ForegroundColor DarkGray

$tp=0; $tf=0; $txf=0; $txp=0; $ts=0
foreach ($r in $results) {
    $title = $r.Title
    if ($title.Length -gt 44) { $title = $title.Substring(0,43) + '.' }
    $color = 'Green'
    if ($r.Fail -gt 0)  { $color = 'Red' }
    elseif ($r.XPass -gt 0) { $color = 'Cyan' }
    elseif ($r.XFail -gt 0) { $color = 'DarkYellow' }
    elseif ($r.Pass -eq 0)  { $color = 'Yellow' }
    Write-Host ('  {0,-8} {1,-46} {2,5} {3,5} {4,6} {5,6} {6,5}' -f `
        $r.Phase, $title, $r.Pass, $r.Fail, $r.XFail, $r.XPass, $r.Skip) -ForegroundColor $color
    $tp += $r.Pass; $tf += $r.Fail; $txf += $r.XFail; $txp += $r.XPass; $ts += $r.Skip
}
Write-Host ('  ' + ('-' * 84)) -ForegroundColor DarkGray
Write-Host ('  {0,-8} {1,-46} {2,5} {3,5} {4,6} {5,6} {6,5}' -f 'TOTAL','',$tp,$tf,$txf,$txp,$ts) -ForegroundColor White

# --- Benchmarks -------------------------------------------------------------
$allBench = @()
foreach ($r in $results) { if ($null -ne $r.Bench) { $allBench += @($r.Bench) } }
if ($allBench.Count -gt 0) {
    Write-Host ''
    Write-Host '  BENCHMARKS' -ForegroundColor White
    Write-Host ('  ' + ('-' * 84)) -ForegroundColor DarkGray
    foreach ($b in $allBench) {
        $line = '  {0,-44} {1,12} {2,-10}' -f $b.Metric, $b.Value, $b.Unit
        if ($b.Note -ne '') { $line = $line + ' ' + $b.Note }
        Write-Host $line -ForegroundColor Magenta
    }
}

Write-Host ''
if ($txf -gt 0) {
    Write-Host ("  $txf XFAIL = falhas esperadas e documentadas (bloqueio conhecido da Fase 5).") -ForegroundColor DarkYellow
    Write-Host '  Nao derrubam a suite. Ver docs/FASE3_DIAGNOSTICO.md.' -ForegroundColor DarkYellow
}
if ($txp -gt 0) {
    Write-Host ("  $txp XPASS = uma falha esperada passou. Um bloqueio conhecido pode ter sido") -ForegroundColor Cyan
    Write-Host '  resolvido: revise a expectativa no teste antes de comemorar.' -ForegroundColor Cyan
}

# --- Saida JSON opcional ----------------------------------------------------
if ($Json -ne '') {
    $payload = [pscustomobject]@{
        Timestamp     = (Get-Date).ToString('o')
        Host          = $env:COMPUTERNAME
        DriverVersion = $verNow
        Totals        = [pscustomobject]@{ Pass=$tp; Fail=$tf; XFail=$txf; XPass=$txp; Skip=$ts }
        Phases        = $results
    }
    $payload | ConvertTo-Json -Depth 6 | Out-File -FilePath $Json -Encoding utf8
    Write-Host ("  JSON gravado em: " + $Json) -ForegroundColor Gray
}

Write-Host ''
if (($tf -gt 0) -or ($txp -gt 0)) { exit 1 }
exit 0
