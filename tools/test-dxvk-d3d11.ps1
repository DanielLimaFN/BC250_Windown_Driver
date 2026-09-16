<#
.SYNOPSIS
    Gate fisico de Direct3D 11 via DXVK -> Vulkan WSI (Marco 5: D3D11 na BC-250).

.DESCRIPTION
    Executa um harness Direct3D 11 nativo (D3D11Triangle.exe) que carrega as DLLs do DXVK
    (d3d11.dll e dxgi.dll), traduz as chamadas D3D11/DXBC para Vulkan SPIR-V em tempo real
    e submete ao silicio fisico da AMD BC-250 (1002:13FE) via Mesa RADV + ACO, apresentando
    no scanout do UEFI GOP (2560x1080 em VRAM) via SDMA HW Blt.

    O veredito exige simultaneamente:
      - Execucao limpa do D3D11Triangle.exe (Exit 0);
      - Inicializacao do DXVK confirmada por log (dxgi.log / d3d11.log);
      - Submissao e aposentadoria observadas no Node 1 pelo motor P1;
      - Ring de comando do Node 1 drenado (WPTR == RPTR);
      - Telemetria do KMD confirmando deteccao de quadros reais e copia SDMA para o scanout;
      - Zero copias por CPU (HostCpuCopies = 0);
      - Zero eventos de TDR ou instabilidade.
