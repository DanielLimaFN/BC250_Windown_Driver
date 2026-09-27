<#
.SYNOPSIS
    Runner transacional do primeiro submit Node 1 originado pela UMD.

.DESCRIPTION
    Dry-run e o default. -Execute exige bind Codigo 0 com watchdog ARMED,
    identidade exata da UMD instalada e aceite explicito. A autorizacao e
    desarmada em finally. O gate D3D de producao permanece zero.
#>
[CmdletBinding()]
param(
    [string]$EvidenceDir,
    [string]$Harness,
    [string]$ExpectedUmd,
    [string]$PackageManifest,
    [ValidateRange(5,120)][int]$TimeoutSeconds = 30,
    [switch]$Execute,
    [switch]$IAcceptOnePhysicalNode1Submission
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Harness)) {
    $Harness = Join-Path $root 'driver\umd\output\bc250-umd-node1-tripod.exe'
}
if ([string]::IsNullOrWhiteSpace($ExpectedUmd)) {
    $ExpectedUmd = Join-Path $root 'driver\wdk\package\output_lab\bc250umd.dll'
}
if ([string]::IsNullOrWhiteSpace($PackageManifest)) {
    $PackageManifest = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
}
$installedUmd = Join-Path $env:SystemRoot 'System32\bc250umd.dll'
$audit = Join-Path $root 'tools\audit-umd-node1-submission.py'
$armTool = Join-Path $root 'tools\arm-umd-node1-tripod.ps1'
$probePath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
$watched = @('P1SubmitAccepted','P1SubmitRejectedNode','P1SubmitRejectedPlan',
    'P1SubmitRejectedEmit','P1RetiredTotal','P1LastWptrMmioReadback','P1LastRptrMmio')

foreach ($path in @($Harness,$ExpectedUmd,$PackageManifest,$audit,$armTool)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Preflight recusado: artefato ausente: $path"
    }
}
$manifest = Get-Content -LiteralPath $PackageManifest -Raw | ConvertFrom-Json
if ([string]$manifest.BuildKind -ne 'MCDM_LAB_BRINGUP' -or
    -not [bool]$manifest.RenderNode -or
    -not [bool]$manifest.PersistentSubmissionEnabled) {
    throw 'Preflight recusado: manifesto nao descreve o candidato Node 1 persistente.'
}
if ([bool]$manifest.Node1TripodDefaultArmed -or [bool]$manifest.D3D10_11NativeReady) {
    throw 'Preflight recusado: manifesto abriu um gate que deve permanecer fechado.'
}
$expectedHash = (Get-FileHash -LiteralPath $ExpectedUmd -Algorithm SHA256).Hash
if ([string]::IsNullOrWhiteSpace([string]$manifest.PackageUmdSha256) -or
    $expectedHash -ne [string]$manifest.PackageUmdSha256) {
    throw 'Preflight recusado: UMD esperada diverge do PackageUmdSha256 auditado.'
}
$auditOutput = (& py -3 $audit 2>&1) -join "`r`n"
if ($LASTEXITCODE -ne 0) { throw "Auditoria UMD falhou:`r`n$auditOutput" }
$source = Get-Content -LiteralPath (Join-Path $root 'driver\umd\bc250_d3d_umd.h') -Raw
if ($source -notmatch '#define\s+BC250_D3D10_11_NATIVE_READY\s+0\b') {
    throw 'Preflight recusado: gate D3D de producao nao esta em zero.'
}

Write-Host '=== B7_UMD_NODE1_TRIPOD ===' -ForegroundColor Magenta
Write-Host "Harness: $Harness"
Write-Host "UMD esperada: $ExpectedUmd"
Write-Host "Manifesto: $PackageManifest"
Write-Host 'Autorizacao: registro + executavel exato + token 128-bit; single-shot'
Write-Host 'Teto: uma execucao fisica por build; abandono no primeiro preflight invalido'
if (-not $Execute) {
    Write-Host 'DRY-RUN PASS: nenhum registro alterado e GPU nao tocada.' -ForegroundColor Green
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 0
}

if (-not $IAcceptOnePhysicalNode1Submission) {
    throw '-Execute exige -IAcceptOnePhysicalNode1Submission.'
}
if ([string]::IsNullOrWhiteSpace($EvidenceDir) -or
    -not (Test-Path -LiteralPath $EvidenceDir -PathType Container)) {
    throw '-Execute exige EvidenceDir existente do bind protegido.'
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '-Execute exige PowerShell elevado.'
}
$safeGuardAuditScript = Join-Path $root 'tools\assert-bc250-safe-bind-guard.ps1'
$safeGuardRaw = (& powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    $safeGuardAuditScript -EvidenceDir $EvidenceDir `
    -RequiredRemainingSeconds ($TimeoutSeconds + 10) 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Guardiao SYSTEM nao comprovado:`n$safeGuardRaw"
}
$watchdog = $safeGuardRaw | ConvertFrom-Json
$watchdogRemaining = [double]$watchdog.remaining_seconds
$device = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like 'PCI\VEN_1002&DEV_13FE*'
})
if ($device.Count -ne 1 -or $device[0].Service -ne 'bc250mcdm' -or
    $device[0].ConfigManagerErrorCode -ne 0) {
    throw 'BC-250 exata nao esta em bc250mcdm, Codigo 0.'
}
if ([string]$watchdog.instance_id -ne [string]$device[0].PNPDeviceID) {
    throw 'Watchdog pertence a outra instancia da BC-250.'
}
if (-not (Test-Path -LiteralPath $installedUmd -PathType Leaf)) {
    throw "UMD instalada ausente: $installedUmd"
}
$installedHash = (Get-FileHash -LiteralPath $installedUmd -Algorithm SHA256).Hash
if ($expectedHash -ne $installedHash) {
    throw 'UMD instalada diverge do build auditado; pacote novo e necessario.'
}
if (-not (Test-Path -LiteralPath $probePath)) { throw 'Telemetria KMD ausente.' }

