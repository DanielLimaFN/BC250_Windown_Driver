<#
.SYNOPSIS
    Valida, sem escrever em hardware, o tripe fisico da Fase P1 (anel de
    submissao persistente).

.DESCRIPTION
    Aceita somente a captura do schema declarado pelo pacote de laboratorio com
    PersistentSubmission=1. Exige, no mesmo ciclo:
      - motor online e fila ativada sem erro;
      - Indirect Buffer relido byte a byte antes da submissao;
      - coerencia HDP/GL2/GCVM concluida antes do kick;
      - RPTR (writeback em memoria) igual ao WPTR do anel persistente;
      - fence do dispatch saindo do poison 0xA55A0FF0 para 0xBC250D15,
        gravada pela GPU;
      - fence de sequencia do anel gravada pela GPU e igual a fence atribuida;
      - resultado do shader saindo do poison 0x43DAFFFE para 0xBC250C01, com
        zero mismatches;
      - exatamente uma submissao aposentada;
      - nenhuma regressao de fence e nenhuma rejeicao de emissao.

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

# Campos exatos. Qualquer divergencia reprova o ciclo inteiro.
$expected = [ordered]@{
    SchemaVersion = [uint32]$manifest.TelemetrySchemaVersion
    SkipHwBringup = 0
    P1EngineState = 1          # BC250_SUBMISSION_ENGINE_ONLINE
    P1ActivateStatus = 0       # BC250_HQD_ACTIVE_OK
    P1SelfTestAttempted = 1
    P1SelfTestPlanStatus = 0
    P1SelfTestSubmitStatus = 0
    P1SelfTestShaderStaged = 1
    P1SelfTestIbReadback = 1
    # 3.45: sem baseline zerado a aposentadoria compara contra memoria velha.
    P1FenceBaselineWritten = 1
    # 3.46: CPU barrier nao substitui invalidacao dos caches da GPU.
    P1CoherHdpApplied = 1
    P1CoherencyPassed = 1
    HqdActiveRlcCpSchedulersRestorePassed = 1
    # 3.52: liberar somente MEC2; MEC1, PFP, ME e CE permanecem haltados.
    P1SnapActCpMecCntl = 0x40000000
    P1SnapActCpMeCntl = 0x15000000
    P1SnapSubCpMecCntl = 0x40000000
    P1SnapSubCpMeCntl = 0x15000000
    P1SelfTestDispatchFenceBefore = 0xA55A0FF0
    P1SelfTestDispatchFenceAfter = 0xBC250D15
    P1SelfTestResultBefore = 0x43DAFFFE
    P1SelfTestResultAfter = 0xBC250C01
    P1SelfTestResultMismatches = 0
    P1RingDwordsMismatch = 0
    P1SelfTestAssignedFence = 1
    P1SelfTestRingFenceObserved = 1
    P1SelfTestRetiredCount = 1
    P1SelfTestTriadPassed = 1
    P1SelfTestPhysicalReady = 1
    P1SubmitRejectedNode = 0
    P1SubmitRejectedPlan = 0
    P1SubmitRejectedEmit = 0
    P1RingFullCount = 0
    P1FenceRegressionCount = 0
}

# P1SubmitAccepted/P1RetiredTotal sao contadores acumulados do motor. No
# pacote combinado P1+P2, o mesmo StartDevice executa primeiro a submissao P1
# e depois os 16 lotes P2; exigir literal 1 reprova falsamente uma captura P2
# valida. As provas especificas de P1 acima continuam exigindo exatamente uma
# fence atribuida e exatamente uma aposentadoria do auto-teste.
$p2Accepted = if ($null -ne $probe.PSObject.Properties['P2SubmissionsAccepted']) {
    To-U32 $probe.P2SubmissionsAccepted
} else { 0 }
$p2Retired = if ($null -ne $probe.PSObject.Properties['P2SubmissionsRetired']) {
    To-U32 $probe.P2SubmissionsRetired
} else { 0 }
$expected['P1SubmitAccepted'] = [uint64]1 + $p2Accepted
$expected['P1RetiredTotal'] = [uint64]1 + $p2Retired

