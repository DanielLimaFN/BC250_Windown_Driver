[CmdletBinding()]
param()

# Somente leitura. Mesmo binario da 115, mas com captura de dump como controle.
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'prepare-corrida115-dx71.ps1')
if ($LASTEXITCODE -ne 0) { throw "Preflight DX.71 falhou: exit=$LASTEXITCODE" }

$crashKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl'
$crash = Get-ItemProperty -LiteralPath $crashKey
if ([int]$crash.CrashDumpEnabled -ne 2 -or [int]$crash.AlwaysKeepMemoryDump -ne 1) {
    throw 'Corrida 116 exige dump de kernel e AlwaysKeepMemoryDump=1 antes do bind.'
}
foreach ($name in @('i8042prt', 'kbdhid')) {
    $key = "HKLM:\SYSTEM\CurrentControlSet\Services\$name\Parameters"
    $value = Get-ItemPropertyValue -LiteralPath $key -Name CrashOnCtrlScroll -ErrorAction Stop
    if ([int]$value -ne 1) { throw "CrashOnCtrlScroll nao armado em $name." }
}
$dumpPath = [Environment]::ExpandEnvironmentVariables([string]$crash.DumpFile)
if ([string]::IsNullOrWhiteSpace($dumpPath)) { throw 'DumpFile ausente.' }
$free = (Get-PSDrive -Name C).Free
if ($free -lt 2GB) { throw "Espaco livre em C: insuficiente para o ensaio de dump: $free bytes." }
$prior = Get-Item -LiteralPath $dumpPath -ErrorAction SilentlyContinue
Write-Host "Corrida 116: dump kernel armado, teclado PS/2 e USB armados, C: livre $([math]::Round($free/1GB,1)) GiB."
if ($prior) {
    Write-Host "Dump anterior: $dumpPath bytes=$($prior.Length) LastWriteUtc=$($prior.LastWriteTimeUtc.ToString('o'))"
} else {
    Write-Host "Dump anterior ausente: $dumpPath"
}
Write-Host 'Nenhuma configuracao foi alterada e nenhum bind ocorreu.'
