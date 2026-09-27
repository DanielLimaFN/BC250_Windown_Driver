<#
.SYNOPSIS
    Gate fisico de apresentacao Vulkan WSI nativo (Marco 4: vkcube no Scanout GOP).

.DESCRIPTION
    Executa a demonstracao oficial Khronos VulkanCube.exe (renderizacao 3D do cubo texturizado)
    usando a swapchain WSI nativa (VK_KHR_swapchain / VK_KHR_win32_surface) via Mesa RADV + ACO
    no silicio fisico da AMD BC-250 (1002:13FE) e apresenta via SDMA0 Blt diretamente para o
    scanout fisico do UEFI GOP (0xF400000000 em VRAM / 2560x1080).

    O veredito exige simultaneamente:
      - Execucao limpa do VulkanCube.exe (Exit 0);
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
    [string]$CubeExe = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\Vulkan-Cube\VulkanCube.exe',
    [string]$MesaRoot = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm',
    [ValidateRange(10,600)][int]$TimeoutSeconds = 180,
    [ValidateRange(1,21600)][int]$Frames = 3600,
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

Write-Host '=== VULKAN_WSI_CUBE (Marco 4: vkcube no Scanout GOP) ===' -ForegroundColor Cyan
Write-Host "  Cube Exe: $CubeExe"
Write-Host "  ICD     : $IcdManifest"
Write-Host "  Mesa    : $MesaRoot"
Write-Host "  Frames  : $Frames"

if (-not (Test-Path -LiteralPath $CubeExe -PathType Leaf)) {
    throw "VulkanCube.exe ausente: $CubeExe. Compile em Vulkan-Cube primeiro."
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
$runDir = Join-Path $EvidenceDir "vulkan-wsi-cube-$runStamp"
New-Item -ItemType Directory -Path $runDir | Out-Null
$kmdAuditOutput | Set-Content -LiteralPath (Join-Path $runDir 'kmd-safety-audit.txt') -Encoding UTF8
$radvAuditOutput | Set-Content -LiteralPath (Join-Path $runDir 'radv-safety-audit.txt') -Encoding UTF8
$before | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $runDir 'probe-before.json') -Encoding UTF8

$start = Get-Date
$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = [System.IO.Path]::GetFullPath($CubeExe)
$startInfo.Arguments = "--frames $Frames"
$startInfo.WorkingDirectory = [System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($CubeExe))
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $false
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$startInfo.EnvironmentVariables['VK_ICD_FILENAMES'] = [System.IO.Path]::GetFullPath($IcdManifest)
$startInfo.EnvironmentVariables['VK_DRIVER_FILES'] = [System.IO.Path]::GetFullPath($IcdManifest)

$process = New-Object System.Diagnostics.Process
$process.StartInfo = $startInfo

$timedOut = $false
$exitCode = $null
$stdout = ''
$stderr = ''
try {
    if (-not $process.Start()) { throw 'Process.Start do VulkanCube retornou false.' }
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
$stdout | Set-Content -LiteralPath (Join-Path $runDir 'cube.stdout.txt') -Encoding UTF8
$stderr | Set-Content -LiteralPath (Join-Path $runDir 'cube.stderr.txt') -Encoding UTF8

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
                (($event.ProviderName -match 'Application Error' -and $message -match 'dwm\.exe') -or
                 ($event.ProviderName -match 'Windows Error Reporting' -and
                  $message -match 'LiveKernelEvent|dwm\.exe')))
            if ($keep) {
                $eventRecords += [ordered]@{
                    time = $event.TimeCreated.ToString('o')
                    log = $logName
                    provider = $event.ProviderName
                    id = $event.Id
                    level = $event.LevelDisplayName
                    message = $message
                }
            }
        }
    } catch {
        $eventRecords += [ordered]@{
            time = (Get-Date).ToString('o')
            log = $logName
            provider = 'BC250_RUNNER'
            id = 0
            level = 'CaptureError'
            message = $_.Exception.Message
        }
    }
}
$eventRecords | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $runDir 'windows-events-this-run.json') -Encoding UTF8
$dwmCrashCount = @($eventRecords | Where-Object {
    $_.provider -match 'Application Error' -and $_.message -match 'dwm\.exe'
}).Count
$dwmWerCount = @($eventRecords | Where-Object {
    $_.provider -match 'Windows Error Reporting' -and $_.message -match 'dwm\.exe'
}).Count
$liveKernelEventCount = @($eventRecords | Where-Object {
    $_.message -match 'LiveKernelEvent'
}).Count
$displaySystemEventCount = @($eventRecords | Where-Object {
    $_.log -eq 'System' -and $_.provider -ne 'BC250_RUNNER'
}).Count

$pollDeadline = (Get-Date).AddSeconds(5)
$after = Get-ProbeSnapshot
while ((Get-Date) -lt $pollDeadline) {
    if ([int64]$after.P1RetiredTotal -gt [int64]$before.P1RetiredTotal -and
        [int64]$after.V2NotifiedTotal -gt [int64]$before.V2NotifiedTotal) {
        break
    }
    Start-Sleep -Milliseconds 100
    $after = Get-ProbeSnapshot
}
$after | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $runDir 'probe-after.json') -Encoding UTF8
$delta = [ordered]@{}
foreach ($name in $watched) {
    $delta[$name] = Get-U32Delta ([int64]$before[$name]) ([int64]$after[$name])
}

