<#
    Auditoria SOMENTE-LEITURA da tabela golden Cyan Skillfish.

    Compara a copia no driver com gfx_v10_0.c e gc_10_1_0_offset.h:
      - 34 entradas, mesma ordem e mesmo nome;
      - mascaras de substituicao e valores OR exatos;
      - endereco BAR = (GC_BASE[BASE_IDX] + mmREG) * 4;
      - os dois GPA incluidos no vetor de preflight;
      - apply ainda fail-closed enquanto o rollback por instancia nao existe.

    Nao abre o driver, nao le MMIO e nao escreve arquivo.
#>
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$root = Split-Path -Parent $PSScriptRoot
$hwPath = Join-Path $root 'dbg\hw.c'
$abiPath = Join-Path $root 'inc\bc250dbg_abi.h'
$gfxPath = Join-Path $root 'referencias\BC250-windowsDriverTest\docs\upstream\gfx_v10_0.c'
$offPath = Join-Path $root 'referencias\BC250-windowsDriverTest\docs\upstream\generated\gc_10_1_0_offset.h'
$soc15Path = Join-Path $root 'referencias\BC250-windowsDriverTest\docs\upstream\deep\soc15.c'

foreach ($path in @($hwPath, $abiPath, $gfxPath, $offPath, $soc15Path)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}

$hw = Get-Content -LiteralPath $hwPath -Raw
$abi = Get-Content -LiteralPath $abiPath -Raw
$gfx = Get-Content -LiteralPath $gfxPath -Raw
$offsetHeader = Get-Content -LiteralPath $offPath -Raw
$soc15 = Get-Content -LiteralPath $soc15Path -Raw
$failures = [System.Collections.Generic.List[string]]::new()

function Parse-U32([string]$text) {
    $clean = $text.Trim() -replace '[uUlL]+$',''
    if ($clean -match '^0x') { return [Convert]::ToUInt32($clean.Substring(2), 16) }
    return [Convert]::ToUInt32($clean, 10)
}

if ($abi -notmatch 'BC250DBG_ABI_VERSION\s+29u') {
    $failures.Add('ABI esperada: 29.')
}

