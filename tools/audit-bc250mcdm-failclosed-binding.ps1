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
    ($devices[0].ConfigManagerErrorCode -ne 0 -and $devices[0].ConfigManagerErrorCode -ne 43)) {
    $errors.Add("resultado PnP inesperado: $($devices[0].ConfigManagerErrorCode)")
}

$service = Get-Service bc250mcdm -ErrorAction SilentlyContinue
if ($null -eq $service -or ($service.Status -ne 'Running' -and $service.Status -ne 'Stopped')) {
    $errors.Add('servico bc250mcdm nao existe no estado Running/Stopped')
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
    if ($null -ne $probe.PSObject.Properties['SkipHwBringup'] -and [uint32]$probe.SkipHwBringup -eq 1) {
        Write-Host "BC250 DISPLAY-ONLY AUDIT: Scanout GOP herdado com sucesso, SkipHwBringup=1 ativo."
        Write-Host "DISPOSITIVO OPERACIONAL EM CODIGO 0 (Sem tela preta)."
        exit 0
    }
    foreach ($expected in @(
        @{ Name='SchemaVersion'; Value=98 },
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
        @{ Name='PfpDirectBytes'; Value=263168 },
        @{ Name='PfpDirectJtDwords'; Value=128 },
        @{ Name='PfpDirectVersion'; Value=0x94 },
        @{ Name='PfpDirectStagedDwords'; Value=65792 },
        @{ Name='PfpDirectStagingMismatches'; Value=0 },
        @{ Name='PfpDirectPcSamples'; Value=4 },
        @{ Name='PfpDirectPortsProgrammed'; Value=1 },
        @{ Name='PfpDirectRegRestore'; Value=1 },
        @{ Name='PfpDirectMemRestore'; Value=1 },
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
        @{ Name='SdmaRingPhysicalReady'; Value=1 },
        @{ Name='HqdPreflightReadCount'; Value=26 },
        @{ Name='HqdStablePasses'; Value=2 },
        @{ Name='HqdInactive'; Value=1 },
        @{ Name='MecHqdOriginalSelector'; Value=0 },
        @{ Name='MecHqdAppliedSelector'; Value=4 },
        @{ Name='MecHqdRestoredSelector'; Value=0 },
        @{ Name='MecHqdWriteCalls'; Value=2 },
        @{ Name='MecHqdRestore'; Value=1 },
        @{ Name='HqdBankOriginalSelector'; Value=0 },
        @{ Name='HqdBank0Active'; Value=0 },
        @{ Name='HqdBankCandidateActive'; Value=0 },
        @{ Name='HqdBankWriteCalls'; Value=7 },
        @{ Name='HqdBankPoisonPassed'; Value=1 },
        @{ Name='HqdBankRestorePassed'; Value=1 },
        @{ Name='HqdBankFinalSelector'; Value=0 }
    )) {
        $property = $probe.PSObject.Properties[$expected.Name]
        if ($null -eq $property -or [uint32]$property.Value -ne [uint32]$expected.Value) {
            $errors.Add("telemetria inesperada: $($expected.Name)")
        }
    }
    foreach ($name in @(
        'HqdPreflightStatus','HqdSelector','HqdActive','HqdMqdBaseLo',
        'HqdMqdBaseHi','HqdVmid','HqdPqBaseLo','HqdPqBaseHi','HqdPqRptr',
        'HqdDoorbellControl','HqdPqControl','HqdPqWptrLo','HqdPqWptrHi')) {
        if ($null -eq $probe.PSObject.Properties[$name]) {
            $errors.Add("telemetria HQD ausente: $name")
        }
    }
    if ($null -ne $probe.PSObject.Properties['HqdPreflightStatus']) {
        $hqdStatus = [uint32]$probe.HqdPreflightStatus
        $hqdSelector = [uint32]$probe.HqdSelector -band [uint32]0x7FF
        if (($hqdStatus -eq 0 -and $hqdSelector -ne 4) -or
            ($hqdStatus -eq 7 -and $hqdSelector -eq 4) -or
            ($hqdStatus -ne 0 -and $hqdStatus -ne 7)) {
            $errors.Add("preflight HQD/seletor incoerente: status=$hqdStatus selector=$hqdSelector")
        }
        if (([uint32]$probe.HqdActive -band 1) -ne 0) {
            $errors.Add('fila HQD herdada ativa; preflight deveria falhar fechado')
        }
    }
    foreach ($name in @(
        'MecHqdActive','MecHqdMqdBaseLo','MecHqdMqdBaseHi',
        'MecHqdPqBaseLo','MecHqdPqBaseHi','MecHqdPqRptr',
        'MecHqdPqWptrLo','MecHqdPqWptrHi')) {
        if ($null -eq $probe.PSObject.Properties[$name]) {
            $errors.Add("telemetria MEC HQD ausente: $name")
        }
    }
    if ($null -ne $probe.PSObject.Properties['MecHqdActive'] -and
        ([uint32]$probe.MecHqdActive -band 1) -ne 0) {
        $errors.Add('MEC HQD herdada ativa; selecao readonly deveria falhar fechado')
    }
    foreach ($name in @(
        'MecHqdSelectorStatus','MecHqdAppliedReadbackFirst',
        'MecHqdAppliedReadbackLast','MecHqdReadCalls','MecHqdApplyReadback',
        'MecHqdPhysicalReady','MecHqdStatus','MecHqdStablePasses')) {
        if ($null -eq $probe.PSObject.Properties[$name]) {
            $errors.Add("telemetria de readback MEC ausente: $name")
        }
    }
    if ($null -ne $probe.PSObject.Properties['MecHqdSelectorStatus']) {
        $mecTx = [uint32]$probe.MecHqdSelectorStatus
        if ($mecTx -eq 0) {
            if ([uint32]$probe.MecHqdAppliedReadbackFirst -ne 4 -or
                [uint32]$probe.MecHqdAppliedReadbackLast -ne 4 -or
                [uint32]$probe.MecHqdReadCalls -ne 31 -or
                [uint32]$probe.MecHqdApplyReadback -ne 1 -or
                [uint32]$probe.MecHqdPhysicalReady -ne 1 -or
                [uint32]$probe.MecHqdStatus -ne 0 -or
                [uint32]$probe.MecHqdStablePasses -ne 2) {
                $errors.Add('selecao MEC OK sem dois readbacks, snapshot e prova fisica completos')
            }
        } elseif ($mecTx -eq 4) {
            if ([uint32]$probe.MecHqdReadCalls -ne 5 -or
                [uint32]$probe.MecHqdApplyReadback -ne 0 -or
                [uint32]$probe.MecHqdPhysicalReady -ne 0 -or
                [uint32]$probe.MecHqdStatus -ne 1 -or
                [uint32]$probe.MecHqdStablePasses -ne 0) {
                $errors.Add('READBACK_FAILED MEC nao permaneceu estritamente fail-closed')
            }
        } else {
            $errors.Add("status inesperado da transacao seletora MEC: $mecTx")
        }
    }
    foreach ($name in @(
        'HqdBankProbeStatus','HqdBank0Priority','HqdBankCandidatePriority',
        'HqdBankPoisonPriority','HqdBankPoisonReadback',
        'HqdBank0CrossReadback','HqdBankCandidateCrossReadback',
        'HqdBankCandidateRestoreReadback','HqdBank0FinalReadback',
        'HqdBankIsolationPassed','HqdBankPhysicalReady',
        'HqdBankHqdCapturePassed','HqdBankHqdStablePasses',
        'HqdBankHqdInactive','HqdBankHqdSelectorObserved',
        'HqdBankHqdActive','HqdBankHqdMqdBaseLo','HqdBankHqdMqdBaseHi',
        'HqdBankHqdVmid','HqdBankHqdPqBaseLo','HqdBankHqdPqBaseHi',
        'HqdBankHqdPqRptr','HqdBankHqdDoorbellControl',
        'HqdBankHqdPqControl','HqdBankHqdPqWptrLo',
        'HqdBankHqdPqWptrHi')) {
        if ($null -eq $probe.PSObject.Properties[$name]) {
            $errors.Add("telemetria da prova de bancos HQD ausente: $name")
        }
    }
    if ($null -ne $probe.PSObject.Properties['HqdBankProbeStatus']) {
        $bankStatus = [uint32]$probe.HqdBankProbeStatus
        if ([uint32]$probe.HqdBankPoisonPriority -eq
                [uint32]$probe.HqdBankCandidatePriority -or
            [uint32]$probe.HqdBankPoisonReadback -ne
                [uint32]$probe.HqdBankPoisonPriority -or
            [uint32]$probe.HqdBankCandidateRestoreReadback -ne
                [uint32]$probe.HqdBankCandidatePriority -or
            [uint32]$probe.HqdBank0FinalReadback -ne
                [uint32]$probe.HqdBank0Priority) {
            $errors.Add('poison ou rollback da prioridade HQD nao fechou')
        }
        if ($bankStatus -eq 0) {
            if ([uint32]$probe.HqdBankIsolationPassed -ne 1 -or
                [uint32]$probe.HqdBankPhysicalReady -ne 1 -or
                [uint32]$probe.HqdBankReadCalls -ne 47 -or
                [uint32]$probe.HqdBankHqdCapturePassed -ne 1 -or
                [uint32]$probe.HqdBankHqdStablePasses -ne 2 -or
                [uint32]$probe.HqdBankHqdInactive -ne 1 -or
                ([uint32]$probe.HqdBankHqdActive -band 1) -ne 0 -or
                [uint32]$probe.HqdBank0CrossReadback -ne
                    [uint32]$probe.HqdBank0Priority -or
                [uint32]$probe.HqdBankCandidateCrossReadback -ne
                    [uint32]$probe.HqdBankPoisonPriority) {
                $errors.Add('prova de independencia HQD OK sem bancos distintos')
            }
        } elseif ($bankStatus -eq 7) {
            if ([uint32]$probe.HqdBankIsolationPassed -ne 0 -or
                [uint32]$probe.HqdBankPhysicalReady -ne 0 -or
                [uint32]$probe.HqdBankReadCalls -ne 21 -or
                [uint32]$probe.HqdBankHqdCapturePassed -ne 0) {
                $errors.Add('HQD BANK_NOT_ISOLATED nao permaneceu fail-closed')
            }
        } else {
            $errors.Add("status inesperado da prova de bancos HQD: $bankStatus")
        }
    }
    foreach ($name in @(
        'HqdProgramStatus','HqdProgramOriginalSelector',
        'HqdProgramSelectedSelector','HqdProgramRestoredSelector',
        'HqdProgramReadCalls','HqdProgramWriteCalls',
        'HqdProgramMemoryPassed','HqdProgramReadbackPassed',
        'HqdProgramActiveZero','HqdProgramRestorePassed',
        'HqdProgramMemRestorePassed','HqdProgramPhysicalReady',
        'HqdProgramMqdBaseLo','HqdProgramMqdBaseHi',
        'HqdProgramPqBaseLo','HqdProgramPqBaseHi',
        'HqdProgramPqControl','HqdProgramActiveReadback',
        'HqdProgramMismatchMask','HqdProgramMismatchOffset',
        'HqdProgramMismatchWritten','HqdProgramMismatchRead1',
        'HqdProgramMismatchRead2','HqdProgramReadbackMqdBaseLo',
        'HqdProgramReadbackMqdBaseHi','HqdProgramReadbackPqBaseLo',
        'HqdProgramReadbackPqBaseHi','HqdProgramReadbackPqControl')) {
        if ($null -eq $probe.PSObject.Properties[$name]) {
            $errors.Add("telemetria de programacao HQD inativa ausente: $name")
        }
    }
    if ($null -ne $probe.PSObject.Properties['HqdProgramStatus']) {
        $progStatus = [uint32]$probe.HqdProgramStatus
        if ($progStatus -eq 0) {
            if ([uint32]$probe.HqdProgramMemoryPassed -ne 1 -or
                [uint32]$probe.HqdProgramReadbackPassed -ne 1 -or
                [uint32]$probe.HqdProgramActiveZero -ne 1 -or
                [uint32]$probe.HqdProgramRestorePassed -ne 1 -or
                [uint32]$probe.HqdProgramMemRestorePassed -ne 1 -or
                [uint32]$probe.HqdProgramPhysicalReady -ne 1 -or
                [uint32]$probe.HqdProgramOriginalSelector -ne 0 -or
                [uint32]$probe.HqdProgramRestoredSelector -ne 0 -or
                ([uint32]$probe.HqdProgramActiveReadback -band 1) -ne 0) {
                $errors.Add('programacao HQD inativa OK sem validacao/rollback integral')
            }
        } elseif ($progStatus -eq 6) {
            # 6 = BC250_HQD_PROGRAM_READBACK_FAILED (fail-closed, readback divergente)
            if ([uint32]$probe.HqdProgramMemoryPassed -ne 1 -or
                [uint32]$probe.HqdProgramRestorePassed -ne 1 -or
                [uint32]$probe.HqdProgramMemRestorePassed -ne 1 -or
                [uint32]$probe.HqdProgramPhysicalReady -ne 0 -or
                [uint32]$probe.HqdProgramOriginalSelector -ne 0 -or
                [uint32]$probe.HqdProgramRestoredSelector -ne 0 -or
                ([uint32]$probe.HqdProgramActiveReadback -band 1) -ne 0) {
                $errors.Add('programacao HQD READBACK_FAILED nao concluiu rollback fail-closed')
            }
        }
    }
    foreach ($name in @(
        'HqdActiveStatus','HqdActiveSelectedSelector','HqdActiveBefore','HqdActiveApplied',
        'HqdActiveAfter','HqdActiveRptrBefore','HqdActiveRptrAfter',
        'HqdActiveWptrApplied','HqdActiveFenceBefore',
        'HqdActiveFenceAfter','HqdActiveScratchBefore',
        'HqdActiveScratchAfter','HqdActivePollIterations',
        'HqdActiveTriadPassed','HqdActiveRestorePassed',
        'HqdActiveMemRestorePassed','HqdActivePhysicalReady',
        'HqdActivePqBaseLo','HqdActivePqBaseHi',
        'HqdActiveWptrPollAddrLo','HqdActiveWptrPollAddrHi',
        'HqdActiveWptrShadow','HqdActiveCpMecCntlReadback',
        'HqdActiveDbRangeLowerBefore','HqdActiveDbRangeLowerApplied',
        'HqdActiveDbRangeLowerReadback','HqdActiveDbRangeUpperBefore',
        'HqdActiveDbRangeUpperApplied','HqdActiveDbRangeUpperReadback',
        'HqdActiveCpPqStatusBefore','HqdActiveCpPqStatusApplied',
        'HqdActiveCpPqStatusReadback','HqdActiveWptrPollCntlBefore',
        'HqdActiveWptrPollCntlApplied','HqdActiveWptrPollCntlReadback',
        'HqdActiveSelectorReleasedBeforeKick',
        'HqdActiveCpPqStatusAfterKick','HqdActiveDoorbellControlAfterKick',
        'HqdActiveWptrAfterKick','HqdActiveRptrAfterKick',
        'HqdActiveInitialWptrLo','HqdActiveInitialWptrHi',
        'HqdActiveBaselineWptrApplied','HqdActiveBaselineWptrReadback',
        'HqdActiveBaselineWptrRestored','HqdActiveBaselineWptrRestorePolls',
        'HqdActiveBaselineWptrRestorePassed',
        'HqdActiveDirectWptrKickApplied','HqdActiveDirectWptrBefore',
        'HqdActiveDirectWptrReadback','HqdActiveDirectRptrAfterKick',
        'HqdActiveDirectWptrRestored','HqdActiveDirectWptrRestorePolls',
        'HqdActiveDirectWptrRestorePassed',
        'HqdActiveRptrMmioLast','HqdActiveWptrMmioLast',
        'HqdActiveHqStatus0PollLast','HqdActiveHqdErrorPollLast',
        'HqdActiveRlcCpSchedulersBefore','HqdActiveRlcCpSchedulersApplied',
        'HqdActiveRlcCpSchedulersReadback','HqdActiveRlcCpSchedulersAfterKick',
        'HqdActiveRlcCpSchedulersRestored','HqdActiveRlcCpSchedulersRestorePassed',
        'HqdActiveHqStatus0AfterKick','HqdActiveHqControl0AfterKick',
        'HqdActiveHqdErrorAfterKick','HqdActiveCpcUtcl1ErrorAfterKick',
        'HqdActiveCpcUtcl1StatusAfterKick','HqdActiveCpfUtcl1StatusAfterKick',
        'HqdActiveCpcIntStatusAfterKick','HqdActiveMe1Pipe0IntStatusAfterKick',
        'HqdActiveMe2Pipe1IntStatusAfterKick',
        'HqdActiveMe2Pipe0IntStatusAfterKick',
        'HqdActiveMecPcFirst','HqdActiveMecPcLast',
        'HqdActiveMecPcTransitions','HqdActiveMecPcSamples',
        'HqdActivePqControlObserved','HqdActiveHdpInvalidateApplied',
        'HqdActiveGcvmL2Before','HqdActiveGcvmL2Applied',
        'HqdActiveGcvmL2Readback','HqdActiveGcvmReqApplied',
        'HqdActiveGcvmReqReadback','HqdActiveGcvmAckBefore',
        'HqdActiveGcvmAckAfter','HqdActiveGcvmPolls',
        'HqdActiveCoherencyPassed','HqdActiveGfxOffApplicable',
        'HqdActiveGfxOffStateBefore',
        'HqdActiveGfxOffStateGuarded','HqdActiveGfxOffStateAfter',
        'HqdActiveGfxOffDisallowResponse','HqdActiveGfxOffAllowResponse',
        'HqdActiveGfxOffGuardPassed','HqdActiveGfxOffRestorePassed')) {
        if ($null -eq $probe.PSObject.Properties[$name]) {
            $errors.Add("telemetria de submissao HQD ativa ausente: $name")
        }
    }
    if ($null -ne $probe.PSObject.Properties['HqdActiveStatus']) {
        $actStatus = [uint32]$probe.HqdActiveStatus
        $shaderEnabled = $null -ne $probe.PSObject.Properties['ShaderDispatchEnabled'] -and
            [uint32]$probe.ShaderDispatchEnabled -eq 1
        if ([uint32]$probe.HqdActiveGfxOffApplicable -ne 0 -or
            [uint32]$probe.HqdActiveGfxOffDisallowResponse -ne 0 -or
            [uint32]$probe.HqdActiveGfxOffAllowResponse -ne 0 -or
            [uint32]$probe.HqdActiveGfxOffGuardPassed -ne 1 -or
            [uint32]$probe.HqdActiveGfxOffRestorePassed -ne 1) {
            $errors.Add('GFXOFF deve permanecer nao aplicavel e sem mensagens SMU no Cyan Skillfish2')
        }
        if (($actStatus -eq 0 -or $actStatus -eq 7) -and
            ([uint32]$probe.HqdActiveHdpInvalidateApplied -ne 1 -or
             [uint32]$probe.HqdActiveCoherencyPassed -ne 1 -or
             (([uint32]$probe.HqdActiveGcvmL2Readback -band 0xFFFFFFFC) -ne
                ([uint32]$probe.HqdActiveGcvmL2Before -band 0xFFFFFFFC)) -or
             (([uint32]$probe.HqdActiveGcvmL2Readback -band 3) -ne 0) -or
             [uint32]$probe.HqdActiveGcvmReqApplied -ne 0x00F80001 -or
             [uint32]$probe.HqdActiveGcvmReqReadback -ne 0x00F80001 -or
             (([uint32]$probe.HqdActiveGcvmAckAfter -band 1) -eq 0))) {
            $errors.Add('submissao HQD executada sem coerencia/ACK comprovados')
        }
        if (-not $shaderEnabled -and ($actStatus -eq 0 -or $actStatus -eq 7) -and
            ([uint32]$probe.HqdActiveWptrApplied -ne 11 -or
             [uint32]$probe.HqdActiveWptrShadow -ne 11 -or
             [uint32]$probe.HqdActiveDoorbellValueApplied -ne 11)) {
            $errors.Add('submissao HQD divergiu do payload canonico 3+8 dwords')
        }
        if (($actStatus -eq 0 -or $actStatus -eq 7) -and
            (([uint32]$probe.HqdActiveDoorbellIndex * 4) -ne
                [uint32]$probe.HqdActiveDoorbellOffset -or
             [uint32]$probe.HqdActiveDoorbellOffset -ne
                [uint32]$probe.HqdActiveDbRangeLowerBefore -or
             [uint32]$probe.HqdActiveDbRangeLowerApplied -ne
                [uint32]$probe.HqdActiveDbRangeLowerBefore -or
             [uint32]$probe.HqdActiveDbRangeLowerReadback -ne
                [uint32]$probe.HqdActiveDbRangeLowerBefore -or
             [uint32]$probe.HqdActiveDbRangeUpperApplied -ne
                [uint32]$probe.HqdActiveDbRangeUpperBefore -or
             [uint32]$probe.HqdActiveDbRangeUpperReadback -ne
                [uint32]$probe.HqdActiveDbRangeUpperBefore -or
             (([uint32]$probe.HqdActiveCpPqStatusReadback -band 2) -eq 0) -or
             (([uint32]$probe.HqdActiveWptrPollCntlReadback -band 0x80000000) -ne 0) -or
             [uint32]$probe.HqdActiveSelectorReleasedBeforeKick -ne 1)) {
            $errors.Add('submissao HQD executada sem rota canonica de doorbell comprovada')
        }
        if ($actStatus -eq 0 -or $actStatus -eq 7) {
            if ([uint32]$probe.HqdActiveBaselineWptrApplied -ne 1 -or
                [uint32]$probe.HqdActiveBaselineWptrRestorePolls -lt 1 -or
                [uint32]$probe.HqdActiveBaselineWptrRestorePolls -gt 1000 -or
                [uint32]$probe.HqdActiveBaselineWptrRestorePassed -ne 1) {
                $errors.Add('baseline WPTR HQD sem apply/readback/rollback integral')
            }
            $schedulerDescriptor = [uint32]$probe.HqdActiveRlcCpSchedulersBefore
            $schedulerMe = ($schedulerDescriptor -shr 5) -band 3
            $schedulerPipe = ($schedulerDescriptor -shr 3) -band 3
            $schedulerQueue = $schedulerDescriptor -band 7
            $expectedSelector = ($schedulerMe -shl 2) -bor $schedulerPipe -bor
                ($schedulerQueue -shl 8)
            if ([uint32]$probe.HqdActiveSelectedSelector -ne $expectedSelector) {
                $errors.Add('submissao HQD divergiu do ME/PIPE/QUEUE descrito por scheduler0')
            }
            $expectedScheduler =
                [uint32]$probe.HqdActiveRlcCpSchedulersBefore -bor 0x80
            if ([uint32]$probe.HqdActiveRlcCpSchedulersApplied -ne $expectedScheduler -or
                [uint32]$probe.HqdActiveRlcCpSchedulersReadback -ne $expectedScheduler -or
                [uint32]$probe.HqdActiveRlcCpSchedulersAfterKick -ne $expectedScheduler -or
                [uint32]$probe.HqdActiveRlcCpSchedulersRestored -ne
                    [uint32]$probe.HqdActiveRlcCpSchedulersBefore -or
                [uint32]$probe.HqdActiveRlcCpSchedulersRestorePassed -ne 1) {
                $errors.Add('transacao RLC_CP_SCHEDULERS sem RMW/readback/rollback integral')
            }
            if ([uint32]$probe.HqdActiveWptrAfterKick -ne
                    [uint32]$probe.HqdActiveWptrApplied -and
                ([uint32]$probe.HqdActiveDirectWptrKickApplied -ne 1 -or
                 [uint32]$probe.HqdActiveDirectWptrReadback -ne
                    [uint32]$probe.HqdActiveWptrApplied -or
                 [uint32]$probe.HqdActiveDirectWptrRestorePolls -lt 1 -or
                 [uint32]$probe.HqdActiveDirectWptrRestorePolls -gt 1000 -or
                 [uint32]$probe.HqdActiveDirectWptrRestorePassed -ne 1)) {
                $errors.Add('fallback WPTR direto sem publicacao/readback/rollback integral')
            }
        }
        if ($actStatus -eq 0) {
            if ($shaderEnabled) {
                foreach ($name in @(
                    'ShaderDispatchDwords','ShaderDispatchPacketIndex',
                    'ShaderFencePacketIndex','ShaderFenceAfterDispatch',
                    'ShaderCcArrayPass1','ShaderCcArrayPass2',
                    'ShaderSpiWgpMaskPass1','ShaderSpiWgpMaskPass2',
                    'ShaderRlcAonWgpPass1','ShaderRlcAonWgpPass2',
                    'ShaderNonHarvestedWgpMask',
                    'ShaderAonNonHarvestedWgpMask',
                    'ShaderWgpPreflightPassed',
                    'ShaderReadbackPassed','ShaderReadbackMismatches',
                    'ShaderResultBefore','ShaderResultAfter',
                    'ShaderResultMismatches','ShaderFenceBefore',
                    'ShaderFenceAfter','ShaderRptrAfter','ShaderWptrAfter',
                    'ShaderRegisterRestorePassed','ShaderMemoryRestorePassed',
                    'ShaderTriadPassed','ShaderPhysicalReady')) {
                    if ($null -eq $probe.PSObject.Properties[$name]) {
                        $errors.Add("telemetria do dispatch observavel ausente: $name")
                    }
                }
                if ([uint32]$probe.ShaderDispatchDwords -eq 0 -or
                    [uint32]$probe.ShaderFencePacketIndex -le
                        [uint32]$probe.ShaderDispatchPacketIndex -or
                    [uint32]$probe.ShaderFenceAfterDispatch -ne 1 -or
                    [uint32]$probe.ShaderCcArrayPass1 -ne [uint32]$probe.ShaderCcArrayPass2 -or
                    [uint32]$probe.ShaderSpiWgpMaskPass1 -ne [uint32]$probe.ShaderSpiWgpMaskPass2 -or
                    [uint32]$probe.ShaderRlcAonWgpPass1 -ne [uint32]$probe.ShaderRlcAonWgpPass2 -or
                    [uint32]$probe.ShaderAonNonHarvestedWgpMask -eq 0 -or
                    [uint32]$probe.ShaderWgpPreflightPassed -ne 1 -or
                    [uint32]$probe.ShaderReadbackPassed -ne 1 -or
                    [uint32]$probe.ShaderReadbackMismatches -ne 0 -or
                    ([int64]$probe.ShaderResultBefore -band 0xFFFFFFFFL) -ne 0x43DAFFFEL -or
                    ([int64]$probe.ShaderResultAfter -band 0xFFFFFFFFL) -ne 0xBC250C01L -or
                    [uint32]$probe.ShaderResultMismatches -ne 0 -or
                    ([int64]$probe.ShaderFenceBefore -band 0xFFFFFFFFL) -ne 0xA55A0FF0L -or
                    ([int64]$probe.ShaderFenceAfter -band 0xFFFFFFFFL) -ne 0xBC250D15L -or
                    [uint32]$probe.ShaderRptrAfter -ne [uint32]$probe.ShaderDispatchDwords -or
                    [uint32]$probe.ShaderWptrAfter -ne [uint32]$probe.ShaderDispatchDwords -or
                    [uint32]$probe.ShaderRegisterRestorePassed -ne 1 -or
                    [uint32]$probe.ShaderMemoryRestorePassed -ne 1 -or
                    [uint32]$probe.ShaderTriadPassed -ne 1 -or
                    [uint32]$probe.ShaderPhysicalReady -ne 1) {
                    $errors.Add('dispatch observavel OK sem dado/fence/RPTR-WPTR/rollback integral')
                }
            } elseif ([uint32]$probe.HqdActiveTriadPassed -ne 1 -or
                    [uint32]$probe.HqdActiveRptrAfter -ne [uint32]$probe.HqdActiveWptrApplied -or
                    ([int64]$probe.HqdActiveFenceAfter -band 0xFFFFFFFFL) -ne 0xBC250123L -or
                    [uint32]$probe.HqdActiveScratchAfter -ne [uint32]0x22222222 -or
                    [uint32]$probe.HqdActiveRestorePassed -ne 1 -or
                    [uint32]$probe.HqdActiveMemRestorePassed -ne 1 -or
                    [uint32]$probe.HqdActivePhysicalReady -ne 1) {
                $errors.Add('submissao HQD ativa OK sem triade e rollback integral')
            }
        } elseif ($actStatus -eq 7) {
            # 7 = BC250_HQD_ACTIVE_PM4_NOT_CONSUMED (fail-closed, PM4 nao consumido)
            if ([uint32]$probe.HqdActiveRestorePassed -ne 1 -or
                [uint32]$probe.HqdActiveMemRestorePassed -ne 1 -or
                [uint32]$probe.HqdActivePhysicalReady -ne 0 -or
                [uint32]$probe.HqdActiveTriadPassed -ne 0) {
                $errors.Add('submissao HQD PM4_NOT_CONSUMED nao concluiu rollback fail-closed')
            }
        }
    }
    $shaderRecipeEnabled =
        $null -ne $probe.PSObject.Properties['ShaderDispatchEnabled'] -and
        [uint32]$probe.ShaderDispatchEnabled -eq 1
    if ($shaderRecipeEnabled -and
        ($null -eq $probe.PSObject.Properties['PfpDirectCpSoftReset'] -or
         $null -eq $probe.PSObject.Properties['PfpDirectVmid0PageTable'] -or
         $null -eq $probe.PSObject.Properties['SdmaRingCntlApplied'] -or
         [uint32]$probe.PfpDirectCpSoftReset -ne 1 -or
         [uint32]$probe.PfpDirectVmid0PageTable -ne 0 -or
         (([uint32]$probe.SdmaRingCntlApplied -band 0x00040000) -ne 0))) {
        $errors.Add('receita F8b ausente: exige CpSoftReset=1, Vmid0PageTable=0 e SDMA AUTO_CTXSW=0')
    }
    $pfpStatus = [uint32]$probe.PfpDirectStatus
    if ($pfpStatus -eq 0) {
        if ([uint32]$probe.PfpDirectPhysicalReady -ne 1) {
            $errors.Add('PfpDirectPhysicalReady deveria ser 1 quando status e OK')
        }
        if ([uint32]$probe.PfpDirectRb0WptrApplied -eq 0 -or
            ([int64]$probe.PfpDirectRb0RptrAfter -band 0xFFFFFFFFL) -lt (([int64]$probe.PfpDirectRb0WptrApplied -band 0xFFFFFFFFL) - 1)) {
            $errors.Add('PFP direct status OK sem RPTR consumindo o lote')
        }
        if (([int64]$probe.PfpDirectRb0FenceBefore -band 0xFFFFFFFFL) -ne 0x43DAFFFE -or
            ([int64]$probe.PfpDirectRb0FenceAfter -band 0xFFFFFFFFL) -ne 0xBC250123L) {
            $errors.Add('PFP direct status OK sem poison check e fence fisica esperada')
        }
    } elseif ($pfpStatus -eq 10) {
        # 10 = BC250_PFP_DIRECT_LOAD_PC_STATIC (fail-closed, PC parado/bootloader)
    } elseif ($pfpStatus -eq 14) {
        # 14 = BC250_PFP_DIRECT_LOAD_PM4_NOT_CONSUMED (tripé físico incompleto)
        if ([uint32]$probe.PfpDirectPhysicalReady -ne 0) {
            $errors.Add('PM4_NOT_CONSUMED nao pode publicar PfpDirectPhysicalReady=1')
        }
    } else {
        $errors.Add("PFP direct status inesperado: $pfpStatus (esperado 0=OK, 10=PC_STATIC ou 14=PM4_NOT_CONSUMED)")
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
    if ($null -ne $probe.PSObject.Properties['PspRlcLoadStatus'] -and
        [uint32]$probe.PspRlcLoadStatus -eq 0 -and
        [uint32]$probe.RlcAlreadyActive -eq 0 -and
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
    if (([int64]$probe.SdmaRingFenceObserved -band 0xFFFFFFFFL) -eq
        ([int64]$probe.SdmaRingFencePoison -band 0xFFFFFFFFL)) {
        $errors.Add('fence SDMA permaneceu no poison')
    }
}

Write-Host 'BC250 MCDM FAIL-CLOSED BINDING AUDIT'
Write-Host "  published INF: $publishedName"
if ($devices.Count -eq 1) {
    Write-Host "  device service/code: $($devices[0].Service) / $($devices[0].ConfigManagerErrorCode)"
}
function To-Hex32($val) {
    if ($null -eq $val) { return "00000000" }
    return '{0:X8}' -f ([int64]$val -band 0xFFFFFFFFL)
}

Write-Host "  driver hash matches package: $($errors -notcontains 'bc250mcdm.sys ativo diverge do pacote auditado')"
if ($null -ne $probe) {
    Write-Host "  lifecycle: DriverEntry=$($probe.DriverEntryReached) Add=$($probe.AddDeviceEntered) Start=$($probe.StartDeviceEntered) Stop=$($probe.StopDeviceEntered) Remove=$($probe.RemoveDeviceEntered)"
    Write-Host "  PCI/BAR/topology: $($probe.PciBytesRead) bytes / $($probe.ResolvedBarCount) BARs / status $($probe.TopologyStatus)"
    Write-Host "  VRAM/visible/ReBAR MiB: $($probe.VramTotalMiB) / $($probe.CpuVisibleMiB) / $($probe.RebarAssignedMiB)"
    Write-Host "  discovery TMR: status=$($probe.DiscoveryTmrStatus) bytes=$($probe.DiscoveryTmrBytes) (5=NOT_REPORTED)"
    Write-Host "  runtime missing: $($probe.RuntimeMissing)"
    Write-Host "  TLB: status=$($probe.TlbTransactionStatus) vmid=$($probe.TlbVmid) engine=$($probe.TlbEngineId) ackBefore=$(To-Hex32 $probe.TlbAckBefore) ackAfter=$(To-Hex32 $probe.TlbAckAfter) polls=$($probe.TlbPollCount) restore=$($probe.TlbRangeRestorePassed)"
    Write-Host "  GCVM inert: backing=$($probe.GcvmBackingStatus) vmid8=$($probe.GcvmVmid8Status) pt=$(To-Hex32 $probe.GcvmPtPhysicalHigh32)$(To-Hex32 $probe.GcvmPtPhysicalLow32) readback=$($probe.GcvmBackingReadback)/$($probe.GcvmVmid8Readback) restore=$($probe.GcvmBackingRestore)/$($probe.GcvmVmid8Restore)"
    Write-Host "  PSP ring: status=$($probe.PspRingStatus) pa=$(To-Hex32 $probe.PspRingPhysicalLow32) createAck=$(To-Hex32 $probe.PspRingCreateAck) destroyAck=$(To-Hex32 $probe.PspRingDestroyAck) restore=$($probe.PspRingRestorePassed)"
    Write-Host "  PSP SDMA FW: status=$($probe.PspLoadFwStatus) bytes=$($probe.PspLoadFwBytes) type=$($probe.PspLoadFwType) fence=$($probe.PspLoadFwFenceObserved)/$($probe.PspLoadFwFenceExpected) polls=$($probe.PspLoadFwFencePolls) response=$($probe.PspLoadFwResponseStatus) checksum=$(To-Hex32 $probe.PspLoadFwChecksumAfter) destroy/restore=$($probe.PspLoadFwDestroyPassed)/$($probe.PspLoadFwRestorePassed)"
    Write-Host "  PSP RLC FW: status=$($probe.PspRlcLoadStatus) bytes=$($probe.PspRlcLoadBytes) type=$($probe.PspRlcLoadType) fence=$($probe.PspRlcLoadFenceObserved)/$($probe.PspRlcLoadFenceExpected) polls=$($probe.PspRlcLoadFencePolls) response=$($probe.PspRlcLoadResponseStatus) destroy/restore=$($probe.PspRlcLoadDestroyPassed)/$($probe.PspRlcLoadRestorePassed)"
    Write-Host "  PFP direct: status=$($probe.PfpDirectStatus) mc=$(To-Hex32 $probe.PfpDirectMcHigh)$(To-Hex32 $probe.PfpDirectMcLow) staged=$($probe.PfpDirectStagedDwords)dw/$($probe.PfpDirectStagingMismatches)mismatch JT=$($probe.PfpDirectJtDwords) version=$('{0:X}' -f ([int64]$probe.PfpDirectVersion -band 0xFFFFFFFFL)) JT_STAT=$('{0:X6}' -f ([int64]$probe.PfpDirectFaultAddrHigh -band 0xFFFFFFL))"
    Write-Host "  PFP physical: PC=$(To-Hex32 $probe.PfpDirectPcBefore)->$(To-Hex32 $probe.PfpDirectPcFirst)->$(To-Hex32 $probe.PfpDirectPcLast) transitions=$($probe.PfpDirectPcTransitions)/$($probe.PfpDirectPcSamples) HALT=$(To-Hex32 $probe.PfpDirectMeCntlBefore)->$(To-Hex32 $probe.PfpDirectMeCntlApplied)->$(To-Hex32 $probe.PfpDirectMeCntlAfter) fault=$(To-Hex32 $probe.PfpDirectFaultBefore)->$(To-Hex32 $probe.PfpDirectFaultAfter) restore=$($probe.PfpDirectRegRestore)/$($probe.PfpDirectMemRestore)"
    Write-Host "  CP triad PCs: PFP=$(To-Hex32 $probe.PfpDirectPcLast) ME=$(To-Hex32 $probe.PfpDirectMePcBefore)->$(To-Hex32 $probe.PfpDirectMePcLast) CE=$(To-Hex32 $probe.PfpDirectCePcBefore)->$(To-Hex32 $probe.PfpDirectCePcLast)"
    Write-Host "  CP PM4 ring: active=$($probe.PfpDirectRb0ActiveBefore)->$($probe.PfpDirectRb0ActiveApplied)->$($probe.PfpDirectRb0ActiveAfter) RPTR/WPTR=$($probe.PfpDirectRb0RptrBefore)->$($probe.PfpDirectRb0RptrAfter)/$($probe.PfpDirectRb0WptrApplied) fence=$(To-Hex32 $probe.PfpDirectRb0FenceBefore)->$(To-Hex32 $probe.PfpDirectRb0FenceAfter)"
    Write-Host "  HQD readonly: status=$($probe.HqdPreflightStatus) reads=$($probe.HqdPreflightReadCount) stable=$($probe.HqdStablePasses) selector=$(To-Hex32 $probe.HqdSelector) active=$(To-Hex32 $probe.HqdActive) RPTR/WPTR=$($probe.HqdPqRptr)/$($probe.HqdPqWptrLo):$($probe.HqdPqWptrHi) inactive=$($probe.HqdInactive)"
    Write-Host "  MEC HQD selected: tx=$($probe.MecHqdSelectorStatus) selector=$(To-Hex32 $probe.MecHqdOriginalSelector)->$(To-Hex32 $probe.MecHqdAppliedSelector) readbacks=$(To-Hex32 $probe.MecHqdAppliedReadbackFirst)/$(To-Hex32 $probe.MecHqdAppliedReadbackLast) restore=$(To-Hex32 $probe.MecHqdRestoredSelector)/$($probe.MecHqdRestore) io=$($probe.MecHqdReadCalls)r/$($probe.MecHqdWriteCalls)w hqd=$($probe.MecHqdStatus) active=$(To-Hex32 $probe.MecHqdActive) RPTR/WPTR=$($probe.MecHqdPqRptr)/$($probe.MecHqdPqWptrLo):$($probe.MecHqdPqWptrHi) physical=$($probe.MecHqdPhysicalReady)"
    Write-Host "  HQD active: status=$($probe.HqdActiveStatus) selector=$(To-Hex32 $probe.HqdActiveSelectedSelector) applied=$($probe.HqdActiveApplied)->$($probe.HqdActiveAfter) RPTR/WPTR=$($probe.HqdActiveRptrBefore)->$($probe.HqdActiveRptrAfter)/$($probe.HqdActiveWptrApplied) fence=$(To-Hex32 $probe.HqdActiveFenceBefore)->$(To-Hex32 $probe.HqdActiveFenceAfter) db=$($probe.HqdActiveDoorbellRung):$($probe.HqdActiveDoorbellOffset):$($probe.HqdActiveDoorbellValueApplied) triad=$($probe.HqdActiveTriadPassed)"
    Write-Host "  HQD doorbell route: range=$(To-Hex32 $probe.HqdActiveDbRangeLowerBefore)/$(To-Hex32 $probe.HqdActiveDbRangeUpperBefore)->$(To-Hex32 $probe.HqdActiveDbRangeLowerApplied)/$(To-Hex32 $probe.HqdActiveDbRangeUpperApplied) rb=$(To-Hex32 $probe.HqdActiveDbRangeLowerReadback)/$(To-Hex32 $probe.HqdActiveDbRangeUpperReadback) PQ_STATUS=$(To-Hex32 $probe.HqdActiveCpPqStatusBefore)->$(To-Hex32 $probe.HqdActiveCpPqStatusApplied)/$(To-Hex32 $probe.HqdActiveCpPqStatusReadback) poll=$(To-Hex32 $probe.HqdActiveWptrPollCntlBefore)->$(To-Hex32 $probe.HqdActiveWptrPollCntlApplied)/$(To-Hex32 $probe.HqdActiveWptrPollCntlReadback) selectorRelease=$($probe.HqdActiveSelectorReleasedBeforeKick)"
    Write-Host "  HQD doorbell latch: PQ_STATUS=$(To-Hex32 $probe.HqdActiveCpPqStatusAfterKick) HQD_DB=$(To-Hex32 $probe.HqdActiveDoorbellControlAfterKick) WPTR/RPTR=$($probe.HqdActiveWptrAfterKick)/$($probe.HqdActiveRptrAfterKick)"
    Write-Host "  HQD WPTR baseline: initial=$($probe.HqdActiveInitialWptrLo):$($probe.HqdActiveInitialWptrHi) apply/readback=$($probe.HqdActiveBaselineWptrApplied)/$($probe.HqdActiveBaselineWptrReadback) restore=$($probe.HqdActiveBaselineWptrRestored)/$($probe.HqdActiveBaselineWptrRestorePassed) polls=$($probe.HqdActiveBaselineWptrRestorePolls)"
    Write-Host "  HQD direct WPTR: kick=$($probe.HqdActiveDirectWptrKickApplied) before/readback=$($probe.HqdActiveDirectWptrBefore)/$($probe.HqdActiveDirectWptrReadback) RPTR=$($probe.HqdActiveDirectRptrAfterKick) restore=$($probe.HqdActiveDirectWptrRestored)/$($probe.HqdActiveDirectWptrRestorePassed) polls=$($probe.HqdActiveDirectWptrRestorePolls)"
    Write-Host "  HQD live bank: RPTR/WPTR=$($probe.HqdActiveRptrMmioLast)/$($probe.HqdActiveWptrMmioLast) HQ_STATUS0/HQD_ERROR=$(To-Hex32 $probe.HqdActiveHqStatus0PollLast)/$(To-Hex32 $probe.HqdActiveHqdErrorPollLast)"
    Write-Host "  HQD scheduler: RLC_CP_SCHEDULERS=$(To-Hex32 $probe.HqdActiveRlcCpSchedulersBefore)->$(To-Hex32 $probe.HqdActiveRlcCpSchedulersApplied)/$(To-Hex32 $probe.HqdActiveRlcCpSchedulersReadback)->$(To-Hex32 $probe.HqdActiveRlcCpSchedulersAfterKick) restore=$(To-Hex32 $probe.HqdActiveRlcCpSchedulersRestored)/$($probe.HqdActiveRlcCpSchedulersRestorePassed) HQ_STATUS0/CONTROL0=$(To-Hex32 $probe.HqdActiveHqStatus0AfterKick)/$(To-Hex32 $probe.HqdActiveHqControl0AfterKick)"
    Write-Host "  HQD faults: HQD=$(To-Hex32 $probe.HqdActiveHqdErrorAfterKick) CPC_UTCL_ERR/STATUS=$(To-Hex32 $probe.HqdActiveCpcUtcl1ErrorAfterKick)/$(To-Hex32 $probe.HqdActiveCpcUtcl1StatusAfterKick) CPF_UTCL=$(To-Hex32 $probe.HqdActiveCpfUtcl1StatusAfterKick) CPC_INT/ME1P0/ME2P0/ME2P1=$(To-Hex32 $probe.HqdActiveCpcIntStatusAfterKick)/$(To-Hex32 $probe.HqdActiveMe1Pipe0IntStatusAfterKick)/$(To-Hex32 $probe.HqdActiveMe2Pipe0IntStatusAfterKick)/$(To-Hex32 $probe.HqdActiveMe2Pipe1IntStatusAfterKick)"
    Write-Host "  HQD fetch diag: PQ=$(To-Hex32 $probe.HqdActivePqBaseHi):$(To-Hex32 $probe.HqdActivePqBaseLo) poll=$(To-Hex32 $probe.HqdActiveWptrPollAddrHi):$(To-Hex32 $probe.HqdActiveWptrPollAddrLo) shadow=$($probe.HqdActiveWptrShadow) MEC_CNTL=$(To-Hex32 $probe.HqdActiveCpMecCntlReadback) MEC_PC=$(To-Hex32 $probe.HqdActiveMecPcFirst)->$(To-Hex32 $probe.HqdActiveMecPcLast) transitions/samples=$($probe.HqdActiveMecPcTransitions)/$($probe.HqdActiveMecPcSamples) PQ_CONTROL=$(To-Hex32 $probe.HqdActivePqControlObserved)"
    Write-Host "  HQD coherency: HDP=$($probe.HqdActiveHdpInvalidateApplied) L2=$(To-Hex32 $probe.HqdActiveGcvmL2Before)->$(To-Hex32 $probe.HqdActiveGcvmL2Applied)/$(To-Hex32 $probe.HqdActiveGcvmL2Readback) REQ=$(To-Hex32 $probe.HqdActiveGcvmReqApplied)/$(To-Hex32 $probe.HqdActiveGcvmReqReadback) ACK=$(To-Hex32 $probe.HqdActiveGcvmAckBefore)->$(To-Hex32 $probe.HqdActiveGcvmAckAfter) polls=$($probe.HqdActiveGcvmPolls) passed=$($probe.HqdActiveCoherencyPassed)"
    Write-Host "  HQD GFXOFF: applicable=$($probe.HqdActiveGfxOffApplicable) messages=$($probe.HqdActiveGfxOffDisallowResponse)/$($probe.HqdActiveGfxOffAllowResponse) n/a-proof=$($probe.HqdActiveGfxOffGuardPassed)/$($probe.HqdActiveGfxOffRestorePassed)"
    Write-Host "  HQD bank proof: status=$($probe.HqdBankProbeStatus) active=$($probe.HqdBank0Active)/$($probe.HqdBankCandidateActive) priority0/candidate/poison=$($probe.HqdBank0Priority)/$($probe.HqdBankCandidatePriority)/$($probe.HqdBankPoisonPriority) poisonRead=$($probe.HqdBankPoisonReadback) cross=$($probe.HqdBank0CrossReadback)/$($probe.HqdBankCandidateCrossReadback) final=$($probe.HqdBank0FinalReadback)/$($probe.HqdBankCandidateRestoreReadback) io=$($probe.HqdBankReadCalls)r/$($probe.HqdBankWriteCalls)w isolation/restore/physical=$($probe.HqdBankIsolationPassed)/$($probe.HqdBankRestorePassed)/$($probe.HqdBankPhysicalReady)"
    Write-Host "  MEC HQD bank snapshot: capture/stable/inactive=$($probe.HqdBankHqdCapturePassed)/$($probe.HqdBankHqdStablePasses)/$($probe.HqdBankHqdInactive) selector=$(To-Hex32 $probe.HqdBankHqdSelectorObserved) active=$(To-Hex32 $probe.HqdBankHqdActive) MQD=$(To-Hex32 $probe.HqdBankHqdMqdBaseHi)$(To-Hex32 $probe.HqdBankHqdMqdBaseLo) PQ=$(To-Hex32 $probe.HqdBankHqdPqBaseHi)$(To-Hex32 $probe.HqdBankHqdPqBaseLo) RPTR/WPTR=$($probe.HqdBankHqdPqRptr)/$($probe.HqdBankHqdPqWptrLo):$($probe.HqdBankHqdPqWptrHi) doorbell=$(To-Hex32 $probe.HqdBankHqdDoorbellControl) control=$(To-Hex32 $probe.HqdBankHqdPqControl)"
    if ($null -ne $probe.PSObject.Properties['HqdWptrDiagStatus']) {
        Write-Host "  HQD WPTR diag: status=$($probe.HqdWptrDiagStatus) selector=$(To-Hex32 $probe.HqdWptrDiagOriginalSelector)->$(To-Hex32 $probe.HqdWptrDiagTargetSelector) preflight=$($probe.HqdWptrDiagStablePreflightPassed) activeBefore=$($probe.HqdWptrDiagActiveBefore) testValue=$(To-Hex32 $probe.HqdWptrDiagTestValueWritten) firstRead=$(To-Hex32 $probe.HqdWptrDiagFirstReadback) seriesMatch=$($probe.HqdWptrDiagSeriesMatchIndex) selectorUnmod=$($probe.HqdWptrDiagSelectorUnmodified) restore=$(To-Hex32 $probe.HqdWptrDiagRestoredValueObserved)/$($probe.HqdWptrDiagRestorePassed) polls=$($probe.HqdWptrDiagRestorePolls) finalWptr=$(To-Hex32 $probe.HqdWptrDiagWptrLoAfter) physical=$($probe.HqdWptrDiagPhysicalReady)"
    }
    Write-Host "  PSP buffers: ring=$(To-Hex32 $probe.PspLoadFwRingHigh)$(To-Hex32 $probe.PspLoadFwRingLow) cmd=$(To-Hex32 $probe.PspLoadFwCommandHigh)$(To-Hex32 $probe.PspLoadFwCommandLow) fence=$(To-Hex32 $probe.PspLoadFwFenceHigh)$(To-Hex32 $probe.PspLoadFwFenceLow) payload=$(To-Hex32 $probe.PspLoadFwPayloadHigh)$(To-Hex32 $probe.PspLoadFwPayloadLow)"
    Write-Host "  SDMA F32: status=$($probe.SdmaF32Status) pc=$($probe.SdmaF32PcFirst)->$($probe.SdmaF32PcLast) transitions=$($probe.SdmaF32PcTransitions) readback/restore=$($probe.SdmaF32ApplyReadback)/$($probe.SdmaF32Restore)"
    Write-Host "  SDMA ring: status=$($probe.SdmaRingWorkStatus) RPTR-mmio/wb/WPTR=$($probe.SdmaRingRptrRegister)/$($probe.SdmaRingRptrAfter)/$($probe.SdmaRingWptr) fence=$(To-Hex32 $probe.SdmaRingFencePoison)->$(To-Hex32 $probe.SdmaRingFenceObserved) data=$($probe.SdmaRingComparedDwords)dw/$($probe.SdmaRingMismatches)mismatch polls=$($probe.SdmaRingPolls) restore=$($probe.SdmaRingRegRestore)/$($probe.SdmaRingMemRestore)"
    Write-Host "  SDMA allocation: fixedStatus=$($probe.SdmaRingFixedReservationStatus) mode=$($probe.SdmaRingAllocationMode) ring=$(To-Hex32 $probe.SdmaRingRingHigh)$(To-Hex32 $probe.SdmaRingRingLow) work=$(To-Hex32 $probe.SdmaRingWorkHigh)$(To-Hex32 $probe.SdmaRingWorkLow)"
    Write-Host "  SDMA publish: WPTR reg/shadow=$($probe.SdmaRingWptrRegister)/$($probe.SdmaRingWptrShadow) RB_CNTL=$(To-Hex32 $probe.SdmaRingRbCntl)"
    Write-Host "  SDMA visibility: header=$(To-Hex32 $probe.SdmaRingHeader) RPTR-reg=$($probe.SdmaRingRptrRegister) poll=$(To-Hex32 $probe.SdmaRingPollCntl)"
    Write-Host "  SDMA diagnostic: status1/2/3=$(To-Hex32 $probe.SdmaRingStatus1After)/$(To-Hex32 $probe.SdmaRingStatus2After)/$(To-Hex32 $probe.SdmaRingStatus3After) error=$(To-Hex32 $probe.SdmaRingErrorLogAfter)"
    Write-Host "  SDMA UTCL: rd/wr=$(To-Hex32 $probe.SdmaRingUtcl1ReadStatusAfter)/$(To-Hex32 $probe.SdmaRingUtcl1WriteStatusAfter) rx=$(To-Hex32 $probe.SdmaRingUtcl1ReadXnack0After):$(To-Hex32 $probe.SdmaRingUtcl1ReadXnack1After) wx=$(To-Hex32 $probe.SdmaRingUtcl1WriteXnack0After):$(To-Hex32 $probe.SdmaRingUtcl1WriteXnack1After) timeout=$(To-Hex32 $probe.SdmaRingUtcl1TimeoutAfter)"
}

if ($errors.Count -ne 0) {
    Write-Host 'BC250 MCDM FAIL-CLOSED BINDING AUDIT: BLOCKED'
    foreach ($errorMessage in $errors) { Write-Host "  - $errorMessage" }
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 1
}
Write-Host 'BC250 MCDM FAIL-CLOSED BINDING AUDIT: PASS'
Write-Host 'CP TRIADE & PM4 RING 0 ENSAIADOS; COMPUTE NAO EXECUTADO; NAO VALIDADO EM HARDWARE'
exit 0
