[CmdletBinding()]
param(
    [string]$EvidenceDir,
    [string]$StagingDir = 'C:\Windows\Temp\BC250_GFX_IB_DUMP'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $EvidenceDir) {
    $lastPath = Join-Path $PSScriptRoot '_last_lab_binding.json'
    if (-not (Test-Path -LiteralPath $lastPath -PathType Leaf)) {
        throw "Sem proveniencia do bind: $lastPath"
    }
    $EvidenceDir = [string]((Get-Content -LiteralPath $lastPath -Raw | ConvertFrom-Json).evidence_dir)
}
if (-not (Test-Path -LiteralPath $EvidenceDir -PathType Container)) {
    throw "Pasta de evidencia ausente: $EvidenceDir"
}
if (-not (Test-Path -LiteralPath $StagingDir -PathType Container)) {
    throw "Staging RADV ausente: $StagingDir"
}
$destination = Join-Path $EvidenceDir 'gfx-ib-dump'
New-Item -ItemType Directory -Path $destination -Force | Out-Null

# O binder coleta antes do rollback e pode apanhar um arquivo ainda crescendo.
# Esta passagem ocorre depois da recuperacao. Preserva a primeira versao quando
# diverge, compara o hash do destino e nunca interpreta uma copia como submit.
$records = [System.Collections.Generic.List[object]]::new()
$files = @(Get-ChildItem -LiteralPath $StagingDir -File | Where-Object {
    $_.Name -match '^bc250_ib_.*\.bin(\.meta)?$' -or
    $_.Name -match '^bc250_flow_pid.*\.log$'
} | Sort-Object Name)
foreach ($file in $files) {
    $target = Join-Path $destination $file.Name
    $sourceHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    $previousHash = $null
    $action = 'new'
    if (Test-Path -LiteralPath $target -PathType Leaf) {
        $previousHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
        if ($previousHash -eq $sourceHash) {
            $action = 'unchanged'
        } else {
            $snapshot = "$target.at-capture.snapshot"
            if (-not (Test-Path -LiteralPath $snapshot -PathType Leaf)) {
                Copy-Item -LiteralPath $target -Destination $snapshot -ErrorAction Stop
            }
            $action = 'updated'
        }
    }
    if ($action -ne 'unchanged') {
        Copy-Item -LiteralPath $file.FullName -Destination $target -Force -ErrorAction Stop
    }
    $targetHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    if ($targetHash -ne $sourceHash) {
        throw "Copia RADV divergiu: $($file.Name) source=$sourceHash target=$targetHash"
    }
    $records.Add([pscustomobject]@{
        name = $file.Name
        bytes = (Get-Item -LiteralPath $target).Length
        source_sha256 = $sourceHash
        previous_sha256 = $previousHash
        action = $action
    })
}

$bins = @($files | Where-Object { $_.Name -like '*.bin' })
$metas = @($files | Where-Object { $_.Name -like '*.bin.meta' })
$flows = @($files | Where-Object { $_.Name -like '*.log' })
$names = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($file in $files) { $null = $names.Add($file.Name) }
$orphanBins = @($bins | Where-Object { -not $names.Contains($_.Name + '.meta') } | ForEach-Object Name)
$orphanMetas = @($metas | Where-Object { -not $names.Contains($_.Name.Substring(0, $_.Name.Length - 5)) } | ForEach-Object Name)
$manifest = [ordered]@{
    schema = 1
    collection = 'post_recovery'
    collected_at = (Get-Date).ToString('o')
    staging_dir = $StagingDir
    evidence_dir = $EvidenceDir
    bin_count = $bins.Count
    meta_count = $metas.Count
    flow_count = $flows.Count
    orphan_bins = $orphanBins
    orphan_metas = $orphanMetas
    files = @($records)
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $destination 'post-recovery-collection.json') -Encoding UTF8
Write-Host ("RADV pos-rollback: {0} IB, {1} meta, {2} fluxos; atualizados={3}; sem par={4}." -f `
    $bins.Count, $metas.Count, $flows.Count,
    @($records | Where-Object action -eq 'updated').Count,
    ($orphanBins.Count + $orphanMetas.Count))
if ($bins.Count -eq 0 -or $flows.Count -eq 0 -or $orphanBins.Count -ne 0 -or $orphanMetas.Count -ne 0) {
    throw 'Coleta RADV incompleta; consultar post-recovery-collection.json e staging antes de outra corrida.'
}

$indexer = Join-Path $PSScriptRoot 'index-bc250-telemetry.py'
$python = Get-Command python -ErrorAction SilentlyContinue
if ($python -and (Test-Path -LiteralPath $indexer -PathType Leaf)) {
    & $python.Source $indexer $EvidenceDir
    if ($LASTEXITCODE -ne 0) { throw "Indexador falhou: $LASTEXITCODE" }
} else {
    Write-Warning 'Indice pos-rollback nao gerado: Python ou indexador ausente.'
}
