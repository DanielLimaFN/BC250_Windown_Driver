#!/usr/bin/env python3
"""Fail-closed static gate for the installable BC-250 MCDM package."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]


def require(text: str, needle: str, label: str, errors: list[str]) -> None:
    if needle not in text:
        errors.append(f"missing {label}: {needle}")


def forbid(text: str, needle: str, label: str, errors: list[str]) -> None:
    if needle in text:
        errors.append(f"forbidden {label}: {needle}")


def function_body(text: str, signature: str) -> str:
    """Return a C function body, including braces, using balanced braces."""
    start = text.find(signature)
    if start < 0:
        return ""
    brace = text.find("{", start)
    if brace < 0:
        return ""
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:index + 1]
    return ""


def main() -> int:
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    callbacks_path = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
    active_test_path = ROOT / "driver" / "tests" / "active_miniport_test.c"
    recovery_path = ROOT / "tools" / "BC250_RECOVERY.cmd"
    inf_path = ROOT / "driver" / "wdk" / "package" / "bc250mcdm.inf"
    stage_path = ROOT / "tools" / "stage-bc250mcdm-package.ps1"
    bind_path = ROOT / "tools" / "bind-bc250mcdm-failclosed.ps1"
    collector_path = ROOT / "tools" / "capture-bc250mcdm-binding.ps1"
    watchdog_path = ROOT / "tools" / "watch-bc250mcdm-binding.ps1"
    confirm_display_path = ROOT / "tools" / "confirm-bc250mcdm-display.ps1"
    arm_reboot_path = (ROOT / "tools" /
                       "arm-bc250mcdm-reboot-watchdog.ps1")
    watchdog_reboot_path = (ROOT / "tools" /
                            "watch-bc250mcdm-reboot.ps1")
    confirm_reboot_path = (ROOT / "tools" /
                           "confirm-bc250mcdm-reboot.ps1")
    rollback_path = ROOT / "tools" / "rollback-bc250mcdm-online.ps1"
    replace_path = ROOT / "tools" / "replace-bc250mcdm.ps1"
    orphan_cleanup_path = (ROOT / "tools" /
                           "cleanup-bc250mcdm-prebinding-residue.ps1")
    readonly_ddi_path = ROOT / "driver" / "wdk" / "bc250_dxgk_readonly_ddi.c"
    platform_snapshot_path = (ROOT / "driver" / "core" /
                              "bc250_platform_register_snapshot.c")
    vram_probe_path = (ROOT / "driver" / "wdk" /
                       "bc250_vram_probe_transaction.c")
    paging_path = ROOT / "driver" / "core" / "bc250_paging_contract.c"
    tlb_transaction_path = (ROOT / "driver" / "core" /
                            "bc250_gfxhub_tlb_transaction.c")
    mmio_transport_path = (ROOT / "driver" / "wdk" /
                           "bc250_mmio_topology_transport.c")
    compute_mqd_path = (ROOT / "driver" / "core" /
                        "bc250_compute_mqd_contract.c")
    pm4_builder_path = (ROOT / "driver" / "core" /
                        "bc250_pm4_packet_builder.c")
    pm4_header_path = (ROOT / "driver" / "core" /
                       "bc250_pm4_packet_builder.h")
    display_path = ROOT / "driver" / "wdk" / "bc250_display_ddi.c"
    build_path = ROOT / "driver" / "wdk" / "build-contract.bat"

    active = active_path.read_text(encoding="utf-8")
    callbacks = callbacks_path.read_text(encoding="utf-8")
    active_test = active_test_path.read_text(encoding="utf-8")
    recovery = recovery_path.read_text(encoding="utf-8")
    inf = inf_path.read_text(encoding="utf-8")
    stage = stage_path.read_text(encoding="utf-8")
    bind = bind_path.read_text(encoding="utf-8")
    collector = collector_path.read_text(encoding="utf-8")
    watchdog = watchdog_path.read_text(encoding="utf-8")
    confirm_display = confirm_display_path.read_text(encoding="utf-8")
    arm_reboot = arm_reboot_path.read_text(encoding="utf-8")
    watchdog_reboot = watchdog_reboot_path.read_text(encoding="utf-8")
    confirm_reboot = confirm_reboot_path.read_text(encoding="utf-8")
    rollback = rollback_path.read_text(encoding="utf-8")
    replace = replace_path.read_text(encoding="utf-8")
    orphan_cleanup = orphan_cleanup_path.read_text(encoding="utf-8")
    readonly_ddi = readonly_ddi_path.read_text(encoding="utf-8")
    platform_snapshot = platform_snapshot_path.read_text(encoding="utf-8")
    vram_probe = vram_probe_path.read_text(encoding="utf-8")
    paging = paging_path.read_text(encoding="utf-8")
    tlb_transaction = tlb_transaction_path.read_text(encoding="utf-8")
    mmio_transport = mmio_transport_path.read_text(encoding="utf-8")
    compute_mqd = compute_mqd_path.read_text(encoding="utf-8")
    pm4_builder = pm4_builder_path.read_text(encoding="utf-8")
    pm4_header = pm4_header_path.read_text(encoding="utf-8")
    display = display_path.read_text(encoding="utf-8")
    build = build_path.read_text(encoding="utf-8")
    errors: list[str] = []

    # Display PnP must not activate laboratory hardware transactions.
    require(active,
            "#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)",
            "compile-time display-only StartDevice gate", errors)
    require(active,
            "registry value must never be able to turn",
            "non-tunable display safety contract", errors)
    require(active,
            "Bc250ProbeRecord(BC250_PROBE_SKIP_HW_BRINGUP, 1u);",
            "compiled display-only bring-up skip evidence", errors)
    forbid(active, "#define BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP",
           "local activation of unsafe StartDevice bring-up", errors)
    forbid(build, "/DBC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP",
           "unsafe StartDevice bring-up in package build", errors)

    vsync_dpc = function_body(display, "static VOID Bc250VsyncDpcRoutine(")
    synchronized_vsync = function_body(
        display, "static BOOLEAN Bc250SynchronizedVsyncCallback(")
    if not vsync_dpc:
        errors.append("missing VSync DPC body")
    if not synchronized_vsync:
        errors.append("missing synchronized VSync callback body")
    require(vsync_dpc, "DxgkCbSynchronizeExecution(",
            "DIRQL-synchronized VSync notification", errors)
    forbid(vsync_dpc, "DxgkCbNotifyInterrupt(",
           "direct NotifyInterrupt call from VSync DPC", errors)
    forbid(vsync_dpc, "Bc250ProbeRecord(",
           "persistent registry telemetry from VSync DPC", errors)
    forbid(synchronized_vsync, "Bc250ProbeRecord(",
           "persistent registry telemetry from synchronized VSync", errors)
    require(display, "KeGetCurrentIrql() == PASSIVE_LEVEL",
            "PASSIVE_LEVEL display telemetry gate", errors)
    require(display, "Mode->Format.Graphics.Stride >=",
            "source stride independent from GOP pitch", errors)
    require(display, "Mode->Format.Graphics.PixelFormat == D3DDDIFMT_X8R8G8B8",
            "32-bit XRGB source mode support", errors)
    require(display, "Bc250FillNativeSignalInfo(",
            "coherent native signal timing helper", errors)
    require(display, "BC250_NATIVE_HBLANK",
            "shared EDID/VidPN horizontal blanking", errors)
    require(display, "BC250_NATIVE_VBLANK",
            "shared EDID/VidPN vertical blanking", errors)
    require(synchronized_vsync, "DxgkCbNotifyInterrupt(",
            "NotifyInterrupt inside synchronized callback", errors)
    require(display, "InterlockedExchange(&g_Bc250DisplayState.DisplayStarted, 0);",
            "VSync lifecycle shutdown before teardown/removal", errors)
    require(display, "g_Bc250DisplayState.VsyncRequested",
            "VSync request preservation across D-state transitions", errors)
    require(display, "PostDisplayInfo->PhysicAddress.QuadPart == 0",
            "GOP physical-address validation", errors)
    require(display, "PostDisplayInfo->Pitch < (PostDisplayInfo->Width * 4u)",
            "GOP pitch validation", errors)
    require(display, "return STATUS_INSUFFICIENT_RESOURCES;",
            "fail-closed framebuffer mapping", errors)
    require(display, "ExAcquireRundownProtection(&g_Bc250DisplayState.PresentRundown)",
            "present/teardown rundown acquisition", errors)
    require(display, "ExWaitForRundownProtectionRelease(&g_Bc250DisplayState.PresentRundown)",
            "present/teardown rundown drain", errors)
    require(display, "pPresentDisplayOnly->BytesPerPixel != 4",
            "display-only present pixel-format validation", errors)
    require(display, "pPresentDisplayOnly->Pitch <= 0",
            "display-only present pitch validation", errors)
    require(display, "&pPresentDisplayOnly->pMoves[i].DestRect, &clipped",
            "bounded display-only move rectangles", errors)
    require(display, "PositionX >= g_Bc250DisplayState.CurrentMode.Width",
            "system-display horizontal origin bound", errors)
    require(display, "SourceStride < SourceWidth * 4u",
            "system-display source-stride bound", errors)
    require(display, "static NTSTATUS Bc250ValidateVidPn(",
            "shared fail-closed VidPN validator", errors)
    require(display, "if (pathCount != 1)",
            "single-path VidPN topology", errors)
    require(display, "Bc250VidPnPathSupported(path, RequirePinnedModes)",
            "VidPN source/target and transform validation", errors)
    require(display, "Bc250SourceModeMatchesGop(sourceMode)",
            "pinned source mode GOP validation", errors)
    require(display, "Bc250TargetModeMatchesGop(targetMode)",
            "pinned target mode GOP validation", errors)
    require(display, "g_Bc250DisplayState.VidPnCommitted",
            "present gated by committed VidPN", errors)
    cofunc = function_body(
        display, "NTSTATUS APIENTRY Bc250DxgkDdiEnumVidPnCofuncModality(")
    require(cofunc, "if (pathCount != 1)",
            "cofunctional enumeration single-path gate", errors)
    require(cofunc, "pfnUpdatePathSupportInfo(",
            "cofunctional identity transform publication", errors)
    require(cofunc, "Bc250PublishCofuncSourceMode(",
            "checked cofunctional source publication", errors)
    require(cofunc, "Bc250PublishCofuncTargetMode(",
            "checked cofunctional target publication", errors)
    require(display, "Successful assignment transfers ownership",
            "explicit VidPN mode-set ownership transfer", errors)
    forbid(active, "context->PostDisplayEvidence.Width ?",
           "fabricated fallback GOP width", errors)
    forbid(active, "context->PostDisplayEvidence.Height ?",
           "fabricated fallback GOP height", errors)
    set_address = function_body(display, "NTSTATUS APIENTRY Bc250DxgkDdiSetVidPnSourceAddress(")
    legacy_present = function_body(display, "NTSTATUS APIENTRY Bc250DxgkDdiPresent(")
    require(set_address, "STATUS_NOT_SUPPORTED",
            "unused full-WDDM source-address DDI fails closed", errors)
    require(legacy_present, "STATUS_NOT_SUPPORTED",
            "unused full-WDDM present DDI fails closed", errors)

    # PM4 GFX10 must be structurally complete before any hardware claim.
    for needle in (
        "BC250_PM4_IT_SET_UCONFIG_REG     0x79u",
        "BC250_PM4_UCONFIG_REG_BASE 0xC000u",
    ):
        require(pm4_header, needle, "canonical GFX10 UCONFIG encoding", errors)
    for needle in (
        "ExtraCount == 0u",
        "BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_NOP, ExtraCount)",
        "(2u << 29)",
        "0u); /* ctxid/reserved */",
    ):
        require(pm4_builder, needle, "complete PM4 packet encoding", errors)
    for needle in (
        "0xC0017900u",
        "0x00000040u",
        "0xC0064900u",
        "0x06603514u",
        "0x20000000u",
        "submittedDwords != 11u",
        "rptrPoll == submittedDwords",
    ):
        require(compute_mqd, needle, "canonical HQD 3+8 dword payload", errors)
    forbid(compute_mqd, "0x00002040u", "raw segment-less SCRATCH PM4 target", errors)

    # Runtime evidence must not be synthesized by AddDevice/StartDevice.
    forbid(active, "Bc250BuildDefaultActiveRuntime", "synthetic runtime builder", errors)
    require(active,
            "context->RuntimeInventoryReport.OwnershipEvidenceFlags;",
            "runtime-derived ownership flags", errors)
    forbid(active, "Reservations[0].Offset =", "fixed inherited reservation", errors)
    require(active,
            "context->Core.ReadonlyGatePassed == 1u &&",
            "VRAM mutation gated by passed readonly preflight", errors)
    require(active, "InterlockedExchange(&context->IsActive, 0);",
            "StartDevice inactive state", errors)
    require(active, "return preflightStatus;",
            "StartDevice fail-closed return", errors)
    require(active, "Bc250AcquirePostDisplayReservation(",
            "physical post-display ownership capture", errors)
    require(active, "BC250_PROBE_POST_DISPLAY_STATUS",
            "post-display telemetry", errors)
    require(active, "BC250_RUNTIME_EVIDENCE_SCANOUT_PROBED |",
            "partial runtime inventory evidence", errors)
    require(active, "BC250_PROBE_RUNTIME_INVENTORY_COMPLETE",
            "runtime inventory completeness telemetry", errors)
    require(active, "Bc250ActiveQueryRuntimeCaps(",
            "runtime-backed capability provider", errors)
    require(callbacks, "runtimeCaps.LocalSegmentBytes",
            "runtime-backed segment sizing", errors)
    require(callbacks, "case DXGKQAITYPE_QUERYSEGMENT4:",
            "mandatory MCDM QUERYSEGMENT4", errors)
    require(callbacks, "case DXGKQAITYPE_NUMPOWERCOMPONENTS:",
            "mandatory MCDM power component query", errors)
    require(callbacks, "case DXGKQAITYPE_HISTORYBUFFERPRECISION:",
            "mandatory MCDM history precision query", errors)
    require(callbacks, "runtimeCaps.FullMcdmReady == 0u",
            "full MCDM capability gate", errors)
    forbid(callbacks, "caps->SupportNonVGA = TRUE",
           "MCDM-prohibited non-VGA capability", errors)
    require(callbacks, "caps->MiscCaps.ComputeOnly = 1u",
            "compute-only driver capability", errors)
    forbid(active, "BC250_RUNTIME_EVIDENCE_COMPLETE;",
           "fabricated complete runtime inventory", errors)
    require(active, "BC250_RUNTIME_EVIDENCE_PLATFORM_CARVEOUTS_PROBED",
            "explicit platform carveout evidence", errors)
    require(active, "AddressSpace.CpuVisibleBytes;",
            "runtime-derived opaque carveout boundary", errors)
    require(active, "BC250_VRAM_ROLE_PLATFORM_RESERVED",
            "opaque non-visible VRAM reservation", errors)
    forbid(active, "0x1F800000", "historical fixed PSP TMR offset", errors)
    require(readonly_ddi, "Bc250CapturePlatformRegistersReadonly(",
            "readonly platform-register capture", errors)
    require(readonly_ddi, "BC250_PROBE_PLATFORM_SNAPSHOT_STATUS",
            "platform-register telemetry", errors)
    require(platform_snapshot, "for (pass = 0u; pass < 2u; ++pass)",
            "two-pass stable platform snapshot", errors)
    require(platform_snapshot, "BC250_PLATFORM_SNAPSHOT_UNSTABLE",
            "unstable platform snapshot rejection", errors)
    require(vram_probe, "original[i] = target[i]",
            "VRAM probe original-byte capture", errors)
    require(vram_probe, "target[i] = original[i]",
            "VRAM probe restoration", errors)
    require(vram_probe, "BC250_VRAM_PROBE_RESTORE_FAILED",
            "VRAM probe restore verification", errors)
    require(paging, "Plan->EngineId = 17u;",
            "GC 10.1.3 GFXHUB invalidation engine", errors)
    require(paging, "Plan->RequiresDummyRequestRead = 1u;",
            "GFXHUB invalidation dummy request read", errors)
    require(paging, "Plan->UsesSemaphore = 0u;",
            "GFXHUB no-semaphore contract", errors)
    require(paging, "Plan->AckMask = 1u << Params->Vmid;",
            "per-VMID invalidation ACK mask", errors)
    flush_start = paging.find("BC250_PAGING_STATUS Bc250PagingBuildFlushTlb(")
    flush_body = paging[flush_start : flush_start + 850]
    forbid(flush_body, "Bc250SdmaEmitNop",
           "fake SDMA NOP TLB invalidation", errors)
    require(flush_body, "BC250_PAGING_UNSUPPORTED_OPERATION",
            "physical TLB flush remains fail-closed", errors)
    require(tlb_transaction, "Evidence->AckBefore == 0xFFFFFFFFu",
            "all-ones ACK rejection", errors)
    require(tlb_transaction, "Evidence->RequestDummyRead",
            "mandatory post-request dummy read evidence", errors)
    require(tlb_transaction, "BC250_TLB_TRANSACTION_TIMEOUT",
            "bounded TLB ACK timeout", errors)
    require(tlb_transaction, "Evidence->RangeRestorePassed = 1u;",
            "invalidation range restoration proof", errors)
    require(mmio_transport,
            "static uint32_t Bc250MmioTopologyWriteOffsetAllowed",
            "explicit MMIO write allowlist", errors)
    require(mmio_transport, "ByteOffset == 0xA2D0u ||",
            "GFXHUB engine-17 request write", errors)
    write_allowlist_start = mmio_transport.find(
        "static uint32_t Bc250MmioTopologyWriteOffsetAllowed")
    write_allowlist_body = mmio_transport[
        write_allowlist_start : write_allowlist_start + 350]
    forbid(write_allowlist_body, "ByteOffset == 0xA318u",
           "write access to read-only ACK", errors)
    require(active, "Bc250ActiveExecuteGcvmBacking(",
            "owned GCVM backing StartDevice transaction", errors)
    require(active, "Bc250ActiveFlushMappedGfxhubTlb(",
            "physical GFXHUB TLB transaction inside VMID8 probe", errors)
    require(active, "context->TlbTransactionEvidence.PhysicalReady",
            "hardware-derived TLB readiness telemetry", errors)
    require(active, "Bc250McdmObjectRegistryInitialize(&context->Objects)",
            "owned MCDM object registry initialization", errors)
    require(active, "Bc250McdmObjectRegistryTeardown(",
            "MCDM object teardown on StopDevice", errors)

    # These DDIs must remain explicit failures until backed by owned hardware state.
    for function in (
        "Bc250McdmBuildPagingBuffer",
        "Bc250McdmCreateAllocation",
        "Bc250McdmCreateContext",
        "Bc250McdmPreemptCommand",
        "Bc250McdmQueryEngineStatus",
        "Bc250McdmResetFromTimeout",
        "Bc250McdmRestartFromTimeout",
        "Bc250McdmSetPowerState",
        "Bc250McdmSubmitCommandVirtual",
    ):
        start = callbacks.find(function)
        if start < 0:
            errors.append(f"missing callback: {function}")
            continue
        body = callbacks[start : start + 1800]
        require(body, "STATUS_NOT_SUPPORTED", f"fail-closed {function}", errors)

    require(callbacks, "g_Bc250CreateProcessObject(",
            "CreateProcess lifecycle routing", errors)
    require(active, "Bc250McdmInstallProcessLifecycle(",
            "active-only process lifecycle installation", errors)
    require(active, "Bc250McdmInstallDeviceLifecycle(",
            "active-only device lifecycle installation", errors)
    require(active, "Bc250McdmInstallContextLifecycle(",
            "active-only context lifecycle installation", errors)
    require(active, "ContextAbiReady", 
            "context ABI evidence gate", errors)
    require(active, "Bc250McdmInstallAllocationLifecycle(",
            "active-only allocation lifecycle installation", errors)
    require(active, "Bc250ActiveOpenAllocationLifecycle",
            "device-specific OpenAllocation lifecycle", errors)
    require(active, "Bc250ActiveCloseAllocationLifecycle",
            "device-specific CloseAllocation lifecycle", errors)
    require(active, "Bc250McdmObjectFindAllocationByToken(",
            "versioned allocation-token binding", errors)
    require(callbacks, "g_Bc250OpenAllocationObjects(",
            "OpenAllocation callback routing", errors)
    require(callbacks, "g_Bc250CloseAllocationObjects(",
            "CloseAllocation callback routing", errors)
    require(active, "Bc250ActiveCollectDebugLifecycle",
            "software-only CollectDbgInfo snapshot", errors)
    require(active, "Bc250McdmInstallDebugLifecycle(",
            "active-only debug lifecycle installation", errors)
    require(callbacks, "g_Bc250CollectDebugSnapshot(",
            "CollectDbgInfo callback routing", errors)
    require(active, "Bc250ProbeGetFirstFailure()",
            "first-failure diagnostic preservation", errors)
    debug_start = active.find("Bc250ActiveCollectDebugLifecycle")
    debug_body = active[debug_start : debug_start + 4200]
    for forbidden_debug_access in (
        "READ_REGISTER",
        "WRITE_REGISTER",
        "DxgkCbReadDeviceSpace",
        "Mmio",
    ):
        forbid(debug_body, forbidden_debug_access,
               "hardware access in CollectDbgInfo", errors)
    require(active, "AllocationAbiReady",
            "allocation ABI evidence gate", errors)
    require(active, "context->AllocationSegmentSet != 1u",
            "allocation segment-1 readiness gate", errors)
    require(active, "request.ByteSize > context->AllocationSegmentBytes",
            "allocation physical segment bound", errors)
    require(active, "info->PreferredSegment.SegmentId0 = 1u",
            "allocation segment-1 preference", errors)
    require(active, "Bc250AllocationAbiParse(",
            "versioned allocation private-data validation", errors)
    require(callbacks, "case DXGKQAITYPE_GPUMMUCAPS:",
            "conditional GPU MMU capabilities query", errors)
    require(callbacks, "case DXGKQAITYPE_PAGETABLELEVELDESC:",
            "conditional page-table level query", errors)
    require(callbacks, "runtimeCaps.GpuVaReady == 0u",
            "GPUVA query readiness gate", errors)
    require(active, "RootPageTableAbiReady",
            "root page-table lifecycle gate", errors)
    require(active, "Bc250McdmObjectSetRootPageTable(",
            "owned root page-table association", errors)
    require(callbacks, "pSetPageTable->Address.SegmentId",
            "VidMm segment-address root page table", errors)
    require(callbacks, "DXGK_OPERATION_UPDATE_PAGE_TABLE",
            "VidMm page-table update operation", errors)
    require(callbacks, "DXGK_PAGETABLEUPDATE_CPU_VIRTUAL",
            "immediate CPU-virtual page-table update", errors)
    require(callbacks, "Source->Segment != 1u",
            "unsupported system/GART PTE rejection", errors)
    provider_start = active.find("static NTSTATUS Bc250ActiveQueryRuntimeCaps")
    provider_body = active[provider_start : provider_start + 4200]
    forbid(provider_body, "Caps->GpuVaReady =",
           "production GPUVA readiness assertion", errors)
    forbid(provider_body, "Caps->PagingReady =",
           "production paging readiness assertion", errors)
    require(active, "g_Bc250AdapterDirectoryLock", 
            "owned adapter directory for handle-only DDIs", errors)
    require(active, "InterlockedCompareExchange(&Context->IsActive, 1, 1)",
            "object creation active-state gate", errors)
    require(active_test, "Bc250RunStopRaceTest",
            "multithread StopDevice/DDI race harness", errors)
    require(active_test, "BC250_TEST_RACE_ROUNDS 16u",
            "bounded repeated race rounds", errors)
    require(active_test, "activeContext->Objects.LiveCount != 0u",
            "post-race registry-empty invariant", errors)
    require(active_test, "InterlockedCompareExchange64(",
            "atomic exclusive test lock", errors)

    # Recovery must recognize the exact active service/binary identity.
    for needle in (
        "call :remove_service bc250mcdm",
        "call :quarantine_driver bc250mcdm.sys",
        'findstr.exe /I /C:"AddService=bc250mcdm,"',
        'findstr.exe /I /C:"ServiceBinary=%%12%%\\bc250mcdm.sys"',
    ):
        require(recovery, needle, "active recovery coverage", errors)

    for needle in (
        "Class=Display",
        "PCI\\VEN_1002&DEV_13FE",
        "AddService=bc250mcdm,",
        "ServiceBinary=%12%\\bc250mcdm.sys",
    ):
        require(inf, needle, "strict active INF identity", errors)

    require(stage, "audit-bc250mcdm-prebinding.ps1",
            "clean-host pre-binding gate before stage", errors)
    require(stage, "$output = (& pnputil.exe /add-driver $packageInf 2>&1)",
            "stage-only pnputil invocation", errors)
    require(stage, "if (-not $Execute)",
            "zero-write default stage mode", errors)
    for forbidden_stage_path in (
        "UpdateDriverForPlugAndPlayDevices",
        "INSTALLFLAG_FORCE",
        "pnputil.exe /add-driver $packageInf /install 2>&1",
    ):
        forbid(stage, forbidden_stage_path,
               "binding operation in stage-only script", errors)

    require(replace, "Fluxo direto replace desativado",
            "legacy direct replacement mutation block", errors)
    require(replace, "Zero escrita, zero binding",
            "legacy replacement read-only default", errors)
    forbid(replace, "UpdateDriverForPlugAndPlayDevicesW",
           "binding API in blocked legacy replacement", errors)
    forbid(replace, "pnputil.exe /delete-driver",
           "package deletion in blocked legacy replacement", errors)
    forbid(replace, "Remove-Item",
           "file deletion in blocked legacy replacement", errors)
    require(orphan_cleanup, "if (-not $Execute)",
            "zero-write default orphan cleanup", errors)
    require(orphan_cleanup, "if (-not $IAcceptOrphanCleanup)",
            "explicit orphan cleanup acceptance", errors)
    require(orphan_cleanup, "$service.Status -ne 'Stopped'",
            "stopped-service orphan gate", errors)
    require(orphan_cleanup, "if (Test-Path -LiteralPath $driverPath)",
            "absent experimental SYS gate", errors)
    require(orphan_cleanup, "if ($matchingPackages.Count -ne 0)",
            "absent MCDM package gate", errors)
    require(orphan_cleanup, "reg.exe export $serviceRegistryNative",
            "recoverable service registry snapshot", errors)
    require(orphan_cleanup, "Move-Item -LiteralPath $markerPath -Destination $quarantinedMarker",
            "recoverable marker quarantine", errors)
    require(orphan_cleanup, "sc.exe delete bc250mcdm",
            "exact orphan service deletion", errors)
    forbid(orphan_cleanup, "Remove-Item",
           "irrecoverable deletion in orphan cleanup", errors)
    require(recovery,
            'findstr.exe /I /C:"Class=ComputeAccelerator" /C:"Class=Display"',
            "recovery coverage for compute and display package classes", errors)
    require(rollback, "^Class=(ComputeAccelerator|Display)",
            "rollback coverage for compute and display package classes", errors)

    require(bind, "audit-bc250mcdm-staged.ps1",
            "staged-package gate before binding", errors)
    require(bind, "if (-not $Execute)",
            "zero-write default binding mode", errors)
    require(bind, "if (-not $IAcceptBlackScreenRisk)",
            "explicit black-screen risk acceptance", errors)
    require(bind, "[Environment]::Is64BitProcess",
            "64-bit binding process gate", errors)
    require(bind, "UpdateDriverForPlugAndPlayDevicesW(",
            "explicit binding API", errors)
    require(bind, "$installFlagForce = [uint32]0x00000001",
            "explicit INSTALLFLAG_FORCE value", errors)
    require(bind, "C:\\BC250_RECOVERY.cmd",
            "binding recovery instruction", errors)
    require(bind, "hardware_validated = $false",
            "binding evidence remains unvalidated", errors)
    require(bind, "$watchdogProcess = Start-Process",
            "watchdog armed before binding", errors)
    if bind.find("$watchdogProcess = Start-Process") > bind.find(
            "$nativeOk = [Bc250McdmNewDev.NativeMethods]::UpdateDriverForPlugAndPlayDevicesW("):
        errors.append("watchdog is armed after binding mutation")
    require(bind, "Read-Host 'Confirmacao visual'",
            "explicit visual confirmation before watchdog disarm", errors)
    require(bind, "-IConfirmDisplayIsVisible",
            "visual confirmation acknowledgement", errors)
    forbid(bind, "Start-Service bc250mcdm",
           "manual service start outside PnP binding", errors)
    forbid(bind, "pnputil.exe /delete-driver",
           "package deletion in binding harness", errors)
    require(collector, "Services\\bc250mcdm\\Parameters\\Probe",
            "MCDM probe telemetry collection", errors)
    require(collector, "setupapi-after-tail.txt",
            "post-binding SetupAPI evidence", errors)

    require(watchdog, "[ValidateRange(30,600)][int]$TimeoutSeconds",
            "bounded watchdog timeout", errors)
    require(watchdog, "if (-not $Execute)",
            "zero-write default watchdog mode", errors)
    require(watchdog, "$devices[0].ConfigManagerErrorCode -notin @(0,43)",
            "watchdog exact Code 0/43 rollback gate", errors)
    require(watchdog, "$markerLines[0].ToLowerInvariant() -ne $TargetInf.ToLowerInvariant()",
            "watchdog immutable OEM marker gate", errors)
    require(watchdog, "$manifest.PackageInfSha256",
            "watchdog INF hash gate", errors)
    require(watchdog, "$manifest.PackageSysSha256",
            "watchdog SYS hash gate", errors)
    require(watchdog, "-Execute -IAcceptDisplayInterruptionRisk",
            "watchdog invocation of bounded online rollback", errors)
    forbid(watchdog, "pnputil.exe /delete-driver *",
           "broad package deletion in watchdog", errors)
    require(confirm_display, "if (-not $IConfirmDisplayIsVisible)",
            "explicit visible-display confirmation", errors)
    require(confirm_display, "[uint32]$probe.SkipHwBringup -ne 1",
            "display-only probe gate before disarm", errors)
    require(confirm_display, "DmmIsSupportedResult",
            "display confirmation VidPN support gate", errors)
    require(confirm_display, "DmmCommitHasPath",
            "display confirmation committed-path gate", errors)
    require(confirm_display, "DmmPresentEntered",
            "display confirmation present-entry gate", errors)
    require(confirm_display, "DmmPresentStatus",
            "display confirmation present-success gate", errors)
    require(confirm_display, "DmmControlInterruptCount",
            "display confirmation interrupt-control gate", errors)
    require(confirm_display, "$vsyncDeadline = (Get-Date).AddSeconds(20)",
            "bounded wait for post-notify VSync proof", errors)
    require(confirm_display, "DmmVsyncCount",
            "display confirmation real VSync proof gate", errors)
    require(confirm_display, "Move-Item -LiteralPath $temporaryPath -Destination $finalPath",
            "atomic watchdog disarm publication", errors)

    require(arm_reboot, "[ValidateRange(180,600)][int]$TimeoutSeconds",
            "bounded post-reboot confirmation interval", errors)
    require(arm_reboot, "if (-not $Execute)",
            "zero-write default reboot guard mode", errors)
    require(arm_reboot, "if (-not $IAcceptPersistentRebootGuard)",
            "explicit persistent reboot guard authorization", errors)
    require(arm_reboot, "New-ScheduledTaskTrigger -AtStartup",
            "persistent startup trigger", errors)
    require(arm_reboot, "-UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest",
            "startup guard SYSTEM principal", errors)
    require(arm_reboot, "reboot_executed = $false",
            "arm evidence that reboot was not executed", errors)
    require(arm_reboot, "[string]$oldState.state -ne 'DISARMED'",
            "completed predecessor DISARMED gate", errors)
    require(arm_reboot, "Move-Item -LiteralPath $resolvedGuard -Destination $resolvedQuarantine",
            "recoverable completed guard quarantine", errors)
    for forbidden_reboot_command in (
        "Restart-Computer",
        "shutdown.exe",
        "shutdown /",
    ):
        forbid(arm_reboot, forbidden_reboot_command,
               "reboot command in arming phase", errors)
    require(watchdog_reboot,
            "$currentBootTicks -le [long]$config.armed_boot_ticks",
            "new-boot identity gate", errors)
    require(watchdog_reboot, "if (-not $Execute)",
            "zero-write default persistent watchdog mode", errors)
    require(watchdog_reboot, "Test-Path -LiteralPath $attemptPath",
            "single rollback attempt gate", errors)
    require(watchdog_reboot, "$config.rollback_sha256",
            "immutable rollback script hash gate", errors)
    require(watchdog_reboot, "$config.expected_inf_sha256",
            "post-reboot INF hash gate", errors)
    require(watchdog_reboot, "$config.expected_sys_sha256",
            "post-reboot SYS hash gate", errors)
    require(watchdog_reboot, "-Execute -IAcceptDisplayInterruptionRisk",
            "bounded exact online rollback invocation after timeout", errors)
    forbid(watchdog_reboot, "pnputil.exe /delete-driver *",
           "broad package deletion in persistent watchdog", errors)
    require(confirm_reboot, "if (-not $IConfirmDisplayIsVisible)",
            "explicit post-reboot visual confirmation", errors)
    require(confirm_reboot,
            "$currentBootTicks -le [long]$config.armed_boot_ticks",
            "post-reboot confirmation boot identity", errors)
    require(confirm_reboot, "[string]$state.state -ne 'OBSERVING'",
            "active watchdog state before confirmation", errors)
    require(confirm_reboot, "StartDeviceEntered = 1",
            "current driver start path evidence", errors)
    require(confirm_reboot, "DmmPresentStatus = 0",
            "post-reboot present success gate", errors)
    require(confirm_reboot, "$vsyncDeadline = (Get-Date).AddSeconds(20)",
            "bounded post-reboot VSync proof wait", errors)
    require(confirm_reboot, "DmmVsyncCount",
            "post-reboot real VSync proof gate", errors)
    require(confirm_reboot, "Get-WinEvent",
            "post-reboot warning/error gate", errors)

    require(display, "g_Bc250DisplayState.VsyncProofRecorded",
            "single-shot VSync proof state", errors)
    require(display, "vsyncCount > 0 && Bc250CanRecordDisplayProbePassive()",
            "positive VSync and PASSIVE_LEVEL persistence gate", errors)
    require(display, "&g_Bc250DisplayState.VsyncProofRecorded, 1, 0",
            "atomic single-shot VSync proof claim", errors)
    require(display, "BC250_PROBE_DMM_VSYNC_COUNT, (ULONG)vsyncCount",
            "persisted post-notify VSync count", errors)

    require(rollback, "$targetInf = $markerLines[0].ToLowerInvariant()",
            "marker-derived online rollback target", errors)
    forbid(rollback, "$targetInf = 'oem19.inf'",
           "historically pinned online rollback target", errors)
    require(rollback, "$preflightDevices[0].ConfigManagerErrorCode -notin @(0,43)",
            "Code 0/43 state gate before rollback", errors)
    require(rollback, "$installedDriverHash -ne $expectedDriverHash",
            "authorized installed SYS hash gate", errors)
    require(rollback, "$installedInfHash -ne [string]$manifest.PackageInfSha256",
            "authorized installed INF hash gate", errors)
    require(rollback, "$secondHash -ne $installedDriverHash",
            "second immutable-state gate before rollback", errors)
    require(rollback, "if (-not $Execute)",
            "zero-write default rollback mode", errors)
    require(rollback, "if (-not $IAcceptDisplayInterruptionRisk)",
            "explicit online rollback risk acceptance", errors)
    require(rollback, "pnputil.exe /delete-driver $targetInf /uninstall /force",
            "exact package uninstall operation", errors)
    require(rollback, "$driverHash -ne $installedDriverHash",
            "captured installed SYS residual hash gate", errors)
    require(rollback, "$manifest.SignerThumbprint",
            "authorized signer gate for installed SYS", errors)
    require(rollback, "audit-bc250mcdm-prebinding.ps1",
            "clean-host proof after rollback", errors)
    for forbidden_rollback_path in (
        "pnputil.exe /delete-driver *",
        "Remove-Item -Recurse",
        "sc.exe delete bc250v4",
        "sc.exe delete BasicDisplay",
    ):
        forbid(rollback, forbidden_rollback_path,
               "broad or protected online rollback target", errors)

    if errors:
        print("BC250 MCDM SAFETY AUDIT: FAILED")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("BC250 MCDM SAFETY AUDIT: PASS")
    print("  synthetic ownership/reservations: absent")
    print("  operational DDIs without hardware backing: fail-closed")
    print("  bc250mcdm recovery identity: covered")
    print("  verdict: NAO VALIDADO EM HARDWARE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
