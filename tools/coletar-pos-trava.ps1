[CmdletBinding()]
param(
    # Pasta de evidencia do bind que travou. Padrao: a *-lab-binding mais recente.
    [string]$EvidenceDir = ''
)

# coletar-pos-trava.ps1 (doc 556) -- rodar ELEVADO, no PRIMEIRO boot depois de uma trava,
# ANTES de qualquer outra coisa (inclusive antes do BC250_RECOVERY.cmd).
# Ordem:
#   1. motivo do reset (bc250-vram.ps1 -Ler): a primeira leitura depois da trava e' a medida;
#   2. logs da UMD do fim da corrida (C:\Windows\Temp e AppContainer), que somem;
#   3. cauda do log do KMD e do rastro de paginacao pelo offset do baseline do bind;
#   4. decodificacao do rastro e SHA-256 de tudo.
# So' le e copia. Nao altera driver, registro nem dispositivo.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Rode em PowerShell como Administrador.'
}

if (-not $EvidenceDir) {
    $last = Get-ChildItem -LiteralPath (Join-Path $root 'docs\evidence') -Directory |
        Where-Object { $_.Name -like '*-lab-binding' } | Sort-Object LastWriteTime | Select-Object -Last 1
    if (-not $last) { throw 'Nenhuma pasta *-lab-binding em docs\evidence.' }
    $EvidenceDir = $last.FullName
}
if (-not (Test-Path -LiteralPath $EvidenceDir -PathType Container)) { throw "Pasta ausente: $EvidenceDir" }
Write-Host "Evidencia: $EvidenceDir" -ForegroundColor Cyan
$bootTime = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
Write-Host "Boot atual: $bootTime"

function Read-Baseline([string]$name) {
    $p = Join-Path $EvidenceDir $name
    if (-not (Test-Path -LiteralPath $p)) { return $null }
    return (Get-Content -LiteralPath $p -Raw | ConvertFrom-Json)
}

function Copy-Tail([string]$source, [int64]$offset, [string]$dest) {
    if (-not (Test-Path -LiteralPath $source)) { Write-Host "  AUSENTE: $source" -ForegroundColor Yellow; return }
    $in = [System.IO.File]::Open($source, 'Open', 'Read', 'ReadWrite')
    try {
        if ($offset -gt $in.Length) { Write-Host "  offset $offset alem do fim de $source ($($in.Length))" -ForegroundColor Yellow; return }
        [void]$in.Seek($offset, 'Begin')
        $out = [System.IO.File]::Create($dest)
        try { $in.CopyTo($out) } finally { $out.Dispose() }
    } finally { $in.Dispose() }
    Write-Host ("  {0}: {1} bytes" -f (Split-Path -Leaf $dest), (Get-Item -LiteralPath $dest).Length)
}

# 1. Motivo do reset -- PRIMEIRO.
Write-Host "`n[1/4] Motivo do ultimo reset (le e limpa, como o Linux)" -ForegroundColor Cyan
$vram = Join-Path $root 'tools\vram-cmos\bc250-vram.ps1'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $vram -Ler
$ler = Get-ChildItem -LiteralPath (Join-Path $root 'docs\evidence\vram-cmos') -Filter '*-ler.txt' |
    Sort-Object LastWriteTime | Select-Object -Last 1
if ($ler -and $ler.LastWriteTime -ge $bootTime) {
    Copy-Item -LiteralPath $ler.FullName -Destination (Join-Path $EvidenceDir 'reset-reason-pos-trava.txt') -Force
    Write-Host "  copiado: reset-reason-pos-trava.txt (de $($ler.Name))"
} else {
    Write-Host '  AVISO: leitura da CMOS deste boot nao encontrada; o motivo do reset NAO foi medido.' -ForegroundColor Yellow
}

# 2. Logs da UMD do fim da corrida.
Write-Host "`n[2/4] Logs da UMD (Windows\Temp e AppContainer)" -ForegroundColor Cyan
$bindTime = (Get-Item -LiteralPath $EvidenceDir).CreationTime.AddMinutes(-5)
$umdDir = Join-Path $EvidenceDir 'umd-logs-pos-travamento'
New-Item -ItemType Directory -Force -Path $umdDir | Out-Null
$n = 0
foreach ($f in @(Get-ChildItem -Path 'C:\Windows\Temp' -Filter 'BC250_UMD_LOG_*.txt' -File -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -ge $bindTime })) {
    Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $umdDir $f.Name) -Force; $n++
}
$acRoots = @(Get-ChildItem -Path 'C:\Users\*\AppData\Local\Packages\*\AC\Temp' -Directory -ErrorAction SilentlyContinue)
foreach ($r in $acRoots) {
    foreach ($f in @(Get-ChildItem -LiteralPath $r.FullName -Filter 'BC250_UMD_LOG_*.txt' -File -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -ge $bindTime })) {
        $pkg = Split-Path -Leaf (Split-Path -Parent (Split-Path -Parent $r.FullName))
        Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $umdDir ($pkg + '__' + $f.Name)) -Force; $n++
    }
}
Write-Host "  $n arquivo(s) em umd-logs-pos-travamento\"

