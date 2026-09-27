<#
.SYNOPSIS
    Extrai a pilha da falha do dump do bc250-radv-compute-gate.exe.

.DESCRIPTION
    Roda o cdb sobre o dump coletado por
    tools\restore-radv-harness-crash-capture.ps1 e arquiva o resultado ao lado
    dele. Puramente offline: nao toca no dispositivo, nao instala nada, nao
    exige elevacao.

    O caminho de simbolos inclui o diretorio de build da RADV, que e onde vive
    o vulkan_radeon.pdb. Sem ele a pilha sai so com enderecos e o exercicio
    perde o sentido.

.PARAMETER EvidenceDir
    Diretorio da corrida (o do binding ou o subdiretorio do tripe).

.PARAMETER DumpPath
    Dump especifico. Sem isso, usa o mais recente encontrado no EvidenceDir.

.PARAMETER MesaBuildDir
    Arvore de build da RADV, para os simbolos do vulkan_radeon.dll.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EvidenceDir,
    [string]$DumpPath,
    [string]$MesaBuildDir = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan'
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $EvidenceDir -PathType Container)) {
    throw "Diretorio de evidencia ausente: $EvidenceDir"
}

if ([string]::IsNullOrWhiteSpace($DumpPath)) {
    $candidates = @(Get-ChildItem -LiteralPath $EvidenceDir -Filter 'radv-harness-*.dmp' `
        -File -Recurse -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending)
    if ($candidates.Count -eq 0) {
        throw ("Nenhum dump em $EvidenceDir. A captura so produz dump se " +
            'tools\arm-radv-harness-crash-capture.ps1 estiver ARMED durante a corrida.')
    }
    $DumpPath = $candidates[0].FullName
}
if (-not (Test-Path -LiteralPath $DumpPath -PathType Leaf)) {
    throw "Dump ausente: $DumpPath"
}

$cdbCandidates = @(
    'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe',
    'C:\Program Files\Windows Kits\10\Debuggers\x64\cdb.exe'
)
$cdb = $null
foreach ($candidate in $cdbCandidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $cdb = $candidate; break }
}
if ($null -eq $cdb) {
    $command = Get-Command cdb.exe -ErrorAction SilentlyContinue
    if ($command) { $cdb = $command.Source }
}
if ($null -eq $cdb) {
    throw 'cdb.exe nao encontrado (Debugging Tools for Windows do WDK).'
}

$symbolPath = "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$MesaBuildDir"
$commands = @(
    '.reload /f'
    '!analyze -v'
    '.ecxr'
    'r'
    'u @rip-40 L40'
    'kP 25'
    'lmvm vulkan_radeon'
    'lmvm bc250_radv_compute_gate'
    '~*k'
    'q'
) -join '; '
$outputPath = [System.IO.Path]::ChangeExtension($DumpPath, '.analysis.txt')

Write-Host "cdb    : $cdb"
Write-Host "dump   : $DumpPath"
Write-Host "saida  : $outputPath"
Write-Host ''

$analysis = (& $cdb -z $DumpPath -y $symbolPath -c $commands 2>&1) -join "`r`n"
$analysis | Set-Content -LiteralPath $outputPath -Encoding UTF8

# O resumo impresso e so conveniencia; a verdade e o arquivo inteiro.
$interesting = @($analysis -split "`r?`n" | Where-Object {
    $_ -match 'EXCEPTION_CODE|FAULTING_IP|STACK_TEXT|MODULE_NAME|vulkan_radeon|bc250-radv|radv_|wddm2|rax=|rip='
})
if ($interesting.Count -gt 0) {
    Write-Host '=== linhas relevantes ==='
    $interesting | Select-Object -First 40 | ForEach-Object { Write-Host "  $_" }
} else {
    Write-Warning 'Nenhuma linha relevante reconhecida; leia o arquivo completo.'
}
Write-Host ''
Write-Host "Analise arquivada: $outputPath"
Write-Host 'NAO VALIDADO EM HARDWARE'
