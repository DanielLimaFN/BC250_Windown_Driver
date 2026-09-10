[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$DumpPath,

    [Parameter(Mandatory=$true)]
    [string]$EvidenceDir,

    [string]$PrivatePdbDir = '',

    [switch]$Execute
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

$cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($PrivatePdbDir)) {
    $PrivatePdbDir = Join-Path $root 'driver\wdk\package\output_lab'
}
$resolvedPdbDir = [IO.Path]::GetFullPath($PrivatePdbDir)
$resolvedEvidence = [IO.Path]::GetFullPath($EvidenceDir)
$resolvedDump = [IO.Path]::GetFullPath($DumpPath)

Write-Host '=== BC250 WATCHDOG LIVE-DUMP ANALYSIS ===' -ForegroundColor Cyan
Write-Host "  Dump      : $resolvedDump"
Write-Host "  Evidencia : $resolvedEvidence"
Write-Host "  CDB       : $cdb"
Write-Host "  PDB       : $resolvedPdbDir"

if (-not $Execute) {
    Write-Host 'DRY-RUN: nenhuma leitura privilegiada ou alteracao executada.' -ForegroundColor Green
    Write-Host 'Passe -Execute em PowerShell Administrador.'
    exit 0
}

if (-not (Test-IsAdministrator)) {
    throw 'Execute em PowerShell elevado (Administrador).'
}
if (-not (Test-Path -LiteralPath $resolvedDump -PathType Leaf)) {
    throw "Dump ausente: $resolvedDump"
}
if (-not (Test-Path -LiteralPath $resolvedEvidence -PathType Container)) {
    throw "EvidenceDir ausente: $resolvedEvidence"
}
if (-not (Test-Path -LiteralPath $cdb -PathType Leaf)) {
    throw "CDB ausente: $cdb"
}
if (-not (Test-Path -LiteralPath (Join-Path $resolvedPdbDir 'bc250mcdm.pdb') -PathType Leaf)) {
    throw "PDB privado ausente: $resolvedPdbDir\bc250mcdm.pdb"
}

$collectedDump = Join-Path $resolvedEvidence ([IO.Path]::GetFileName($resolvedDump))
$analysisPath = Join-Path $resolvedEvidence 'watchdog-1740-analysis.txt'
$stderrPath = Join-Path $resolvedEvidence 'watchdog-1740-cdb-stderr.txt'
$manifestPath = Join-Path $resolvedEvidence 'watchdog-1740-analysis-manifest.json'

Copy-Item -LiteralPath $resolvedDump -Destination $collectedDump -Force
$sourceHash = (Get-FileHash -LiteralPath $resolvedDump -Algorithm SHA256).Hash
$copyHash = (Get-FileHash -LiteralPath $collectedDump -Algorithm SHA256).Hash
if ($sourceHash -ne $copyHash) {
    throw 'Copia do dump recusada: SHA256 de origem e destino divergem.'
}

$commands = ('.reload /f bc250mcdm.sys; !analyze -v; .bugcheck; kP; ' +
    'lmvm bc250mcdm; lmvm dxgkrnl; ' +
    'x bc250mcdm!g_Bc250PagingStageSequence; ' +
    'x bc250mcdm!g_Bc250PagingStages; ' +
    'dt bc250mcdm!BC250_PAGING_STAGE_SLOT bc250mcdm!g_Bc250PagingStages L64; q')
$psi = New-Object Diagnostics.ProcessStartInfo
$psi.FileName = $cdb
$psi.Arguments = ('-y "' + $resolvedPdbDir +
    ';srv*C:\Symbols*https://msdl.microsoft.com/download/symbols" ' +
    '-z "' + $collectedDump + '" -c "' + $commands + '"')
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$process = New-Object Diagnostics.Process
$process.StartInfo = $psi
try {
    if (-not $process.Start()) { throw 'Process.Start do CDB retornou false.' }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(120000)) {
        try { $process.Kill() } catch {}
        throw 'CDB excedeu o teto de 120 segundos.'
    }
    $stdoutTask.Result | Set-Content -LiteralPath $analysisPath -Encoding UTF8
    $stderrTask.Result | Set-Content -LiteralPath $stderrPath -Encoding UTF8
    $exitCode = $process.ExitCode
} finally {
    $process.Dispose()
}

[ordered]@{
    captured_at = (Get-Date).ToString('o')
    source_dump = $resolvedDump
    collected_dump = $collectedDump
    sha256 = $copyHash
    bytes = (Get-Item -LiteralPath $collectedDump).Length
    cdb_exit_code = $exitCode
    analysis = $analysisPath
    private_pdb_dir = $resolvedPdbDir
    hardware_validated = $false
} | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "WATCHDOG DUMP COLLECTED: $collectedDump" -ForegroundColor Green
Write-Host "CDB ANALYSIS: $analysisPath"
Write-Host "SHA256: $copyHash"
Write-Host 'NÃO VALIDADO EM HARDWARE'
exit $exitCode
