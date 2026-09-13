<#
.SYNOPSIS
    Recupera do log cumulativo o trecho de uma corrida que o arquivamento perdeu.

.DESCRIPTION
    C2 (10/09) -- DEFEITO MEDIDO NA CORRIDA DO MARCO.

    C:\BC250_DISPLAY_LOG.txt e' CUMULATIVO e sobrevive a reboot. O binder copia
    esse arquivo integral para a pasta de evidencia e grava bind_start_offset,
    mas a copia acontece no FIM do roteiro do binder. Se a maquina sofrer
    BugCheck antes disso, o watchdog nao roda, a copia nunca e' refeita e a
    pasta de evidencia fica TRUNCADA -- sem que nada indique isso.

    Foi exatamente o que aconteceu em 10/09 11:21: o arquivo em
    docs/evidence/2026-09-10_11-21-16-.../ tinha 15.146.336 bytes com
    bind_start_offset=15.009.172 (1.480 linhas de bind), e o log vivo tinha
    15.584.374 bytes -- 438.038 bytes a mais. O trecho perdido continha o
    FenceId=770, a linha IB_TRIPOD TIMEOUT e o FENCE_VISIBILIDADE que provou a
    causa-raiz do BugCheck 0x119. A prova esteve em disco o tempo todo e a
    auditoria da corrida foi feita sem ela.

    Este roteiro compara o tamanho arquivado de cada pasta recente com o log
    vivo e, quando o vivo e' maior, grava o trecho faltante com SHA-256 e
    proveniencia. Quando existe uma pasta MAIS NOVA, o start_offset dela
    delimita o fim exato do trecho; sem ela, o trecho vai ate o fim do arquivo e
    isso e' DECLARADO no json.

    Somente leitura sobre o log vivo. Nao toca em driver, servico ou registro.

.PARAMETER MaxFolders
    Quantas pastas de evidencia mais recentes examinar. Default 8.

.PARAMETER DisplayLogPath
    Log cumulativo. Default C:\BC250_DISPLAY_LOG.txt.
#>
[CmdletBinding()]
param(
    [int]$MaxFolders = 8,
    [string]$DisplayLogPath = 'C:\BC250_DISPLAY_LOG.txt'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$evidenceRoot = Join-Path $root 'docs\evidence'

if (-not (Test-Path -LiteralPath $DisplayLogPath -PathType Leaf)) {
    Write-Host "LOG VIVO AUSENTE: $DisplayLogPath -- nada a recuperar." -ForegroundColor Yellow
    exit 0
}
if (-not (Test-Path -LiteralPath $evidenceRoot)) {
    Write-Host "docs/evidence ausente -- nada a recuperar." -ForegroundColor Yellow
    exit 0
}

$liveLength = [int64](Get-Item -LiteralPath $DisplayLogPath).Length
Write-Host "Log vivo: $DisplayLogPath ($liveLength bytes)" -ForegroundColor Cyan

$folders = Get-ChildItem -LiteralPath $evidenceRoot -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like '*-lab-binding' -or $_.Name -like '*-mcdm-display-binding' } |
    Sort-Object Name -Descending |
    Select-Object -First $MaxFolders

if (-not $folders) {
    Write-Host "Nenhuma pasta de bind encontrada." -ForegroundColor Yellow
    exit 0
}