if ((To-U32 $probe.P1RearmAttempted) -ne 0) {
    $expected['P1SnapReCpMecCntl'] = 0x40000000
    $expected['P1SnapReCpMeCntl'] = 0x15000000
}

foreach ($entry in $expected.GetEnumerator()) {
    $property = $probe.PSObject.Properties[$entry.Key]
    if ($null -eq $property) {
        $errors.Add("telemetria ausente: $($entry.Key)")
        continue
    }
    if ((To-U32 $property.Value) -ne (To-U32 $entry.Value)) {
        $errors.Add(("{0}: observado=0x{1:X8}, esperado=0x{2:X8}" -f
            $entry.Key, (To-U32 $property.Value), (To-U32 $entry.Value)))
    }
}

# O cursor tem de ter sido drenado pela GPU, nao apenas publicado pela CPU.
$ringWptr = To-U32 $probe.P1SelfTestRingWptrAfter
$rptrReport = To-U32 $probe.P1SelfTestRptrReportAfter
$rptrMmio = To-U32 $probe.P1SelfTestRptrMmioAfter
$ibDwords = To-U32 $probe.P1SelfTestIbDwords

if ($ringWptr -ne 12) {
    $errors.Add("WPTR do anel deveria ser 12 dwords (IB + RELEASE_MEM): $ringWptr")
}
if ($rptrReport -ne $ringWptr) {
    $errors.Add("RPTR em memoria nao alcancou o WPTR: rptr=$rptrReport wptr=$ringWptr")
}
if ($rptrMmio -ne $ringWptr) {
    $errors.Add("RPTR do registrador nao alcancou o WPTR: rptr=$rptrMmio wptr=$ringWptr")
}
if ($ibDwords -ne 45) {
    $errors.Add("Indirect Buffer deveria ter os 45 dwords do 3.43: $ibDwords")
}
if (((To-U32 $probe.HqdActiveRlcCpSchedulersReadback) -band 0xFF) -ne 0xC0) {
    $errors.Add(("scheduler0 nao publicou ENABLE|ME2|P0|Q0: 0x{0:X8}" -f
        (To-U32 $probe.HqdActiveRlcCpSchedulersReadback)))
}

# Provas internas da coerencia: Passed nao substitui os readbacks crus.
$coherL2 = To-U32 $probe.P1CoherL2Readback
$coherAck = To-U32 $probe.P1CoherInvalidateAck
$coherPolls = To-U32 $probe.P1CoherInvalidatePolls
if (($coherL2 -band 0x3) -ne 0) {
    $errors.Add(("bits de invalidate da GL2 nao auto-limparam: 0x{0:X8}" -f
        $coherL2))
}
if (($coherAck -band 0x1) -eq 0) {
    $errors.Add(("ACK do invalidate VMID0 ausente: 0x{0:X8}" -f $coherAck))
}
if ($coherPolls -lt 1 -or $coherPolls -gt 1000) {
    $errors.Add("polls de invalidate fora da janela 1..1000: $coherPolls")
}

# Uma submissao nao pode ter dado a volta no anel.
if ((To-U32 $probe.P1RingWrapCount) -ne 0) {
    $errors.Add('anel deu wraparound numa unica submissao')
}

if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $stamp = (Get-Date).ToString('yyyy-MM-dd_HH-mm-ss')
    $EvidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-p1-submission"
}
New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
$reportPath = Join-Path $EvidenceDir 'p1-submission-acceptance.json'

