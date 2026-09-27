param(
    [string]$DumpPath = 'C:\Windows\MEMORY.DMP',
    [string]$EvidenceDirectory = 'C:\Users\Daniel\Desktop\NamixWDDM\docs\evidence\2026-09-07_20-41-12-DESKTOP-HQAG6T4-mcdm-display-binding',
    [string]$ReportName = 'memory-dmp-analysis-symbols-fixed.txt'
)

$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Este analisador precisa executar elevado para abrir C:\Windows\MEMORY.DMP.'
}

$cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
if (-not (Test-Path -LiteralPath $cdb -PathType Leaf)) {
    throw "CDB ausente: $cdb"
}
if (-not (Test-Path -LiteralPath $DumpPath -PathType Leaf)) {
    throw "Dump ausente: $DumpPath"
}
if (-not (Test-Path -LiteralPath $EvidenceDirectory -PathType Container)) {
    throw "Diretorio de evidencia ausente: $EvidenceDirectory"
}

$output = Join-Path $EvidenceDirectory $ReportName
$commands = '.symfix C:\Symbols; .reload /f; !analyze -v; .bugcheck; kv; q'

& $cdb -z $DumpPath -c $commands 2>&1 |
    Tee-Object -FilePath $output
$cdbExit = $LASTEXITCODE

if ($cdbExit -ne 0) {
    throw "CDB terminou com EXIT=$cdbExit. Saida preservada em $output"
}

Get-FileHash -LiteralPath $DumpPath, $output -Algorithm SHA256 |
    Format-Table -AutoSize |
    Out-File (Join-Path $EvidenceDirectory ($ReportName + '.sha256.txt')) -Encoding utf8

Write-Host "MEMORY_DMP_ANALYSIS: PASS ($output)"