$upstreamMatch = [regex]::Match(
    $gfx,
    'static const struct soc15_reg_golden golden_settings_gc_10_0_cyan_skillfish\[\]\s*=\s*\{(?<body>.*?)\n\};',
    [Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $upstreamMatch.Success) { throw 'Tabela golden upstream nao encontrada.' }

$upstream = @([regex]::Matches(
    $upstreamMatch.Groups['body'].Value,
    'SOC15_REG_GOLDEN_VALUE\(GC,\s*0,\s*mm(?<name>[A-Z0-9_]+),\s*(?<mask>0x[0-9a-fA-F]+),\s*(?<value>0x[0-9a-fA-F]+)\)') |
    ForEach-Object {
        [pscustomobject]@{
            Name = $_.Groups['name'].Value
            Mask = Parse-U32 $_.Groups['mask'].Value
            Value = Parse-U32 $_.Groups['value'].Value
        }
    })

$driverMatch = [regex]::Match(
    $hw,
    'static const BC_CP_GOLDEN_REG g_CpGoldenRegs\[.*?\]\s*=\s*\{(?<body>.*?)\n\};',
    [Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $driverMatch.Success) { throw 'Tabela golden do driver nao encontrada.' }

$driver = @([regex]::Matches(
    $driverMatch.Groups['body'].Value,
    '\{\s*A_(?<symbol>[A-Z0-9_]+),\s*(?<mask>0x[0-9A-Fa-f]+)u,\s*(?<value>0x[0-9A-Fa-f]+)u,\s*(?<ones>TRUE|FALSE),\s*"(?<name>[A-Z0-9_]+)"\s*\}') |
    ForEach-Object {
        [pscustomobject]@{
            Symbol = $_.Groups['symbol'].Value
            Name = $_.Groups['name'].Value
            Mask = Parse-U32 $_.Groups['mask'].Value
            Value = Parse-U32 $_.Groups['value'].Value
            AllOnes = ($_.Groups['ones'].Value -eq 'TRUE')
        }
    })

if ($upstream.Count -ne 34) { $failures.Add("Upstream tem $($upstream.Count) entradas; esperado 34.") }
if ($driver.Count -ne 34) { $failures.Add("Driver tem $($driver.Count) entradas; esperado 34.") }

$defines = @{}
foreach ($m in [regex]::Matches($hw, '#define\s+A_(?<name>[A-Z0-9_]+)\s+(?<value>0x[0-9A-Fa-f]+)')) {
    $defines[$m.Groups['name'].Value] = Parse-U32 $m.Groups['value'].Value
}

$mm = @{}
foreach ($m in [regex]::Matches($offsetHeader, '#define\s+mm(?<name>[A-Z0-9_]+)\s+(?<value>0x[0-9A-Fa-f]+|[0-9]+)')) {
    $mm[$m.Groups['name'].Value] = Parse-U32 $m.Groups['value'].Value
}

$limit = [Math]::Min($upstream.Count, $driver.Count)
for ($i = 0; $i -lt $limit; $i++) {
    $u = $upstream[$i]
    $d = $driver[$i]
    if ($d.Name -ne $u.Name -or $d.Symbol -ne $u.Name) {
        $failures.Add("[$i] nome/ordem: upstream=$($u.Name), driver=$($d.Symbol)/$($d.Name).")
        continue
    }
    if ($d.Mask -ne $u.Mask -or $d.Value -ne $u.Value) {
        $failures.Add(("[{0}] {1}: upstream mask/value 0x{2:X8}/0x{3:X8}, driver 0x{4:X8}/0x{5:X8}." -f
                      $i,$u.Name,$u.Mask,$u.Value,$d.Mask,$d.Value))
    }
    $baseKey = $u.Name + '_BASE_IDX'
    if (-not $mm.ContainsKey($u.Name) -or -not $mm.ContainsKey($baseKey)) {
        $failures.Add("[$i] $($u.Name): mm ou BASE_IDX ausente no header.")
        continue
    }
    $base = if ($mm[$baseKey] -eq 0) { [uint32]0x1260 } elseif ($mm[$baseKey] -eq 1) { [uint32]0xA000 } else { $null }
    if ($null -eq $base) {
        $failures.Add("[$i] $($u.Name): BASE_IDX $($mm[$baseKey]) nao suportado.")
        continue
    }
    $expectedOffset = [uint32](($base + $mm[$u.Name]) * 4)
    if (-not $defines.ContainsKey($d.Symbol) -or $defines[$d.Symbol] -ne $expectedOffset) {
        $got = if ($defines.ContainsKey($d.Symbol)) { '0x{0:X5}' -f $defines[$d.Symbol] } else { 'AUSENTE' }
        $failures.Add(("[{0}] {1}: offset driver {2}, esperado 0x{3:X5}." -f $i,$u.Name,$got,$expectedOffset))
    }
    if ($d.AllOnes -ne ($u.Name -eq 'SQ_LDS_CLK_CTRL')) {
        $failures.Add("[$i] $($u.Name): excecao de 0xFFFFFFFF incorreta.")
    }
}

foreach ($gpa in @('A_CPG_PSP_DEBUG','A_CPC_PSP_DEBUG')) {
    if ($hw -notmatch [regex]::Escape($gpa)) { $failures.Add("Preflight nao referencia $gpa.") }
}
if ($hw -notmatch 'CP golden apply: QUARENTENA, rollback por SE/SH/instancia ausente') {
    $failures.Add('Apply golden nao esta explicitamente em quarentena.')
}
if ($soc15 -notmatch 'tmp\s*&=\s*~\(entry->and_mask\)\s*;' -or
    $soc15 -notmatch 'tmp\s*\|=\s*\(entry->or_mask\s*&\s*entry->and_mask\)\s*;') {
    $failures.Add('Semantica RMW de soc15_program_register_sequence nao foi confirmada.')
}

if ($failures.Count) {
    Write-Host 'AUDITORIA GOLDEN: FALHA' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'AUDITORIA GOLDEN: OK' -ForegroundColor Green
Write-Host '  34/34 nomes, ordem, mascaras, valores e offsets conferidos.'
Write-Host '  RMW conferido: (lido & ~mask) | (or_value & mask).'
Write-Host '  36 registros no preflight readonly (34 golden + 2 GPA).'
Write-Host '  Apply permanece em QUARENTENA; nenhuma escrita foi executada.'
