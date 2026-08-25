[CmdletBinding()]
param([switch]$Execute)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$packageDir = Join-Path $root 'driver\wdk\package\output'
$inf = Join-Path $packageDir 'bc250readonly.inf'
$sys = Join-Path $packageDir 'bc250readonly.sys'
$cat = Join-Path $packageDir 'bc250readonly.cat'
$marker = 'C:\BC250_TEST_OEMINF.txt'
$recoveryRoot = 'C:\BC250_RECOVERY.cmd'
$recoverySource = Join-Path $root 'tools\BC250_RECOVERY.cmd'

foreach ($path in @($inf, $sys, $cat)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Pacote incompleto: $path" }
}
$infText = Get-Content -LiteralPath $inf -Raw
if ($infText -notmatch '(?im)^DriverVer=08/25/2026,0\.5\.0\.0\s*$') {
    throw 'Pre-stage recusado: o pacote nao e exatamente 0.5.0.0.'
}
if (-not (Test-Path -LiteralPath $recoveryRoot)) {
    throw 'Recuperacao C:\BC250_RECOVERY.cmd ausente.'
}
if ((Get-FileHash $recoveryRoot).Hash -ne (Get-FileHash $recoverySource).Hash) {
    throw 'Recuperacao em C:\ diverge da fonte versionada.'
}
if (Get-Service bc250v4 -ErrorAction SilentlyContinue) {
    throw 'Servico legado bc250v4 existe; pre-stage recusado.'
}
if (Get-Service bc250readonly -ErrorAction SilentlyContinue) {
    throw 'Servico bc250readonly existe; recupere o ensaio anterior antes do pre-stage.'
}
if (Test-Path -LiteralPath $marker) {
    throw 'Marcador OEM existente; recupere ou use o refresh controlado antes do pre-stage.'
}
if ((Get-AuthenticodeSignature $sys).Status -ne 'Valid' -or
    (Get-AuthenticodeSignature $cat).Status -ne 'Valid') {
    throw 'Assinatura do pacote nao e valida.'
}

Write-Host 'Preflight aprovado: pacote assinado e recuperacao identica.'
Write-Host 'Esta fase apenas adiciona o INF ao Driver Store; nao usa /install.'
if (-not $Execute) {
    Write-Host 'Zero escrita: repita com -Execute para pre-estagiar.'
    Write-Host 'Veredito: NAO VALIDADO EM HARDWARE'
    exit 0
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em PowerShell elevado.'
}

$output = (& pnputil.exe /add-driver $inf 2>&1) -join "`n"
$exitCode = $LASTEXITCODE
$output | Write-Host
if ($exitCode -ne 0) { throw "pnputil /add-driver falhou: exit=$exitCode" }
$publishedNames = @(
    [regex]::Matches($output, '(?i)\boem\d+\.inf\b') |
        ForEach-Object { $_.Value.ToLowerInvariant() } |
        Select-Object -Unique
)
if ($publishedNames.Count -ne 1) {
    throw 'Pacote entrou no Driver Store, mas o nome oemNN.inf nao foi identificado de forma unica. Nao ative o driver.'
}
$publishedName = $publishedNames[0]
$publishedName | Set-Content -LiteralPath $marker -Encoding Ascii
$writtenLines = @(Get-Content -LiteralPath $marker)
if ($writtenLines.Count -ne 1 -or $writtenLines[0] -cne $publishedName) {
    throw 'Marcador gravado diverge do nome publicado. Nao ative o driver.'
}
Write-Host "Marcador de recuperacao gravado: $marker = $publishedName"
Write-Host 'Dispositivo nao foi vinculado; nenhuma troca de driver foi solicitada.'
Write-Host 'Veredito: NAO VALIDADO EM HARDWARE'
