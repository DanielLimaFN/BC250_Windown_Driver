[CmdletBinding()]
param(
    [string]$PackageDir,
    [switch]$Execute,
    [switch]$IAcceptBlackScreenRisk
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($PackageDir)) {
    $PackageDir = Join-Path $root 'driver\wdk\package\output_active'
}
foreach ($name in @('bc250mcdm.inf','bc250mcdm.sys','bc250mcdm.cat')) {
    $path = Join-Path $PackageDir $name
    if (-not (Test-Path -LiteralPath $path)) { throw "Pacote incompleto: $path" }
}

if ($Execute -or $IAcceptBlackScreenRisk) {
    throw ('Fluxo direto replace desativado: ele nao oferece stage separado nem watchdog. ' +
        'Use stage-bc250mcdm-package.ps1, audit-bc250mcdm-staged.ps1 e ' +
        'bind-bc250mcdm-failclosed.ps1.')
}
Write-Host 'REPLACE LEGADO BLOQUEADO EM MODO SOMENTE LEITURA.'
Write-Host 'Use o fluxo transacional stage -> audit -> binding com watchdog.'
Write-Host 'Zero escrita, zero binding, NAO VALIDADO EM HARDWARE.'
exit 0
