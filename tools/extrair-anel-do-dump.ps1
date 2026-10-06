# FB.1 (doc 607): extrai do MEMORY.DMP o anel de log do KMD (g_Bc250PagingLogRing,
# 128 KB) e o registro de falha do blt (g_Bc250SdmaLastBltFailure).
#
# Por que: num BugCheck as ultimas linhas do log ficam no anel em RAM e nunca
# chegam ao C:\BC250_DISPLAY_LOG.txt (Corrida 227: a linha da falha do flip #1024
# se perdeu). A drenagem so' avanca o Tail; os bytes continuam no anel ate serem
# sobrescritos, entao os ultimos ~128 KB de texto estao no dump.
#
# So' leitura do dump. Exige PowerShell ELEVADO (o MEMORY.DMP e' protegido) e o
# cdb do WDK. Usa o PDB arquivado na pasta da corrida (o mesmo SYS do dump).
param(
    [Parameter(Mandatory = $true)][string]$EvidenceDir,
    [string]$Dump = 'C:\Windows\MEMORY.DMP'
)
$ErrorActionPreference = 'Stop'
$cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
if (-not (Test-Path -LiteralPath $cdb)) { throw "cdb ausente em $cdb" }
if (-not (Test-Path -LiteralPath $Dump)) { throw "dump ausente: $Dump" }
$EvidenceDir = (Resolve-Path -LiteralPath $EvidenceDir).Path
$pdb = Join-Path $EvidenceDir 'bc250mcdm.pdb'
if (-not (Test-Path -LiteralPath $pdb)) { throw "PDB da corrida ausente: $pdb (sem ele os simbolos estaticos nao resolvem)" }

