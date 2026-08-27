[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $root 'driver\wdk\package\output_active.manifest.json'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$probePath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$errors = [System.Collections.Generic.List[string]]::new()

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\audit-bc250mcdm-package.ps1')
if ($LASTEXITCODE -ne 0) { $errors.Add('pacote local falhou na auditoria de integridade') }

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$markerLines = @(Get-Content -LiteralPath $markerPath -ErrorAction SilentlyContinue)
if ($markerLines.Count -ne 1 -or $markerLines[0] -notmatch '^oem\d+\.inf$') {
    $errors.Add('marcador OEM ausente ou invalido')
    $publishedName = $null
} else { $publishedName = $markerLines[0].ToLowerInvariant() }

$devices = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like 'PCI\VEN_1002&DEV_13FE*'
})
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'bc250mcdm' -or
    $devices[0].ConfigManagerErrorCode -ne 43) {
    $errors.Add('resultado PnP nao e bc250mcdm Codigo 43 fail-closed')
}

$service = Get-Service bc250mcdm -ErrorAction SilentlyContinue
if ($null -eq $service -or $service.Status -ne 'Stopped') {
    $errors.Add('servico bc250mcdm nao existe parado apos falha de StartDevice')
}
if (-not (Test-Path -LiteralPath $driverPath)) {
    $errors.Add('bc250mcdm.sys ativo ausente')
} elseif ((Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash -ne
    $manifest.PackageSysSha256) {
    $errors.Add('bc250mcdm.sys ativo diverge do pacote auditado')
}

if (-not (Test-Path -LiteralPath $probePath)) {
    $errors.Add('telemetria Parameters\Probe ausente')
    $probe = $null
} else {
    $probe = Get-ItemProperty -LiteralPath $probePath
    foreach ($expected in @(
        @{ Name='SchemaVersion'; Value=40 },
        @{ Name='DriverEntryReached'; Value=1 },
        @{ Name='InitializationDataReady'; Value=1 },
        @{ Name='AddDeviceEntered'; Value=1 },
        @{ Name='AddDeviceStatus'; Value=0 },
        @{ Name='StartDeviceEntered'; Value=1 },
        @{ Name='InterfaceAccepted'; Value=1 },
        @{ Name='DeviceInfoStatus'; Value=0 },
        @{ Name='PciStatus'; Value=0 },
        @{ Name='PciBytesRead'; Value=64 },
        @{ Name='BarCount'; Value=3 },
        @{ Name='ResolvedBarCount'; Value=3 },
        @{ Name='MmioStatus'; Value=0 },
        @{ Name='TopologyStatus'; Value=0 },
        @{ Name='TopologyStablePasses'; Value=2 },
        @{ Name='DiscoveryTmrStatus'; Value=5 },
        @{ Name='DiscoveryTmrBytes'; Value=0 },
        @{ Name='RuntimeMissing'; Value=1 },
        @{ Name='AllocationSegmentSet'; Value=1 },
        @{ Name='AllocationMaxLow32'; Value=252911616 },
        @{ Name='GpuVaDescriptorReady'; Value=0 },
        @{ Name='RootPageTableAbiReady'; Value=0 },
        @{ Name='RootPageTableMaxEntries'; Value=0 },
        @{ Name='PagingCpuUpdateReady'; Value=0 },
        @{ Name='TlbFlushPhysicalReady'; Value=1 },
        @{ Name='TlbTransactionStatus'; Value=0 },
        @{ Name='TlbVmid'; Value=8 },
        @{ Name='TlbEngineId'; Value=17 },
        @{ Name='TlbRequestValue'; Value=0x00F80100 },
        @{ Name='TlbRangeLowProgrammed'; Value=[uint32]::MaxValue },
        @{ Name='TlbRangeHighProgrammed'; Value=0x0000001F },
        @{ Name='TlbRequestWritten'; Value=1 },
        @{ Name='TlbDummyReadCompleted'; Value=1 },
        @{ Name='TlbAckObserved'; Value=1 },
        @{ Name='TlbRangeRestorePassed'; Value=1 },
        @{ Name='GcvmBackingStatus'; Value=0 },
        @{ Name='GcvmBackingOwned'; Value=1 },
        @{ Name='GcvmBackingReadback'; Value=1 },
        @{ Name='GcvmBackingRestore'; Value=1 },
        @{ Name='GcvmVmid8Status'; Value=0 },
        @{ Name='GcvmVmid8Readback'; Value=1 },
        @{ Name='GcvmVmid8TlbPassed'; Value=1 },
        @{ Name='GcvmVmid8Restore'; Value=1 },
        @{ Name='GcvmInertPhysicalReady'; Value=1 },
        @{ Name='PspRingStatus'; Value=0 },
        @{ Name='PspRingCreatePassed'; Value=1 },
        @{ Name='PspRingDestroyPassed'; Value=1 },
        @{ Name='PspRingRestorePassed'; Value=1 },
        @{ Name='PspRingPhysicalReady'; Value=1 },
        @{ Name='PspLoadFwStatus'; Value=0 },
        @{ Name='PspLoadFwBytes'; Value=33536 },
        @{ Name='PspLoadFwType'; Value=9 },
        @{ Name='PspLoadFwFenceExpected'; Value=1 },
        @{ Name='PspLoadFwFenceObserved'; Value=1 },
        @{ Name='PspLoadFwResponseStatus'; Value=0 },
        @{ Name='PspLoadFwFencePassed'; Value=1 },
        @{ Name='PspLoadFwResponsePassed'; Value=1 },
        @{ Name='PspLoadFwChecksumPassed'; Value=1 },
        @{ Name='PspLoadFwDestroyPassed'; Value=1 },
        @{ Name='PspLoadFwRestorePassed'; Value=1 },
        @{ Name='PspLoadFwPhysicalReady'; Value=1 },
        @{ Name='PspRlcLoadStatus'; Value=0 },
        @{ Name='PspRlcLoadType'; Value=8 },
        @{ Name='PspRlcLoadPhysicalReady'; Value=1 },
        @{ Name='RlcExecRomAfter'; Value=0 },
        @{ Name='RlcEnableF32After'; Value=1 },
        @{ Name='RlcCntlRestore'; Value=1 },
        @{ Name='RlcPhysicalReady'; Value=1 },
        @{ Name='PfpDirectStatus'; Value=0 },
        @{ Name='PfpDirectBytes'; Value=263168 },
        @{ Name='PfpDirectJtDwords'; Value=128 },
        @{ Name='PfpDirectVersion'; Value=0x94 },
        @{ Name='PfpDirectStagedDwords'; Value=65792 },
        @{ Name='PfpDirectStagingMismatches'; Value=0 },
        @{ Name='PfpDirectPcSamples'; Value=1 },
        @{ Name='PfpDirectPortsProgrammed'; Value=0 },
        @{ Name='PfpDirectRegRestore'; Value=1 },
        @{ Name='PfpDirectMemRestore'; Value=1 },
        @{ Name='PfpDirectPhysicalReady'; Value=1 },
        @{ Name='SdmaF32Status'; Value=0 },
        @{ Name='SdmaF32F32Applied'; Value=0 },
        @{ Name='SdmaF32ApplyReadback'; Value=1 },
        @{ Name='SdmaF32Restore'; Value=1 },
        @{ Name='SdmaF32PhysicalReady'; Value=1 },
        @{ Name='SdmaRingWorkStatus'; Value=0 },
        @{ Name='SdmaRingFixedReservationStatus'; Value=5 },
        @{ Name='SdmaRingAllocationMode'; Value=2 },
        @{ Name='SdmaRingRptrBefore'; Value=0 },
        @{ Name='SdmaRingRptrRegister'; Value=68 },
        @{ Name='SdmaRingWptr'; Value=68 },
        @{ Name='SdmaRingWptrRegister'; Value=68 },
        @{ Name='SdmaRingWptrShadow'; Value=68 },
        @{ Name='SdmaRingHeader'; Value=2 },
        @{ Name='SdmaRingFencePoison'; Value=0x43DAFFFE },
        @{ Name='SdmaRingFenceExpected'; Value=[uint64]3156541731 },
        @{ Name='SdmaRingFenceObserved'; Value=[uint64]3156541731 },
        @{ Name='SdmaRingComparedDwords'; Value=4 },
        @{ Name='SdmaRingMismatches'; Value=0 },
        @{ Name='SdmaRingRegRestore'; Value=1 },
        @{ Name='SdmaRingMemRestore'; Value=1 },
        @{ Name='SdmaRingPhysicalReady'; Value=1 }
    )) {
        $property = $probe.PSObject.Properties[$expected.Name]
        if ($null -eq $property -or [uint32]$property.Value -ne [uint32]$expected.Value) {
            $errors.Add("telemetria inesperada: $($expected.Name)")
        }
    }
    $ackAfter = $probe.PSObject.Properties['TlbAckAfter']
    if ($null -eq $ackAfter -or
        [uint32]$ackAfter.Value -eq [uint32]::MaxValue -or
        ([uint32]$ackAfter.Value -band [uint32]0x100) -eq 0) {
        $errors.Add('ACK TLB fisico ausente, all-ones ou sem o bit do VMID 8')
    }
    $pollCount = $probe.PSObject.Properties['TlbPollCount']
    if ($null -eq $pollCount -or [uint32]$pollCount.Value -eq 0 -or
        [uint32]$pollCount.Value -gt 1000) {
        $errors.Add('contagem de polls TLB fora de 1..1000')
    }
    if (([uint32]$probe.GcvmVmid8CntlBefore -band 1) -ne 0) {
        $errors.Add('VMID8 estava habilitado durante a transacao que deveria ser inerte')
    }
    $pte = ([uint64][uint32]$probe.GcvmFirstPteHigh32 -shl 32) -bor
        [uint64][uint32]$probe.GcvmFirstPteLow32
    $expectedTarget = ([uint64][uint32]$probe.FbOffset16M -shl 24) +
        [uint64][uint32]$probe.GcvmTargetOffsetLow32
    if (($pte -band [uint64]0xFF) -ne [uint64]0x61 -or
        ($pte -band [uint64]0x0000FFFFFFFFF000) -ne $expectedTarget) {
        $errors.Add('PTE GCVM nao corresponde ao buffer owned e flags VALID|READ|WRITE')
    }
    $ringMc = ([uint64][uint32]$probe.SdmaRingRingHigh -shl 32) -bor
        [uint64][uint32]$probe.SdmaRingRingLow
    $workMc = ([uint64][uint32]$probe.SdmaRingWorkHigh -shl 32) -bor
        [uint64][uint32]$probe.SdmaRingWorkLow
    if ($ringMc -eq 0 -or $workMc -ne ($ringMc + [uint64]0x1000)) {
        $errors.Add('ring/work nao formam o layout contiguo relativo esperado')
    }
    $tosReadyMask = [uint32]2147483648
    $pspAckMask = [uint32]2147549183
    $pspAckValue = [uint32]2147483648
    if (([uint32]$probe.PspRingC2pmsg64Before -band $tosReadyMask) -eq 0 -or
        [uint32]$probe.PspRingCreateAck -eq [uint32]::MaxValue -or
        ([uint32]$probe.PspRingCreateAck -band $pspAckMask) -ne $pspAckValue -or
        [uint32]$probe.PspRingDestroyAck -eq [uint32]::MaxValue -or
        ([uint32]$probe.PspRingDestroyAck -band $pspAckMask) -ne $pspAckValue) {
        $errors.Add('ACK fisico de create/destroy do ring PSP ausente')
    }
    foreach ($name in @('PspRingCreatePolls','PspRingDestroyPolls')) {
        if ([uint32]$probe.$name -eq 0 -or [uint32]$probe.$name -gt 500) {
            $errors.Add("poll PSP fora de 1..500: $name")
        }
    }
    if ([uint32]$probe.PspLoadFwFencePolls -eq 0 -or
        [uint32]$probe.PspLoadFwFencePolls -gt 500) {
        $errors.Add('poll do fence PSP LOAD_IP_FW fora de 1..500')
    }
    if ([uint32]$probe.RlcAlreadyActive -eq 0 -and
        ([uint32]$probe.PspRlcLoadFencePolls -eq 0 -or
         [uint32]$probe.PspRlcLoadFencePolls -gt 500 -or
         [uint32]$probe.PspRlcLoadBytes -ne 25088 -or
         [uint32]$probe.PspRlcLoadFenceObserved -ne 1 -or
         [uint32]$probe.PspRlcLoadResponseStatus -ne 0 -or
         [uint32]$probe.PspRlcLoadDestroyPassed -ne 1 -or
         [uint32]$probe.PspRlcLoadRestorePassed -ne 1)) {
        $errors.Add('transacao PSP RLC nova incompleta')
    }
    if ([uint32]$probe.PspLoadFwChecksumAfter -eq 0 -or
        [uint32]$probe.PspLoadFwChecksumAfter -eq [uint32]::MaxValue) {
        $errors.Add('checksum fisico SDMA ausente, zero ou all-ones')
    }
    foreach ($name in @('PspLoadFwCreateAck','PspLoadFwDestroyAck')) {
        if ([uint32]$probe.$name -eq [uint32]::MaxValue -or
            ([uint32]$probe.$name -band $pspAckMask) -ne $pspAckValue) {
            $errors.Add("ACK PSP LOAD_IP_FW invalido: $name")
        }
    }
    if ([uint32]$probe.SdmaF32PcTransitions -eq 0) {
        $errors.Add('PC fisico da F32 SDMA permaneceu estatico')
    }
    if (([uint32]$probe.SdmaF32ControlApplied -band [uint32]0x00040000) -eq 0) {
        $errors.Add('readback do CNTL SDMA nao contem a mascara aplicada')
    }
    if ([uint32]$probe.SdmaRingPolls -eq 0 -or
        [uint32]$probe.SdmaRingPolls -gt 1000) {
        $errors.Add('poll do ring SDMA fora de 1..1000')
    }
    if ([uint32]$probe.SdmaRingRptrRegister -ne [uint32]$probe.SdmaRingWptr) {
        $errors.Add('RPTR MMIO fisico da SDMA nao alcancou WPTR')
    }
    if ([uint32]$probe.SdmaRingFenceObserved -eq
        [uint32]$probe.SdmaRingFencePoison) {
        $errors.Add('fence SDMA permaneceu no poison')
    }
}

Write-Host 'BC250 MCDM FAIL-CLOSED BINDING AUDIT'
Write-Host "  published INF: $publishedName"
if ($devices.Count -eq 1) {
    Write-Host "  device service/code: $($devices[0].Service) / $($devices[0].ConfigManagerErrorCode)"
}
Write-Host "  driver hash matches package: $($errors -notcontains 'bc250mcdm.sys ativo diverge do pacote auditado')"
if ($null -ne $probe) {
    Write-Host "  lifecycle: DriverEntry=$($probe.DriverEntryReached) Add=$($probe.AddDeviceEntered) Start=$($probe.StartDeviceEntered) Stop=$($probe.StopDeviceEntered) Remove=$($probe.RemoveDeviceEntered)"
    Write-Host "  PCI/BAR/topology: $($probe.PciBytesRead) bytes / $($probe.ResolvedBarCount) BARs / status $($probe.TopologyStatus)"
    Write-Host "  VRAM/visible/ReBAR MiB: $($probe.VramTotalMiB) / $($probe.CpuVisibleMiB) / $($probe.RebarAssignedMiB)"
    Write-Host "  discovery TMR: status=$($probe.DiscoveryTmrStatus) bytes=$($probe.DiscoveryTmrBytes) (5=NOT_REPORTED)"
    Write-Host "  runtime missing: $($probe.RuntimeMissing)"
    Write-Host "  TLB: status=$($probe.TlbTransactionStatus) vmid=$($probe.TlbVmid) engine=$($probe.TlbEngineId) ackBefore=$('{0:X8}' -f [uint32]$probe.TlbAckBefore) ackAfter=$('{0:X8}' -f [uint32]$probe.TlbAckAfter) polls=$($probe.TlbPollCount) restore=$($probe.TlbRangeRestorePassed)"
    Write-Host "  GCVM inert: backing=$($probe.GcvmBackingStatus) vmid8=$($probe.GcvmVmid8Status) pt=$('{0:X8}{1:X8}' -f [uint32]$probe.GcvmPtPhysicalHigh32,[uint32]$probe.GcvmPtPhysicalLow32) readback=$($probe.GcvmBackingReadback)/$($probe.GcvmVmid8Readback) restore=$($probe.GcvmBackingRestore)/$($probe.GcvmVmid8Restore)"
    Write-Host "  PSP ring: status=$($probe.PspRingStatus) pa=$('{0:X8}' -f [uint32]$probe.PspRingPhysicalLow32) createAck=$('{0:X8}' -f [uint32]$probe.PspRingCreateAck) destroyAck=$('{0:X8}' -f [uint32]$probe.PspRingDestroyAck) restore=$($probe.PspRingRestorePassed)"
    Write-Host "  PSP SDMA FW: status=$($probe.PspLoadFwStatus) bytes=$($probe.PspLoadFwBytes) type=$($probe.PspLoadFwType) fence=$($probe.PspLoadFwFenceObserved)/$($probe.PspLoadFwFenceExpected) polls=$($probe.PspLoadFwFencePolls) response=$($probe.PspLoadFwResponseStatus) checksum=$('{0:X8}' -f [uint32]$probe.PspLoadFwChecksumAfter) destroy/restore=$($probe.PspLoadFwDestroyPassed)/$($probe.PspLoadFwRestorePassed)"
    Write-Host "  PSP RLC FW: status=$($probe.PspRlcLoadStatus) bytes=$($probe.PspRlcLoadBytes) type=$($probe.PspRlcLoadType) fence=$($probe.PspRlcLoadFenceObserved)/$($probe.PspRlcLoadFenceExpected) polls=$($probe.PspRlcLoadFencePolls) response=$($probe.PspRlcLoadResponseStatus) destroy/restore=$($probe.PspRlcLoadDestroyPassed)/$($probe.PspRlcLoadRestorePassed)"
    Write-Host "  RLC: CNTL=$('{0:X8}' -f [uint32]$probe.RlcCntlBefore)->$('{0:X8}' -f [uint32]$probe.RlcCntlAfter) GPM=$('{0:X8}' -f [uint32]$probe.RlcGpmBefore)->$('{0:X8}' -f [uint32]$probe.RlcGpmAfter) ROM=$($probe.RlcExecRomBefore)->$($probe.RlcExecRomAfter) F32=$($probe.RlcEnableF32After) already=$($probe.RlcAlreadyActive) restore=$($probe.RlcCntlRestore)"
    Write-Host "  PFP direct: status=$($probe.PfpDirectStatus) mc=$('{0:X8}{1:X8}' -f [uint32]$probe.PfpDirectMcHigh,[uint32]$probe.PfpDirectMcLow) staged=$($probe.PfpDirectStagedDwords)dw/$($probe.PfpDirectStagingMismatches)mismatch JT=$($probe.PfpDirectJtDwords) version=$('{0:X}' -f [uint32]$probe.PfpDirectVersion)"
    Write-Host "  PFP physical: PC=$('{0:X8}' -f [uint32]$probe.PfpDirectPcBefore)->$('{0:X8}' -f [uint32]$probe.PfpDirectPcFirst)->$('{0:X8}' -f [uint32]$probe.PfpDirectPcLast) transitions=$($probe.PfpDirectPcTransitions)/$($probe.PfpDirectPcSamples) HALT=$('{0:X8}' -f [uint32]$probe.PfpDirectMeCntlBefore)->$('{0:X8}' -f [uint32]$probe.PfpDirectMeCntlApplied)->$('{0:X8}' -f [uint32]$probe.PfpDirectMeCntlAfter) fault=$('{0:X8}' -f [uint32]$probe.PfpDirectFaultBefore)->$('{0:X8}' -f [uint32]$probe.PfpDirectFaultAfter) restore=$($probe.PfpDirectRegRestore)/$($probe.PfpDirectMemRestore)"
    Write-Host "  PSP buffers: ring=$('{0:X8}{1:X8}' -f [uint32]$probe.PspLoadFwRingHigh,[uint32]$probe.PspLoadFwRingLow) cmd=$('{0:X8}{1:X8}' -f [uint32]$probe.PspLoadFwCommandHigh,[uint32]$probe.PspLoadFwCommandLow) fence=$('{0:X8}{1:X8}' -f [uint32]$probe.PspLoadFwFenceHigh,[uint32]$probe.PspLoadFwFenceLow) payload=$('{0:X8}{1:X8}' -f [uint32]$probe.PspLoadFwPayloadHigh,[uint32]$probe.PspLoadFwPayloadLow)"
    Write-Host "  SDMA F32: status=$($probe.SdmaF32Status) pc=$($probe.SdmaF32PcFirst)->$($probe.SdmaF32PcLast) transitions=$($probe.SdmaF32PcTransitions) readback/restore=$($probe.SdmaF32ApplyReadback)/$($probe.SdmaF32Restore)"
    Write-Host "  SDMA ring: status=$($probe.SdmaRingWorkStatus) RPTR-mmio/wb/WPTR=$($probe.SdmaRingRptrRegister)/$($probe.SdmaRingRptrAfter)/$($probe.SdmaRingWptr) fence=$('{0:X8}' -f [uint32]$probe.SdmaRingFencePoison)->$('{0:X8}' -f [uint32]$probe.SdmaRingFenceObserved) data=$($probe.SdmaRingComparedDwords)dw/$($probe.SdmaRingMismatches)mismatch polls=$($probe.SdmaRingPolls) restore=$($probe.SdmaRingRegRestore)/$($probe.SdmaRingMemRestore)"
    Write-Host "  SDMA allocation: fixedStatus=$($probe.SdmaRingFixedReservationStatus) mode=$($probe.SdmaRingAllocationMode) ring=$('{0:X8}{1:X8}' -f [uint32]$probe.SdmaRingRingHigh,[uint32]$probe.SdmaRingRingLow) work=$('{0:X8}{1:X8}' -f [uint32]$probe.SdmaRingWorkHigh,[uint32]$probe.SdmaRingWorkLow)"
    Write-Host "  SDMA publish: WPTR reg/shadow=$($probe.SdmaRingWptrRegister)/$($probe.SdmaRingWptrShadow) RB_CNTL=$('{0:X8}' -f [uint32]$probe.SdmaRingRbCntl)"
    Write-Host "  SDMA visibility: header=$('{0:X8}' -f [uint32]$probe.SdmaRingHeader) RPTR-reg=$($probe.SdmaRingRptrRegister) poll=$('{0:X8}' -f [uint32]$probe.SdmaRingPollCntl)"
    Write-Host "  SDMA diagnostic: status1/2/3=$('{0:X8}' -f [uint32]$probe.SdmaRingStatus1After)/$('{0:X8}' -f [uint32]$probe.SdmaRingStatus2After)/$('{0:X8}' -f [uint32]$probe.SdmaRingStatus3After) error=$('{0:X8}' -f [uint32]$probe.SdmaRingErrorLogAfter)"
    Write-Host "  SDMA UTCL: rd/wr=$('{0:X8}' -f [uint32]$probe.SdmaRingUtcl1ReadStatusAfter)/$('{0:X8}' -f [uint32]$probe.SdmaRingUtcl1WriteStatusAfter) rx=$('{0:X8}' -f [uint32]$probe.SdmaRingUtcl1ReadXnack0After):$('{0:X8}' -f [uint32]$probe.SdmaRingUtcl1ReadXnack1After) wx=$('{0:X8}' -f [uint32]$probe.SdmaRingUtcl1WriteXnack0After):$('{0:X8}' -f [uint32]$probe.SdmaRingUtcl1WriteXnack1After) timeout=$('{0:X8}' -f [uint32]$probe.SdmaRingUtcl1TimeoutAfter)"
}

if ($errors.Count -ne 0) {
    Write-Host 'BC250 MCDM FAIL-CLOSED BINDING AUDIT: BLOCKED'
    foreach ($errorMessage in $errors) { Write-Host "  - $errorMessage" }
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 1
}
Write-Host 'BC250 MCDM FAIL-CLOSED BINDING AUDIT: PASS'
Write-Host 'PFP STAGING ISOLADO; PM4/COMPUTE NAO EXECUTADOS; NAO VALIDADO EM HARDWARE'
exit 0
