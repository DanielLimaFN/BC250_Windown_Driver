$ErrorActionPreference = 'Stop'
$watcher = Join-Path $PSScriptRoot 'watch-bc250mcdm-safe-bind.ps1'
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    $watcher, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) { throw 'Watchdog com erro de sintaxe.' }
foreach ($name in @('Write-AtomicJson', 'Archive-Bc250UmdLogsAfterRecovery')) {
    $functionAst = $ast.Find({
        param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq $name
    }, $true)
    if ($null -eq $functionAst) { throw "Funcao ausente: $name" }
    . ([scriptblock]::Create($functionAst.Extent.Text))
}

$fixture = Join-Path $env:TEMP ("bc250-dx57-" + [guid]::NewGuid().ToString('N'))
$source = Join-Path $fixture 'source'
$evidence = Join-Path $fixture 'evidence'
New-Item -ItemType Directory -Path $source, $evidence -Force | Out-Null
try {
    $payload = '11:49:27.714 [pid=1176 dwm.exe] DX57 TESTE'
    $sourceLog = Join-Path $source 'BC250_UMD_LOG_1176.txt'
    [IO.File]::WriteAllText($sourceLog, $payload)
    $config = [pscustomobject]@{
        evidence_dir = $evidence
        armed_boot_ticks = 1234
    }
    $script:currentBootTicks = 5678
    Archive-Bc250UmdLogsAfterRecovery -SourceDirectory $source
    $manifest = Get-Content -LiteralPath (Join-Path $evidence 'umd-post-recovery-manifest.json') `
        -Raw | ConvertFrom-Json
    $copy = Join-Path $evidence 'umd-logs-pos-travamento\BC250_UMD_LOG_1176.txt'
    if ($manifest.kind -ne 'POST_RECOVERY_UMD_TAIL' -or
        $manifest.files.Count -ne 1 -or
        $manifest.files[0].sha256 -ne (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -or
        [IO.File]::ReadAllText($copy) -ne $payload -or
        -not $manifest.acl_reset_ok -or
        $manifest.errors.Count -ne 0) {
        throw 'DX.57: copia ou manifesto divergente.'
    }
    Write-Host 'DX.57 UMD tail archive: PASS'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixture)
    $tempRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'DX.57: fixture fora do diretorio temporario; limpeza recusada.'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
