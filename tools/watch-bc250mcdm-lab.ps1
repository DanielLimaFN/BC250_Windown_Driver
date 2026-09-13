[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EvidenceDir,
    [Parameter(Mandatory = $true)][string]$InstanceId,
    [Parameter(Mandatory = $true)][string]$TargetInf,
    [Parameter(Mandatory = $true)][string]$Token,
    [ValidateRange(30,600)][int]$TimeoutSeconds = 180,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$rollbackScript = Join-Path $root 'tools\rollback-bc250mcdm-lab.ps1'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$statePath = Join-Path $EvidenceDir 'watchdog-state.json'
$disarmPath = Join-Path $EvidenceDir 'watchdog-disarm.json'

if (-not (Test-Path -LiteralPath $EvidenceDir -PathType Container)) {
    throw 'Watchdog recusado: diretorio de evidencia ausente.'
}
if ($InstanceId -notmatch '(?i)^PCI\\VEN_1002&DEV_13FE(?:&|\\)') {
    throw 'Watchdog recusado: instance ID nao pertence ao BC-250 exato.'
}

if (-not $Execute) {
    Write-Host "WATCHDOG LAB DRY-RUN: target=$TargetInf timeout=$TimeoutSeconds instance=$InstanceId"
    exit 0
}

function Write-WatchdogState([string]$State, [string]$Detail, [int]$ExitCode = 0) {
    [ordered]@{
        state = $State
        detail = $Detail
        target_inf = $TargetInf
        instance_id = $InstanceId
        timestamp = [datetime]::Now.ToString('o')
        exit_code = $ExitCode
        hardware_validated = $false
    } | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8
}

Write-WatchdogState 'ARMED' "automatic rollback in $TimeoutSeconds seconds"
$deadline = [datetime]::Now.AddSeconds($TimeoutSeconds)
do {
    if (Test-Path -LiteralPath $disarmPath) {
        try {
            $disarm = Get-Content -LiteralPath $disarmPath -Raw | ConvertFrom-Json
            if ([string]$disarm.token -eq $Token -and
                [string]$disarm.instance_id -eq $InstanceId -and
                [bool]$disarm.display_visible) {
                Write-WatchdogState 'DISARMED' 'visual confirmation received'
                exit 0
            }
        } catch {
        }
    }
    Start-Sleep -Milliseconds 250
} until ([datetime]::Now -ge $deadline)

# C3.15 (12/09): RECOVERY PRIMEIRO, EVIDENCIA DEPOIS.
#
# Na corrida 18:21 o log cumulativo cresceu para mais de 1 GB. O watchdog
# expirou, mas tentou copiar, calcular hash e contar todas as linhas ANTES do
# rollback. A copia pre-rollback existe; o estado nunca avancou de ARMED e o
# driver permaneceu vinculado ate o reset manual. Nenhuma coleta best-effort
# pode preceder a unica acao de seguranca do processo.
Write-WatchdogState 'ROLLBACK_STARTED' 'visual confirmation timeout expired'
$rollbackOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $rollbackScript `
    -Execute -IAcceptDisplayInterruptionRisk 2>&1) -join "`n"
$rollbackExit = $LASTEXITCODE
$rollbackOutput | Set-Content -LiteralPath (Join-Path $EvidenceDir 'watchdog-rollback.txt') -Encoding UTF8

if ($rollbackExit -ne 0) {
    Write-WatchdogState 'ROLLBACK_FAILED' 'online rollback failed; safe-mode recovery required' $rollbackExit
    exit $rollbackExit
}
Write-WatchdogState 'ROLLED_BACK' 'BasicDisplay restoration completed'

# O rollback encerrou o produtor. Arquive apenas a cauda delimitada do log;
# nunca volte a copiar/varrer o arquivo cumulativo inteiro no caminho critico.
$liveKmdLog = 'C:\BC250_DISPLAY_LOG.txt'
if (Test-Path -LiteralPath $liveKmdLog) {
    try {
        $maxTailBytes = 64MB
        $sourceStream = [System.IO.File]::Open($liveKmdLog,
            [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
        try {
            $sourceLength = $sourceStream.Length
            $startOffset = [Math]::Max(0L, $sourceLength - [int64]$maxTailBytes)
            $baselinePath = Join-Path $EvidenceDir 'display-log-baseline.json'
            if (Test-Path -LiteralPath $baselinePath) {
                $baseline = Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json
                $candidateOffset = [int64]$baseline.start_offset
                if ($candidateOffset -ge 0 -and $candidateOffset -le $sourceLength -and
                    ($sourceLength - $candidateOffset) -le $maxTailBytes) {
                    $startOffset = $candidateOffset
                }
            }
            $tailPath = Join-Path $EvidenceDir 'bc250_display_log_tail_post_rollback.txt'
            $targetStream = [System.IO.File]::Open($tailPath,
                [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write,
                [System.IO.FileShare]::Read)
            try {
                [void]$sourceStream.Seek($startOffset, [System.IO.SeekOrigin]::Begin)
                $remaining = $sourceLength - $startOffset
                $buffer = New-Object byte[] (1MB)
                while ($remaining -gt 0) {
                    $wanted = [int][Math]::Min([int64]$buffer.Length, $remaining)
                    $read = $sourceStream.Read($buffer, 0, $wanted)
                    if ($read -le 0) { break }
                    $targetStream.Write($buffer, 0, $read)
                    $remaining -= $read
                }
            } finally {
                $targetStream.Dispose()
            }
        } finally {
            $sourceStream.Dispose()
        }
        [ordered]@{
            source = $liveKmdLog
            source_length = $sourceLength
            copied_from = $startOffset
            copied_bytes = $sourceLength - $startOffset
            truncated = ($startOffset -gt 0)
            sha256 = (Get-FileHash -LiteralPath $tailPath -Algorithm SHA256).Hash
            recovery_completed_before_capture = $true
        } | ConvertTo-Json | Set-Content -LiteralPath `
            (Join-Path $EvidenceDir 'bc250_display_log_tail_post_rollback.json') -Encoding UTF8
    } catch {
        "falha ao arquivar cauda de $liveKmdLog : $($_.Exception.Message)" |
            Set-Content -LiteralPath (Join-Path $EvidenceDir 'bc250_display_log_tail_ERRO.txt') -Encoding UTF8
    }
}

$umdLogCollected = $false
$umdAggregate = Join-Path $EvidenceDir 'bc250_umd_log.txt'
$umdLogs = @(Get-ChildItem -LiteralPath 'C:\Windows\Temp' `
    -Filter 'BC250_UMD_LOG_*.txt' -File -ErrorAction SilentlyContinue |
    Sort-Object Name)
foreach ($umdLog in $umdLogs) {
    Copy-Item -LiteralPath $umdLog.FullName `
        -Destination (Join-Path $EvidenceDir $umdLog.Name.ToLowerInvariant()) -Force
    ("===== {0} =====" -f $umdLog.Name),
        (Get-Content -LiteralPath $umdLog.FullName -ErrorAction SilentlyContinue) |
        Add-Content -LiteralPath $umdAggregate -Encoding UTF8
    $umdLogCollected = $true
}
if (-not $umdLogCollected) {
    'bc250umd.dll nunca escreveu: a DLL nao foi carregada por nenhum processo.' |
        Set-Content -LiteralPath $umdAggregate -Encoding UTF8
}
exit 0