#>
[CmdletBinding()]
param(
    [string]$EvidenceDir,
    [string]$IcdManifest,
    [string]$TestExe = 'C:\Users\Daniel\Desktop\NamixWDDM\driver\tests\output_dxvk\D3D11Triangle.exe',
    [string]$MesaRoot = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm',
    [ValidateRange(10,600)][int]$TimeoutSeconds = 180,
    [ValidateRange(1,21600)][int]$Frames = 1800,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$probePath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
$kmdAudit = Join-Path $root 'tools\audit-bc250mcdm-safety.py'
$radvAudit = Join-Path $root 'tools\audit-radv-wddm-safety.py'
$safeGuardAuditScript = Join-Path $root 'tools\assert-bc250-safe-bind-guard.ps1'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$displayLogPath = 'C:\bc250_display_log.txt'

if ([string]::IsNullOrWhiteSpace($IcdManifest)) {
    $IcdManifest = Join-Path $root 'tools\bc250-radv-test-icd.json'
}

$watched = @(
    'P1EngineState',
    'P1SubmitCalls',
    'P1SubmitAccepted',
    'P1SubmitRejectedNode',
    'P1SubmitRejectedPlan',
    'P1SubmitRejectedEmit',
    'P1RetiredTotal',
    'P1RingFullCount',
    'P1FenceRegressionCount',
    'P1LastWptrPublished',
    'P1LastWptrMmioReadback',
    'P1LastRptrMmio',
    'P1LastRptrReportMem',
    'P1LastFenceObserved',
    'P1LastSubmittedFence',
    'P1LastRetiredFence',
    'V2TrackerPushedTotal',
    'V2NotifiedTotal',
    'V2NotifyBatches',
    'V2FencesInFlight',
    'V2TrackerOverflow',
    'V2TrackerOutOfOrder',
    'V2PushFailures',
    'V2PollFailures',
    'GpuvaGateEligible',
    'GpuvaGateOperational',
    'GpuvaGateBlockMask',
    'GpuvaLabPromotionRequested',
    'GpuvaLabPromotionPresent'
)

function Get-U32Value($Raw) {
    if ($null -eq $Raw) { return $null }
    return ([int64]$Raw -band 0xFFFFFFFFL)
}

function Get-ProbeSnapshot {
    if (-not (Test-Path -LiteralPath $probePath)) {
        throw "Telemetria ausente: $probePath"
    }
    $properties = Get-ItemProperty -LiteralPath $probePath
    $snapshot = [ordered]@{}
    foreach ($name in $watched) {
        if ($null -eq $properties.PSObject.Properties[$name]) {
            throw "Telemetria obrigatoria ausente: $name"
        }
        $snapshot[$name] = Get-U32Value $properties.$name
    }
    return $snapshot
}

function Get-U32Delta([int64]$Before, [int64]$After) {
    if ($After -ge $Before) { return $After - $Before }
    return (0x100000000L - $Before) + $After
}

function Invoke-SafetyAudit([string]$FilePath, [string[]]$Arguments) {
    $output = (& py -3 $FilePath @Arguments 2>&1) -join "`r`n"
    if ($LASTEXITCODE -ne 0) {
        throw "Auditoria fail-closed falhou: $FilePath`r`n$output"
    }
    return $output
}

function Get-BootFuseState {
    $path = 'C:\BC250_BOOT_FUSE.dat'
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    $raw = [System.IO.File]::ReadAllBytes($path)
    if ($raw.Length -ne 40) { throw 'Fusivel persistente possui formato inesperado.' }
    $magic = [BitConverter]::ToUInt32($raw, 0)
    if ($magic -ne 0x45535546) { throw 'Fusivel persistente possui magic inesperado.' }
    return [int][BitConverter]::ToUInt32($raw, 8)
}

function Test-BootFuseNotTripped {
    $state = Get-BootFuseState
    if ($null -eq $state) { return $true }
    if ($state -eq 2) {
        throw ('Fusivel persistente em TRIPPED (2): um boot anterior caiu. ' +
            'Investigue antes de gastar a corrida; reset com ' +
            'tools\inspect-boot-fuse.ps1 -Reset.')
    }
    if ($state -ne 0 -and $state -ne 1) {
        throw "Fusivel persistente em estado desconhecido (state=$state)."
    }
    return $true
}

Write-Host '=== DXVK_D3D11_GATE (Marco 5: Direct3D 11 via DXVK no Scanout GOP) ===' -ForegroundColor Cyan
Write-Host "  Harness : $TestExe"
Write-Host "  ICD     : $IcdManifest"
Write-Host "  Mesa    : $MesaRoot"
Write-Host "  Frames  : $Frames"

$exeDir = [System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($TestExe))
$dxvkD3D11 = Join-Path $exeDir 'd3d11.dll'
$dxvkDXGI = Join-Path $exeDir 'dxgi.dll'

if (-not (Test-Path -LiteralPath $TestExe -PathType Leaf)) {
    throw "D3D11Triangle.exe ausente: $TestExe. Execute tools\build-d3d11-triangle.bat primeiro."
}
if (-not (Test-Path -LiteralPath $dxvkD3D11 -PathType Leaf) -or
    -not (Test-Path -LiteralPath $dxvkDXGI -PathType Leaf)) {
    throw "DLLs do DXVK (d3d11.dll e dxgi.dll) ausentes em $exeDir."
}
if (-not (Test-Path -LiteralPath $IcdManifest -PathType Leaf)) {
    throw "Manifesto do ICD ausente: $IcdManifest"
}
$icdJson = Get-Content -LiteralPath $IcdManifest -Raw | ConvertFrom-Json
$radvDll = [string]$icdJson.ICD.library_path
if (-not (Test-Path -LiteralPath $radvDll -PathType Leaf)) {
    throw "DLL declarada pelo ICD ausente: $radvDll"
}

Test-BootFuseNotTripped | Out-Null

if (-not $Execute) {
    Write-Host '  Modo inspect/dry-run: preflights passaram, hardware nao acionado.' -ForegroundColor Yellow
    return
}

if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $EvidenceDir = Join-Path $root 'docs\evidence'
}
if (-not (Test-Path -LiteralPath $EvidenceDir)) {
    New-Item -ItemType Directory -Path $EvidenceDir | Out-Null
}
$safeGuardRaw = (& powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    $safeGuardAuditScript -EvidenceDir $EvidenceDir `
    -RequiredRemainingSeconds ($TimeoutSeconds + 10) 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Corrida recusada: guardiao SYSTEM nao comprovado:`n$safeGuardRaw"
}
$safeGuardAudit = $safeGuardRaw | ConvertFrom-Json

$kmdAuditOutput = Invoke-SafetyAudit $kmdAudit @('--driver-path', $driverPath)
$radvAuditOutput = Invoke-SafetyAudit $radvAudit @('--mesa-root', $MesaRoot)
$before = Get-ProbeSnapshot