# 3. Caudas do KMD e do rastro.
Write-Host "`n[3/4] Caudas do log do KMD e do rastro de paginacao" -ForegroundColor Cyan
$dl = Read-Baseline 'display-log-baseline.json'
if ($dl) { Copy-Tail $dl.path ([int64]$dl.start_offset) (Join-Path $EvidenceDir 'display-log-this-bind-RECUPERADO.txt') }
else { Write-Host '  display-log-baseline.json ausente' -ForegroundColor Yellow }
$pt = Read-Baseline 'paging-trace-baseline.json'
$traceOut = Join-Path $EvidenceDir 'paging-trace-this-bind-RECUPERADO.bin'
if ($pt) { Copy-Tail $pt.path ([int64]$pt.start_offset) $traceOut }
else { Write-Host '  paging-trace-baseline.json ausente' -ForegroundColor Yellow }

# 4. Decodificacao e hashes.
Write-Host "`n[4/4] Decodificacao e SHA-256" -ForegroundColor Cyan
if (Test-Path -LiteralPath $traceOut) {
    $json = Join-Path $EvidenceDir 'paging-trace-decodificado.json'
    & python (Join-Path $root 'tools\decode-bc250-paging-trace.py') $traceOut | Set-Content -LiteralPath $json -Encoding UTF8
    Write-Host "  paging-trace-decodificado.json escrito (contem tambem o boot atual; a corrida termina na queda do writer_tick)"
}
# 4b (doc 571). Se a corrida terminou em BugCheck, a pilha do MEMORY.DMP sai junto
# (so' leitura do dump; exige o cdb do WDK). So' o dump POSTERIOR ao bind conta.
$dump = 'C:\Windows\MEMORY.DMP'
$cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
$bindTime = (Get-Item -LiteralPath $EvidenceDir).CreationTime
if ((Test-Path -LiteralPath $dump) -and (Get-Item -LiteralPath $dump).LastWriteTime -gt $bindTime) {
    if (Test-Path -LiteralPath $cdb) {
        $dumpOut = Join-Path $EvidenceDir 'dump-analyze-RECUPERADO.txt'
        Write-Host "  MEMORY.DMP posterior ao bind: rodando cdb (!analyze -v), pode levar alguns minutos..."
        & $cdb -z $dump -y 'srv*C:\Symbols*https://msdl.microsoft.com/download/symbols' `
            -c '!analyze -v; lm m bc250*; q' *> $dumpOut
        Get-Content -LiteralPath $dumpOut | Select-String 'BUGCHECK_CODE|BUGCHECK_P1|PROCESS_NAME|FAILURE_BUCKET_ID' |
            ForEach-Object { Write-Host "  $_" }
        # FB.1 (doc 607): o fim do log que o BugCheck nao deixou chegar ao disco
        # esta no anel em RAM, dentro do dump; junto o registro de falha do blt.
        try {
            & (Join-Path $root 'tools\extrair-anel-do-dump.ps1') -EvidenceDir $EvidenceDir -Dump $dump
        } catch {
            Write-Host "  extracao do anel do dump falhou: $($_.Exception.Message)" -ForegroundColor Yellow
        }
    } else {
        Write-Host "  MEMORY.DMP posterior ao bind, mas cdb ausente em $cdb" -ForegroundColor Yellow
    }
} else {
    Write-Host '  sem MEMORY.DMP posterior ao bind (trava dura sem BugCheck, ou sem dump)'
}
$hashes = Get-ChildItem -LiteralPath $EvidenceDir -File |
    Where-Object { $_.Name -like '*RECUPERADO*' -or $_.Name -like 'reset-reason-*' } |
    ForEach-Object { '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash, $_.Name }
$hashes | Set-Content -LiteralPath (Join-Path $EvidenceDir 'SHA256-recuperados.txt') -Encoding ASCII
$hashes | ForEach-Object { Write-Host "  $_" }
Write-Host "`nColeta pos-trava concluida. Agora sim: tools\BC250_RECOVERY.cmd, inspect-boot-fuse.ps1 -Reset e boot frio." -ForegroundColor Green