$report = [ordered]@{
    captured_at = (Get-Date).ToString('o')
    package_sys_sha256 = $manifest.PackageSysSha256
    schema_version = To-U32 $probe.SchemaVersion
    engine_state = To-U32 $probe.P1EngineState
    activate_status = To-U32 $probe.P1ActivateStatus
    teardown_status = To-U32 $probe.P1TeardownStatus
    ib_gpuva_low = ('0x{0:X8}' -f (To-U32 $probe.P1SelfTestIbGpuVaLow))
    ib_dwords = $ibDwords
    ib_readback_passed = To-U32 $probe.P1SelfTestIbReadback
    shader_staged_passed = To-U32 $probe.P1SelfTestShaderStaged
    ring_wptr_after = $ringWptr
    rptr_report_after = $rptrReport
    rptr_mmio_after = $rptrMmio
    ring_pad_dwords_total = To-U32 $probe.P1RingPadDwordsTotal
    ring_wrap_count = To-U32 $probe.P1RingWrapCount
    assigned_fence = To-U32 $probe.P1SelfTestAssignedFence
    ring_fence_observed = To-U32 $probe.P1SelfTestRingFenceObserved
    dispatch_fence_before = ('0x{0:X8}' -f (To-U32 $probe.P1SelfTestDispatchFenceBefore))
    dispatch_fence_after = ('0x{0:X8}' -f (To-U32 $probe.P1SelfTestDispatchFenceAfter))
    result_before = ('0x{0:X8}' -f (To-U32 $probe.P1SelfTestResultBefore))
    result_after = ('0x{0:X8}' -f (To-U32 $probe.P1SelfTestResultAfter))
    result_mismatches = To-U32 $probe.P1SelfTestResultMismatches
    retired_count = To-U32 $probe.P1SelfTestRetiredCount
    retired_total = To-U32 $probe.P1RetiredTotal
    polls = To-U32 $probe.P1SelfTestPolls
    doorbell_kicks = To-U32 $probe.P1DoorbellKicks
    direct_wptr_writes = To-U32 $probe.P1DirectWptrWrites
    wptr_shadow_readback = To-U32 $probe.P1LastWptrShadowReadback
    wptr_mmio_readback = To-U32 $probe.P1LastWptrMmioReadback
    submit_calls = To-U32 $probe.P1SubmitCalls
    submit_accepted = To-U32 $probe.P1SubmitAccepted
    ring_full_count = To-U32 $probe.P1RingFullCount
    fence_regression_count = To-U32 $probe.P1FenceRegressionCount
    triad_passed = To-U32 $probe.P1SelfTestTriadPassed
    physical_ready = To-U32 $probe.P1SelfTestPhysicalReady

    coherency = [ordered]@{
        hdp_applied = To-U32 $probe.P1CoherHdpApplied
        l2_readback = ('0x{0:X8}' -f (To-U32 $probe.P1CoherL2Readback))
        invalidate_ack = ('0x{0:X8}' -f (To-U32 $probe.P1CoherInvalidateAck))
        invalidate_polls = To-U32 $probe.P1CoherInvalidatePolls
        passed = To-U32 $probe.P1CoherencyPassed
    }
    scheduler_boundaries = [ordered]@{
        before_pfp = ('0x{0:X8}' -f (To-U32 $probe.PfpSchedulerBefore))
        before_pfp_read_passed = To-U32 $probe.PfpSchedulerBeforeReadPassed
        after_pfp = ('0x{0:X8}' -f (To-U32 $probe.PfpSchedulerAfter))
        after_pfp_read_passed = To-U32 $probe.PfpSchedulerAfterReadPassed
        at_hqd_preflight = ('0x{0:X8}' -f
            (To-U32 $probe.HqdActiveRlcCpSchedulersBefore))
    }

    # Baseline da fence do anel (correcao do bug do 3.44).
    fence_baseline_written = To-U32 $probe.P1FenceBaselineWritten
    fence_baseline_readback = To-U32 $probe.P1FenceBaselineReadback

    # Diagnostico da borda do ACTIVE. Um ciclo reprovado aqui ainda diz
    # exatamente onde a fila parou, que e o objetivo do 3.45.
    queue_after_activate = [ordered]@{
        active = ('0x{0:X8}' -f (To-U32 $probe.P1SnapActActive))
        hq_status0 = ('0x{0:X8}' -f (To-U32 $probe.P1SnapActHqStatus0))
        pq_control = ('0x{0:X8}' -f (To-U32 $probe.P1SnapActPqControl))
        doorbell_control = ('0x{0:X8}' -f (To-U32 $probe.P1SnapActDoorbellControl))
        cp_pq_status = ('0x{0:X8}' -f (To-U32 $probe.P1SnapActCpPqStatus))
        cpc_status = ('0x{0:X8}' -f (To-U32 $probe.P1SnapActCpcStatus))
        hqd_error = ('0x{0:X8}' -f (To-U32 $probe.P1SnapActHqdError))
        pq_wptr_lo = To-U32 $probe.P1SnapActPqWptrLo
        pq_rptr = To-U32 $probe.P1SnapActPqRptr
        mec_me2 = ('0x{0:X8}' -f (To-U32 $probe.P1SnapActMecMe2))
        cp_mec_cntl = ('0x{0:X8}' -f (To-U32 $probe.P1SnapActCpMecCntl))
        cp_me_cntl = ('0x{0:X8}' -f (To-U32 $probe.P1SnapActCpMeCntl))
        rlc_schedulers = ('0x{0:X8}' -f (To-U32 $probe.P1SnapActRlcSchedulers))
        wptr_poll_cntl = ('0x{0:X8}' -f (To-U32 $probe.P1SnapActWptrPollCntl))
        wptr_poll_cntl1 = ('0x{0:X8}' -f (To-U32 $probe.P1SnapActWptrPollCntl1))
    }
    queue_after_submit = [ordered]@{
        active = ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubActive))
        hq_status0 = ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubHqStatus0))
        pq_control = ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubPqControl))
        doorbell_control = ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubDoorbellControl))
        cp_pq_status = ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubCpPqStatus))
        cpc_status = ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubCpcStatus))
        hqd_error = ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubHqdError))
        pq_wptr_lo = To-U32 $probe.P1SnapSubPqWptrLo
        pq_rptr = To-U32 $probe.P1SnapSubPqRptr
        mec_me2 = ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubMecMe2))
        cp_mec_cntl = ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubCpMecCntl))
        cp_me_cntl = ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubCpMeCntl))
        rlc_schedulers = ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubRlcSchedulers))
        wptr_poll_cntl = ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubWptrPollCntl))
        wptr_poll_cntl1 = ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubWptrPollCntl1))
    }
    rearm = [ordered]@{
        attempted = To-U32 $probe.P1RearmAttempted
        active_before = ('0x{0:X8}' -f (To-U32 $probe.P1RearmActiveBefore))
        deactivate_polls = To-U32 $probe.P1RearmDeactivatePolls
        active_after_off = ('0x{0:X8}' -f (To-U32 $probe.P1RearmActiveAfterOff))
        queue_mask_applied = ('0x{0:X8}' -f (To-U32 $probe.P1RearmQueueMaskApplied))
        queue_mask_readback = ('0x{0:X8}' -f (To-U32 $probe.P1RearmQueueMaskReadback))
        queue_mask_passed = To-U32 $probe.P1RearmQueueMaskPassed
        active_after_on = ('0x{0:X8}' -f (To-U32 $probe.P1RearmActiveAfterOn))
        polls = To-U32 $probe.P1RearmPolls
        rptr_mmio = To-U32 $probe.P1RearmRptrMmio
        rptr_report = To-U32 $probe.P1RearmRptrReport
        dispatch_fence = ('0x{0:X8}' -f (To-U32 $probe.P1RearmDispatchFence))
        result = ('0x{0:X8}' -f (To-U32 $probe.P1RearmResult))
        ring_fence = To-U32 $probe.P1RearmRingFence
        completed = To-U32 $probe.P1RearmCompleted
        queue_after = [ordered]@{
            active = ('0x{0:X8}' -f (To-U32 $probe.P1SnapReActive))
            hq_status0 = ('0x{0:X8}' -f (To-U32 $probe.P1SnapReHqStatus0))
            pq_control = ('0x{0:X8}' -f (To-U32 $probe.P1SnapRePqControl))
            pq_wptr_lo = To-U32 $probe.P1SnapRePqWptrLo
            pq_rptr = To-U32 $probe.P1SnapRePqRptr
            mec_me2 = ('0x{0:X8}' -f (To-U32 $probe.P1SnapReMecMe2))
            cp_mec_cntl = ('0x{0:X8}' -f (To-U32 $probe.P1SnapReCpMecCntl))
            cp_me_cntl = ('0x{0:X8}' -f (To-U32 $probe.P1SnapReCpMeCntl))
            rlc_schedulers = ('0x{0:X8}' -f (To-U32 $probe.P1SnapReRlcSchedulers))
            wptr_poll_cntl = ('0x{0:X8}' -f (To-U32 $probe.P1SnapReWptrPollCntl))
            wptr_poll_cntl1 = ('0x{0:X8}' -f (To-U32 $probe.P1SnapReWptrPollCntl1))
        }
    }
    ring_dwords = @(
        ('0x{0:X8}' -f (To-U32 $probe.P1RingDword0)),
        ('0x{0:X8}' -f (To-U32 $probe.P1RingDword1)),
        ('0x{0:X8}' -f (To-U32 $probe.P1RingDword2)),
        ('0x{0:X8}' -f (To-U32 $probe.P1RingDword3)),
        ('0x{0:X8}' -f (To-U32 $probe.P1RingDword4)),
        ('0x{0:X8}' -f (To-U32 $probe.P1RingDword5)),
        ('0x{0:X8}' -f (To-U32 $probe.P1RingDword6)),
        ('0x{0:X8}' -f (To-U32 $probe.P1RingDword7)),
        ('0x{0:X8}' -f (To-U32 $probe.P1RingDword8)),
        ('0x{0:X8}' -f (To-U32 $probe.P1RingDword9)),
        ('0x{0:X8}' -f (To-U32 $probe.P1RingDword10)),
        ('0x{0:X8}' -f (To-U32 $probe.P1RingDword11))
    )
    ring_dwords_mismatch = To-U32 $probe.P1RingDwordsMismatch
    hqd_base = [ordered]@{
        pq_base_lo = ('0x{0:X8}' -f (To-U32 $probe.P1HqdPqBaseLo))
        pq_base_hi = ('0x{0:X8}' -f (To-U32 $probe.P1HqdPqBaseHi))
        rptr_report_addr = ('0x{0:X8}:{1:X8}' -f (To-U32 $probe.P1HqdRptrReportAddrHi), (To-U32 $probe.P1HqdRptrReportAddrLo))
        wptr_poll_addr = ('0x{0:X8}:{1:X8}' -f (To-U32 $probe.P1HqdWptrPollAddrHi), (To-U32 $probe.P1HqdWptrPollAddrLo))
        vmid = To-U32 $probe.P1HqdVmid
        mqd_control = ('0x{0:X8}' -f (To-U32 $probe.P1HqdMqdControl))
        ib_control = ('0x{0:X8}' -f (To-U32 $probe.P1HqdIbControl))
    }
    post_submit_engine = [ordered]@{
        cpc_status = ('0x{0:X8}' -f (To-U32 $probe.P1PostCpcStatus))
        cpc_busy_stat = ('0x{0:X8}' -f (To-U32 $probe.P1PostCpcBusyStat))
        cpc_stalled_stat1 = ('0x{0:X8}' -f (To-U32 $probe.P1PostCpcStalledStat1))
        cpf_status = ('0x{0:X8}' -f (To-U32 $probe.P1PostCpfStatus))
        cpf_busy_stat = ('0x{0:X8}' -f (To-U32 $probe.P1PostCpfBusyStat))
        cpf_stalled_stat1 = ('0x{0:X8}' -f (To-U32 $probe.P1PostCpfStalledStat1))
        cp_stat = ('0x{0:X8}' -f (To-U32 $probe.P1PostCpStat))
        cp_stalled_stat1 = ('0x{0:X8}' -f (To-U32 $probe.P1PostCpStalledStat1))
        mec_pc = ('0x{0:X8}' -f (To-U32 $probe.P1PostMecPc))
    }
    errors = @($errors)
    verdict = if ($errors.Count -eq 0) {
        'P1 SUBMISSAO PERSISTENTE VALIDADA EM HARDWARE'
    } else {
        'NAO VALIDADO EM HARDWARE'
    }
}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8

