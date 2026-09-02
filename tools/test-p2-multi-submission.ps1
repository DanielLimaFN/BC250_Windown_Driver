<#
.SYNOPSIS
    Valida, sem escrever em hardware, o tripe fisico da Fase P2 (Multi-Submissao
    Sequencial e Streams Continuos no anel persistente).

.DESCRIPTION
    Aceita a captura do Schema 127 declarado pelo pacote de laboratorio com
    PersistentSubmission=1 e ShaderDispatch=1. Exige, no mesmo ciclo:
      - Motor online e P1 validado;
      - P2StreamAttempted = 1;
      - P2BatchCount = 16 lotes de dispatch;
      - P2SubmissionsAccepted = 16;
      - P2SubmissionsRetired = 16 lotes aposentados pela GPU;
      - P2ResultMismatches = 0 (16 slots de dados e fences conferidos byte a byte);
      - RPTR (writeback em memoria e MMIO) acompanhando WPTR;
      - P2TriadPassed = 1 e P2PhysicalReady = 1.

    Nada aqui escreve no dispositivo: e leitura de telemetria mais conferencia.
#>
[CmdletBinding()]
param(
    [string]$EvidenceDir
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$probePath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
$manifestPath = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$errors = [System.Collections.Generic.List[string]]::new()

function To-U32($Value) {
    return [uint64]([int64]$Value -band 0xFFFFFFFFL)
}

if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Manifesto de laboratorio ausente: $manifestPath"
}
if (-not (Test-Path -LiteralPath $probePath)) {
    throw "Telemetria ausente: $probePath"
}

$probe = Get-ItemProperty -LiteralPath $probePath
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