$raw = Join-Path $EvidenceDir 'anel-log-do-dump.bin'
$cdbOut = Join-Path $EvidenceDir 'anel-log-do-dump-cdb.txt'
$txt = Join-Path $EvidenceDir 'anel-log-do-dump.txt'
$sym = "$EvidenceDir;srv*C:\Symbols*https://msdl.microsoft.com/download/symbols"
# Corrida 227 (1a tentativa): com -c "a; b; q", um simbolo que nao resolve ABORTA
# o resto da linha, inclusive o q, e o cdb fica parado no prompt esperando teclado.
# Agora: um comando por linha num arquivo (-cf), e "q" tambem pela entrada padrao.
$cmdFile = Join-Path $env:TEMP 'bc250-extrair-anel.cdb'
@(
    '.symopt+ 0x40',
    '.reload /f bc250mcdm.sys',
    'lmvm bc250mcdm',
    'x bc250mcdm!g_Bc250PagingLog*',
    'x bc250mcdm!g_Bc250SdmaLastBltFailure',
    'db bc250mcdm!g_Bc250PagingLogRing L?0x20000',  # texto: .writemem nao deixou arquivo na 227
    'dd bc250mcdm!g_Bc250PagingLogHead L1',
    'dd bc250mcdm!g_Bc250PagingLogTail L1',
    'dt bc250mcdm!BC250_SDMA_BLT_FAILURE bc250mcdm!g_Bc250SdmaLastBltFailure',
    'q'
) | Set-Content -LiteralPath $cmdFile -Encoding ASCII
Write-Host "Extraindo o anel de log e o registro de falha do blt de $Dump (1-3 min) ..."
"q`r`nq`r`n" | & $cdb -z $Dump -y $sym -cf $cmdFile *> $cdbOut
$lines = Get-Content -LiteralPath $cdbOut
$hex = @()
$afterHead = $false
foreach ($l in $lines) {
    if ($l -match 'dd bc250mcdm!g_Bc250PagingLogHead') { $afterHead = $true; continue }
    if ($afterHead -and $l -match '^[0-9a-f`]{8,17}\s+([0-9a-f]{8})\s') { $hex += $Matches[1]; break }
}
# Anel pelo "db": as 0x20000 bytes seguem o comando, 16 por linha.
$ringBytes = New-Object byte[] 0x20000
$ringLines = 0; $inRing = $false; $ringBase = [uint64]0
foreach ($l in $lines) {
    if ($l -match 'db bc250mcdm!g_Bc250PagingLogRing') { $inRing = $true; continue }
    if (-not $inRing) { continue }
    if ($l -match 'kd>') { break }
    if ($l -notmatch '^([0-9a-f]{8})`([0-9a-f]{8})  (.{47})') { continue }
    $addr = [Convert]::ToUInt64($Matches[1] + $Matches[2], 16)
    if ($ringLines -eq 0) { $ringBase = $addr }
    $off = [int]($addr - $ringBase)
    $tok = ($Matches[3] -replace '-', ' ').Split(' ', [StringSplitOptions]::RemoveEmptyEntries)
    if ($tok.Count -ne 16 -or $off -lt 0 -or $off + 16 -gt $ringBytes.Length) { continue }
    for ($k = 0; $k -lt 16; $k++) { if ($tok[$k] -ne '??') { $ringBytes[$off + $k] = [Convert]::ToByte($tok[$k], 16) } }
    $ringLines++
}
if ($ringLines -gt 0) { [IO.File]::WriteAllBytes($raw, $ringBytes) }
if ($ringLines -eq 0 -or $hex.Count -lt 1) {
    # PLANO B (Corrida 227): builds ate 3.375 tinham o anel como static, fora do
    # PDB. O anel mora na imagem do bc250mcdm.sys; copia o modulo inteiro do dump
    # e pega o maior trecho de texto ASCII contiguo, que e' o anel de 128 KB.
    Write-Host "Simbolo do anel ausente no PDB; plano B: imagem do modulo inteira." -ForegroundColor Yellow
    $range = $null
    foreach ($l in $lines) {
        if ($l -match '^([0-9a-f]{8})`([0-9a-f]{8})\s+([0-9a-f]{8})`([0-9a-f]{8})\s+bc250mcdm\b') {
            $range = @(("{0}{1}" -f $Matches[1], $Matches[2]), ("{0}{1}" -f $Matches[3], $Matches[4])); break
        }
    }
    if ($null -eq $range) {
        Write-Host "FALHOU: modulo bc250mcdm nao encontrado no lmvm; ver $cdbOut" -ForegroundColor Red
        exit 2
    }
    $start = [Convert]::ToUInt64($range[0], 16); $end = [Convert]::ToUInt64($range[1], 16)
    $modRaw = Join-Path $EvidenceDir 'modulo-bc250mcdm-do-dump.bin'
    # Corrida 227: o .writemem do modulo inteiro morreu numa pagina ausente do dump
    # ("Unable to read memory ... file is incomplete") e o arquivo nao ficou.
    # Pagina por pagina; a que faltar vira zeros na remontagem.
    # Corrida 227: nem .writemem por pagina deixou arquivo (o cdb disse "Writing"
    # e a pasta ficou vazia). Sem depender de o cdb gravar arquivo: "db" em texto
    # na saida que o PowerShell ja captura; pagina ilegivel sai como "??" -> zero.
    $size = [uint64]($end - $start)
    $dbOut = Join-Path $EvidenceDir 'modulo-bc250mcdm-db.txt'
    $cmdFile2 = Join-Path $env:TEMP 'bc250-extrair-modulo.cdb'
    @(("db 0x{0:x} L?0x{1:x}" -f $start, $size), 'q') | Set-Content -LiteralPath $cmdFile2 -Encoding ASCII
    Write-Host ("Lendo o modulo em texto (0x{0:X} bytes) ..." -f $size)
    "q`r`nq`r`n" | & $cdb -z $Dump -y $sym -cf $cmdFile2 *> $dbOut
    $mb = New-Object byte[] ([int]$size)
    $okLines = 0; $badLines = 0
    foreach ($l in [IO.File]::ReadLines($dbOut)) {
        if ($l -notmatch '^([0-9a-f]{8})`([0-9a-f]{8})  (.{47})') { continue }
        $addr = [Convert]::ToUInt64($Matches[1] + $Matches[2], 16)
        if ($addr -lt $start -or $addr -ge $end) { continue }
        $off = [int]($addr - $start)
        $tok = ($Matches[3] -replace '-', ' ').Split(' ', [StringSplitOptions]::RemoveEmptyEntries)
        if ($tok.Count -ne 16) { continue }
        if ($tok[0] -eq '??') { $badLines++; continue }
        for ($k = 0; $k -lt 16 -and ($off + $k) -lt $mb.Length; $k++) {
            if ($tok[$k] -ne '??') { $mb[$off + $k] = [Convert]::ToByte($tok[$k], 16) }
        }
        $okLines++
    }
    [IO.File]::WriteAllBytes($modRaw, $mb)
    Write-Host "Linhas de 16 bytes lidas do dump: $okLines (ilegiveis: $badLines)."
    if ($okLines -eq 0) {
        Write-Host "FALHOU: nenhuma linha do modulo lida; ver $dbOut" -ForegroundColor Red
        exit 2
    }
    $bestStart = 0; $bestLen = 0; $runStart = -1
    for ($i = 0; $i -le $mb.Length; $i++) {
        $printable = ($i -lt $mb.Length) -and (($mb[$i] -ge 0x20 -and $mb[$i] -le 0x7E) -or $mb[$i] -eq 0x0A -or $mb[$i] -eq 0x0D -or $mb[$i] -eq 0x09)
        if ($printable) { if ($runStart -lt 0) { $runStart = $i } }
        elseif ($runStart -ge 0) {
            if (($i - $runStart) -gt $bestLen) { $bestLen = $i - $runStart; $bestStart = $runStart }
            $runStart = -1
        }
    }
    $ringText = [Text.Encoding]::ASCII.GetString($mb, $bestStart, $bestLen)
    $hdr = "# PLANO B: maior trecho ASCII da imagem do bc250mcdm.sys no dump ($bestLen bytes no offset 0x{0:X}).`r`n" -f $bestStart
    $hdr += "# E' o anel de log, mas em ORDEM DE MEMORIA: o ponto de escrita (Head) e' a costura onde o texto novo encontra o antigo.`r`n"
    Set-Content -LiteralPath $txt -Value ($hdr + $ringText) -Encoding ASCII
    Write-Host "anel-log-do-dump.txt escrito pelo plano B ($bestLen bytes)."
    $rl = $ringText -split "`n"
    $bestSeq = -1; $bestIdx = -1
    for ($k = 0; $k -lt $rl.Count; $k++) {
        if ($rl[$k] -match '\[PRIMARY_COPY #(\d+)\]') {
            $n = [int]$Matches[1]; if ($n -ge $bestSeq) { $bestSeq = $n; $bestIdx = $k }
        }
    }
    if ($bestIdx -ge 0) {
        Write-Host "--- em torno da ultima PRIMARY_COPY (#$bestSeq) no anel ---"
        $from = [Math]::Max(0, $bestIdx - 5); $to = [Math]::Min($rl.Count - 1, $bestIdx + 40)
        $rl[$from..$to] | ForEach-Object { Write-Host "  $($_.TrimEnd())" }
    }
    $rl | Select-String 'Blt Timeout|Blt RECUSADO|PRIMARY_COPY #\d+\] FAIL|MPO3 FAIL_CLOSED' |
        ForEach-Object { Write-Host "  ACHADO: $($_.Line.TrimEnd())" -ForegroundColor Cyan }
    Write-Host "Somente leitura do dump. NAO VALIDADO EM HARDWARE (coleta, nao medicao)."
    exit 0
}
$head = [Convert]::ToInt32($hex[0], 16)
$bytes = [IO.File]::ReadAllBytes($raw)
if ($head -lt 0 -or $head -ge $bytes.Length) { $head = 0 }
$ordered = New-Object byte[] $bytes.Length
[Array]::Copy($bytes, $head, $ordered, 0, $bytes.Length - $head)
[Array]::Copy($bytes, 0, $ordered, $bytes.Length - $head, $head)
$text = [Text.Encoding]::ASCII.GetString($ordered) -replace "`0", ''
$hdr = "# Anel de log do KMD extraido do dump ($Dump), Head=$head. Ordem cronologica; a 1a linha pode estar cortada.`r`n" +
       "# Linhas que tambem estao no log em disco aparecem de novo aqui: o que importa e' o FIM.`r`n"
Set-Content -LiteralPath $txt -Value ($hdr + $text) -Encoding ASCII
Write-Host "anel-log-do-dump.txt escrito ($($text.Length) caracteres, Head=$head)."
Write-Host "--- ultimas 15 linhas do anel ---"
($text -split "`n") | Select-Object -Last 15 | ForEach-Object { Write-Host "  $_" }
Write-Host "--- g_Bc250SdmaLastBltFailure ---"
$in = $false
foreach ($l in $lines) {
    if ($l -match 'BC250_SDMA_BLT_FAILURE|^\s+\+0x') { $in = $true }
    if ($in -and $l -match '^\s+\+0x') { Write-Host "  $l" }
}
Write-Host "Somente leitura do dump. NAO VALIDADO EM HARDWARE (coleta, nao medicao)."
