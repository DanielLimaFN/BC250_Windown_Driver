#include "bc250_dxgk_readonly_ddi.h"
#include "bc250_cp_firmware_manifest.h"
#include "bc250_probe_telemetry.h"

static volatile LONG g_Bc250TopologyGeneration;

static void Bc250DxgkResetStartState(BC250_DXGK_READONLY_CONTEXT *Context)
{
    RtlZeroMemory(&Context->DxgkInterface, sizeof(Context->DxgkInterface));
    RtlZeroMemory(&Context->Runtime, sizeof(Context->Runtime));
    (void)Bc250StartDeviceInitialize(&Context->StartDevice);
    Context->RuntimeStaged = 0;
    Context->LastStage = BC250_DXGK_STAGE_EMPTY;
    Context->ReadonlyGatePassed = 0;
    Context->LastCallbackStatus = STATUS_NOT_SUPPORTED;
    Context->LastAdapterStatus = BC250_START_DEVICE_BAD_ARGUMENT;
    Context->LastPciStatus = BC250_PCI_BAR_READ_BAD_ARGUMENT;
    Context->LastRebarStatus = BC250_REBAR_BAD_ARGUMENT;
    Context->LastAddressStatus = BC250_ADDRESS_BAD_ARGUMENT;
    Context->LastPspStatus = BC250_PSP_CONTRACT_BAD_ARGUMENT;
    Context->LastCmStatus = BC250_CM_RESOURCE_BAD_ARGUMENT;
    Context->LastResourceStatus = BC250_RESOURCE_BAD_ARGUMENT;
    Context->LastMmioStatus = BC250_MMIO_TOPOLOGY_BAD_ARGUMENT;
    Context->LastTopologyReaderStatus =
        BC250_VRAM_TOPOLOGY_READER_BAD_ARGUMENT;
    Context->LastDiscoveryTmrStatus =
        BC250_DISCOVERY_TMR_BAD_ARGUMENT;
    Context->LastPlatformSnapshotStatus =
        BC250_PLATFORM_SNAPSHOT_BAD_ARGUMENT;
    Context->PciBytesRead = 0;
    Context->DiscoveredBarCount = 0;
    Context->ResolvedBarCount = 0;
    RtlZeroMemory(Context->ResolvedBars, sizeof(Context->ResolvedBars));
    RtlZeroMemory(&Context->RebarEvidence, sizeof(Context->RebarEvidence));
    RtlZeroMemory(&Context->TopologyEvidence,
                  sizeof(Context->TopologyEvidence));
    RtlZeroMemory(&Context->DiscoveryTmrEvidence,
                  sizeof(Context->DiscoveryTmrEvidence));
    RtlZeroMemory(&Context->PlatformSnapshot,
                  sizeof(Context->PlatformSnapshot));
    (void)Bc250MmioTopologyInitialize(&Context->MmioTransport);
}

NTSTATUS Bc250DxgkReadonlyInitialize(BC250_DXGK_READONLY_CONTEXT *Context)
{
    if (Context == NULL) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Context, sizeof(*Context));
    Context->Signature = BC250_DXGK_READONLY_SIGNATURE;
    Bc250DxgkResetStartState(Context);
    return STATUS_SUCCESS;
}

