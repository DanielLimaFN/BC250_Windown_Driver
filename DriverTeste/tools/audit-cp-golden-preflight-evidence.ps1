<#
    Auditoria offline da evidencia fisica do preflight golden ABI 29.

    Nao abre o driver e nao acessa MMIO. Cruza:
      - result.txt do ensaio Windows;
      - as 34 entradas g_CpGoldenRegs do driver;
      - o sweep Linux nomeado, capturado depois do golden init do amdgpu.
#>
param(
    [string]$EvidenceDir,
    [string]$LinuxSweep
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$root = Split-Path -Parent $PSScriptRoot
if (-not $EvidenceDir) {
    $EvidenceDir = Join-Path $root 'docs\evidence\2026-08-24_17-23-21-DESKTOP-HQAG6T4-cp-golden-preflight'
}
if (-not $LinuxSweep) {
    $LinuxSweep = Join-Path $root 'referencias\linuxDump\parsed_linux_sweep_named.json'
}
$resultPath = Join-Path $EvidenceDir 'result.txt'
$hwPath = Join-Path $root 'dbg\hw.c'
foreach ($path in @($resultPath, $hwPath, $LinuxSweep)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}

$result = Get-Content -LiteralPath $resultPath -Raw
$hw = Get-Content -LiteralPath $hwPath -Raw
$linux = @(Get-Content -LiteralPath $LinuxSweep -Raw | ConvertFrom-Json)
# Windows PowerShell 5.1 preserva o array JSON como um unico item Object[];
# PowerShell 7 o enumera. Normalize para o mesmo formato nas duas versoes.
if ($linux.Count -eq 1 -and $linux[0] -is [Array]) { $linux = @($linux[0]) }
$failures = [System.Collections.Generic.List[string]]::new()

$driverMatch = [regex]::Match(
    $hw,
    'static const BC_CP_GOLDEN_REG g_CpGoldenRegs\[.*?\]\s*=\s*\{(?<body>.*?)\n\};',
    [Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $driverMatch.Success) { throw 'Tabela golden do driver nao encontrada.' }

$defines = @{}
foreach ($m in [regex]::Matches($hw, '#define\s+A_(?<name>[A-Z0-9_]+)\s+(?<value>0x[0-9A-Fa-f]+)')) {
    $defines[$m.Groups['name'].Value] = [Convert]::ToUInt32($m.Groups['value'].Value.Substring(2), 16)
}
$golden = @([regex]::Matches(
    $driverMatch.Groups['body'].Value,
    '\{\s*A_(?<symbol>[A-Z0-9_]+),\s*(?<mask>0x[0-9A-Fa-f]+)u,\s*(?<or>0x[0-9A-Fa-f]+)u,.*?"(?<name>[A-Z0-9_]+)"\s*\}') |
    ForEach-Object {
        $symbol = $_.Groups['symbol'].Value
        [pscustomobject]@{
            Name=$_.Groups['name'].Value
            Offset=[uint32]$defines[$symbol]
            Mask=[Convert]::ToUInt64($_.Groups['mask'].Value.Substring(2),16)
            OrValue=[Convert]::ToUInt64($_.Groups['or'].Value.Substring(2),16)
        }
    })
if ($golden.Count -ne 34) { $failures.Add("Tabela do driver tem $($golden.Count) entradas; esperado 34.") }

$values = @{}
foreach ($m in [regex]::Matches($result, '(?m)^\s+(0x[0-9A-Fa-f]{5})\s+:\s+(0x[0-9A-Fa-f]{8})\s*$')) {
    $values[[Convert]::ToUInt32($m.Groups[1].Value.Substring(2),16)] =
        [Convert]::ToUInt32($m.Groups[2].Value.Substring(2),16)
}
$bad = @([regex]::Matches(
    $result,
    '(?m)^\s+(0x[0-9A-Fa-f]{5})\s+:\s+leituras\s+0x[0-9A-Fa-f]{8}\s+/\s+0x[0-9A-Fa-f]{8}\s*$') |
    ForEach-Object { [Convert]::ToUInt32($_.Groups[1].Value.Substring(2),16) })

if ($values.Count -ne 36) { $failures.Add("Evidencia reporta $($values.Count) valores unicos; esperado 36.") }
if ($result -notmatch 'escritas\s*:\s*0(?:\D|$)') { $failures.Add('Evidencia nao confirma zero escritas.') }
if ($result -notmatch 'falhas\s*:\s*4(?:\D|$)') { $failures.Add('Evidencia nao reporta exatamente quatro falhas.') }

$gl2 = @([uint32]0x33884,[uint32]0x3380C)
$gpa = @([uint32]0x3F040,[uint32]0x3F044)
$allOnes = [Convert]::ToUInt32('FFFFFFFF',16)
$expectedBad = @($gl2 + $gpa)
if ($bad.Count -ne 4 -or @($expectedBad | Where-Object { $_ -notin $bad }).Count) {
    $failures.Add('Conjunto de falhas difere de GL2A/GL2C/CPG/CPC esperado.')
}

$confirmed = 0
foreach ($reg in $golden) {
    if (-not $values.ContainsKey($reg.Offset)) {
        $failures.Add(('Golden {0} 0x{1:X5} ausente na evidencia.' -f $reg.Name,$reg.Offset))
        continue
    }
    if ($reg.Offset -notin $gl2) { $confirmed++ }
}

$linuxByOffset = @{}
foreach ($entry in $linux) {
    $offText = ([string]$entry.offset).Trim()
    $valueText = ([string]$entry.value).Trim()
    if ($offText -notmatch '^0x[0-9A-Fa-f]+$' -or
        $valueText -notmatch '^0x[0-9A-Fa-f]+$') {
        continue
    }
    $linuxByOffset[$offText.ToUpperInvariant()] = $valueText.ToUpperInvariant()
}
foreach ($off in $gl2) {
    $linuxKey = ('0x{0:X5}' -f $off).ToUpperInvariant()
    if (-not $linuxByOffset.ContainsKey($linuxKey) -or
        $linuxByOffset[$linuxKey] -ne '0XFFFFFFCF') {
        $failures.Add(('Sweep Linux nao confirma 0xFFFFFFCF em 0x{0:X5}.' -f $off))
    }
    if (-not $values.ContainsKey($off) -or $values[$off] -ne $allOnes) {
        $failures.Add(('Evidencia Windows nao confirma 0xFFFFFFFF em 0x{0:X5}.' -f $off))
    }
}
foreach ($off in $gpa) {
    if (-not $values.ContainsKey($off) -or $values[$off] -ne $allOnes) {
        $failures.Add(('GPA 0x{0:X5} nao esta indisponivel como esperado.' -f $off))
    }
}

# Projeta exatamente soc15_program_register_sequence():
#   novo = (lido & ~mask) | (or_value & mask)
# A implementacao upstream possui um atalho equivalente para mask=FFFFFFFF.
$u32Max = [Convert]::ToUInt64('FFFFFFFF',16)
$wouldChange = 0
$linuxComparable = 0
$linuxMatches = 0
$linuxDiffs = [System.Collections.Generic.List[string]]::new()
foreach ($reg in $golden) {
    if (-not $values.ContainsKey($reg.Offset)) { continue }
    $current = [uint64]$values[$reg.Offset]
    $notMask = $u32Max -bxor $reg.Mask
    $projected = (($current -band $notMask) -bor ($reg.OrValue -band $reg.Mask)) -band $u32Max
    if ($projected -ne $current) { $wouldChange++ }

    $linuxKey = ('0x{0:X5}' -f $reg.Offset).ToUpperInvariant()
    if ($linuxByOffset.ContainsKey($linuxKey)) {
        $linuxComparable++
        $linuxValue = [Convert]::ToUInt64($linuxByOffset[$linuxKey].Substring(2),16)
        if ($projected -eq $linuxValue) {
            $linuxMatches++
        } else {
            $linuxDiffs.Add(('{0}: projetado 0x{1:X8}, Linux 0x{2:X8}' -f
                $reg.Name,$projected,$linuxValue))
        }
    }
}
if ($wouldChange -ne 30) {
    $failures.Add("Projecao golden mudaria $wouldChange registros; esperado 30 na evidencia ABI 29.")
}
if ($linuxComparable -ne 31 -or $linuxMatches -ne 30) {
    $failures.Add("Projecao/Linux: $linuxMatches de $linuxComparable; esperado 30 de 31.")
}
if ($linuxDiffs.Count -ne 1 -or $linuxDiffs[0] -notmatch '^RMI_SPARE:') {
    $failures.Add('Diferenca projetada contra Linux nao se limita a RMI_SPARE.')
}

if ($failures.Count) {
    Write-Host 'AUDITORIA DA EVIDENCIA GOLDEN: FALHA' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'AUDITORIA DA EVIDENCIA GOLDEN: OK' -ForegroundColor Green
Write-Host "  $confirmed golden confirmados por leitura nao ambigua."
Write-Host '  2 golden GL2 ambiguos: Windows FFFFFFFF; Linux pos-init FFFFFFCF.'
Write-Host '  2 GPA indisponiveis: CPG/CPC leem FFFFFFFF no estado Windows.'
Write-Host '  36 valores, 4 falhas classificadas e zero escritas.'
Write-Host "  Semantica upstream: 30 mudariam, 4 ja estao no valor projetado."
Write-Host "  Projecao coincide com Linux em $linuxMatches/$linuxComparable; diferenca: $($linuxDiffs[0])."
Write-Host '  Veredito: NAO VALIDADO EM HARDWARE.'
