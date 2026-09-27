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
$report = Join-Path $evidence 'bugcheck-scheduler-table-fixed-elevated.txt'
if (-not (Test-Path -LiteralPath $dump -PathType Leaf)) { throw "Dump ausente: $dump" }
if (-not (Test-Path -LiteralPath $debugger -PathType Leaf)) { throw "Debugger ausente: $debugger" }
if (Test-Path -LiteralPath $report) { throw "Relatorio ja existe: $report" }

$symbols = 'srv*C:\BC250_SYMBOLS*https://msdl.microsoft.com/download/symbols;' + $evidence
# A extracao anterior mostrou rdi=ffffe706`81d2a000 no frame do scheduler.
# O comando 'r' seguinte restaurou o frame do BugCheck e invalidou as leituras
# relativas a @rdi. Aqui os enderecos sao ABSOLUTOS: nao dependem de frame.
# [rdi+40h/48h] sao comparacoes, [rdi+98h] e o Arg3 (3072),
# [rdi+57Ch] e o indice, [rdi+588h] inicia a tabela de 16 ponteiros.
$commands = '.bugcheck; dd ffffe706`81d2a040 L18; dd ffffe706`81d2a57c L1; dq ffffe706`81d2a588 L10; q'
& $debugger -z $dump -y $symbols -logo $report -c $commands
if ($LASTEXITCODE -ne 0) { throw "cdb terminou com exit=$LASTEXITCODE; confira $report" }
if (-not (Test-Path -LiteralPath $report -PathType Leaf)) { throw 'cdb nao gerou relatorio.' }
$hash = (Get-FileHash -LiteralPath $report -Algorithm SHA256).Hash
Write-Host "Tabela fixa do scheduler arquivada: $report SHA256=$hash"