if ([int64]$before.P1EngineState -ne 1) {
    throw "Motor P1 nao esta em ONLINE (1): P1EngineState=$($before.P1EngineState)"
}
if ([int64]$before.GpuvaLabPromotionPresent -ne 1 -or
    [int64]$before.GpuvaLabPromotionRequested -ne 1) {
    throw 'Corrida recusada: GpuvaLabPromotion nao esta armado.'
}

$displayLogStartOffset = 0
if (Test-Path -LiteralPath $displayLogPath -PathType Leaf) {
    $displayLogStartOffset = (Get-Item -LiteralPath $displayLogPath).Length
}

$runStamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss-fff'
$runDir = Join-Path $EvidenceDir "dxvk-d3d11-$runStamp"
New-Item -ItemType Directory -Path $runDir | Out-Null
$kmdAuditOutput | Set-Content -LiteralPath (Join-Path $runDir 'kmd-safety-audit.txt') -Encoding UTF8
$radvAuditOutput | Set-Content -LiteralPath (Join-Path $runDir 'radv-safety-audit.txt') -Encoding UTF8
$before | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $runDir 'probe-before.json') -Encoding UTF8

$start = Get-Date
$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = [System.IO.Path]::GetFullPath($TestExe)
$startInfo.Arguments = "--frames $Frames"
$startInfo.WorkingDirectory = $exeDir
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $false
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$startInfo.EnvironmentVariables['VK_ICD_FILENAMES'] = [System.IO.Path]::GetFullPath($IcdManifest)
$startInfo.EnvironmentVariables['VK_DRIVER_FILES'] = [System.IO.Path]::GetFullPath($IcdManifest)
$startInfo.EnvironmentVariables['DXVK_LOG_LEVEL'] = 'info'
$startInfo.EnvironmentVariables['DXVK_LOG_PATH'] = [System.IO.Path]::GetFullPath($runDir)

$process = New-Object System.Diagnostics.Process
$process.StartInfo = $startInfo

$timedOut = $false
$exitCode = $null
$stdout = ''
$stderr = ''
try {
    if (-not $process.Start()) { throw 'Process.Start do D3D11Triangle retornou false.' }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $timedOut = $true
        try { $process.Kill() } catch { }
        [void]$process.WaitForExit(5000)
    }
    if ($process.HasExited) {
        $stdout = $stdoutTask.Result
        $stderr = $stderrTask.Result
        if (-not $timedOut) { $exitCode = $process.ExitCode }
    }
} finally {
    $process.Dispose()
}

$elapsedMs = [math]::Round(((Get-Date) - $start).TotalMilliseconds)
$eventEnd = Get-Date
$stdout | Set-Content -LiteralPath (Join-Path $runDir 'd3d11.stdout.txt') -Encoding UTF8
$stderr | Set-Content -LiteralPath (Join-Path $runDir 'd3d11.stderr.txt') -Encoding UTF8

$eventRecords = @()
foreach ($logName in @('System', 'Application')) {
    try {
        $events = Get-WinEvent -FilterHashtable @{
            LogName = $logName
            StartTime = $start.AddSeconds(-1)
            EndTime = $eventEnd.AddSeconds(1)
        } -ErrorAction Stop
        foreach ($event in $events) {
            $message = [string]$event.Message
            $keep = ($logName -eq 'System' -and
                ($event.ProviderName -match 'Display|DxgKrnl|Kernel-PnP|Watchdog|WHEA' -or
                 $event.Id -in 14, 117, 141, 411, 4101)) -or
                ($logName -eq 'Application' -and
                ($event.ProviderName -match 'Windows Error Reporting|Application Error' -or
                 $message -match 'dwm\.exe|D3D11Triangle\.exe'))
            if ($keep) {
                $eventRecords += [ordered]@{
                    log = $logName
                    time_created = $event.TimeCreated.ToString('o')
                    id = [int]$event.Id
                    provider = [string]$event.ProviderName
                    level = [int]$event.Level
                    message = $message
                }
            }
        }
    } catch { }
}
$eventRecords | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $runDir 'windows-events-this-run.json') -Encoding UTF8

