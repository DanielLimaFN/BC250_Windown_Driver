<#
.SYNOPSIS
    Gate fisico de apresentacao RADV -> WDDM -> BC-250 (Marco 2: Vulkan Present no Scanout GOP).

.DESCRIPTION
    Executa um pipeline completo de renderizacao Vulkan (2560x1080 BGRA8) via Mesa RADV + ACO
    no silicio fisico da AMD BC-250 (1002:13FE) e aciona a apresentacao WDDM (D3DKMTPresent)
    via SDMA Blt diretamente para o scanout fisico do UEFI GOP (0xC0000000 em VRAM).

    O veredito exige simultaneamente:
      - Execucao limpa do harness bc250-radv-present-gate.exe (Exit 0);
      - Verificacao byte-a-byte na VRAM das 8 zonas de amostragem (SMPTE Color Bars + BC250 Badge);
      - Disparo e conclusao do D3DKMTPresent Blt;
      - Submissao e aposentadoria observadas no Node 1 pelo motor P1;
      - Telemetria do KMD confirmando deteccao de pixels reais e copia SDMA para o scanout.
#>
[CmdletBinding()]
param(
    [string]$EvidenceDir,
    [string]$IcdManifest,
    [string]$Harness,
    [string]$Shader,
    [string]$MesaRoot = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm',
    [ValidateRange(10,300)][int]$TimeoutSeconds = 60,
    [ValidateRange(0,30)][int]$HoldSeconds = 2,
    [ValidateRange(1,3600)][int]$Frames = 600,
    [ValidateRange(1,120)][int]$TargetFps = 60,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$probePath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
$manifestPath = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
$kmdAudit = Join-Path $root 'tools\audit-bc250mcdm-safety.py'
$radvAudit = Join-Path $root 'tools\audit-radv-wddm-safety.py'
$safeGuardAuditScript = Join-Path $root 'tools\assert-bc250-safe-bind-guard.ps1'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$displayLogPath = 'C:\bc250_display_log.txt'

if ([string]::IsNullOrWhiteSpace($IcdManifest)) {
    $IcdManifest = Join-Path $root 'tools\bc250-radv-test-icd.json'
}
if ([string]::IsNullOrWhiteSpace($Harness)) {
    $Harness = Join-Path $root 'driver\tests\output_radv_gate\bc250-radv-present-gate.exe'
}
if ([string]::IsNullOrWhiteSpace($Shader)) {
    $Shader = Join-Path $root 'driver\tests\output_radv_gate\bc250_radv_present_gate.spv'
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

function Get-FileEvidence([string]$Path) {
    return [ordered]@{
        path = [System.IO.Path]::GetFullPath($Path)
        size = (Get-Item -LiteralPath $Path).Length
        sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    }
}

function Invoke-IcdIdentityProbe([string]$HarnessPath, [string]$IcdPath) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = [System.IO.Path]::GetFullPath($HarnessPath)
    $psi.Arguments = '--identity-only "' + [System.IO.Path]::GetFullPath($IcdPath) + '"'
    $psi.WorkingDirectory = [System.IO.Path]::GetDirectoryName(
        [System.IO.Path]::GetFullPath($HarnessPath))
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    try {
        if (-not $proc.Start()) { throw 'Process.Start do identity probe retornou false.' }
        $stdout = $proc.StandardOutput.ReadToEnd()
        $stderr = $proc.StandardError.ReadToEnd()
        $proc.WaitForExit()
        return [pscustomobject]@{
            exit_code = [int]$proc.ExitCode
            stdout = [string]$stdout
            stderr = [string]$stderr
        }
    } finally {
        $proc.Dispose()
    }
}

Write-Host '=== RADV_WDDM_PRESENT_TRIPOD (Marco 2: Apresentacao Scanout GOP) ===' -ForegroundColor Cyan
Write-Host "  Harness : $Harness"
Write-Host "  Shader  : $Shader"
Write-Host "  ICD     : $IcdManifest"
Write-Host "  Mesa    : $MesaRoot"

if (-not (Test-Path -LiteralPath $Harness -PathType Leaf)) {
    throw "Harness ausente: $Harness. Execute tools\build-radv-present-gate.bat primeiro."
}
if (-not (Test-Path -LiteralPath $Shader -PathType Leaf)) {
    throw "Shader ausente: $Shader. Execute tools\build-radv-present-gate.bat primeiro."
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
$identity = Invoke-IcdIdentityProbe $Harness $radvDll
if ($identity.exit_code -ne 0) {
    throw "Probe de identidade falhou (exit=$($identity.exit_code)):`r`n$($identity.stderr)"
}
$expectedIcdPath = [System.IO.Path]::GetFullPath($radvDll)
$identityPassed = ($identity.stdout -split "`r?`n" | Where-Object {
    [string]::Equals($_.Trim(), "icd_module=$expectedIcdPath",
        [System.StringComparison]::OrdinalIgnoreCase)
}).Count -eq 1
if (-not $identityPassed) {
    throw "Identidade do ICD nao confere:`r`n$($identity.stdout)"
}
Write-Host "  ICD verificado e entrada de apresentacao identificada." -ForegroundColor Green

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
$runDir = Join-Path $EvidenceDir "radv-present-tripod-$runStamp"
New-Item -ItemType Directory -Path $runDir | Out-Null
$kmdAuditOutput | Set-Content -LiteralPath (Join-Path $runDir 'kmd-safety-audit.txt') -Encoding UTF8
$radvAuditOutput | Set-Content -LiteralPath (Join-Path $runDir 'radv-safety-audit.txt') -Encoding UTF8
$before | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $runDir 'probe-before.json') -Encoding UTF8

$start = Get-Date
$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = [System.IO.Path]::GetFullPath($Harness)
$startInfo.Arguments = ('"' + [System.IO.Path]::GetFullPath($Shader) + '" "' +
    [System.IO.Path]::GetFullPath($radvDll) + '" --frames ' + $Frames +
    ' --target-fps ' + $TargetFps + ' --hold-seconds ' + $HoldSeconds)
$startInfo.WorkingDirectory = [System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($Harness))
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$stageJournalPath = Join-Path $runDir 'harness-stage-journal.txt'
$startInfo.EnvironmentVariables['BC250_GATE_STAGE_FILE'] = $stageJournalPath
$process = New-Object System.Diagnostics.Process
$process.StartInfo = $startInfo

$timedOut = $false
$exitCode = $null
$stdout = ''
$stderr = ''
try {
    if (-not $process.Start()) { throw 'Process.Start retornou false.' }
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
$stdout | Set-Content -LiteralPath (Join-Path $runDir 'harness.stdout.txt') -Encoding UTF8
$stderr | Set-Content -LiteralPath (Join-Path $runDir 'harness.stderr.txt') -Encoding UTF8

# Arquiva a janela de eventos do proprio ensaio. Em 12/09 10:42 o harness
# recebeu STATUS_DEVICE_REMOVED enquanto MEC e SDMA terminaram drenados, e a
# evidencia temporal externa preservada no host foi uma tempestade de crashes
# do dwm.exe. Sem este recorte, DEVICE_REMOVED pode ser atribuido erradamente
# ao primeiro snapshot MEC_PENDING encontrado no log acumulado.
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

# Capture display log delta
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
    loaded_icd_identity = ($stdout -split "`r?`n" | Where-Object {
        [string]::Equals($_.Trim(), "icd_module=$expectedIcdPath",
            [System.StringComparison]::OrdinalIgnoreCase)
    }).Count -eq 1
    process_exit_zero = (-not $timedOut -and $exitCode -eq 0)
    # Sao provas monotônicas: se aconteceram antes de uma falha posterior da
    # swapchain, continuam verdadeiras. Condiciona-las ao exit code apagava
    # exatamente o progresso que o veredito deveria preservar.
    render_byte_proof = ($stdout -match '(?m)^BC250 RADV PRESENT BYTE PROOF: PASS')
    wddm_present_called = ($stdout -match '(?m)^BC250 RADV WDDM PRESENT: PASS')
    swapchain_completed = (-not $timedOut -and $exitCode -eq 0 -and
        ($Frames -le 1 -or $stdout -match 'SWAPCHAIN CONCLUIDO COM SUCESSO'))
    submit_accepted = ([int64]$delta.P1SubmitAccepted -gt 0)
    retired = ([int64]$delta.P1RetiredTotal -gt 0)
    ring_drained = ([int64]$after.P1LastWptrMmioReadback -eq [int64]$after.P1LastRptrMmio)
    # C3 (12/09): este check casava com a string 'Bc250Escape', isto e', era
    # verdadeiro so' porque o escape foi CHAMADO. Agora exige que o escape tenha
    # entrado no caminho de present com geometria decodificada.
    desktop_detected_in_kmd = ($displayLogDelta -match 'DESKTOP REAL DETECTADO' -or
        $displayLogDelta -match 'TILE_PROBE' -or
        $displayLogDelta -match '\[Bc250Escape\] PRESENT_BLT: \d+x\d+')
    # C3 (12/09) -- DEFEITO CORRIGIDO. Este check casava com 'PRESENT_BLT' e ate'
    # com 'BYTE_PROOF INCONCLUSIVO'. Resultado: a corrida 11/09 22:46 saiu
    # "10/10 PASS" com ZERO linhas de SDMA e o present inteiro feito por
    # RtlCopyMemory da CPU. Agora so' a string que o ramo de GPU emite vale.
    # Mesma classe de defeito de A15, mcdm_callbacks_test e C1: o gate carimbava
    # o falso sucesso.
    sdma_blt_executed = ($displayLogDelta -match 'SDMA HW BLT CONCLUIDO COM SUCESSO')
    # C3 (12/09): pixel produzido por CPU reprova a corrida. Cobre as duas
    # assinaturas -- a copia efetivamente feita pelo escape e a recusa do ramo de
    # GPU que leva ate ela.
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
        # C3: contagens brutas do log do KMD. O log do escape e' amostrado (1a linha
        # e a cada 60), entao estes numeros sao AMOSTRAS, nao totais. O total
        # autoritativo sai da linha de teardown, capturada em sdma_teardown_line.
        sdma_blt_success_logged = ([regex]::Matches($displayLogDelta, 'SDMA HW BLT CONCLUIDO COM SUCESSO')).Count
        sdma_blt_failed_logged = ([regex]::Matches($displayLogDelta, 'SDMA HW BLT falhou')).Count
        host_cpu_copy_logged = ([regex]::Matches($displayLogDelta, 'COPIA POR CPU|Host Kernel Blt CONCLUIDO|Garantia de Scanout GOP')).Count
        sdma_teardown_line = $(
            $m = [regex]::Match($displayLogDelta, '\[SdmaPresent\] Teardown Complete\.[^\r\n]*')
            if ($m.Success) { $m.Value } else { '' }
        )
        # C3: contagem NAO amostrada de escapes vista pelo KMD, e custo do blt.
        sdma_escape_archive_line = $(
            $m = [regex]::Match($displayLogDelta, '\[SdmaPresent\] Escape Archive\.[^\r\n]*')
            if ($m.Success) { $m.Value } else { '' }
        )
        # C3: as cinco corridas de 12/09 romperam nos quadros 13, 41, 44, 64 e
        # 447, e isso so' era visivel lendo stderr a mao. Extrair para o veredito
        # torna as corridas comparaveis sem mineracao manual.
        first_failed_frame = $(
            $m = [regex]::Match($stderr, 'failed on frame (\d+)')
            if ($m.Success) { [int]$m.Groups[1].Value } else { -1 }
        )
        failure_mode = $(
            if ($stderr -match 'vkWaitForFences failed on frame') { 'VK_WAIT_FENCES' }
            elseif ($stderr -match 'radv_bc250_present_scanout failed on frame') { 'PRESENT_ESCAPE' }
            elseif ($timedOut) { 'HARNESS_TIMEOUT' }
            elseif ($exitCode -ne 0) { 'EXIT_NONZERO' }
            else { 'NONE' }
        )
        # C3: discriminante que faltava. Sem evento de TDR (4101/117/141) e com
        # Sub==Ret==Notified, um STATUS_DEVICE_REMOVED nao e' atribuivel a hang
        # da nossa engine -- e o driver nao emite esse codigo em linha nenhuma.
        tdr_events = @($eventRecords | Where-Object {
            $_.log -eq 'System' -and $_.id -in 4101, 117, 141, 142
        }).Count
        status_device_removed = ($stderr -match '0xC00002B6|STATUS_DEVICE_REMOVED')
        dwm_crash_events = $dwmCrashCount
        dwm_wer_events = $dwmWerCount
        live_kernel_events = $liveKernelEventCount
        display_system_events = $displaySystemEventCount
        event_window_start = $start.ToString('o')
        event_window_end = $eventEnd.ToString('o')
    }
}
$verdict | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $runDir 'present-verdict.json') -Encoding UTF8

Write-Host ''
Write-Host "=== VEREDITO MARCO 2: $($verdict.verdict) ===" -ForegroundColor $(if ($hardwareValidated) { 'Green' } else { 'Red' })
foreach ($entry in $checks.GetEnumerator()) {
    $c = if ($entry.Value) { 'Green' } else { 'Red' }
    Write-Host ("  {0,-28}: {1}" -f $entry.Key, $entry.Value) -ForegroundColor $c
}
Write-Host ("  {0,-28}: {1}" -f 'status_device_removed', $verdict.diagnostics.status_device_removed) -ForegroundColor Yellow
Write-Host ("  {0,-28}: {1}" -f 'dwm_crash_events', $verdict.diagnostics.dwm_crash_events) -ForegroundColor Yellow
Write-Host ("  {0,-28}: {1}" -f 'dwm_wer_events', $verdict.diagnostics.dwm_wer_events) -ForegroundColor Yellow
Write-Host ("  {0,-28}: {1}" -f 'live_kernel_events', $verdict.diagnostics.live_kernel_events) -ForegroundColor Yellow
Write-Host "Evidencia gravada em: $runDir"