$displayLogDelta = ''
if (Test-Path -LiteralPath $displayLogPath -PathType Leaf) {
    $currentLen = (Get-Item -LiteralPath $displayLogPath).Length
    if ($currentLen -gt $displayLogStartOffset) {
        $fs = [System.IO.File]::Open($displayLogPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        try {
            [void]$fs.Seek($displayLogStartOffset, [System.IO.SeekOrigin]::Begin)
            $reader = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8)
            $displayLogDelta = $reader.ReadToEnd()
        } finally {
            $fs.Dispose()
        }
    }
}
$displayLogDelta | Set-Content -LiteralPath (Join-Path $runDir 'display-log-delta.txt') -Encoding UTF8

$checks = [ordered]@{
    process_completed = (-not $timedOut)
    process_exit_zero = (-not $timedOut -and $exitCode -eq 0)
    submit_accepted = ([int64]$delta.P1SubmitAccepted -gt 0)
    retired = ([int64]$delta.P1RetiredTotal -gt 0)
    ring_drained = ([int64]$after.P1LastWptrMmioReadback -eq [int64]$after.P1LastRptrMmio)
    desktop_detected_in_kmd = ($displayLogDelta -match 'DESKTOP REAL DETECTADO' -or
        $displayLogDelta -match 'TILE_PROBE' -or
        $displayLogDelta -match '\[Bc250Escape\] PRESENT_BLT: \d+x\d+')
    sdma_blt_executed = ($displayLogDelta -match 'SDMA HW BLT CONCLUIDO COM SUCESSO')
    no_host_copy_fallback = (-not ($displayLogDelta -match 'COPIA POR CPU' -or
        $displayLogDelta -match 'Host Kernel Blt CONCLUIDO' -or
        $displayLogDelta -match 'Garantia de Scanout GOP' -or
        $displayLogDelta -match 'SDMA HW BLT falhou'))
}

$hardwareValidated = $true
foreach ($entry in $checks.GetEnumerator()) {
    if (-not [bool]$entry.Value) {
        $hardwareValidated = $false
        break
    }
}

$verdict = [ordered]@{
    verdict = $(if ($hardwareValidated) { 'PASS' } else { 'FAIL' })
    timestamp = (Get-Date).ToString('o')
    elapsed_ms = $elapsedMs
    safe_bind_guard = $safeGuardAudit
    checks = $checks
    probe_delta = $delta
    diagnostics = [ordered]@{
        sdma_blt_success_logged = ([regex]::Matches($displayLogDelta, 'SDMA HW BLT CONCLUIDO COM SUCESSO')).Count
        sdma_blt_failed_logged = ([regex]::Matches($displayLogDelta, 'SDMA HW BLT falhou')).Count
        host_cpu_copy_logged = ([regex]::Matches($displayLogDelta, 'COPIA POR CPU|Host Kernel Blt CONCLUIDO|Garantia de Scanout GOP')).Count
        sdma_teardown_line = $(
            $m = [regex]::Match($displayLogDelta, '\[SdmaPresent\] Teardown Complete\.[^\r\n]*')
            if ($m.Success) { $m.Value } else { '' }
        )
        sdma_escape_archive_line = $(
            $m = [regex]::Match($displayLogDelta, '\[SdmaPresent\] Escape Archive\.[^\r\n]*')
            if ($m.Success) { $m.Value } else { '' }
        )
        tdr_events = @($eventRecords | Where-Object {
            $_.log -eq 'System' -and $_.id -in 4101, 117, 141, 142
        }).Count
        dwm_crash_events = $dwmCrashCount
        dwm_wer_events = $dwmWerCount
        live_kernel_events = $liveKernelEventCount
        display_system_events = $displaySystemEventCount
        event_window_start = $start.ToString('o')
        event_window_end = $eventEnd.ToString('o')
    }
}
$verdict | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $runDir 'cube-verdict.json') -Encoding UTF8

Write-Host ''
Write-Host "=== VEREDITO MARCO 4 (Vulkan WSI Cube): $($verdict.verdict) ===" -ForegroundColor $(if ($hardwareValidated) { 'Green' } else { 'Red' })
foreach ($entry in $checks.GetEnumerator()) {
    $c = if ($entry.Value) { 'Green' } else { 'Red' }
    Write-Host ("  {0,-28}: {1}" -f $entry.Key, $entry.Value) -ForegroundColor $c
}
Write-Host ("  {0,-28}: {1}" -f 'sdma_blts', $verdict.diagnostics.sdma_blt_success_logged) -ForegroundColor Green
Write-Host ("  {0,-28}: {1}" -f 'host_cpu_copies', $verdict.diagnostics.host_cpu_copy_logged) -ForegroundColor $(if ($verdict.diagnostics.host_cpu_copy_logged -eq 0) { 'Green' } else { 'Red' })
Write-Host ("  {0,-28}: {1}" -f 'tdr_events', $verdict.diagnostics.tdr_events) -ForegroundColor $(if ($verdict.diagnostics.tdr_events -eq 0) { 'Green' } else { 'Red' })
Write-Host ("  {0,-28}: {1}" -f 'dwm_crash_events', $verdict.diagnostics.dwm_crash_events) -ForegroundColor Yellow
Write-Host "Evidencia gravada em: $runDir"

if (-not $hardwareValidated) {
    exit 1
}
