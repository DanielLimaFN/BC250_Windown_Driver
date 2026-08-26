[CmdletBinding()]
param([switch]$Execute)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$prebindingAudit = Join-Path $root 'tools\audit-bc250mcdm-prebinding.ps1'
$packageInf = Join-Path $root 'driver\wdk\package\output_active\bc250mcdm.inf'
$marker = 'C:\BC250_TEST_OEMINF.txt'

& powershell -NoProfile -ExecutionPolicy Bypass -File $prebindingAudit
if ($LASTEXITCODE -ne 0) {
    throw 'Pre-stage bloqueado: host nao passou no gate limpo de pre-binding.'
}

Write-Host 'Preflight aprovado. O modo Execute usa somente pnputil /add-driver.'
Write-Host 'Nenhum /install ou binding faz parte deste script.'
if (-not $Execute) {
    Write-Host 'Zero escrita: repita com -Execute somente apos autorizacao explicita.'
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 0
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute o stage em PowerShell elevado.'
}
if (Test-Path -LiteralPath $marker) {
    throw 'Marcador OEM apareceu depois do preflight; stage recusado.'
}

$output = (& pnputil.exe /add-driver $packageInf 2>&1) -join "`n"
$exitCode = $LASTEXITCODE
$output | Write-Host
if ($exitCode -ne 0) { throw "pnputil /add-driver falhou: exit=$exitCode" }
$publishedNames = @(
    [regex]::Matches($output, '(?i)\boem\d+\.inf\b') |
        ForEach-Object { $_.Value.ToLowerInvariant() } |
        Select-Object -Unique
)
if ($publishedNames.Count -ne 1) {
    throw 'Stage ocorreu, mas o nome oemNN.inf nao foi identificado unicamente. Nao faca binding.'
}
$publishedName = $publishedNames[0]
$publishedName | Set-Content -LiteralPath $marker -Encoding ascii
$markerLines = @(Get-Content -LiteralPath $marker)
if ($markerLines.Count -ne 1 -or $markerLines[0] -cne $publishedName) {
    throw 'Marcador gravado diverge do INF publicado. Nao faca binding.'
}

Write-Host "Stage concluido sem binding: $publishedName"
Write-Host "Recovery marker: $marker"
Write-Host 'NAO VALIDADO EM HARDWARE'