function Get-Snapshot {
    $properties = Get-ItemProperty -LiteralPath $probePath
    $result = [ordered]@{}
    foreach ($name in $watched) {
        if ($null -eq $properties.PSObject.Properties[$name]) {
            throw "Telemetria obrigatoria ausente: $name"
        }
        $result[$name] = [int64]$properties.$name -band 0xFFFFFFFFL
    }
    return $result
}
function Delta([int64]$before,[int64]$after) {
    if ($after -ge $before) { return [uint64]($after-$before) }
    return [uint64](0x100000000L-$before+$after)
}

$before = Get-Snapshot
$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss-fff'
$runDir = Join-Path $EvidenceDir "B7_UMD_NODE1_TRIPOD_$stamp"
New-Item -ItemType Directory -Path $runDir | Out-Null
$auditOutput | Set-Content -LiteralPath (Join-Path $runDir 'umd-audit.txt') -Encoding UTF8
$before | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'probe-before.json') -Encoding UTF8
$token = [Guid]::NewGuid().ToString('N').ToUpperInvariant()
$stdoutPath = Join-Path $runDir 'harness.stdout.txt'
$stderrPath = Join-Path $runDir 'harness.stderr.txt'
$exitCode = -1
try {
    & $armTool -Arm -Token $token -IAcceptOnePhysicalNode1Submission | Out-Null
    $env:BC250_UMD_NODE1_TRIPOD_TOKEN = $token
    $harnessProcess = Start-Process -FilePath $Harness -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    if (-not $harnessProcess.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $harnessProcess.Id -Force -ErrorAction SilentlyContinue
        $exitCode = 1460
    } else {
        $exitCode = $harnessProcess.ExitCode
    }
} finally {
    Remove-Item Env:\BC250_UMD_NODE1_TRIPOD_TOKEN -ErrorAction SilentlyContinue
    & $armTool -Disarm | Out-Null
}
$after = Get-Snapshot
$after | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'probe-after.json') -Encoding UTF8
$stdout = if (Test-Path $stdoutPath) { Get-Content $stdoutPath -Raw } else { '' }
$umdLog = $null
if ($stdout -match '(?m)^umd_log=(.+)$') { $umdLog = $Matches[1].Trim() }
if ($umdLog -and (Test-Path -LiteralPath $umdLog -PathType Leaf)) {
    Copy-Item -LiteralPath $umdLog -Destination (Join-Path $runDir 'umd-log.txt')
}
$checks = [ordered]@{
    harness_callback_legs = ($exitCode -eq 0)
    submit_accepted = ((Delta $before.P1SubmitAccepted $after.P1SubmitAccepted) -gt 0)
    retired = ((Delta $before.P1RetiredTotal $after.P1RetiredTotal) -gt 0)
    ring_drained = ($after.P1LastWptrMmioReadback -eq $after.P1LastRptrMmio)
    no_node_reject = ((Delta $before.P1SubmitRejectedNode $after.P1SubmitRejectedNode) -eq 0)
    no_plan_reject = ((Delta $before.P1SubmitRejectedPlan $after.P1SubmitRejectedPlan) -eq 0)
    no_emit_reject = ((Delta $before.P1SubmitRejectedEmit $after.P1SubmitRejectedEmit) -eq 0)
    authorization_disarmed = ((Get-ItemProperty 'HKLM:\SOFTWARE\BC250\Umd' `
        -Name Node1TripodArmed -ErrorAction SilentlyContinue).Node1TripodArmed -eq 0)
}
$passed = -not ($checks.Values -contains $false)
$report = [ordered]@{
    schema_version = 1
    test = 'B7_UMD_NODE1_TRIPOD'
    executed_at = (Get-Date).ToString('o')
    harness_sha256 = (Get-FileHash $Harness -Algorithm SHA256).Hash
    umd_sha256 = $expectedHash
    package_manifest_sha256 = (Get-FileHash $PackageManifest -Algorithm SHA256).Hash
    harness_timeout_seconds = $TimeoutSeconds
    harness_exit_code = $exitCode
    before = $before
    after = $after
    checks = $checks
    hardware_validated = $passed
    verdict = if ($passed) { 'B7_UMD_NODE1_TRIPOD PASSOU NO SILICIO' } else { 'NAO VALIDADO EM HARDWARE' }
}
$reportPath = Join-Path $runDir 'b7-umd-node1-tripod.json'
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Host "Evidencia: $reportPath"
Write-Host "Veredito: $($report.verdict)"
if (-not $passed) { exit 1 }