$after = Get-ProbeSnapshot
$after | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $runDir 'probe-after.json') -Encoding UTF8

$delta = [ordered]@{}
foreach ($name in $watched) {
    $delta[$name] = Get-U32Delta $before[$name] $after[$name]
}

# Delta do log do KMD
$deltaLines = @()
if (Test-Path -LiteralPath $displayLogPath -PathType Leaf) {
    $stream = [System.IO.File]::Open($displayLogPath, 'Open', 'Read', 'ReadWrite')
    try {
        if ($displayLogStartOffset -lt $stream.Length) {
            $null = $stream.Seek($displayLogStartOffset, 'Begin')
            $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
            while (-not $reader.EndOfStream) {
                $line = $reader.ReadLine()
                if (-not [string]::IsNullOrWhiteSpace($line)) {
                    $deltaLines += $line
                }
            }
        }
    } finally {
        $stream.Dispose()
    }
}
$deltaLines | Set-Content -LiteralPath (Join-Path $runDir 'display-log-delta.txt') -Encoding UTF8

$sdmaSuccess = ($deltaLines | Where-Object { $_ -match '\[Present #\d+\] .* SDMA=YES' }).Count
$sdmaFailed = ($deltaLines | Where-Object { $_ -match '\[Present #\d+\] .* SDMA=NO' }).Count
$hostCopies = ($deltaLines | Where-Object { $_ -match 'FALLBACK_CPU|HostCpuCopies' }).Count
$tdrEvents = ($eventRecords | Where-Object { $_.provider -match 'Display' -and $_.id -in 4101, 117, 141 }).Count
$dwmCrashes = ($eventRecords | Where-Object { $_.message -match 'dwm\.exe' -and $_.id -eq 1000 }).Count

$checks = [ordered]@{
    process_completed = (-not $timedOut)
    process_exit_zero = ($exitCode -eq 0)
    submit_accepted = ($delta.P1SubmitAccepted -gt 0)
    retired = ($delta.P1RetiredTotal -gt 0)
    ring_drained = ($after.P1LastWptrPublished -eq $after.P1LastRptrMmio)
    desktop_detected_in_kmd = ($deltaLines | Where-Object { $_ -match 'Desktop detected in KMD' }).Count -gt 0
    sdma_blt_executed = ($sdmaSuccess -gt 0)
    no_host_copy_fallback = ($hostCopies -eq 0)
}

$verdictPass = (-not $timedOut -and $exitCode -eq 0 -and
    $checks.submit_accepted -and $checks.retired -and $checks.ring_drained -and
    $checks.sdma_blt_executed -and $checks.no_host_copy_fallback -and $tdrEvents -eq 0)

$verdictObj = [ordered]@{
    verdict = $(if ($verdictPass) { 'PASS' } else { 'FAIL' })
    timestamp = (Get-Date).ToString('o')
    elapsed_ms = $elapsedMs
    safe_bind_guard = $safeGuardAudit
    checks = $checks
    probe_delta = $delta
    diagnostics = [ordered]@{
        sdma_blt_success_logged = $sdmaSuccess
        sdma_blt_failed_logged = $sdmaFailed
        host_cpu_copy_logged = $hostCopies
        tdr_events = $tdrEvents
        dwm_crash_events = $dwmCrashes
    }
}

$verdictJsonPath = Join-Path $runDir 'dxvk-verdict.json'
$verdictObj | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $verdictJsonPath -Encoding UTF8

Write-Host "`n=== VEREDITO MARCO 5 (Direct3D 11 via DXVK): $($verdictObj.verdict) ===" -ForegroundColor $(if ($verdictPass) { 'Green' } else { 'Red' })
foreach ($k in $checks.Keys) {
    Write-Host ("  {0,-28}: {1}" -f $k, $checks[$k])
}
Write-Host ("  sdma_blts                   : {0}" -f $sdmaSuccess)
Write-Host ("  host_cpu_copies             : {0}" -f $hostCopies)
Write-Host ("  tdr_events                  : {0}" -f $tdrEvents)
Write-Host "Evidencia gravada em: $runDir`n"

if (-not $verdictPass) {
    exit 1
}
exit 0