function Read-JsonOrNull([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    try {
        return (Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json)
    } catch { return $null }
}

# Ordem cronologica crescente, para que cada pasta conheca a start_offset da seguinte.
$ordered = @($folders | Sort-Object Name)
$recovered = 0
$examined = 0

for ($i = 0; $i -lt $ordered.Count; $i++) {
    $dir = $ordered[$i]
    $baseline = Read-JsonOrNull (Join-Path $dir.FullName 'display-log-baseline.json')
    if ($null -eq $baseline -or $null -eq $baseline.start_offset) { continue }
    $examined++

    $startOffset = [int64]$baseline.start_offset
    $archive = Read-JsonOrNull (Join-Path $dir.FullName 'display-log-archive.json')

    # De onde o trecho perdido comeca: fim do que ja foi arquivado, ou o proprio
    # inicio do bind quando nao houve arquivamento nenhum.
    if ($null -ne $archive -and $null -ne $archive.bytes) {
        $tailStart = [int64]$archive.bytes
        $reason = 'arquivo integral existe mas foi copiado antes do fim da corrida'
    } else {
        $tailStart = $startOffset
        $reason = 'nenhum display-log-archive.json: o bind morreu antes de arquivar'
    }

    # Fim exato quando existe um bind posterior; senao, fim do arquivo vivo.
    $boundedByNext = $false
    $tailEnd = $liveLength
    if ($i + 1 -lt $ordered.Count) {
        $nextBaseline = Read-JsonOrNull (Join-Path $ordered[$i + 1].FullName 'display-log-baseline.json')
        if ($null -ne $nextBaseline -and $null -ne $nextBaseline.start_offset) {
            $candidate = [int64]$nextBaseline.start_offset
            if ($candidate -gt $tailStart -and $candidate -le $liveLength) {
                $tailEnd = $candidate
                $boundedByNext = $true
            }
        }
    }

    $missing = $tailEnd - $tailStart
    if ($missing -le 0) {
        Write-Host ("  {0}: completo (nada faltando)" -f $dir.Name) -ForegroundColor DarkGray
        continue
    }

    $tailDest = Join-Path $dir.FullName 'bc250_display_log-tail-recovered.txt'
    if (Test-Path -LiteralPath $tailDest -PathType Leaf) {
        Write-Host ("  {0}: trecho ja recuperado antes" -f $dir.Name) -ForegroundColor DarkGray
        continue
    }

    Write-Host ("  {0}: FALTAM {1} bytes [{2}..{3}) -- recuperando" -f `
        $dir.Name, $missing, $tailStart, $tailEnd) -ForegroundColor Yellow

    # C3.10 (12/09) -- DEFEITO CORRIGIDO: esta rotina TRAVAVA o bind.
    #
    # A versao anterior alocava um byte[] do tamanho INTEIRO do trecho e depois
    # gravava com "$buffer[0..($read - 1)]". Esse fatiamento constroi um array
    # de OBJETOS do PowerShell, elemento a elemento. Ate' 12/09 os trechos eram
    # de 1 a 5 MB e ninguem notou; na corrida 13:29 o dwm.exe sobreviveu 3 min
    # 13 s e o log cresceu 209 MB de uma vez. O bind seguinte ficou preso
    # fatiando 201 milhoes de elementos, e o operador teve de dar Ctrl+C.
    #
    # Agora e' copia em blocos, memoria limitada e proporcional ao bloco.
    $stream = [System.IO.File]::Open($DisplayLogPath, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    $dest = $null
    $read = 0
    try {
        $null = $stream.Seek($tailStart, [System.IO.SeekOrigin]::Begin)
        $dest = [System.IO.File]::Open($tailDest, [System.IO.FileMode]::Create,
            [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
        $blockSize = 1MB
        $buffer = New-Object byte[] $blockSize
        while ($read -lt $missing) {
            $want = [Math]::Min([int64]$blockSize, $missing - $read)
            $chunk = $stream.Read($buffer, 0, [int]$want)
            if ($chunk -le 0) { break }
            $dest.Write($buffer, 0, $chunk)
            $read += $chunk
        }
        $dest.Flush()
    } finally {
        if ($null -ne $dest) { $dest.Dispose() }
        $stream.Dispose()
    }

    $hash = (Get-FileHash -LiteralPath $tailDest -Algorithm SHA256).Hash
    [ordered]@{
        source = $DisplayLogPath
        recovered_as = 'bc250_display_log-tail-recovered.txt'
        reason = $reason
        bind_start_offset = $startOffset
        tail_start_offset = $tailStart
        tail_end_offset = $tailEnd
        bytes = $read
        sha256 = $hash
        bounded_by_next_bind = $boundedByNext
        note = if ($boundedByNext) {
            'fim delimitado pela start_offset do bind seguinte: o trecho pertence somente a esta corrida'
        } else {
            'SEM bind posterior conhecido: o trecho vai ate o fim do log vivo e PODE conter linhas de atividade posterior'
        }
        recovered_at = (Get-Date).ToString('o')
        hardware_validated = $false
    } | ConvertTo-Json | Set-Content -LiteralPath `
        (Join-Path $dir.FullName 'display-log-tail-recovered.json') -Encoding UTF8

    Write-Host ("    -> {0} bytes, SHA-256 {1}" -f $read, $hash) -ForegroundColor Green
    $recovered++
}

Write-Host ""
Write-Host ("RECUPERACAO DE TRECHO: {0} pasta(s) examinada(s), {1} recuperada(s)." -f `
    $examined, $recovered) -ForegroundColor Cyan
Write-Host "Somente leitura sobre o log vivo. NAO VALIDADO EM HARDWARE (coleta, nao medicao)." -ForegroundColor DarkGray
exit 0
