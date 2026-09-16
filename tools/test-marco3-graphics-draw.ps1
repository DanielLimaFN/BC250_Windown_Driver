<#
.SYNOPSIS
    Gate de Rasterizacao Grafica Vulkan (MARCO3_GRAPHICS_DRAW).

.DESCRIPTION
    Executa o pipeline grafico canonico (vkCreateGraphicsPipelines, VkRenderPass,
    shaders SPIR-V de vertex/fragment e rasterizacao via vkCmdDraw) no silicio
    fisico da AMD BC-250 (1002:13FE).
    
    Verifica byte-a-byte na VRAM host-visible as 8 zonas de amostragem
    (SMPTE Color Bars + BC-250 Gold Badge), realiza a apresentacao WDDM
    via SDMA Blt e sustenta 600 quadros a 60 FPS com zero fallback e zero
    copia por CPU.
#>
[CmdletBinding()]
param(
    [string]$EvidenceDir,
    [string]$IcdManifest,
    [string]$Harness,
    [string]$VertShader,
    [string]$FragShader,
    [string]$MesaRoot = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm',
    [ValidateRange(10,300)][int]$TimeoutSeconds = 120,
    [ValidateRange(0,30)][int]$HoldSeconds = 5,
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
    $Harness = Join-Path $root 'driver\tests\output_radv_gate\bc250-radv-graphics-gate.exe'
}
if ([string]::IsNullOrWhiteSpace($VertShader)) {
    $VertShader = Join-Path $root 'driver\tests\output_radv_gate\bc250_radv_graphics_gate_vert.spv'
}
if ([string]::IsNullOrWhiteSpace($FragShader)) {
    $FragShader = Join-Path $root 'driver\tests\output_radv_gate\bc250_radv_graphics_gate_frag.spv'
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

Write-Host '=== TESTE MARCO 3 GRAPHICS DRAW (vkCmdDraw + Graphics Pipeline) ===' -ForegroundColor Cyan
Write-Host "  Harness     : $Harness"
Write-Host "  Vert Shader : $VertShader"
Write-Host "  Frag Shader : $FragShader"
Write-Host "  ICD         : $IcdManifest"
Write-Host "  Mesa        : $MesaRoot"

if (-not (Test-Path -LiteralPath $Harness -PathType Leaf)) {
    throw "Harness ausente: $Harness. Execute tools\build-marco3-graphics-gate.bat primeiro."
}
if (-not (Test-Path -LiteralPath $VertShader -PathType Leaf)) {
    throw "Vert shader ausente: $VertShader. Execute tools\build-marco3-graphics-gate.bat primeiro."
}
if (-not (Test-Path -LiteralPath $FragShader -PathType Leaf)) {
    throw "Frag shader ausente: $FragShader. Execute tools\build-marco3-graphics-gate.bat primeiro."
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
$runDir = Join-Path $EvidenceDir "marco3-graphics-draw-$runStamp"
New-Item -ItemType Directory -Path $runDir | Out-Null
$kmdAuditOutput | Set-Content -LiteralPath (Join-Path $runDir 'kmd-safety-audit.txt') -Encoding UTF8
$radvAuditOutput | Set-Content -LiteralPath (Join-Path $runDir 'radv-safety-audit.txt') -Encoding UTF8
$before | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $runDir 'probe-before.json') -Encoding UTF8

$start = Get-Date
$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = [System.IO.Path]::GetFullPath($Harness)
$startInfo.Arguments = ('"' + [System.IO.Path]::GetFullPath($VertShader) + '" "' +
    [System.IO.Path]::GetFullPath($FragShader) + '" "' +
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
    loaded_icd_identity = ($stdout -split "`r?`n" | Where-Object {
        [string]::Equals($_.Trim(), "icd_module=$expectedIcdPath",
            [System.StringComparison]::OrdinalIgnoreCase)
    }).Count -eq 1
    process_exit_zero = (-not $timedOut -and $exitCode -eq 0)
    graphics_queue_selected = ($stdout -match 'SELECTED_QUEUE: .*HAS_GRAPHICS_BIT=TRUE')
    graphics_pipeline_created = ($stdout -match 'GRAPHICS_PIPELINE: Created successfully with vkCreateGraphicsPipelines')
    render_byte_proof = ($stdout -match '(?m)^BC250 RADV GRAPHICS DRAW BYTE PROOF: PASS')
    wddm_present_called = ($stdout -match '(?m)^BC250 RADV WDDM PRESENT: PASS')
    swapchain_completed = (-not $timedOut -and $exitCode -eq 0 -and
        ($Frames -le 1 -or $stdout -match 'SWAPCHAIN CONCLUIDO COM EXITO'))
    submit_accepted = ([int64]$delta.P1SubmitAccepted -gt 0)
    retired = ([int64]$delta.P1RetiredTotal -gt 0)
    ring_drained = ([int64]$after.P1LastWptrMmioReadback -eq [int64]$after.P1LastRptrMmio)
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
}

$verdict | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $runDir 'm3-graphics-verdict.json') -Encoding UTF8

Write-Host "`n=== RESULTADO DO GATE MARCO 3 GRAPHICS DRAW ===" -ForegroundColor $(if ($hardwareValidated) { 'Green' } else { 'Red' })
Write-Host "  Veredito : $($verdict.verdict)"
Write-Host "  Evidencia: $runDir"
foreach ($k in $checks.Keys) {
    $val = $checks[$k]
    $col = if ($val) { 'Green' } else { 'Red' }
    Write-Host "    $k : $val" -ForegroundColor $col
}
