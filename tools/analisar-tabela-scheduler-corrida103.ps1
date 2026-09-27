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
$report = Join-Path $evidence 'bugcheck-scheduler-table-elevated.txt'
if (-not (Test-Path -LiteralPath $dump -PathType Leaf)) { throw "Dump ausente: $dump" }
if (-not (Test-Path -LiteralPath $debugger -PathType Leaf)) { throw "Debugger ausente: $debugger" }
if (Test-Path -LiteralPath $report) { throw "Relatorio ja existe: $report" }

$symbols = 'srv*C:\BC250_SYMBOLS*https://msdl.microsoft.com/download/symbols;' + $evidence
# Frame 2: VidSchiProcessIsrCompletedPacket. O binario 10.0.19041.2913
# compara a fence com [rdi+40h]/[rdi+48h], depois usa o indice [rdi+57Ch]
# para buscar um ponteiro em [rdi+588h+indice*8]. O salto fatal ocorreu quando
# esse ponteiro era nulo. Lemos somente registradores e memoria do dump.
$commands = '.bugcheck; kn; .frame /r 2; r; dd @rdi+40 L18; dd @rdi+57c L1; dq @rdi+588 L10; dq @r15 L4; q'
& $debugger -z $dump -y $symbols -logo $report -c $commands
if ($LASTEXITCODE -ne 0) { throw "cdb terminou com exit=$LASTEXITCODE; confira $report" }
if (-not (Test-Path -LiteralPath $report -PathType Leaf)) { throw 'cdb nao gerou relatorio.' }
$hash = (Get-FileHash -LiteralPath $report -Algorithm SHA256).Hash
Write-Host "Tabela do scheduler arquivada: $report SHA256=$hash"