$device = Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.DeviceID -like 'PCI\VEN_1002&DEV_13FE*'
}
if ($null -eq $device -or $device.Service -ne 'bc250mcdm' -or
    $device.ConfigManagerErrorCode -ne 0) {
    $errors.Add('dispositivo nao esta em bc250mcdm com Codigo 0')
}
if (-not (Test-Path -LiteralPath $driverPath)) {
    $errors.Add('driver instalado ausente')
} elseif ((Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash -ne
        $manifest.PackageSysSha256) {
    $errors.Add('hash do driver instalado diverge do pacote de laboratorio')
}
if (-not $manifest.PersistentSubmissionEnabled) {
    $errors.Add('pacote de laboratorio nao tem PersistentSubmission habilitado')
}

# Campos exatos da Fase P2. Qualquer divergencia reprova o ciclo.
$expected = [ordered]@{
    SchemaVersion = [uint32]$manifest.TelemetrySchemaVersion
    SkipHwBringup = 0
    P1EngineState = 1          # BC250_SUBMISSION_ENGINE_ONLINE
    P1ActivateStatus = 0       # BC250_HQD_ACTIVE_OK
    P2StreamAttempted = 1
    P2BatchCount = 16
    P2SubmissionsAccepted = 16
    P2SubmissionsRetired = 16
    P2ResultMismatches = 0
    P2TriadPassed = 1
    P2PhysicalReady = 1
    P2DoorbellWriteFailures = 0
}

foreach ($entry in $expected.GetEnumerator()) {
    $property = $probe.PSObject.Properties[$entry.Key]
    if ($null -eq $property) {
        $errors.Add("campo ausente na telemetria: $($entry.Key)")
        continue
    }
    $actual = To-U32 $property.Value
    $wanted = [uint64]$entry.Value
    if ($actual -ne $wanted) {
        $errors.Add("$($entry.Key): observado=0x$($actual.ToString('X8')), esperado=0x$($wanted.ToString('X8'))")
    }
}

# Consistencia dos cursores
$finalWptr = To-U32 $probe.P2FinalWptr
$finalRptrMem = To-U32 $probe.P2FinalRptrReport
$finalRptrMmio = To-U32 $probe.P2FinalRptrMmio
$doorbellValueLow = To-U32 $probe.P2DoorbellLastValueLow
$doorbellValueHigh = To-U32 $probe.P2DoorbellLastValueHigh
$doorbellControlBefore = To-U32 $probe.P2DoorbellControlBefore
$doorbellControlAfter = To-U32 $probe.P2DoorbellControlAfter

if ($finalRptrMem -ne $finalWptr) {
    $errors.Add("P2FinalRptrReport ($finalRptrMem) diverge de P2FinalWptr ($finalWptr)")
}
if ($finalRptrMmio -ne $finalWptr) {
    $errors.Add("P2FinalRptrMmio ($finalRptrMmio) diverge de P2FinalWptr ($finalWptr)")
}
if ($doorbellValueLow -ne $finalWptr -or $doorbellValueHigh -ne 0) {
    $errors.Add("doorbell 64-bit diverge do WPTR final: hi=0x$($doorbellValueHigh.ToString('X8')) lo=0x$($doorbellValueLow.ToString('X8')) wptr=0x$($finalWptr.ToString('X8'))")
}
foreach ($doorbellControl in @($doorbellControlBefore, $doorbellControlAfter)) {
    if (($doorbellControl -band 0x40000000L) -eq 0 -or
        ($doorbellControl -band 0x0FFFFFFCL) -ne 0x00000110L) {
        $errors.Add("DOORBELL_CONTROL nao preservou ENABLE+offset 0x110: 0x$($doorbellControl.ToString('X8'))")
    }
}

$evidence = [ordered]@{
    captured_at = [DateTime]::Now.ToString('o')
    package_sys_sha256 = $manifest.PackageSysSha256
    schema_version = To-U32 $probe.SchemaVersion
    engine_state = To-U32 $probe.P1EngineState
    p2_batch_count = To-U32 $probe.P2BatchCount
    p2_submissions_accepted = To-U32 $probe.P2SubmissionsAccepted
    p2_submissions_retired = To-U32 $probe.P2SubmissionsRetired
    p2_wrap_count = To-U32 $probe.P2WrapCount
    p2_result_mismatches = To-U32 $probe.P2ResultMismatches
    p2_last_fence_observed = To-U32 $probe.P2LastFenceObserved
    p2_final_wptr = $finalWptr
    p2_final_rptr_report = $finalRptrMem
    p2_final_rptr_mmio = $finalRptrMmio
    p2_triad_passed = To-U32 $probe.P2TriadPassed
    p2_physical_ready = To-U32 $probe.P2PhysicalReady
    p2_doorbell_write_failures = To-U32 $probe.P2DoorbellWriteFailures
    p2_doorbell_last_value_low = $doorbellValueLow
    p2_doorbell_last_value_high = $doorbellValueHigh
    p2_doorbell_control_before = $doorbellControlBefore
    p2_doorbell_control_after = $doorbellControlAfter
    p2_doorbell_hit_observed = To-U32 $probe.P2DoorbellHitObserved
    errors = @($errors)
    verdict = $(if ($errors.Count -eq 0) {
        'P2 MULTI-SUBMISSAO VALIDADA EM HARDWARE'
    } else {
        'NAO VALIDADO EM HARDWARE'
    })
}

if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $EvidenceDir = Join-Path $root "docs\evidence\$([DateTime]::Now.ToString('yyyy-MM-dd_HH-mm-ss'))-$env:COMPUTERNAME-p2-multi-submission"
}
New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
$reportPath = Join-Path $EvidenceDir 'p2-multi-submission-acceptance.json'
$evidence | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding utf8

Write-Host ""
if ($errors.Count -eq 0) {
    Write-Host "=========================================================================" -ForegroundColor Green
    Write-Host "P2: MULTI-SUBMISSAO SEQUENCIAL E STREAMS VALIDADOS EM HARDWARE" -ForegroundColor Green
    Write-Host "16/16 submissoes aposentadas, fences e dados conferidos byte a byte." -ForegroundColor Green
    Write-Host "=========================================================================" -ForegroundColor Green
} else {
    Write-Host "P2: REPROVADO" -ForegroundColor Red
    foreach ($err in $errors) {
        Write-Host "  - $err" -ForegroundColor Red
    }
    Write-Host "NAO VALIDADO EM HARDWARE" -ForegroundColor Yellow
}
Write-Host "Relatorio: $reportPath"

if ($errors.Count -ne 0) {
    exit 1
}
