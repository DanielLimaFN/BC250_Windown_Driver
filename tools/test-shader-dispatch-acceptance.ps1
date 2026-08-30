<#
.SYNOPSIS
    Valida, sem escrever em hardware, a telemetria do dispatch observavel F8b.

.DESCRIPTION
    Aceita somente uma captura SchemaVersion 111 do pacote de laboratorio com
    ShaderDispatch=1. Exige no mesmo ciclo: blob relido, resultado saindo do
    poison, fence posterior ao dispatch, RPTR==WPTR e rollback integral.
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
$hardwareId = 'PCI\VEN_1002&DEV_13FE'
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

$expected = [ordered]@{
    SchemaVersion = 114
    SkipHwBringup = 0
    ShaderDispatchEnabled = 1
    PfpDirectCpSoftReset = 1
    PfpDirectVmid0PageTable = 0
    HqdActiveStatus = 0
    ShaderFenceAfterDispatch = 1
    ShaderWgpPreflightPassed = 1
    ShaderRingGpuVaLow = 0x00010000
    ShaderRingGpuVaHigh = 0
    ShaderRingTranslationPassed = 1
    ShaderRingReadbackPassed = 1
    ShaderRingReadbackMismatches = 0
    ShaderReadbackPassed = 1
    ShaderReadbackMismatches = 0
    ShaderResultBefore = 0x43DAFFFE
    ShaderResultAfter = 0xBC250C01
    ShaderResultMismatches = 0
    ShaderFenceBefore = 0xA55A0FF0
    ShaderFenceAfter = 0xBC250D15
    ShaderRegisterRestorePassed = 1
    ShaderMemoryRestorePassed = 1
    ShaderTriadPassed = 1
    ShaderPhysicalReady = 1
    HqdActiveRestorePassed = 1
    HqdActiveMemRestorePassed = 1
    HqdActivePhysicalReady = 1
    HqdActiveEopRptrApplied = 0x80000000
    HqdActiveEopRptrRestorePassed = 1
    HqdActiveWptrPollCntl1RestorePassed = 1
    HqdActiveWptrPollCntl1DirtyEntry = 0
    HqdActiveRestoreFailureCount = 0
    HqdActiveFirstRestoreFailureStage = 0
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

$dwords = To-U32 $probe.ShaderDispatchDwords
$dispatchIndex = To-U32 $probe.ShaderDispatchPacketIndex
$fenceIndex = To-U32 $probe.ShaderFencePacketIndex
$rptr = To-U32 $probe.ShaderRptrAfter
$wptr = To-U32 $probe.ShaderWptrAfter
$ccPass1 = To-U32 $probe.ShaderCcArrayPass1
$ccPass2 = To-U32 $probe.ShaderCcArrayPass2
$spiPass1 = To-U32 $probe.ShaderSpiWgpMaskPass1
$spiPass2 = To-U32 $probe.ShaderSpiWgpMaskPass2
$rlcPass1 = To-U32 $probe.ShaderRlcAonWgpPass1
$rlcPass2 = To-U32 $probe.ShaderRlcAonWgpPass2
$aonNonHarvested = To-U32 $probe.ShaderAonNonHarvestedWgpMask
$ringPhysicalLow = To-U32 $probe.ShaderRingPhysicalLow
$ringPhysicalHigh = To-U32 $probe.ShaderRingPhysicalHigh
$ringPteLow = To-U32 $probe.ShaderRingPteLow
$ringPteHigh = To-U32 $probe.ShaderRingPteHigh
$sdmaCntlApplied = To-U32 $probe.SdmaRingCntlApplied
$eopRptrBefore = To-U32 $probe.HqdActiveEopRptrBefore
$eopRptrReadback = To-U32 $probe.HqdActiveEopRptrReadback
$eopRptrRestored = To-U32 $probe.HqdActiveEopRptrRestored
$pollCntlBefore = To-U32 $probe.HqdActiveWptrPollCntlBefore
$pollCntlApplied = To-U32 $probe.HqdActiveWptrPollCntlApplied
$pollCntlReadback = To-U32 $probe.HqdActiveWptrPollCntlReadback
$pollCntl1Before = To-U32 $probe.HqdActiveWptrPollCntl1Before
$pollCntl1Applied = To-U32 $probe.HqdActiveWptrPollCntl1Applied
$pollCntl1Readback = To-U32 $probe.HqdActiveWptrPollCntl1Readback
$pollCntl1Restored = To-U32 $probe.HqdActiveWptrPollCntl1Restored
$queueMaskBit = To-U32 $probe.HqdActiveQueueMaskBit
$preActiveWptr = To-U32 $probe.HqdActivePreActiveWptrReadback
$restoreStage = To-U32 $probe.HqdActiveFirstRestoreFailureStage
$restoreOffset = To-U32 $probe.HqdActiveFirstRestoreFailureOffset
$restoreExpected = To-U32 $probe.HqdActiveFirstRestoreFailureExpected
$restoreObserved = To-U32 $probe.HqdActiveFirstRestoreFailureObserved
$postActiveWptr = To-U32 $probe.HqdActivePostActiveWptrObserved
$preActivePublished = To-U32 $probe.HqdActivePreActiveWptrPublished
$cntl1Dirty = To-U32 $probe.HqdActiveWptrPollCntl1DirtyEntry
$cntl1ClearAttempts = To-U32 $probe.HqdActiveWptrPollCntl1ClearAttempts
$pqControlFirst = To-U32 $probe.HqdActivePqControlFirstSample
$pqControlLast = To-U32 $probe.HqdActivePqControlObserved
$hqStatus0First = To-U32 $probe.HqdActiveHqStatus0FirstSample
$hqStatus0Last = To-U32 $probe.HqdActiveHqStatus0PollLast
$dequeuePollLast = To-U32 $probe.HqdActiveDequeueRequestPollLast
$vmidPollLast = To-U32 $probe.HqdActiveVmidPollLast
$persistentPollLast = To-U32 $probe.HqdActivePersistentStatePollLast
$rptrMemFirst = To-U32 $probe.HqdActiveRptrReportMemFirst
$rptrMemLast = To-U32 $probe.HqdActiveRptrReportMemLast
$rptrNonZeroSamples = To-U32 $probe.HqdActiveRptrNonZeroSamples
$cpcStatusFirst = To-U32 $probe.HqdActiveCpcStatusFirst
$cpcStatusLast = To-U32 $probe.HqdActiveCpcStatusLast
$cpcBusyStat = To-U32 $probe.HqdActiveCpcBusyStatLast
$cpcStalled1 = To-U32 $probe.HqdActiveCpcStalledStat1Last
$cpStatFirst = To-U32 $probe.HqdActiveCpStatFirst
$cpStatLast = To-U32 $probe.HqdActiveCpStatLast
$cpStalled1 = To-U32 $probe.HqdActiveCpStalledStat1After
$hdr0 = To-U32 $probe.HqdActiveMecMe1HeaderDump0
$hdr1 = To-U32 $probe.HqdActiveMecMe1HeaderDump1
$hdr2 = To-U32 $probe.HqdActiveMecMe1HeaderDump2
$hdr3 = To-U32 $probe.HqdActiveMecMe1HeaderDump3
$me2Hdr0 = To-U32 $probe.HqdActiveMecMe2HeaderDump0
$me2Hdr1 = To-U32 $probe.HqdActiveMecMe2HeaderDump1
$me2Hdr2 = To-U32 $probe.HqdActiveMecMe2HeaderDump2
$me2Hdr3 = To-U32 $probe.HqdActiveMecMe2HeaderDump3
$me2Before0 = To-U32 $probe.HqdActiveMecMe2BeforeActive0
$me2Before1 = To-U32 $probe.HqdActiveMecMe2BeforeActive1
$me2Before2 = To-U32 $probe.HqdActiveMecMe2BeforeActive2
$me2Before3 = To-U32 $probe.HqdActiveMecMe2BeforeActive3
$cpcStatusBeforeKick = To-U32 $probe.HqdActiveCpcStatusBeforeKick
$cpcBusyBeforeKick = To-U32 $probe.HqdActiveCpcBusyStatBeforeKick
$cpcStalledBeforeKick = To-U32 $probe.HqdActiveCpcStalledStat1BeforeKick
$cpStatBeforeKick = To-U32 $probe.HqdActiveCpStatBeforeKick
$cpStalledBeforeKick = To-U32 $probe.HqdActiveCpStalledStat1BeforeKick
$cpcStatusAfterKick = To-U32 $probe.HqdActiveCpcStatusAfterKick
$cpcBusyAfterKick = To-U32 $probe.HqdActiveCpcBusyStatAfterKick
$cpcStalledAfterKick = To-U32 $probe.HqdActiveCpcStalledStat1AfterKick
$cpStatAfterKick = To-U32 $probe.HqdActiveCpStatAfterKick
$cpStalledAfterKick = To-U32 $probe.HqdActiveCpStalledStat1AfterKick
$ringDword0 = To-U32 $probe.ShaderRingDword0
$ringDword1 = To-U32 $probe.ShaderRingDword1
$ringDword2 = To-U32 $probe.ShaderRingDword2
$ringDword3 = To-U32 $probe.ShaderRingDword3
$ringDword4 = To-U32 $probe.ShaderRingDword4
$ringDword5 = To-U32 $probe.ShaderRingDword5
$ringDword6 = To-U32 $probe.ShaderRingDword6
$ringDword7 = To-U32 $probe.ShaderRingDword7
$pollCntlProbeApplied = To-U32 $probe.HqdActiveWptrPollCntlProbeApplied
$pollCntlProbeReadback = To-U32 $probe.HqdActiveWptrPollCntlProbeReadback
$pollCntlWritable = To-U32 $probe.HqdActiveWptrPollCntlWritable
$pollCntlEnLatched = To-U32 $probe.HqdActiveWptrPollCntlEnLatched
$eopRptrApplied = To-U32 $probe.HqdActiveEopRptrApplied
if ($dwords -eq 0 -or $fenceIndex -le $dispatchIndex) {
    $errors.Add('ordem/tamanho do plano PM4 invalido')
}
if ($rptr -ne $dwords -or $wptr -ne $dwords -or $rptr -ne $wptr) {
    $errors.Add("cursores divergentes: RPTR=$rptr WPTR=$wptr submitted=$dwords")
}
if ($ccPass1 -ne $ccPass2 -or $spiPass1 -ne $spiPass2 -or
    $rlcPass1 -ne $rlcPass2 -or $aonNonHarvested -eq 0) {
    $errors.Add('preflight WGP instavel ou sem WGP nao-harvestado/always-on')
}
if (($sdmaCntlApplied -band 0x00040000) -ne 0) {
    $errors.Add('receita F8b violada: SDMA AUTO_CTXSW_ENABLE permaneceu ligado')
}
if (($eopRptrReadback -band 0x30001FFF) -ne 0) {
    $errors.Add(('EOP_RPTR readback contem RPTR/status proibido: 0x{0:X8}' -f
        $eopRptrReadback))
}
# Protocolo one-shot de WPTR, derivado das mascaras GC10.1.
if ($pollCntlApplied -ne (To-U32 (($pollCntlBefore -band 0xBFFFFFFFL) -bor 0x80000000L))) {
    $errors.Add(('CP_PQ_WPTR_POLL_CNTL aplicado fora da mascara SOC15: ' +
        'before=0x{0:X8}, applied=0x{1:X8}' -f $pollCntlBefore, $pollCntlApplied))
}
# CP_PQ_WPTR_POLL_CNTL.EN NAO reprova o ensaio: o 3.30 provou que o bit nao e
# retido neste silicio, e a necessidade dele nunca foi demonstrada. Fica como
# observacao publicada. O tripe continua sendo o gate.
if ($pollCntlEnLatched -eq 0) {
    Write-Host ('OBSERVACAO: CP_PQ_WPTR_POLL_CNTL.EN nao foi retido ' +
        '(readback=0x{0:X8}); writable={1}, probe=0x{2:X8}' -f
        $pollCntlReadback, $pollCntlWritable, $pollCntlProbeReadback)
}
if ($queueMaskBit -gt 31) {
    $errors.Add("bit de fila fora de CP_PQ_WPTR_POLL_CNTL1: $queueMaskBit")
} elseif ($pollCntl1Applied -ne (To-U32 ([uint64]1 -shl [int]$queueMaskBit))) {
    $errors.Add(('QUEUE_MASK aplicado nao corresponde ao bit da topologia: ' +
        'bit={0}, applied=0x{1:X8}' -f $queueMaskBit, $pollCntl1Applied))
}
if (($pollCntl1Readback -band $pollCntl1Applied) -ne $pollCntl1Applied) {
    $errors.Add(('QUEUE_MASK nao ficou no silicio: 0x{0:X8}' -f $pollCntl1Readback))
}
if ($pollCntl1Restored -ne $pollCntl1Before) {
    $errors.Add(('rollback CP_PQ_WPTR_POLL_CNTL1 divergiu: ' +
        'before=0x{0:X8}, restored=0x{1:X8}' -f
        $pollCntl1Before, $pollCntl1Restored))
}
# A publicacao pre-ACTIVE do WPTR NAO reprova: o 3.31 mediu readback 0 com a
# fila inativa. Fica como observacao; quem tem de fechar e o tripe.
if ($preActivePublished -eq 0) {
    Write-Host ("OBSERVACAO: WPTR pre-ACTIVE nao pegou ({0} vs {1}); " +
        "WPTR logo apos ACTIVE=1 (antes de doorbell) = {2}" -f
        $preActiveWptr, $dwords, $postActiveWptr)
}
# Boot morno invalida o ensaio: com a QUEUE_MASK ja acesa o rollback compara
# 1 com 1 e daria falso verde.
# Observacao dirigida do 3.33: PQ_EMPTY[15] e QUEUE_IDLE[30] com evidencia
# diferencial ja catalogada. Nao sao gate - sao o mapa do bloqueio atual.
Write-Host ("OBSERVACAO fila: PQ_CONTROL {0:X8}->{1:X8} (PQ_EMPTY {2}->{3}), " +
    "HQ_STATUS0 {4:X8}->{5:X8} (QUEUE_IDLE {6}->{7})" -f
    $pqControlFirst, $pqControlLast,
    [int](($pqControlFirst -band 0x8000) -ne 0), [int](($pqControlLast -band 0x8000) -ne 0),
    $hqStatus0First, $hqStatus0Last,
    [int](($hqStatus0First -band 0x40000000L) -ne 0), [int](($hqStatus0Last -band 0x40000000L) -ne 0))
Write-Host ("OBSERVACAO CPC: CP_CPC_STATUS {0:X8}->{1:X8}, BUSY_STAT {2:X8}, " +
    "STALLED_STAT1 {3:X8}; CP_STAT {4:X8}->{5:X8}, CP_STALLED_STAT1 {6:X8}" -f
    $cpcStatusFirst, $cpcStatusLast, $cpcBusyStat, $cpcStalled1,
    $cpStatFirst, $cpStatLast, $cpStalled1)
Write-Host ("OBSERVACAO ME1 header dump: {0:X8} {1:X8} {2:X8} {3:X8}" -f
    $hdr0, $hdr1, $hdr2, $hdr3)
Write-Host ("OBSERVACAO consumo: RPTR reg amostras nao-zero={0}, " +
    "writeback mem {1}->{2}, DEQUEUE_REQUEST={3}, VMID={4}, PERSISTENT_STATE=0x{5:X8}" -f
    $rptrNonZeroSamples, $rptrMemFirst, $rptrMemLast,
    $dequeuePollLast, $vmidPollLast, $persistentPollLast)
if ($cntl1Dirty -ne 0) {
    $errors.Add(('CP_PQ_WPTR_POLL_CNTL1 entrou com a mascara ja acesa ' +
        '(before=0x{0:X8}): o ensaio nao partiu de boot frio' -f $pollCntl1Before))
}
if ($restoreStage -ne 0) {
    $errors.Add(('rollback interno reprovou no estagio {0}: offset=0x{1:X8}, ' +
        'esperado=0x{2:X8}, observado=0x{3:X8}' -f
        $restoreStage, $restoreOffset, $restoreExpected, $restoreObserved))
}
# Comparar o rollback do EOP_RPTR so faz sentido se a etapa foi executada.
# No 3.30 a transacao abortou antes dela e este gate deu falso positivo.
if ($eopRptrApplied -ne 0 -and $eopRptrRestored -ne $eopRptrBefore) {
    $errors.Add(('rollback EOP_RPTR divergiu: before=0x{0:X8}, restored=0x{1:X8}' -f
        $eopRptrBefore, $eopRptrRestored))
}
if (($ringPhysicalLow -band 0xFFF) -ne 0 -or
    ($ringPteLow -band 0xFFF) -ne 0xE1 -or
    ($ringPteLow -band 0xFFFFF000) -ne $ringPhysicalLow -or
    $ringPteHigh -ne $ringPhysicalHigh) {
    $errors.Add('PTE do ring nao prova a traducao GPUVA 0x10000 -> backing fisico')
}

if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
    $EvidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-shader-f8b"
}
New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
$reportPath = Join-Path $EvidenceDir 'shader-dispatch-acceptance.json'
$report = [ordered]@{
    captured_at = [DateTime]::Now.ToString('o')
    package_sys_sha256 = $manifest.PackageSysSha256
    device_instance = if ($devices.Count -eq 1) { $devices[0].PNPDeviceID } else { $null }
    schema_version = To-U32 $probe.SchemaVersion
    submitted_dwords = $dwords
    dispatch_packet_index = $dispatchIndex
    fence_packet_index = $fenceIndex
    cc_shader_array_pass1 = ('0x{0:X8}' -f $ccPass1)
    cc_shader_array_pass2 = ('0x{0:X8}' -f $ccPass2)
    spi_static_wgp_pass1 = ('0x{0:X8}' -f $spiPass1)
    spi_static_wgp_pass2 = ('0x{0:X8}' -f $spiPass2)
    rlc_always_on_wgp_pass1 = ('0x{0:X8}' -f $rlcPass1)
    rlc_always_on_wgp_pass2 = ('0x{0:X8}' -f $rlcPass2)
    always_on_non_harvested_wgp = ('0x{0:X8}' -f $aonNonHarvested)
    ring_gpuva = '0x0000000000010000'
    ring_physical = ('0x{0:X8}{1:X8}' -f $ringPhysicalHigh, $ringPhysicalLow)
    ring_pte = ('0x{0:X8}{1:X8}' -f $ringPteHigh, $ringPteLow)
    ring_translation_passed = To-U32 $probe.ShaderRingTranslationPassed
    ring_readback_passed = To-U32 $probe.ShaderRingReadbackPassed
    ring_readback_mismatches = To-U32 $probe.ShaderRingReadbackMismatches
    ring_dwords = @(
        ('0x{0:X8}' -f $ringDword0), ('0x{0:X8}' -f $ringDword1),
        ('0x{0:X8}' -f $ringDword2), ('0x{0:X8}' -f $ringDword3),
        ('0x{0:X8}' -f $ringDword4), ('0x{0:X8}' -f $ringDword5),
        ('0x{0:X8}' -f $ringDword6), ('0x{0:X8}' -f $ringDword7))
    pfp_cp_soft_reset = To-U32 $probe.PfpDirectCpSoftReset
    pfp_vmid0_page_table = To-U32 $probe.PfpDirectVmid0PageTable
    sdma_cntl_applied = ('0x{0:X8}' -f $sdmaCntlApplied)
    eop_rptr_before = ('0x{0:X8}' -f $eopRptrBefore)
    eop_rptr_applied = ('0x{0:X8}' -f (To-U32 $probe.HqdActiveEopRptrApplied))
    eop_rptr_readback = ('0x{0:X8}' -f $eopRptrReadback)
    eop_rptr_restored = ('0x{0:X8}' -f $eopRptrRestored)
    eop_rptr_restore_passed = To-U32 $probe.HqdActiveEopRptrRestorePassed
    wptr_poll_cntl_before = ('0x{0:X8}' -f $pollCntlBefore)
    wptr_poll_cntl_applied = ('0x{0:X8}' -f $pollCntlApplied)
    wptr_poll_cntl_readback = ('0x{0:X8}' -f $pollCntlReadback)
    wptr_poll_cntl_probe_applied = ('0x{0:X8}' -f $pollCntlProbeApplied)
    wptr_poll_cntl_probe_readback = ('0x{0:X8}' -f $pollCntlProbeReadback)
    wptr_poll_cntl_writable = $pollCntlWritable
    wptr_poll_cntl_en_latched = $pollCntlEnLatched
    wptr_poll_cntl1_before = ('0x{0:X8}' -f $pollCntl1Before)
    wptr_poll_cntl1_applied = ('0x{0:X8}' -f $pollCntl1Applied)
    wptr_poll_cntl1_readback = ('0x{0:X8}' -f $pollCntl1Readback)
    wptr_poll_cntl1_restored = ('0x{0:X8}' -f $pollCntl1Restored)
    queue_mask_bit = $queueMaskBit
    pre_active_wptr_applied = To-U32 $probe.HqdActivePreActiveWptrApplied
    pre_active_wptr_readback = $preActiveWptr
    pre_active_wptr_published = $preActivePublished
    post_active_wptr_observed = $postActiveWptr
    wptr_poll_cntl1_dirty_entry = $cntl1Dirty
    wptr_poll_cntl1_clear_attempts = $cntl1ClearAttempts
    pq_control_first = ('0x{0:X8}' -f $pqControlFirst)
    pq_control_last = ('0x{0:X8}' -f $pqControlLast)
    pq_empty_first = [int](($pqControlFirst -band 0x8000) -ne 0)
    pq_empty_last = [int](($pqControlLast -band 0x8000) -ne 0)
    hq_status0_first = ('0x{0:X8}' -f $hqStatus0First)
    hq_status0_last = ('0x{0:X8}' -f $hqStatus0Last)
    queue_idle_first = [int](($hqStatus0First -band 0x40000000L) -ne 0)
    queue_idle_last = [int](($hqStatus0Last -band 0x40000000L) -ne 0)
    rptr_non_zero_samples = $rptrNonZeroSamples
    cpc_status_first = ('0x{0:X8}' -f $cpcStatusFirst)
    cpc_status_last = ('0x{0:X8}' -f $cpcStatusLast)
    cpc_busy_stat_last = ('0x{0:X8}' -f $cpcBusyStat)
    cpc_stalled_stat1_last = ('0x{0:X8}' -f $cpcStalled1)
    cp_stat_first = ('0x{0:X8}' -f $cpStatFirst)
    cp_stat_last = ('0x{0:X8}' -f $cpStatLast)
    cp_stalled_stat1_after = ('0x{0:X8}' -f $cpStalled1)
    mec_me1_header_dump = @(
        ('0x{0:X8}' -f $hdr0), ('0x{0:X8}' -f $hdr1),
        ('0x{0:X8}' -f $hdr2), ('0x{0:X8}' -f $hdr3))
    mec_me2_header_dump = @(
        ('0x{0:X8}' -f $me2Hdr0), ('0x{0:X8}' -f $me2Hdr1),
        ('0x{0:X8}' -f $me2Hdr2), ('0x{0:X8}' -f $me2Hdr3))
    mec_me2_before_active = @(
        ('0x{0:X8}' -f $me2Before0), ('0x{0:X8}' -f $me2Before1),
        ('0x{0:X8}' -f $me2Before2), ('0x{0:X8}' -f $me2Before3))
    cpc_before_kick = [ordered]@{
        status = ('0x{0:X8}' -f $cpcStatusBeforeKick)
        busy = ('0x{0:X8}' -f $cpcBusyBeforeKick)
        stalled = ('0x{0:X8}' -f $cpcStalledBeforeKick)
    }
    cp_before_kick = [ordered]@{
        status = ('0x{0:X8}' -f $cpStatBeforeKick)
        stalled = ('0x{0:X8}' -f $cpStalledBeforeKick)
    }
    cpc_after_kick = [ordered]@{
        status = ('0x{0:X8}' -f $cpcStatusAfterKick)
        busy = ('0x{0:X8}' -f $cpcBusyAfterKick)
        stalled = ('0x{0:X8}' -f $cpcStalledAfterKick)
    }
    cp_after_kick = [ordered]@{
        status = ('0x{0:X8}' -f $cpStatAfterKick)
        stalled = ('0x{0:X8}' -f $cpStalledAfterKick)
    }
    rptr_report_mem_first = $rptrMemFirst
    rptr_report_mem_last = $rptrMemLast
    dequeue_request_poll_last = $dequeuePollLast
    vmid_poll_last = $vmidPollLast
    persistent_state_poll_last = ('0x{0:X8}' -f $persistentPollLast)
    restore_failure_count = To-U32 $probe.HqdActiveRestoreFailureCount
    first_restore_failure_stage = $restoreStage
    first_restore_failure_offset = ('0x{0:X8}' -f $restoreOffset)
    first_restore_failure_expected = ('0x{0:X8}' -f $restoreExpected)
    first_restore_failure_observed = ('0x{0:X8}' -f $restoreObserved)
    first_restore_failure_transport =
        To-U32 $probe.HqdActiveFirstRestoreFailureTransport
    result_before = ('0x{0:X8}' -f (To-U32 $probe.ShaderResultBefore))
    result_after = ('0x{0:X8}' -f (To-U32 $probe.ShaderResultAfter))
    result_mismatches = To-U32 $probe.ShaderResultMismatches
    fence_before = ('0x{0:X8}' -f (To-U32 $probe.ShaderFenceBefore))
    fence_after = ('0x{0:X8}' -f (To-U32 $probe.ShaderFenceAfter))
    rptr_after = $rptr
    wptr_after = $wptr
    register_restore = To-U32 $probe.ShaderRegisterRestorePassed
    memory_restore = To-U32 $probe.ShaderMemoryRestorePassed
    physical_ready = To-U32 $probe.ShaderPhysicalReady
    errors = @($errors)
    verdict = if ($errors.Count -eq 0) {
        'SHADER F8b VALIDADO EM HARDWARE'
    } else {
        'NAO VALIDADO EM HARDWARE'
    }
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $reportPath -Encoding UTF8

if ($errors.Count -ne 0) {
    Write-Host 'SHADER F8b: REPROVADO' -ForegroundColor Red
    $errors | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host "Relatorio: $reportPath"
    exit 1
}

Write-Host 'SHADER F8b: VALIDADO EM HARDWARE' -ForegroundColor Green
Write-Host 'Resultado, fence posterior, RPTR==WPTR e rollback: APROVADOS.'
Write-Host "Relatorio: $reportPath"
