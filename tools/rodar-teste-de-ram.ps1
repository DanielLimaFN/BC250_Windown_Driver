[CmdletBinding()]
param(
    # MB a testar; 0 = 70% da RAM livre (teto 12 GB).
    [int]$MB = 0,
    [int]$Passes = 2,
    # Pasta de saida; padrao: a *-lab-binding mais recente.
    [string]$EvidenceDir = ''
)

# rodar-teste-de-ram.ps1 (doc 595) -- roda tools\bc250-ramtest\out\bc250_ramtest.exe e grava o
# relatorio na pasta de evidencia da ultima corrida (ramtest-<hora>.txt). Nao precisa de elevacao.
# A coleta automatica no boot ja roda o mesmo teste depois de uma trava; este e' o avulso.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'tools\bc250-ramtest\out\bc250_ramtest.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    & cmd.exe /c (Join-Path $root 'tools\bc250-ramtest\build.bat')
    if (-not (Test-Path -LiteralPath $exe)) { throw "Nao compilou: $exe" }
}
if (-not $EvidenceDir) {
    $last = Get-ChildItem -LiteralPath (Join-Path $root 'docs\evidence') -Directory |
        Where-Object { $_.Name -like '*-lab-binding' } | Sort-Object CreationTime | Select-Object -Last 1
    $EvidenceDir = if ($last) { $last.FullName } else { (Get-Location).Path }
}
$out = Join-Path $EvidenceDir ('ramtest-{0}.txt' -f (Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'))
$args2 = @('--passes', "$Passes", '--out', $out)
if ($MB -gt 0) { $args2 += @('--mb', "$MB") }
& $exe @args2
$code = $LASTEXITCODE
switch ($code) {
    0 { Write-Host "SEM ERROS no escopo testado. Relatorio: $out" -ForegroundColor Green }
    1 { Write-Host "ERROS DE MEMORIA ENCONTRADOS. Relatorio: $out" -ForegroundColor Red }
    default { Write-Host "O teste nao rodou (exit=$code). Relatorio: $out" -ForegroundColor Yellow }
}
exit $code
