param(
    [Parameter(Mandatory = $true)]
    [string]$EvidenceDir
)

$ErrorActionPreference = 'Stop'
$evidence = (Resolve-Path -LiteralPath $EvidenceDir).Path
$baselinePath = Join-Path $evidence 'paging-trace-baseline.json'
if (-not (Test-Path -LiteralPath $baselinePath -PathType Leaf)) {
    throw "Baseline DX.56 ausente: $baselinePath"
}
$baseline = Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json
$source = [string]$baseline.path
$offset = [int64]$baseline.start_offset
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "Rastro DX.56 ausente: $source"
}
$destination = Join-Path $evidence 'paging-trace-this-bind-RECUPERADO.bin'
if (Test-Path -LiteralPath $destination) {
    throw "Rastro ja arquivado: $destination"
}
$inputFile = [System.IO.File]::Open($source, 'Open', 'Read', 'ReadWrite')
try {
    if ($offset -lt 0 -or $offset -gt $inputFile.Length) {
        throw "Baseline fora do arquivo: offset=$offset bytes=$($inputFile.Length)"
    }
    $null = $inputFile.Seek($offset, 'Begin')
    $outputFile = [System.IO.File]::Open($destination, 'CreateNew', 'Write', 'None')
    try { $inputFile.CopyTo($outputFile) } finally { $outputFile.Dispose() }
} finally { $inputFile.Dispose() }

$hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
$decoder = Join-Path $PSScriptRoot 'decode-bc250-paging-trace.py'
$summaryPath = Join-Path $evidence 'paging-trace-this-bind-RECUPERADO.json'
& python $decoder $destination --last 12 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
if ($LASTEXITCODE -ne 0) { throw "Decodificador DX.56 falhou: exit=$LASTEXITCODE" }
[ordered]@{
    source = $source
    baseline_offset = $offset
    bytes = (Get-Item -LiteralPath $destination).Length
    sha256 = $hash
    recovered_at = (Get-Date).ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath `
    (Join-Path $evidence 'paging-trace-recovery-manifest.json') -Encoding UTF8
Write-Host "DX.56 arquivado: $destination SHA256=$hash"