if ($errors.Count -ne 0) {
    Write-Host 'P1: REPROVADO' -ForegroundColor Red
    $errors | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }

    # Candidato 3.53: dump detalhado do anel e registradores base
    Write-Host ''
    Write-Host 'OBSERVABILIDADE CANDIDATO 3.53 (ANEL & HQD):' -ForegroundColor Cyan
    Write-Host ('  HQD PQ_BASE: {0}:{1}  VMID: {2}' -f
        ('0x{0:X8}' -f (To-U32 $probe.P1HqdPqBaseHi)),
        ('0x{0:X8}' -f (To-U32 $probe.P1HqdPqBaseLo)),
        (To-U32 $probe.P1HqdVmid))
    Write-Host ('  RPTR_REPORT: {0}:{1}  WPTR_POLL: {2}:{3}' -f
        ('0x{0:X8}' -f (To-U32 $probe.P1HqdRptrReportAddrHi)),
        ('0x{0:X8}' -f (To-U32 $probe.P1HqdRptrReportAddrLo)),
        ('0x{0:X8}' -f (To-U32 $probe.P1HqdWptrPollAddrHi)),
        ('0x{0:X8}' -f (To-U32 $probe.P1HqdWptrPollAddrLo)))
    Write-Host ('  IB_CONTROL: {0}  MQD_CONTROL: {1}' -f
        ('0x{0:X8}' -f (To-U32 $probe.P1HqdIbControl)),
        ('0x{0:X8}' -f (To-U32 $probe.P1HqdMqdControl)))
    Write-Host ('  CPC_STATUS: {0}  CPC_BUSY: {1}  MEC_PC: {2}' -f
        ('0x{0:X8}' -f (To-U32 $probe.P1PostCpcStatus)),
        ('0x{0:X8}' -f (To-U32 $probe.P1PostCpcBusyStat)),
        ('0x{0:X8}' -f (To-U32 $probe.P1PostMecPc)))
    Write-Host ('  CPF_STATUS: {0}  CPF_BUSY: {1}  CP_STAT: {2}' -f
        ('0x{0:X8}' -f (To-U32 $probe.P1PostCpfStatus)),
        ('0x{0:X8}' -f (To-U32 $probe.P1PostCpfBusyStat)),
        ('0x{0:X8}' -f (To-U32 $probe.P1PostCpStat)))

    Write-Host '  12 DWORDS DO ANEL REAL:' -ForegroundColor Yellow
    for ($i = 0; $i -lt 12; $i += 4) {
        Write-Host ('    [{0,2}..{1,2}]: {2}  {3}  {4}  {5}' -f
            $i, ($i + 3),
            ('0x{0:X8}' -f (To-U32 $probe.("P1RingDword$i"))),
            ('0x{0:X8}' -f (To-U32 $probe.("P1RingDword$($i+1)"))),
            ('0x{0:X8}' -f (To-U32 $probe.("P1RingDword$($i+2)"))),
            ('0x{0:X8}' -f (To-U32 $probe.("P1RingDword$($i+3)"))))
    }

    # O 3.45 existe para responder UMA pergunta. Responda-a na tela, sem
    # obrigar a abrir o JSON.
    Write-Host ''
    Write-Host 'DIAGNOSTICO DA BORDA DO ACTIVE:' -ForegroundColor Yellow
    if ((To-U32 $probe.P1SelfTestAttempted) -eq 0) {
        Write-Host ('  auto-teste nao foi iniciado: motor offline, ' +
            'P1ActivateStatus={0}.' -f (To-U32 $probe.P1ActivateStatus)) -ForegroundColor Red
        Write-Host '  coerencia, kick e consumo do anel nao foram testados.'
        Write-Host ('  RLC_CP_SCHEDULERS antes/depois PFP/HQD: {0} / {1} / {2}' -f
            ('0x{0:X8}' -f (To-U32 $probe.PfpSchedulerBefore)),
            ('0x{0:X8}' -f (To-U32 $probe.PfpSchedulerAfter)),
            ('0x{0:X8}' -f (To-U32 $probe.HqdActiveRlcCpSchedulersBefore)))
    } elseif ((To-U32 $probe.P1RearmAttempted) -eq 0) {
        Write-Host '  re-arme nao foi tentado (a GPU consumiu na primeira janela)'
    } elseif ((To-U32 $probe.P1RearmCompleted) -ne 0) {
        Write-Host ('  RE-ARME COMPLETO CONCLUIU: QUEUE_MASK + borda de ' +
            'CP_HQD_ACTIVE destravaram o fetch.') -ForegroundColor Green
        Write-Host ('  RPTR apos re-arme: {0} (era {1})' -f
            (To-U32 $probe.P1RearmRptrMmio), (To-U32 $probe.P1LastRptrMmio))
        Write-Host '  -> O gatilho de load precisa ser reconciliado com a fila persistente.'
    } else {
        Write-Host '  re-arme completo NAO destravou.' -ForegroundColor Red
        Write-Host ('  ACTIVE off/on: {0} -> {1}, RPTR: {2}' -f
            ('0x{0:X8}' -f (To-U32 $probe.P1RearmActiveAfterOff)),
            ('0x{0:X8}' -f (To-U32 $probe.P1RearmActiveAfterOn)),
            (To-U32 $probe.P1RearmRptrMmio))
        Write-Host ('  QUEUE_MASK apply/readback/pass: {0} / {1} / {2}' -f
            ('0x{0:X8}' -f (To-U32 $probe.P1RearmQueueMaskApplied)),
            ('0x{0:X8}' -f (To-U32 $probe.P1RearmQueueMaskReadback)),
            (To-U32 $probe.P1RearmQueueMaskPassed))
        Write-Host ('  HQ_STATUS0 pos-submit: {0}  PQ_CONTROL: {1}' -f
            ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubHqStatus0)),
            ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubPqControl)))
        Write-Host ('  DOORBELL_CONTROL pos-submit: {0}  HQD_ERROR: {1}' -f
            ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubDoorbellControl)),
            ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubHqdError)))
        Write-Host ('  CP_MEC/CP_ME/RLC_SCHED: {0} / {1} / {2}' -f
            ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubCpMecCntl)),
            ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubCpMeCntl)),
            ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubRlcSchedulers)))
        Write-Host ('  WPTR_POLL_CNTL/CNTL1: {0} / {1}' -f
            ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubWptrPollCntl)),
            ('0x{0:X8}' -f (To-U32 $probe.P1SnapSubWptrPollCntl1)))
    }
    Write-Host ''
    Write-Host "Relatorio: $reportPath"
    exit 1
}

Write-Host 'P1: SUBMISSAO PERSISTENTE VALIDADA EM HARDWARE' -ForegroundColor Green
Write-Host 'IB executado pelo anel persistente, duas fences gravadas pela GPU,'
Write-Host 'resultado do shader conferido byte a byte e RPTR==WPTR: APROVADOS.'
Write-Host "Relatorio: $reportPath"