NTSTATUS Bc250DxgkReadonlyStageRuntime(
    BC250_DXGK_READONLY_CONTEXT *Context,
    const BC250_DXGK_READONLY_INPUT *Runtime)
{
    if (Context == NULL || Runtime == NULL ||
        Context->Signature != BC250_DXGK_READONLY_SIGNATURE ||
        Context->StartDevice.State != BC250_START_DEVICE_EMPTY ||
        Context->RuntimeStaged != 0 ||
        Runtime->ReservationCount > BC250_VRAM_MAX_REGIONS ||
        Runtime->ManifestCount != BC250_CP_BOOTSTRAP_FIRMWARE_COUNT ||
        Runtime->VramApertureBarIndex !=
            BC250_PCI_VRAM_APERTURE_BAR_INDEX ||
        Runtime->MmioBarIndex != BC250_PCI_MMIO_BAR_INDEX ||
        Runtime->TopologyReaderIdentityFlags !=
            BC250_TOPOLOGY_READER_IDENTITY_REQUIRED ||
        Runtime->RuntimeOwnershipFlags !=
            (BC250_OWNERSHIP_EXCLUSIVE_ADAPTER |
             BC250_OWNERSHIP_INVENTORY_COMPLETE) ||
        Runtime->SnapshotGeneration == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    Context->Runtime = *Runtime;
    Context->RuntimeStaged = 1;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceReadonly(
    CONST PVOID MiniportDeviceContext,
    PDXGK_START_INFO DxgkStartInfo,
    PDXGKRNL_INTERFACE DxgkInterface,
    PULONG NumberOfVideoPresentSources,
    PULONG NumberOfChildren)
{
    BC250_DXGK_READONLY_CONTEXT *context =
        (BC250_DXGK_READONLY_CONTEXT *)MiniportDeviceContext;
    DXGK_DEVICE_INFO deviceInfo;
    BC250_CM_RESOURCE_STATUS cmStatus;
    BC250_RESOURCE_STATUS resourceStatus;
    BC250_OWNED_ADAPTER_STATUS ownedStatus;
    BC250_CP_BOOTSTRAP_STATUS bootstrapStatus;
    BC250_VRAM_STATUS vramStatus;
    BC250_ADDRESS_STATUS addressStatus;
    BC250_PSP_CONTRACT_STATUS pspStatus;
    BC250_START_DEVICE_STATUS adapterStatus;
    BC250_PCI_BAR_READ_STATUS pciStatus;
    BC250_REBAR_STATUS rebarStatus;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_VRAM_TOPOLOGY_READER_STATUS topologyReaderStatus;
    BC250_DISCOVERY_TMR_STATUS discoveryTmrStatus;
    BC250_PLATFORM_SNAPSHOT_STATUS platformSnapshotStatus;
    BC250_START_DEVICE_TOPOLOGY capturedTopology;
    BC250_VRAM_TOPOLOGY_READER_INTERFACE topologyReader;
    size_t resolvedBarCount = 0;
    size_t apertureCount = 0;
    uint64_t cpuVisibleBytes = 0;
    size_t i;
    size_t discoveredBarCount = 0;
    ULONG snapshotGeneration;
    uint32_t topologyIdentityFlags;
    SIZE_T interfaceBytes;
    NTSTATUS status;
    const BC250_CP_BOOTSTRAP_POLICY *policy;
    const BC250_CP_FIRMWARE_ENTRY *manifest;
    uint32_t manifestCount;
    const SIZE_T minimumInterfaceBytes =
        FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbGetDeviceInformation) +
        sizeof(DxgkInterface->DxgkCbGetDeviceInformation);

    Bc250ProbeRecord(BC250_PROBE_START_DEVICE_ENTERED, 1);
    if (NumberOfVideoPresentSources != NULL) *NumberOfVideoPresentSources = 0;
    if (NumberOfChildren != NULL) *NumberOfChildren = 0;
    if (context == NULL || DxgkStartInfo == NULL || DxgkInterface == NULL ||
        NumberOfVideoPresentSources == NULL || NumberOfChildren == NULL ||
        context->Signature != BC250_DXGK_READONLY_SIGNATURE ||
        context->StartDevice.State != BC250_START_DEVICE_EMPTY ||
        context->MmioTransport.State != BC250_MMIO_TOPOLOGY_EMPTY ||
        DxgkInterface->Size < minimumInterfaceBytes ||
        DxgkInterface->DeviceHandle == NULL ||
        DxgkInterface->DxgkCbGetDeviceInformation == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    interfaceBytes = DxgkInterface->Size;
    if (interfaceBytes > sizeof(context->DxgkInterface)) {
        interfaceBytes = sizeof(context->DxgkInterface);
    }
    RtlZeroMemory(&context->DxgkInterface, sizeof(context->DxgkInterface));
    RtlCopyMemory(&context->DxgkInterface, DxgkInterface, interfaceBytes);
    context->LastStage = BC250_DXGK_STAGE_INTERFACE_ACCEPTED;
    Bc250ProbeRecord(BC250_PROBE_INTERFACE_ACCEPTED, 1);

    RtlZeroMemory(&deviceInfo, sizeof(deviceInfo));
    status = context->DxgkInterface.DxgkCbGetDeviceInformation(
        context->DxgkInterface.DeviceHandle, &deviceInfo);
    context->LastCallbackStatus = status;
    Bc250ProbeRecord(BC250_PROBE_DEVICE_INFO_STATUS, (ULONG)status);
    if (!NT_SUCCESS(status)) {
        context->LastStage = BC250_DXGK_STAGE_REJECTED;
        return status;
    }
    if (deviceInfo.TranslatedResourceList == NULL ||
        deviceInfo.TranslatedResourceList->Count == 0) {
        context->LastStage = BC250_DXGK_STAGE_REJECTED;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    context->LastStage = BC250_DXGK_STAGE_DEVICE_INFO_READ;

    pciStatus = Bc250ReadPciBarsReadonly(
        &context->DxgkInterface, context->DiscoveredBars,
        BC250_RESOURCE_MAX_BARS, &discoveredBarCount,
        &context->PciBytesRead, &context->LastCallbackStatus);
    context->LastPciStatus = pciStatus;
    Bc250ProbeRecord(BC250_PROBE_PCI_STATUS, (ULONG)pciStatus);
    Bc250ProbeRecord(BC250_PROBE_PCI_BYTES_READ, context->PciBytesRead);
    Bc250ProbeRecord(BC250_PROBE_BAR_COUNT, (ULONG)discoveredBarCount);
    if (pciStatus != BC250_PCI_BAR_READ_OK) {
        context->LastStage = BC250_DXGK_STAGE_REJECTED;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    context->DiscoveredBarCount = (uint32_t)discoveredBarCount;

    RtlZeroMemory(context->ResolvedBars, sizeof(context->ResolvedBars));
    cmStatus = Bc250ResolveCmTranslatedBars(
        deviceInfo.TranslatedResourceList, context->DiscoveredBars,
        context->DiscoveredBarCount, context->ResolvedBars,
        BC250_RESOURCE_MAX_BARS, &resolvedBarCount, &resourceStatus);
    context->LastCmStatus = cmStatus;
    context->LastResourceStatus = resourceStatus;
    context->ResolvedBarCount = (uint32_t)resolvedBarCount;
    Bc250ProbeRecord(BC250_PROBE_CM_STATUS, (ULONG)cmStatus);
    Bc250ProbeRecord(BC250_PROBE_RESOURCE_STATUS, (ULONG)resourceStatus);
    Bc250ProbeRecord(
        BC250_PROBE_RESOLVED_BAR_COUNT, (ULONG)resolvedBarCount);
    if (cmStatus != BC250_CM_RESOURCE_OK) {
        context->LastStage = BC250_DXGK_STAGE_REJECTED;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    for (i = 0; i < resolvedBarCount; ++i) {
        if (context->ResolvedBars[i].Index ==
            BC250_PCI_VRAM_APERTURE_BAR_INDEX) {
            ++apertureCount;
            cpuVisibleBytes = context->ResolvedBars[i].Length;
        }
    }
    if (apertureCount != 1 || cpuVisibleBytes == 0) {
        context->LastStage = BC250_DXGK_STAGE_REJECTED;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    context->LastStage = BC250_DXGK_STAGE_RESOURCES_RESOLVED;

    mmioStatus = Bc250MmioTopologyMapReadonly(
        &context->MmioTransport, &context->DxgkInterface,
        context->ResolvedBars,
        resolvedBarCount, BC250_PCI_MMIO_BAR_INDEX);
    context->LastMmioStatus = mmioStatus;
    context->LastCallbackStatus =
        context->MmioTransport.LastCallbackStatus;
    Bc250ProbeRecord(BC250_PROBE_MMIO_STATUS, (ULONG)mmioStatus);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        context->LastStage = BC250_DXGK_STAGE_REJECTED;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    RtlZeroMemory(&topologyReader, sizeof(topologyReader));
    topologyReader.Read32 = Bc250MmioTopologyRead32;
    topologyReader.Context = &context->MmioTransport;
    topologyIdentityFlags = BC250_TOPOLOGY_READER_IDENTITY_REQUIRED;
    if (context->RuntimeStaged != 0) {
        topologyIdentityFlags = context->Runtime.TopologyReaderIdentityFlags;
    }
    topologyReader.IdentityFlags = topologyIdentityFlags;
    snapshotGeneration = context->RuntimeStaged != 0 ?
        context->Runtime.SnapshotGeneration :
        (ULONG)InterlockedIncrement(&g_Bc250TopologyGeneration);
    if (snapshotGeneration == 0) {
        snapshotGeneration = (ULONG)InterlockedIncrement(
            &g_Bc250TopologyGeneration);
    }
    topologyReaderStatus = Bc250CaptureVramTopologyReadonly(
        &topologyReader, cpuVisibleBytes,
        snapshotGeneration, &context->TopologyEvidence);
    context->LastTopologyReaderStatus = topologyReaderStatus;
    Bc250ProbeRecord(
        BC250_PROBE_TOPOLOGY_READER_STATUS, (ULONG)topologyReaderStatus);
    Bc250ProbeRecord(
        BC250_PROBE_TOPOLOGY_STATUS,
        topologyReaderStatus == BC250_VRAM_TOPOLOGY_READER_OK ?
            (ULONG)context->TopologyEvidence.TopologyStatus : MAXULONG);
    Bc250ProbeRecord(
        BC250_PROBE_TOPOLOGY_READ_COUNT,
        context->TopologyEvidence.ReadCount);
    Bc250ProbeRecord(
        BC250_PROBE_TOPOLOGY_STABLE_PASSES,
        context->TopologyEvidence.StablePasses);

    discoveryTmrStatus = BC250_DISCOVERY_TMR_BAD_ARGUMENT;
    if (topologyReaderStatus == BC250_VRAM_TOPOLOGY_READER_OK) {
        discoveryTmrStatus = Bc250CaptureDiscoveryTmrReadonly(
            &topologyReader,
            context->TopologyEvidence.AddressSpace.TotalBytes,
            snapshotGeneration,
            &context->DiscoveryTmrEvidence);
    }
    context->LastDiscoveryTmrStatus = discoveryTmrStatus;
    Bc250ProbeRecord(
        BC250_PROBE_DISCOVERY_TMR_STATUS, (ULONG)discoveryTmrStatus);
    Bc250ProbeRecord(
        BC250_PROBE_DISCOVERY_TMR_OFFSET_LOW32,
        (ULONG)context->DiscoveryTmrEvidence.Offset);
    Bc250ProbeRecord(
        BC250_PROBE_DISCOVERY_TMR_BYTES,
        (ULONG)context->DiscoveryTmrEvidence.Length);
    Bc250ProbeRecord(
        BC250_PROBE_DISCOVERY_TMR_READ_COUNT,
        context->DiscoveryTmrEvidence.ReadCount);
    Bc250ProbeRecord(
        BC250_PROBE_DISCOVERY_TMR_STABLE_PASSES,
        context->DiscoveryTmrEvidence.StablePasses);

    platformSnapshotStatus = Bc250CapturePlatformRegistersReadonly(
        &topologyReader, snapshotGeneration, &context->PlatformSnapshot);
    context->LastPlatformSnapshotStatus = platformSnapshotStatus;
    Bc250ProbeRecord(BC250_PROBE_PLATFORM_SNAPSHOT_STATUS,
        (ULONG)platformSnapshotStatus);
    Bc250ProbeRecord(BC250_PROBE_PLATFORM_SNAPSHOT_READ_COUNT,
        context->PlatformSnapshot.ReadCount);
    Bc250ProbeRecord(BC250_PROBE_PLATFORM_SNAPSHOT_STABLE_PASSES,
        context->PlatformSnapshot.StablePasses);
    Bc250ProbeRecord(BC250_PROBE_GCVM_PT_BASE_LO,
        context->PlatformSnapshot.GcvmPtBaseLo);
    Bc250ProbeRecord(BC250_PROBE_GCVM_PT_BASE_HI,
        context->PlatformSnapshot.GcvmPtBaseHi);
    Bc250ProbeRecord(BC250_PROBE_GCVM_PT_START_LO,
        context->PlatformSnapshot.GcvmPtStartLo);
    Bc250ProbeRecord(BC250_PROBE_GCVM_PT_START_HI,
        context->PlatformSnapshot.GcvmPtStartHi);
    Bc250ProbeRecord(BC250_PROBE_GCVM_PT_END_LO,
        context->PlatformSnapshot.GcvmPtEndLo);
    Bc250ProbeRecord(BC250_PROBE_GCVM_PT_END_HI,
        context->PlatformSnapshot.GcvmPtEndHi);
    Bc250ProbeRecord(BC250_PROBE_CP_RB0_BASE,
        context->PlatformSnapshot.CpRb0Base);
    Bc250ProbeRecord(BC250_PROBE_CP_RB0_BASE_HI,
        context->PlatformSnapshot.CpRb0BaseHi);
    Bc250ProbeRecord(BC250_PROBE_CP_RB0_CNTL,
        context->PlatformSnapshot.CpRb0Cntl);
    Bc250ProbeRecord(BC250_PROBE_SDMA0_RB_BASE,
        context->PlatformSnapshot.Sdma0RbBase);
    Bc250ProbeRecord(BC250_PROBE_SDMA0_RB_BASE_HI,
        context->PlatformSnapshot.Sdma0RbBaseHi);
    Bc250ProbeRecord(BC250_PROBE_SDMA0_RB_CNTL,
        context->PlatformSnapshot.Sdma0RbCntl);
    Bc250ProbeRecord(BC250_PROBE_CP_RB0_RPTR,
        context->PlatformSnapshot.CpRb0Rptr);
    Bc250ProbeRecord(BC250_PROBE_CP_RB0_RPTR_ADDR_LO,
        context->PlatformSnapshot.CpRb0RptrAddrLo);
    Bc250ProbeRecord(BC250_PROBE_CP_RB0_RPTR_ADDR_HI,
        context->PlatformSnapshot.CpRb0RptrAddrHi);
    Bc250ProbeRecord(BC250_PROBE_CP_RB0_WPTR,
        context->PlatformSnapshot.CpRb0Wptr);
    Bc250ProbeRecord(BC250_PROBE_CP_RB0_WPTR_HI,
        context->PlatformSnapshot.CpRb0WptrHi);
    Bc250ProbeRecord(BC250_PROBE_SDMA0_RB_RPTR,
        context->PlatformSnapshot.Sdma0RbRptr);
    Bc250ProbeRecord(BC250_PROBE_SDMA0_RB_RPTR_HI,
        context->PlatformSnapshot.Sdma0RbRptrHi);
    Bc250ProbeRecord(BC250_PROBE_SDMA0_RB_WPTR,
        context->PlatformSnapshot.Sdma0RbWptr);
    Bc250ProbeRecord(BC250_PROBE_SDMA0_RB_WPTR_HI,
        context->PlatformSnapshot.Sdma0RbWptrHi);
    Bc250ProbeRecord(BC250_PROBE_SDMA0_RB_RPTR_ADDR_LO,
        context->PlatformSnapshot.Sdma0RbRptrAddrLo);
    Bc250ProbeRecord(BC250_PROBE_SDMA0_RB_RPTR_ADDR_HI,
        context->PlatformSnapshot.Sdma0RbRptrAddrHi);
    Bc250ProbeRecord(BC250_PROBE_PSP_C2PMSG_35,
        context->PlatformSnapshot.PspC2pmsg35);
    Bc250ProbeRecord(BC250_PROBE_PSP_C2PMSG_36,
        context->PlatformSnapshot.PspC2pmsg36);
    Bc250ProbeRecord(BC250_PROBE_PSP_C2PMSG_37,
        context->PlatformSnapshot.PspC2pmsg37);
    Bc250ProbeRecord(BC250_PROBE_PSP_C2PMSG_81,
        context->PlatformSnapshot.PspC2pmsg81);
    Bc250ProbeRecord(BC250_PROBE_PSP_C2PMSG_64,
        context->PlatformSnapshot.PspC2pmsg64);
    Bc250ProbeRecord(BC250_PROBE_PSP_C2PMSG_67,
        context->PlatformSnapshot.PspC2pmsg67);
    Bc250ProbeRecord(BC250_PROBE_PSP_C2PMSG_69,
        context->PlatformSnapshot.PspC2pmsg69);
    Bc250ProbeRecord(BC250_PROBE_PSP_C2PMSG_70,
        context->PlatformSnapshot.PspC2pmsg70);
    Bc250ProbeRecord(BC250_PROBE_PSP_C2PMSG_71,
        context->PlatformSnapshot.PspC2pmsg71);

    mmioStatus = Bc250MmioTopologyUnmap(&context->MmioTransport);
    context->LastMmioStatus = mmioStatus;
    context->LastCallbackStatus =
        context->MmioTransport.LastCallbackStatus;
    Bc250ProbeRecord(BC250_PROBE_MMIO_STATUS, (ULONG)mmioStatus);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        context->LastStage = BC250_DXGK_STAGE_MMIO_UNMAP_FAILED;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (topologyReaderStatus != BC250_VRAM_TOPOLOGY_READER_OK) {
        context->LastStage = BC250_DXGK_STAGE_REJECTED;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (platformSnapshotStatus != BC250_PLATFORM_SNAPSHOT_OK) {
        context->LastStage = BC250_DXGK_STAGE_REJECTED;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    context->LastStage = BC250_DXGK_STAGE_TOPOLOGY_CAPTURED;
    Bc250ProbeRecord(
        BC250_PROBE_VRAM_TOTAL_MIB,
        context->TopologyEvidence.Snapshot.RccConfigMemsizeMiB);
    Bc250ProbeRecord(
        BC250_PROBE_CPU_VISIBLE_MIB,
        (ULONG)(context->TopologyEvidence.Snapshot.CpuVisibleBytes >> 20));
    Bc250ProbeRecord(
        BC250_PROBE_FB_BASE_16M,
        context->TopologyEvidence.Snapshot.GcmcVmFbLocationBase);
    Bc250ProbeRecord(
        BC250_PROBE_FB_TOP_16M,
        context->TopologyEvidence.Snapshot.GcmcVmFbLocationTop);
    Bc250ProbeRecord(
        BC250_PROBE_FB_OFFSET_16M,
        context->TopologyEvidence.Snapshot.GcmcVmFbOffset);

    rebarStatus = Bc250ReadRebarReadonly(
        &context->DxgkInterface, context->ResolvedBars, resolvedBarCount,
        BC250_PCI_VRAM_APERTURE_BAR_INDEX,
        &context->RebarEvidence, &context->LastCallbackStatus);
    context->LastRebarStatus = rebarStatus;
    Bc250ProbeRecord(BC250_PROBE_REBAR_STATUS, (ULONG)rebarStatus);
    Bc250ProbeRecord(
        BC250_PROBE_REBAR_SUPPORTED_MASK,
        context->RebarEvidence.SizesSupportedMask);
    Bc250ProbeRecord(
        BC250_PROBE_REBAR_SELECTED_ENCODING,
        context->RebarEvidence.SelectedSizeEncoding);
    Bc250ProbeRecord(
        BC250_PROBE_REBAR_SELECTED_MIB,
        (ULONG)(context->RebarEvidence.SelectedBytes >> 20));
    Bc250ProbeRecord(
        BC250_PROBE_REBAR_ASSIGNED_MIB,
        (ULONG)(context->RebarEvidence.AssignedBytes >> 20));
    Bc250ProbeRecord(
        BC250_PROBE_REBAR_READ_COUNT,
        context->RebarEvidence.ReadCount);
    if (rebarStatus != BC250_REBAR_OK) {
        context->LastStage = BC250_DXGK_STAGE_REJECTED;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (context->RuntimeStaged == 0) {
        /*
         * Topologia e ReBAR nao provam que uma faixa de VRAM esta livre nem
         * fora de scanout. Sem produtor runtime, nao invente ownership.
         */
        context->LastStage = BC250_DXGK_STAGE_RUNTIME_MISSING;
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_MISSING, 1);
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&capturedTopology, sizeof(capturedTopology));
    capturedTopology.VramApertureBarIndex =
        BC250_PCI_VRAM_APERTURE_BAR_INDEX;
    capturedTopology.RuntimeEvidenceFlags =
        BC250_OWNERSHIP_VRAM_TOTAL_RUNTIME |
        BC250_OWNERSHIP_MC_RANGE_RUNTIME |
        BC250_OWNERSHIP_FB_OFFSET_RUNTIME |
        context->Runtime.RuntimeOwnershipFlags;
    capturedTopology.VramTotalBytes =
        context->TopologyEvidence.AddressSpace.TotalBytes;
    capturedTopology.VramMcStart =
        context->TopologyEvidence.AddressSpace.VramMcStart;
    capturedTopology.VramMcEnd =
        context->TopologyEvidence.AddressSpace.VramMcEnd;
    capturedTopology.VramBaseOffset =
        context->TopologyEvidence.AddressSpace.VramBaseOffset;
    capturedTopology.SnapshotGeneration =
        context->TopologyEvidence.AddressSpace.SnapshotGeneration;
    adapterStatus = Bc250StartDevicePrepareOwnership(
        &context->StartDevice, deviceInfo.TranslatedResourceList,
        context->DiscoveredBars, context->DiscoveredBarCount,
        &capturedTopology,
        context->RuntimeStaged != 0 ? context->Runtime.Reservations : NULL,
        context->RuntimeStaged != 0 ? context->Runtime.ReservationCount : 0,
        &cmStatus, &resourceStatus,
        &ownedStatus, &bootstrapStatus, &vramStatus);
    context->LastAdapterStatus = adapterStatus;
    Bc250ProbeRecord(BC250_PROBE_ADAPTER_STATUS, (ULONG)adapterStatus);
    Bc250ProbeRecord(BC250_PROBE_OWNED_STATUS, (ULONG)ownedStatus);
    Bc250ProbeRecord(BC250_PROBE_BOOTSTRAP_STATUS, (ULONG)bootstrapStatus);
    Bc250ProbeRecord(BC250_PROBE_VRAM_STATUS, (ULONG)vramStatus);
    if (adapterStatus != BC250_START_DEVICE_OK) {
        context->LastStage = BC250_DXGK_STAGE_REJECTED;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    context->LastStage = BC250_DXGK_STAGE_OWNERSHIP_READY;
    Bc250ProbeRecord(
        BC250_PROBE_OWNERSHIP_STATE, context->StartDevice.State);
    Bc250ProbeRecord(
        BC250_PROBE_OWNERSHIP_FLAGS,
        context->StartDevice.Owned.OwnershipEvidenceFlags);

    policy = &context->Runtime.Policy;
    manifest = context->Runtime.Manifest;
    manifestCount = context->Runtime.ManifestCount;

    adapterStatus = Bc250StartDeviceBuildReadonlyPreflight(
        &context->StartDevice, policy, manifest, manifestCount,
        &vramStatus, &addressStatus, &pspStatus, &bootstrapStatus);
    context->LastAdapterStatus = adapterStatus;
    context->LastAddressStatus = addressStatus;
    context->LastPspStatus = pspStatus;
    Bc250ProbeRecord(BC250_PROBE_ADAPTER_STATUS, (ULONG)adapterStatus);
    Bc250ProbeRecord(BC250_PROBE_BOOTSTRAP_STATUS, (ULONG)bootstrapStatus);
    Bc250ProbeRecord(BC250_PROBE_VRAM_STATUS, (ULONG)vramStatus);
    Bc250ProbeRecord(BC250_PROBE_ADDRESS_STATUS, (ULONG)addressStatus);
    Bc250ProbeRecord(BC250_PROBE_PSP_STATUS, (ULONG)pspStatus);
    if (adapterStatus != BC250_START_DEVICE_OK) {
        context->LastStage = BC250_DXGK_STAGE_REJECTED;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    context->LastStage = BC250_DXGK_STAGE_PREFLIGHT_READY;
    context->ReadonlyGatePassed = 1;
    Bc250ProbeRecord(
        BC250_PROBE_MANIFEST_CONTRACT_VERSION,
        BC250_CP_MANIFEST_CONTRACT_VERSION);
    Bc250ProbeRecord(BC250_PROBE_PFP_PAYLOAD_BYTES,
                     manifest[0].PayloadBytes);
    Bc250ProbeRecord(BC250_PROBE_ME_PAYLOAD_BYTES,
                     manifest[1].PayloadBytes);
    Bc250ProbeRecord(BC250_PROBE_CE_PAYLOAD_BYTES,
                     manifest[2].PayloadBytes);
    Bc250ProbeRecord(BC250_PROBE_MEC_PAYLOAD_BYTES,
                     manifest[3].PayloadBytes);
    Bc250ProbeRecord(BC250_PROBE_MEC_JT1_PAYLOAD_BYTES,
                     manifest[4].PayloadBytes);
    Bc250ProbeRecord(
        BC250_PROBE_CANDIDATE_TMR_OFFSET_LOW32,
        (ULONG)context->StartDevice.Preflight.TmrPlan.Binding.Offset);
    Bc250ProbeRecord(
        BC250_PROBE_CANDIDATE_TMR_BYTES,
        context->StartDevice.Preflight.TmrPlan.BufferSize);
    Bc250ProbeRecord(
        BC250_PROBE_STAGING_OFFSET_LOW32,
        (ULONG)context->StartDevice.Preflight.FirmwarePlans[0].Binding.Offset);
    Bc250ProbeRecord(
        BC250_PROBE_STAGING_BYTES,
        context->StartDevice.Preflight.FirmwarePlans[3].PayloadBytes);
    Bc250ProbeRecord(BC250_PROBE_FIRMWARE_BYTES_STAGED, 0);
    Bc250ProbeRecord(BC250_PROBE_PSP_TRANSPORT_CALLS, 0);
    Bc250ProbeRecord(BC250_PROBE_READONLY_GATE_PASSED, 1);

    /*
     * Intencional: ainda nao ha engine, scheduler, interrupt, reset ou power.
     * O preflight pode ser inspecionado, mas esta DDI nao anuncia um adapter.
     */
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250DxgkDdiStopDeviceReadonly(
    CONST PVOID MiniportDeviceContext)
{
    BC250_DXGK_READONLY_CONTEXT *context =
        (BC250_DXGK_READONLY_CONTEXT *)MiniportDeviceContext;
    Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_ENTERED, 1);
    if (context == NULL ||
        context->Signature != BC250_DXGK_READONLY_SIGNATURE) {
        Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_STATUS,
            (ULONG)STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    if (context->MmioTransport.State != BC250_MMIO_TOPOLOGY_EMPTY) {
        BC250_MMIO_TOPOLOGY_STATUS mmioStatus =
            Bc250MmioTopologyUnmap(&context->MmioTransport);
        context->LastMmioStatus = mmioStatus;
        context->LastCallbackStatus =
            context->MmioTransport.LastCallbackStatus;
        if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
            context->LastStage = BC250_DXGK_STAGE_MMIO_UNMAP_FAILED;
            Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_STATUS,
                (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }
    (void)Bc250StartDeviceTeardown(&context->StartDevice);
    Bc250DxgkResetStartState(context);
    Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_STATUS, (ULONG)STATUS_SUCCESS);
    return STATUS_SUCCESS;
}
