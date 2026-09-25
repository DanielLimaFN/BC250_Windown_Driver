[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em PowerShell elevado para ler C:\Windows\MEMORY.DMP.'
}

$root = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $root 'docs\evidence\2026-09-24_13-54-18-DESKTOP-HQAG6T4-lab-binding'
$dump = 'C:\Windows\MEMORY.DMP'
$debugger = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
$report = Join-Path $evidence 'bugcheck-fence-watermark-raw-elevated.txt'
if (-not (Test-Path -LiteralPath $dump -PathType Leaf)) { throw "Dump ausente: $dump" }
if (-not (Test-Path -LiteralPath $debugger -PathType Leaf)) { throw "Debugger ausente: $debugger" }
if (Test-Path -LiteralPath $report) { throw "Relatorio ja existe: $report" }

$symbols = 'srv*C:\BC250_SYMBOLS*https://msdl.microsoft.com/download/symbols;' + $evidence
# Os enderecos sao da Corrida 103. [Context+0x1A4D44] e a marca do no 0
# conforme a desmontagem do binario; nao requer tipos privados no PDB.
$commands = '.bugcheck; dd ffffe706`79f94d44 L2; dd fffff804`4fd6eb70 L10; u fffff804`7599df86 L12; q'
& $debugger -z $dump -y $symbols -logo $report -c $commands
if ($LASTEXITCODE -ne 0) { throw "cdb terminou com exit=$LASTEXITCODE; confira $report" }
if (-not (Test-Path -LiteralPath $report -PathType Leaf)) { throw 'cdb nao gerou relatorio.' }
$hash = (Get-FileHash -LiteralPath $report -Algorithm SHA256).Hash
Write-Host "Marca bruta arquivada: $report SHA256=$hash"
