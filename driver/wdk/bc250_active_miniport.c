#include "bc250_active_miniport.h"
#include "bc250_mcdm_callbacks.h"
#include "bc250_display_ddi.h"
#include "bc250_probe_telemetry.h"
#include "bc250_cp_firmware_manifest.h"
#include "../core/bc250_adapter_info_contract.h"
#include "../core/bc250_allocation_abi_contract.h"
#include "../core/bc250_mcdm_debug_contract.h"
#include "../core/bc250_firmware_staging.h"

DRIVER_INITIALIZATION_DATA g_Bc250ActiveInitializationData;

#define BC250_ACTIVE_MAX_ADAPTERS 8u
static EX_PUSH_LOCK g_Bc250AdapterDirectoryLock;
static BC250_DXGK_ACTIVE_CONTEXT *
    g_Bc250AdapterDirectory[BC250_ACTIVE_MAX_ADAPTERS];

static uint32_t Bc250ActiveExecuteGcvmBacking(
    void *Context, uint64_t PageTablePhysical);
static BC250_PSP_RING_STATUS Bc250ActiveProbePspRing(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PSP_RING_EVIDENCE *Evidence);
static BC250_PSP_LOAD_FW_STATUS Bc250ActiveLoadSdmaFirmware(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PSP_LOAD_FW_EVIDENCE *Evidence);
static BC250_PSP_LOAD_FW_STATUS Bc250ActiveLoadRlcFirmware(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PSP_LOAD_FW_EVIDENCE *Evidence,
    BC250_RLC_LOAD_EVIDENCE *RlcEvidence);
static void Bc250ActiveTlbStall(void *Context, uint32_t Microseconds);
static void Bc250ActivePspBarrier(void *Context);
static uint32_t Bc250ActiveVmidRead32(
    void *Context, uint32_t ByteOffset, uint32_t *Value);
static uint32_t Bc250ActiveVmidWrite32(
    void *Context, uint32_t ByteOffset, uint32_t Value);

static ULONG Bc250ActiveReadTunable(PCWSTR ValueName, ULONG DefaultValue)
{
#if defined(BC250_ENABLE_PERSISTENT_TELEMETRY)
    RTL_QUERY_REGISTRY_TABLE table[2];
    ULONG val = DefaultValue;
    NTSTATUS status;

    RtlZeroMemory(table, sizeof(table));
    table[0].Flags         = RTL_QUERY_REGISTRY_DIRECT;
    table[0].Name          = (PWSTR)ValueName;
    table[0].EntryContext  = &val;
    table[0].DefaultType   = REG_DWORD;
    table[0].DefaultData   = &DefaultValue;
    table[0].DefaultLength = sizeof(ULONG);

    status = RtlQueryRegistryValues(
        RTL_REGISTRY_SERVICES,
        L"bc250mcdm\\Parameters",
        table,
        NULL,
        NULL);

    return NT_SUCCESS(status) ? val : DefaultValue;
#else
    UNREFERENCED_PARAMETER(ValueName);
    return DefaultValue;
#endif
}

static BC250_HQD_PREFLIGHT_STATUS Bc250ActiveProbeHqdReadonly(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_HQD_READONLY_SNAPSHOT *Snapshot,
    uint32_t *ReadCount)
{
    static const uint32_t offsets[BC250_HQD_PREFLIGHT_REGISTER_COUNT] = {
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
        BC250_HQD_MMIO_MQD_BASE_LO,
        BC250_HQD_MMIO_MQD_BASE_HI,
        BC250_HQD_MMIO_ACTIVE,
        BC250_HQD_MMIO_VMID,
        BC250_HQD_MMIO_PQ_BASE_LO,
        BC250_HQD_MMIO_PQ_BASE_HI,
        BC250_HQD_MMIO_PQ_RPTR,
        BC250_HQD_MMIO_DOORBELL_CONTROL,
        BC250_HQD_MMIO_PQ_CONTROL,
        BC250_HQD_MMIO_PQ_WPTR_LO,
        BC250_HQD_MMIO_PQ_WPTR_HI,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET
    };
    uint32_t pass1[BC250_HQD_PREFLIGHT_REGISTER_COUNT];
    uint32_t pass2[BC250_HQD_PREFLIGHT_REGISTER_COUNT];
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_HQD_PREFLIGHT_STATUS status =
        BC250_HQD_PREFLIGHT_TRANSPORT_FAILED;
    uint32_t reads = 0u, writes = 0u, index;

    if (Context == NULL || Snapshot == NULL || ReadCount == NULL) {
        return BC250_HQD_PREFLIGHT_BAD_ARGUMENT;
    }
    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    *ReadCount = 0u;
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) return status;

    for (index = 0u; index < BC250_HQD_PREFLIGHT_REGISTER_COUNT; ++index) {
        if (!Bc250MmioTopologyRead32(
                &Context->Core.MmioTransport, offsets[index], &pass1[index])) {
            goto Unmap;
        }
    }
    KeMemoryBarrier();
    for (index = 0u; index < BC250_HQD_PREFLIGHT_REGISTER_COUNT; ++index) {
        if (!Bc250MmioTopologyRead32(
                &Context->Core.MmioTransport, offsets[index], &pass2[index])) {
            goto Unmap;
        }
    }
    status = Bc250ComputeHqdReadonlyPreflight(pass1, pass2, Snapshot);

Unmap:
    reads = Context->Core.MmioTransport.ReadCount;
    writes = Context->Core.MmioTransport.WriteCount;
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    *ReadCount = reads;
    if (writes != 0u) return BC250_HQD_PREFLIGHT_WRITE_DETECTED;
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK)
        return BC250_HQD_PREFLIGHT_TRANSPORT_FAILED;
    return status;
}

static BC250_HQD_SELECTOR_STATUS Bc250ActiveProbeMecHqdSelected(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_HQD_SELECTOR_EVIDENCE *Evidence)
{
    BC250_HQD_SELECTOR_INTERFACE interfaceData;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_HQD_SELECTOR_STATUS status;
    uint32_t reads, writes;

    if (Context == NULL || Evidence == NULL)
        return BC250_HQD_SELECTOR_BAD_ARGUMENT;
    RtlZeroMemory(Evidence, sizeof(*Evidence));
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK)
        return BC250_HQD_SELECTOR_PREFLIGHT_FAILED;
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Barrier = Bc250ActivePspBarrier;
    interfaceData.Context = &Context->Core.MmioTransport;
    status = Bc250ComputeProbeMecHqdSelected(&interfaceData, Evidence);
    reads = Context->Core.MmioTransport.ReadCount;
    writes = Context->Core.MmioTransport.WriteCount;
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK ||
        reads != Evidence->ReadCalls || writes != Evidence->WriteCalls) {
        Evidence->PhysicalReady = 0u;
        return BC250_HQD_SELECTOR_RESTORE_FAILED;
    }
    return status;
}

static BC250_HQD_BANK_PROBE_STATUS Bc250ActiveProbeMecHqdBankIsolation(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_HQD_BANK_PROBE_EVIDENCE *Evidence)
{
    BC250_HQD_SELECTOR_INTERFACE interfaceData;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_HQD_BANK_PROBE_STATUS status;
    uint32_t reads, writes;

    if (Context == NULL || Evidence == NULL)
        return BC250_HQD_BANK_PROBE_BAD_ARGUMENT;
    RtlZeroMemory(Evidence, sizeof(*Evidence));
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK)
        return BC250_HQD_BANK_PROBE_PREFLIGHT_FAILED;
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Barrier = Bc250ActivePspBarrier;
    interfaceData.Context = &Context->Core.MmioTransport;
    status = Bc250ComputeProbeMecHqdBankIsolation(
        &interfaceData, Evidence);
    reads = Context->Core.MmioTransport.ReadCount;
    writes = Context->Core.MmioTransport.WriteCount;
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK ||
        reads != Evidence->ReadCalls || writes != Evidence->WriteCalls) {
        Evidence->PhysicalReady = 0u;
        return BC250_HQD_BANK_PROBE_RESTORE_FAILED;
    }
    return status;
}

static BC250_HQD_WPTR_DIAGNOSTIC_STATUS Bc250ActiveProbeMecHqdWptrBehavior(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_HQD_WPTR_DIAGNOSTIC_EVIDENCE *Evidence)
{
    BC250_HQD_SELECTOR_INTERFACE interfaceData;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_HQD_WPTR_DIAGNOSTIC_STATUS status;
    uint32_t reads, writes;

    if (Context == NULL || Evidence == NULL)
        return BC250_HQD_WPTR_DIAG_BAD_ARGUMENT;
    RtlZeroMemory(Evidence, sizeof(*Evidence));
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK)
        return BC250_HQD_WPTR_DIAG_PREFLIGHT_FAILED;
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Barrier = Bc250ActivePspBarrier;
    interfaceData.Context = &Context->Core.MmioTransport;
    status = Bc250ComputeProbeMecHqdWptrBehavior(&interfaceData, Evidence);
    reads = Context->Core.MmioTransport.ReadCount;
    writes = Context->Core.MmioTransport.WriteCount;
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK ||
        reads != Evidence->ReadCalls || writes != Evidence->WriteCalls) {
        Evidence->PhysicalReady = 0u;
        return BC250_HQD_WPTR_DIAG_RESTORE_FAILED;
    }
    return status;
}

BC250_HQD_PROGRAM_STATUS Bc250ActiveRunInactiveHqdProgramTransaction(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_HQD_PROGRAM_EVIDENCE *Evidence)
{
    BC250_HQD_SELECTOR_INTERFACE interfaceData;
    BC250_HQD_MC_BUFFERS buffers;
    BC250_COMPUTE_MQD_PROPERTIES props;
    BC250_GFX10_COMPUTE_MQD mqd;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_HQD_PROGRAM_STATUS status;
    BC250_VRAM_ALLOCATOR *allocator;
    const BC250_VRAM_ADDRESS_SPACE *addressSpace;
    BC250_VRAM_TOKEN token = {0};
    BC250_VRAM_REGION region = {0};
    BC250_VRAM_STATUS vramStatus;
    const BC250_RESOLVED_BAR_RESOURCE *bar = NULL;
    PHYSICAL_ADDRESS physical;
    volatile uint32_t *mapped = NULL;
    NTSTATUS ntStatus;
    uint32_t totalBytes = 73728u; /* 72 KiB */
    uint32_t reads, writes;
    uint32_t i;
    size_t matchCount = 0;

    if (Context == NULL || Evidence == NULL)
        return BC250_HQD_PROGRAM_BAD_ARGUMENT;

    RtlZeroMemory(Evidence, sizeof(*Evidence));
    RtlZeroMemory(&buffers, sizeof(buffers));
    RtlZeroMemory(&props, sizeof(props));
    RtlZeroMemory(&mqd, sizeof(mqd));

    allocator = &Context->Core.StartDevice.Owned.Bootstrap.Allocator;
    addressSpace = &Context->Core.TopologyEvidence.AddressSpace;
    if (addressSpace->TotalBytes == 0u || addressSpace->VramMcStart == 0u) {
        return BC250_HQD_PROGRAM_PREFLIGHT_FAILED;
    }

    for (i = 0; i < Context->Core.ResolvedBarCount; ++i) {
        if (Context->Core.ResolvedBars[i].Index == BC250_PCI_VRAM_APERTURE_BAR_INDEX) {
            bar = &Context->Core.ResolvedBars[i];
            ++matchCount;
        }
    }
    if (matchCount != 1 || bar == NULL) {
        return BC250_HQD_PROGRAM_PREFLIGHT_FAILED;
    }

    vramStatus = Bc250VramAllocate(
        allocator, totalBytes, 256u,
        BC250_VRAM_ROLE_MQD, BC250_VRAM_ALLOC_CPU_VISIBLE, &token);
    if (vramStatus != BC250_VRAM_OK) {
        return BC250_HQD_PROGRAM_PREFLIGHT_FAILED;
    }

    if (Bc250VramGetRegion(allocator, token, &region) != BC250_VRAM_OK) {
        (void)Bc250VramRelease(allocator, token);
        return BC250_HQD_PROGRAM_PREFLIGHT_FAILED;
    }

    physical.QuadPart = (LONGLONG)(bar->Start + region.Offset);
    ntStatus = Context->Core.DxgkInterface.DxgkCbMapMemory(
        Context->Core.DxgkInterface.DeviceHandle,
        physical,
        (ULONG)totalBytes,
        FALSE,
        FALSE,
        MmNonCached,
        (PVOID *)&mapped);
    if (!NT_SUCCESS(ntStatus) || mapped == NULL) {
        (void)Bc250VramRelease(allocator, token);
        return BC250_HQD_PROGRAM_PREFLIGHT_FAILED;
    }

    buffers.MqdVirtual = (volatile void *)((PUCHAR)mapped + 0x0000);
    buffers.MqdGpuAddress = addressSpace->VramMcStart + region.Offset + 0x0000;
    buffers.MqdSizeBytes = BC250_MQD_SIZE_BYTES;

    buffers.RingVirtual = (volatile void *)((PUCHAR)mapped + 0x0200);
    buffers.RingGpuAddress = addressSpace->VramMcStart + region.Offset + 0x0200;
    buffers.RingSizeBytes = 65536u;

    buffers.EopVirtual = (volatile void *)((PUCHAR)mapped + 0x10200);
    buffers.EopGpuAddress = addressSpace->VramMcStart + region.Offset + 0x10200;
    buffers.EopSizeBytes = 4096u;

    buffers.RptrVirtual = (volatile void *)((PUCHAR)mapped + 0x11200);
    buffers.RptrGpuAddress = addressSpace->VramMcStart + region.Offset + 0x11200;
    buffers.RptrSizeBytes = 8u;

    buffers.WptrVirtual = (volatile void *)((PUCHAR)mapped + 0x11300);
    buffers.WptrGpuAddress = addressSpace->VramMcStart + region.Offset + 0x11300;
    buffers.WptrSizeBytes = 8u;

    buffers.FenceVirtual = (volatile void *)((PUCHAR)mapped + 0x11400);
    buffers.FenceGpuAddress = addressSpace->VramMcStart + region.Offset + 0x11400;
    buffers.FenceSizeBytes = 8u;

    props.MqdGpuAddress = buffers.MqdGpuAddress;
    props.HqdBaseGpuAddress = buffers.RingGpuAddress;
    props.QueueSizeDwords = 16384u;
    props.EopGpuAddress = buffers.EopGpuAddress;
    props.EopSizeBytes = 4096u;
    props.RptrGpuAddress = buffers.RptrGpuAddress;
    props.WptrGpuAddress = buffers.WptrGpuAddress;
    props.UseDoorbell = 0u;
    props.DoorbellIndex = 0u;
    props.PipePriority = 0u;
    props.QueuePriority = 0u;
    props.HqdActive = 0u;

    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        (void)Context->Core.DxgkInterface.DxgkCbUnmapMemory(
            Context->Core.DxgkInterface.DeviceHandle, (PVOID)mapped);
        (void)Bc250VramRelease(allocator, token);
        return BC250_HQD_PROGRAM_PREFLIGHT_FAILED;
    }

    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Barrier = Bc250ActivePspBarrier;
    interfaceData.Context = &Context->Core.MmioTransport;

    status = Bc250ComputeRunInactiveHqdProgramTransaction(
        &interfaceData, &buffers, &props, &mqd, Evidence);

    reads = Context->Core.MmioTransport.ReadCount;
    writes = Context->Core.MmioTransport.WriteCount;
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);

    (void)Context->Core.DxgkInterface.DxgkCbUnmapMemory(
        Context->Core.DxgkInterface.DeviceHandle, (PVOID)mapped);
    (void)Bc250VramRelease(allocator, token);

    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK ||
        reads != Evidence->ReadCalls || writes != Evidence->WriteCalls) {
        Evidence->PhysicalReady = 0u;
        return BC250_HQD_PROGRAM_RESTORE_FAILED;
    }

    return status;
}

static BC250_HQD_ACTIVE_STATUS Bc250ActiveRunActiveHqdSubmissionTransaction(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE *Evidence)
{
    BC250_HQD_SELECTOR_INTERFACE interfaceData;
    BC250_HQD_MC_BUFFERS buffers;
    BC250_COMPUTE_MQD_PROPERTIES props;
    BC250_GFX10_COMPUTE_MQD mqd;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_HQD_ACTIVE_STATUS status;
    BC250_VRAM_ALLOCATOR *allocator;
    const BC250_VRAM_ADDRESS_SPACE *addressSpace;
    BC250_VRAM_TOKEN token = {0};
    BC250_VRAM_REGION region = {0};
    BC250_VRAM_STATUS vramStatus;
    const BC250_RESOLVED_BAR_RESOURCE *bar = NULL;
    PHYSICAL_ADDRESS physical;
    volatile uint32_t *mapped = NULL;
    NTSTATUS ntStatus;
    uint32_t totalBytes = 131072u; /* 128 KiB para garantir alinhamento de 64 KiB no anel */
    uint32_t reads, writes;
    uint32_t i;
    size_t matchCount = 0;

    if (Context == NULL || Evidence == NULL)
        return BC250_HQD_ACTIVE_BAD_ARGUMENT;

    RtlZeroMemory(Evidence, sizeof(*Evidence));
    Evidence->GfxOffApplicable = 0u;
    Evidence->GfxOffGuardPassed = 1u;
    Evidence->GfxOffRestorePassed = 1u;
    RtlZeroMemory(&buffers, sizeof(buffers));
    RtlZeroMemory(&props, sizeof(props));
    RtlZeroMemory(&mqd, sizeof(mqd));

    allocator = &Context->Core.StartDevice.Owned.Bootstrap.Allocator;
    addressSpace = &Context->Core.TopologyEvidence.AddressSpace;
    if (addressSpace->TotalBytes == 0u || addressSpace->VramMcStart == 0u) {
        return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
    }

    for (i = 0; i < Context->Core.ResolvedBarCount; ++i) {
        if (Context->Core.ResolvedBars[i].Index == BC250_PCI_VRAM_APERTURE_BAR_INDEX) {
            bar = &Context->Core.ResolvedBars[i];
            ++matchCount;
        }
    }
    if (matchCount != 1 || bar == NULL) {
        return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
    }

    vramStatus = Bc250VramAllocate(
        allocator, totalBytes, 65536u,
        BC250_VRAM_ROLE_MQD, BC250_VRAM_ALLOC_CPU_VISIBLE, &token);
    if (vramStatus != BC250_VRAM_OK) {
        return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
    }

    if (Bc250VramGetRegion(allocator, token, &region) != BC250_VRAM_OK) {
        (void)Bc250VramRelease(allocator, token);
        return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
    }

    physical.QuadPart = (LONGLONG)(bar->Start + region.Offset);
    ntStatus = Context->Core.DxgkInterface.DxgkCbMapMemory(
        Context->Core.DxgkInterface.DeviceHandle,
        physical,
        (ULONG)totalBytes,
        FALSE,
        FALSE,
        MmNonCached,
        (PVOID *)&mapped);
    if (!NT_SUCCESS(ntStatus) || mapped == NULL) {
        (void)Bc250VramRelease(allocator, token);
        return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
    }

    {
        uint64_t basePhys = (addressSpace->VramMcStart + region.Offset);

        /* Enderecos MC fisicos em VRAM para o microengine MEC (VMID 0) */
        buffers.MqdVirtual = (volatile void *)((PUCHAR)mapped + 0x0000);
        buffers.MqdGpuAddress = basePhys + 0x0000ull;
        buffers.MqdSizeBytes = BC250_MQD_SIZE_BYTES;

        buffers.EopVirtual = (volatile void *)((PUCHAR)mapped + 0x1000);
        buffers.EopGpuAddress = basePhys + 0x1000ull;
        buffers.EopSizeBytes = 4096u;

        buffers.RptrVirtual = (volatile void *)((PUCHAR)mapped + 0x2000);
        buffers.RptrGpuAddress = basePhys + 0x2000ull;
        buffers.RptrSizeBytes = 256u;

        buffers.WptrVirtual = (volatile void *)((PUCHAR)mapped + 0x2100);
        buffers.WptrGpuAddress = basePhys + 0x2100ull;
        buffers.WptrSizeBytes = 256u;

        buffers.FenceVirtual = (volatile void *)((PUCHAR)mapped + 0x2200);
        buffers.FenceGpuAddress = basePhys + 0x2200ull;
        buffers.FenceSizeBytes = 256u;

        /* Page Table Context 0 (VMID 0) em 0x3000 (4 KiB = 512 PTEs) */
        buffers.PtVirtual = (volatile void *)((PUCHAR)mapped + 0x3000);
        buffers.PtGpuAddress = basePhys + 0x3000ull;
        buffers.PtSizeBytes = 4096u;

        {
            volatile uint64_t *pPt = (volatile uint64_t *)buffers.PtVirtual;
            uint32_t pteIdx;
            for (pteIdx = 0u; pteIdx < 512u; ++pteIdx) {
                uint64_t pagePhys = basePhys + ((uint64_t)pteIdx * 4096ull);
                pPt[pteIdx] = (pagePhys & 0x0000FFFFFFFFF000ull) | 0xE1ull;
            }
            KeMemoryBarrier();
        }

        /* Ring buffer em 0x10000 (64 KiB) perfeitamente alinhado a 64 KiB */
        buffers.RingVirtual = (volatile void *)((PUCHAR)mapped + 0x10000);
        buffers.RingGpuAddress = basePhys + 0x10000ull;
        buffers.RingSizeBytes = 65536u;
    }

    props.MqdGpuAddress = buffers.MqdGpuAddress;
    props.HqdBaseGpuAddress = buffers.RingGpuAddress;
    props.QueueSizeDwords = 16384u;
    props.EopGpuAddress = buffers.EopGpuAddress;
    props.EopSizeBytes = 4096u;
    props.RptrGpuAddress = buffers.RptrGpuAddress;
    props.WptrGpuAddress = buffers.WptrGpuAddress;
    volatile void *doorbellMapped = NULL;
    NTSTATUS dbStatus = STATUS_SUCCESS;

    props.UseDoorbell = 0u;
    props.DoorbellIndex = 0u;
    props.PipePriority = 0u;
    props.QueuePriority = 0u;
    props.HqdActive = 1u;

    if (Context->Core.ResolvedBarCount > BC250_ADAPTER_DOORBELL_BAR_INDEX &&
        Context->Core.ResolvedBars[BC250_ADAPTER_DOORBELL_BAR_INDEX].Length >= 4096) {
        PHYSICAL_ADDRESS dbPhysical;
        dbPhysical.QuadPart = (LONGLONG)Context->Core.ResolvedBars[BC250_ADAPTER_DOORBELL_BAR_INDEX].Start;
        dbStatus = Context->Core.DxgkInterface.DxgkCbMapMemory(
            Context->Core.DxgkInterface.DeviceHandle,
            dbPhysical,
            (ULONG)Context->Core.ResolvedBars[BC250_ADAPTER_DOORBELL_BAR_INDEX].Length,
            FALSE,
            FALSE,
            MmNonCached,
            (PVOID *)&doorbellMapped);
        if (NT_SUCCESS(dbStatus) && doorbellMapped != NULL) {
            buffers.DoorbellVirtual = doorbellMapped;
            buffers.DoorbellSizeBytes = (uint32_t)Context->Core.ResolvedBars[BC250_ADAPTER_DOORBELL_BAR_INDEX].Length;
            props.UseDoorbell = 1u;
            /* A transacao deriva o indice KIQ da faixa MEC fisica. */
            props.DoorbellIndex = 0u;
        }
    }

    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        if (doorbellMapped != NULL) {
            (void)Context->Core.DxgkInterface.DxgkCbUnmapMemory(
                Context->Core.DxgkInterface.DeviceHandle, (PVOID)doorbellMapped);
        }
        (void)Context->Core.DxgkInterface.DxgkCbUnmapMemory(
            Context->Core.DxgkInterface.DeviceHandle, (PVOID)mapped);
        (void)Bc250VramRelease(allocator, token);
        return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
    }

    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Barrier = Bc250ActivePspBarrier;
    interfaceData.Context = &Context->Core.MmioTransport;

    /*
     * Cyan Skillfish2 (PCI 1002:13FE) usa SMU 11.8. O ABI oficial nao
     * possui AllowGfxOff/DisallowGfxOff e o caminho upstream configura
     * clock/power gating como nao aplicavel. Nao emita mensagens Van Gogh
     * neste ASIC. A protecao real da submissao permanece nos preflights de
     * firmware/MEC, coerencia HDP/L2, readback e tripé fisico.
     */
    status = Bc250ComputeRunActiveHqdSubmissionTransaction(
        &interfaceData, &buffers, &props, &mqd, Evidence);
    Evidence->GfxOffApplicable = 0u;
    Evidence->GfxOffGuardPassed = 1u;
    Evidence->GfxOffRestorePassed = 1u;

    reads = Context->Core.MmioTransport.ReadCount;
    writes = Context->Core.MmioTransport.WriteCount;
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);

    if (doorbellMapped != NULL) {
        (void)Context->Core.DxgkInterface.DxgkCbUnmapMemory(
            Context->Core.DxgkInterface.DeviceHandle, (PVOID)doorbellMapped);
    }
    (void)Context->Core.DxgkInterface.DxgkCbUnmapMemory(
        Context->Core.DxgkInterface.DeviceHandle, (PVOID)mapped);
    (void)Bc250VramRelease(allocator, token);

    if (status == BC250_HQD_ACTIVE_OK &&
        mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        Evidence->PhysicalReady = 0u;
        return BC250_HQD_ACTIVE_RESTORE_FAILED;
    }

    return status;
}

static void Bc250ActiveRecordHqdSubmission(
    const BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    const BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE *e;

    if (Context == NULL) return;
    e = &Context->HqdActiveEvidence;
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_STATUS,
        (ULONG)Context->HqdActiveStatus);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_SELECTED_SELECTOR,
        e->SelectedSelector);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BEFORE, e->ActiveBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_APPLIED, e->ActiveApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_AFTER, e->ActiveAfter);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_BEFORE, e->RptrBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_AFTER, e->RptrAfter);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_APPLIED, e->WptrApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FENCE_BEFORE, e->FenceBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FENCE_AFTER, e->FenceAfter);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_SCRATCH_BEFORE, e->ScratchBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_SCRATCH_AFTER, e->ScratchAfter);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_POLL_ITERATIONS, e->PollIterations);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_TRIAD_PASSED, e->TriadPassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RESTORE_PASSED, e->RestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEM_RESTORE_PASSED,
        e->MemoryRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PHYSICAL_READY, e->PhysicalReady);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_RUNG, e->DoorbellRung);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_INDEX, e->DoorbellIndex);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_OFFSET, e->DoorbellOffset);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_VALUE_APPLIED,
        e->DoorbellValueApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_LOWER_BEFORE,
        e->DoorbellRangeLowerBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_LOWER_APPLIED,
        e->DoorbellRangeLowerApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_LOWER_READBACK,
        e->DoorbellRangeLowerReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_UPPER_BEFORE,
        e->DoorbellRangeUpperBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_UPPER_APPLIED,
        e->DoorbellRangeUpperApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_UPPER_READBACK,
        e->DoorbellRangeUpperReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_BEFORE,
        e->CpPqStatusBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_APPLIED,
        e->CpPqStatusApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_READBACK,
        e->CpPqStatusReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_BEFORE,
        e->WptrPollCntlBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_APPLIED,
        e->WptrPollCntlApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_READBACK,
        e->WptrPollCntlReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_SELECTOR_RELEASED_BEFORE_KICK,
        e->SelectorReleasedBeforeKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_AFTER_KICK,
        e->CpPqStatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_CONTROL_AFTER_KICK,
        e->DoorbellControlAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_AFTER_KICK,
        e->WptrAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_AFTER_KICK,
        e->RptrAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_INITIAL_WPTR_LO,
        e->InitialWptrLo);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_INITIAL_WPTR_HI,
        e->InitialWptrHi);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_APPLIED,
        e->BaselineWptrApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_READBACK,
        e->BaselineWptrReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_RESTORED,
        e->BaselineWptrRestored);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_RESTORE_POLLS,
        e->BaselineWptrRestorePolls);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_RESTORE_PASSED,
        e->BaselineWptrRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_KICK_APPLIED,
        e->DirectWptrKickApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_BEFORE,
        e->DirectWptrBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_READBACK,
        e->DirectWptrReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_RPTR_AFTER_KICK,
        e->DirectRptrAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_RESTORED,
        e->DirectWptrRestored);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_RESTORE_POLLS,
        e->DirectWptrRestorePolls);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_RESTORE_PASSED,
        e->DirectWptrRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_MMIO_LAST,
        e->RptrMmioLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_MMIO_LAST,
        e->WptrMmioLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQ_STATUS0_POLL_LAST,
        e->HqStatus0PollLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQD_ERROR_POLL_LAST,
        e->HqdErrorPollLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_BEFORE,
        e->RlcCpSchedulersBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_APPLIED,
        e->RlcCpSchedulersApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_READBACK,
        e->RlcCpSchedulersReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_AFTER_KICK,
        e->RlcCpSchedulersAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_RESTORED,
        e->RlcCpSchedulersRestored);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_RESTORE_PASSED,
        e->RlcCpSchedulersRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQ_STATUS0_AFTER_KICK,
        e->HqStatus0AfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQ_CONTROL0_AFTER_KICK,
        e->HqControl0AfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQD_ERROR_AFTER_KICK,
        e->HqdErrorAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_UTCL1_ERROR_AFTER_KICK,
        e->CpcUtcl1ErrorAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_UTCL1_STATUS_AFTER_KICK,
        e->CpcUtcl1StatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_UTCL1_STATUS_AFTER_KICK,
        e->CpfUtcl1StatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_INT_STATUS_AFTER_KICK,
        e->CpcIntStatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_ME1_PIPE0_INT_STATUS_AFTER_KICK,
        e->Me1Pipe0IntStatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_ME2_PIPE1_INT_STATUS_AFTER_KICK,
        e->Me2Pipe1IntStatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_ME2_PIPE0_INT_STATUS_AFTER_KICK,
        e->Me2Pipe0IntStatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PQ_BASE_LO, e->PqBaseLoApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PQ_BASE_HI, e->PqBaseHiApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_ADDR_LO,
        e->WptrPollAddrLoApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_ADDR_HI,
        e->WptrPollAddrHiApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_SHADOW, e->WptrShadowApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_MEC_CNTL_READBACK,
        e->CpMecCntlReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_PC_FIRST, e->MecPcFirst);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_PC_LAST, e->MecPcLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_PC_TRANSITIONS,
        e->MecPcTransitions);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_PC_SAMPLES, e->MecPcSamples);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PQ_CONTROL_OBSERVED,
        e->PqControlObserved);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HDP_INVALIDATE_APPLIED,
        e->HdpInvalidateApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_L2_BEFORE,
        e->GcvmL2Cntl2Before);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_L2_APPLIED,
        e->GcvmL2Cntl2Applied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_L2_READBACK,
        e->GcvmL2Cntl2Readback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_REQ_APPLIED,
        e->GcvmInvalidateRequestApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_REQ_READBACK,
        e->GcvmInvalidateRequestReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_ACK_BEFORE,
        e->GcvmInvalidateAckBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_ACK_AFTER,
        e->GcvmInvalidateAckAfter);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_POLLS,
        e->GcvmInvalidatePolls);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_COHERENCY_PASSED,
        e->CoherencyPassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_APPLICABLE,
        e->GfxOffApplicable);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_STATE_BEFORE,
        e->GfxOffStateBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_STATE_GUARDED,
        e->GfxOffStateGuarded);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_STATE_AFTER,
        e->GfxOffStateAfter);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_DISALLOW_RESPONSE,
        e->GfxOffDisallowResponse);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_ALLOW_RESPONSE,
        e->GfxOffAllowResponse);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_GUARD_PASSED,
        e->GfxOffGuardPassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_RESTORE_PASSED,
        e->GfxOffRestorePassed);
}

static void Bc250ActiveRecordHqdWptrDiag(
    const BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    const BC250_HQD_WPTR_DIAGNOSTIC_EVIDENCE *e;

    if (Context == NULL) return;
    e = &Context->HqdWptrDiagEvidence;
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_STATUS,
        (ULONG)Context->HqdWptrDiagStatus);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_ORIGINAL_SELECTOR,
        e->OriginalSelector);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_TARGET_SELECTOR,
        e->TargetSelector);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_ACTIVE_BEFORE,
        e->ActiveBeforePass1);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_RPTR_BEFORE,
        e->RptrBeforePass1);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_WPTR_LO_BEFORE,
        e->WptrLoBeforePass1);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_WPTR_HI_BEFORE,
        e->WptrHiBeforePass1);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_PQ_CONTROL_BEFORE,
        e->PqControlBeforePass1);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_STABLE_PREFLIGHT_PASSED,
        e->StablePreflightPassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_TEST_VALUE_WRITTEN,
        e->TestValueWritten);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_FIRST_READBACK,
        e->FirstReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_SERIES_MATCH_INDEX,
        e->SeriesMatchIndex);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_SELECTOR_UNMODIFIED,
        e->SelectorRemainedUnmodified);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_RESTORED_VALUE_OBSERVED,
        e->RestoredValueObserved);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_RESTORE_POLLS,
        e->RestorePolls);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_RESTORE_PASSED,
        e->RestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_ACTIVE_AFTER,
        e->ActiveAfterPass1);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_WPTR_LO_AFTER,
        e->WptrLoAfterPass1);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_SELECTOR_RESTORE_PASSED,
        e->SelectorRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_READ_CALLS,
        e->ReadCalls);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_WRITE_CALLS,
        e->WriteCalls);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_PHYSICAL_READY,
        e->PhysicalReady);
}

static NTSTATUS Bc250ActiveQueryRuntimeCaps(
    HANDLE AdapterContext,
    BC250_MCDM_RUNTIME_CAPS *Caps)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext;
    uint32_t i;
    uint64_t localStart = 0u;
    uint64_t localEnd;
    uint64_t cpuVisibleBytes;
    if (Caps == NULL) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Caps, sizeof(*Caps));
    if (context == NULL || context->Signature != BC250_ACTIVE_SIGNATURE ||
        context->RuntimeInventoryStatus != BC250_RUNTIME_INVENTORY_OK ||
        context->VramProbeStatus != BC250_VRAM_PROBE_OK) {
        return STATUS_DEVICE_NOT_READY;
    }
    cpuVisibleBytes = context->RuntimeInventoryReport.CpuVisibleBytes;
    localEnd = context->Core.StartDevice.Preflight.FirmwarePlans[0]
        .Binding.Offset;
    if (context->SecondPreflightStage != BC250_DXGK_STAGE_PREFLIGHT_READY ||
        context->Core.StartDevice.Preflight.FirmwareCount !=
            BC250_CP_BOOTSTRAP_FIRMWARE_COUNT ||
        localEnd == 0u || localEnd > cpuVisibleBytes) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    for (i = 0u;
         i < context->RuntimeInventoryReport.ReservationCount;
         ++i) {
        const BC250_INHERITED_VRAM_RESERVATION *reservation =
            &context->RuntimeInventoryReport.Reservations[i];
        uint64_t reservationEnd;
        if (reservation->Offset >= cpuVisibleBytes) continue;
        reservationEnd = reservation->Length >
            cpuVisibleBytes - reservation->Offset ?
            cpuVisibleBytes : reservation->Offset + reservation->Length;
        if (reservationEnd > localStart) localStart = reservationEnd;
    }
    if (localStart >= localEnd) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    for (i = 0u; i < context->Core.ResolvedBarCount; ++i) {
        if (context->Core.ResolvedBars[i].Index ==
            BC250_PCI_VRAM_APERTURE_BAR_INDEX) {
            Caps->CpuPhysicalStart =
                context->Core.ResolvedBars[i].Start + localStart;
            break;
        }
    }
    if (i == context->Core.ResolvedBarCount || Caps->CpuPhysicalStart == 0u) {
        RtlZeroMemory(Caps, sizeof(*Caps));
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Caps->LocalSegmentBaseOffset = localStart;
    Caps->LocalSegmentBytes = localEnd - localStart;
    Caps->NodeCount = BC250_ADAPTER_NODE_COUNT;
    Caps->Ready = 1u;
    Caps->FullMcdmReady = 1u;
    Caps->TdrReady = 1u;
    Caps->PreemptionReady = 1u;
    Caps->SubmitReady = 1u;
    Caps->HistoryBufferReady = 1u;
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250ActiveRegisterAdapter(
    BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    uint32_t index;
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        if (g_Bc250AdapterDirectory[index] == NULL) {
            g_Bc250AdapterDirectory[index] = Context;
            status = STATUS_SUCCESS;
            break;
        }
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return status;
}

static BC250_PSP_LOAD_FW_STATUS Bc250ActiveLoadSdmaFirmware(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PSP_LOAD_FW_EVIDENCE *Evidence)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    static const unsigned char expectedDigest[BC250_FIRMWARE_SHA256_BYTES] = {
        0x15,0xD0,0xD3,0x62,0x6D,0xA7,0xF2,0x51,
        0x3F,0x03,0xB1,0x3B,0xB7,0xE0,0x2E,0xEE,
        0xEC,0xCA,0xB2,0x76,0x27,0x6A,0xE2,0x7D,
        0x0B,0x60,0x67,0xB5,0xF2,0x5E,0x9E,0x95
    };
    static const unsigned char expectedPayloadDigest[BC250_FIRMWARE_SHA256_BYTES] = {
        0xAC,0xBA,0x51,0xD3,0xD7,0x34,0x63,0x7F,
        0x5A,0xCB,0xBA,0xBF,0x3A,0x7D,0xA5,0xCE,
        0xCD,0xC4,0x90,0x96,0x28,0xD7,0x9A,0x40,
        0xF2,0x9F,0x5A,0x39,0xEB,0x13,0x38,0xFE
    };
    UNICODE_STRING path = RTL_CONSTANT_STRING(
        L"\\SystemRoot\\System32\\drivers\\cyan_skillfish2_sdma.bin");
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK ioStatus;
    FILE_STANDARD_INFORMATION fileInfo;
    HANDLE file = NULL;
    PHYSICAL_ADDRESS low, high, boundary;
    PVOID ring = NULL, command = NULL, fence = NULL, firmware = NULL;
    PHYSICAL_ADDRESS ringPa, commandPa, fencePa, firmwarePa;
    unsigned char digest[BC250_FIRMWARE_SHA256_BYTES];
    BC250_PSP_LOAD_FW_INTERFACE interfaceData;
    BC250_SDMA_F32_INTERFACE f32Interface;
    BC250_PSP_LOAD_FW_STATUS status = BC250_PSP_LOAD_FW_BAD_ARGUMENT;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    NTSTATUS ntStatus;
    uint32_t safeToFree = 1u;

    if (Context == NULL || Evidence == NULL) return status;
    RtlZeroMemory(Evidence, sizeof(*Evidence));
    InitializeObjectAttributes(&attributes, &path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    ntStatus = ZwCreateFile(&file, GENERIC_READ | SYNCHRONIZE, &attributes,
        &ioStatus, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0u);
    if (!NT_SUCCESS(ntStatus)) return status;
    ntStatus = ZwQueryInformationFile(file, &ioStatus, &fileInfo,
        sizeof(fileInfo), FileStandardInformation);
    if (!NT_SUCCESS(ntStatus) || fileInfo.EndOfFile.QuadPart != 33792) {
        ZwClose(file);
        return status;
    }
    low.QuadPart = 0;
    high.QuadPart = 0xFFFFFFFFFFull;
    boundary.QuadPart = 0;
    ring = MmAllocateContiguousMemorySpecifyCache(
        BC250_PSP_RING_BYTES, low, high, boundary, MmNonCached);
    command = MmAllocateContiguousMemorySpecifyCache(
        BC250_PSP_LOAD_FW_COMMAND_BYTES, low, high, boundary, MmNonCached);
    fence = MmAllocateContiguousMemorySpecifyCache(
        PAGE_SIZE, low, high, boundary, MmNonCached);
    firmware = MmAllocateContiguousMemorySpecifyCache(
        33792u, low, high, boundary, MmNonCached);
    if (ring == NULL || command == NULL || fence == NULL || firmware == NULL)
        goto Cleanup;
    RtlZeroMemory(ring, BC250_PSP_RING_BYTES);
    RtlZeroMemory(command, BC250_PSP_LOAD_FW_COMMAND_BYTES);
    RtlZeroMemory(fence, PAGE_SIZE);
    RtlZeroMemory(firmware, 33792u);
    ntStatus = ZwReadFile(file, NULL, NULL, NULL, &ioStatus, firmware,
        33792u, NULL, NULL);
    if (!NT_SUCCESS(ntStatus) || ioStatus.Information != 33792u) goto Cleanup;
    Bc250FirmwareSha256(firmware, 33792u, digest);
    if (!Bc250FirmwareDigestEqual(digest, expectedDigest)) goto Cleanup;
    RtlMoveMemory(firmware, (PUCHAR)firmware + 0x100u, 33536u);
    RtlZeroMemory((PUCHAR)firmware + 33536u, 0x100u);
    Bc250FirmwareSha256(firmware, 33536u, digest);
    if (!Bc250FirmwareDigestEqual(digest, expectedPayloadDigest)) goto Cleanup;
    ringPa = MmGetPhysicalAddress(ring);
    commandPa = MmGetPhysicalAddress(command);
    fencePa = MmGetPhysicalAddress(fence);
    firmwarePa = MmGetPhysicalAddress(firmware);
    if (ringPa.QuadPart == 0 || commandPa.QuadPart == 0 ||
        fencePa.QuadPart == 0 || firmwarePa.QuadPart == 0) goto Cleanup;
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        status = BC250_PSP_LOAD_FW_READ_FAILED;
        goto Cleanup;
    }
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250ActiveVmidRead32;
    interfaceData.Write32 = Bc250ActiveVmidWrite32;
    interfaceData.Stall = Bc250ActiveTlbStall;
    interfaceData.Barrier = Bc250ActivePspBarrier;
    interfaceData.Context = Context;
    interfaceData.Identity = BC250_PSP_LOAD_FW_IDENTITY;
    status = Bc250RunPspLoadFirmwareTransaction(
        (uint64_t)ringPa.QuadPart, (uint32_t *)ring,
        (uint64_t)commandPa.QuadPart, (uint32_t *)command,
        (uint64_t)fencePa.QuadPart, (volatile uint32_t *)fence,
        (uint64_t)firmwarePa.QuadPart, 33536u,
        BC250_PSP_LOAD_FW_SDMA0_TYPE, &interfaceData, 500u, Evidence);
    if (status == BC250_PSP_LOAD_FW_OK && Evidence->PhysicalReady != 0u) {
        RtlZeroMemory(&f32Interface, sizeof(f32Interface));
        f32Interface.Read32 = Bc250ActiveVmidRead32;
        f32Interface.Write32 = Bc250ActiveVmidWrite32;
        f32Interface.Stall = Bc250ActiveTlbStall;
        f32Interface.Context = Context;
        f32Interface.Identity = BC250_SDMA_F32_IDENTITY;
        Context->SdmaF32Status = Bc250RunSdmaF32Transaction(
            &f32Interface, &Context->SdmaF32Evidence);
        if (Context->SdmaF32Status == BC250_SDMA_F32_OK &&
            Context->SdmaF32Evidence.PhysicalReady != 0u) {
            Context->SdmaRingWorkStatus = Bc250ActiveRunSdmaRingWork(
                Context, &Context->SdmaRingWorkEvidence);
        }
    }
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK &&
        status == BC250_PSP_LOAD_FW_OK) {
        status = BC250_PSP_LOAD_FW_RESTORE_FAILED;
        Evidence->PhysicalReady = 0u;
    }
    if (Evidence->CreatePassed != 0u && Evidence->DestroyPassed == 0u)
        safeToFree = 0u;
Cleanup:
    if (file != NULL) ZwClose(file);
    if (safeToFree != 0u) {
        if (firmware != NULL) MmFreeContiguousMemory(firmware);
        if (fence != NULL) MmFreeContiguousMemory(fence);
        if (command != NULL) MmFreeContiguousMemory(command);
        if (ring != NULL) MmFreeContiguousMemory(ring);
    } else {
        Context->DeferredPspLoadRing = ring;
        Context->DeferredPspLoadCommand = command;
        Context->DeferredPspLoadFence = fence;
        Context->DeferredPspLoadFirmware = firmware;
    }
    return status;
#else
    UNREFERENCED_PARAMETER(Context);
    if (Evidence != NULL) RtlZeroMemory(Evidence, sizeof(*Evidence));
    return BC250_PSP_LOAD_FW_BAD_ARGUMENT;
#endif
}

static BC250_PSP_LOAD_FW_STATUS Bc250ActiveLoadRlcFirmware(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PSP_LOAD_FW_EVIDENCE *Evidence,
    BC250_RLC_LOAD_EVIDENCE *RlcEvidence)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    static const unsigned char expectedDigest[BC250_FIRMWARE_SHA256_BYTES] = {
        0x20,0xAC,0xEF,0xDB,0x61,0x28,0xA3,0x62,
        0x75,0xF4,0x38,0x24,0x25,0xA1,0x09,0xA7,
        0xC9,0x92,0x7F,0x60,0x53,0xEA,0xB6,0x85,
        0xBB,0x51,0x16,0x4F,0xF1,0xD1,0x8C,0xFB
    };
    static const unsigned char expectedPayloadDigest[BC250_FIRMWARE_SHA256_BYTES] = {
        0x16,0x79,0x4B,0x48,0x17,0xCC,0xB8,0xE8,
        0x2E,0x45,0xFE,0x35,0x24,0xAE,0xC7,0xA8,
        0xA0,0xE5,0x8D,0x03,0x2E,0x76,0xAC,0x1C,
        0x33,0x85,0xC4,0x17,0xA1,0x45,0xBE,0x48
    };
    UNICODE_STRING path = RTL_CONSTANT_STRING(
        L"\\SystemRoot\\System32\\drivers\\cyan_skillfish2_rlc.bin");
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK ioStatus;
    FILE_STANDARD_INFORMATION fileInfo;
    HANDLE file = NULL;
    PHYSICAL_ADDRESS low, high, boundary;
    PVOID ring = NULL, command = NULL, fence = NULL, firmware = NULL;
    PHYSICAL_ADDRESS ringPa, commandPa, fencePa, firmwarePa;
    unsigned char digest[BC250_FIRMWARE_SHA256_BYTES];
    BC250_PSP_LOAD_FW_INTERFACE interfaceData;
    BC250_PSP_LOAD_FW_STATUS status = BC250_PSP_LOAD_FW_BAD_ARGUMENT;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    NTSTATUS ntStatus;
    uint32_t cntlReadback = 0u, safeToFree = 1u;
    const uint32_t rlcCntl = 0x0003B000u;
    const uint32_t rlcGpmStat = 0x0003B9B8u;
    const uint32_t execRomMask = 0x00400000u;

    if (Context == NULL || Evidence == NULL || RlcEvidence == NULL)
        return status;
    RtlZeroMemory(Evidence, sizeof(*Evidence));
    RtlZeroMemory(RlcEvidence, sizeof(*RlcEvidence));
    InitializeObjectAttributes(&attributes, &path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    ntStatus = ZwCreateFile(&file, GENERIC_READ | SYNCHRONIZE, &attributes,
        &ioStatus, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0u);
    if (!NT_SUCCESS(ntStatus)) return status;
    ntStatus = ZwQueryInformationFile(file, &ioStatus, &fileInfo,
        sizeof(fileInfo), FileStandardInformation);
    if (!NT_SUCCESS(ntStatus) || fileInfo.EndOfFile.QuadPart != 25344) {
        ZwClose(file);
        return status;
    }
    low.QuadPart = 0;
    high.QuadPart = 0xFFFFFFFFFFull;
    boundary.QuadPart = 0;
    ring = MmAllocateContiguousMemorySpecifyCache(
        BC250_PSP_RING_BYTES, low, high, boundary, MmNonCached);
    command = MmAllocateContiguousMemorySpecifyCache(
        BC250_PSP_LOAD_FW_COMMAND_BYTES, low, high, boundary, MmNonCached);
    fence = MmAllocateContiguousMemorySpecifyCache(
        PAGE_SIZE, low, high, boundary, MmNonCached);
    firmware = MmAllocateContiguousMemorySpecifyCache(
        25344u, low, high, boundary, MmNonCached);
    if (ring == NULL || command == NULL || fence == NULL || firmware == NULL)
        goto Cleanup;
    RtlZeroMemory(ring, BC250_PSP_RING_BYTES);
    RtlZeroMemory(command, BC250_PSP_LOAD_FW_COMMAND_BYTES);
    RtlZeroMemory(fence, PAGE_SIZE);
    RtlZeroMemory(firmware, 25344u);
    ntStatus = ZwReadFile(file, NULL, NULL, NULL, &ioStatus, firmware,
        25344u, NULL, NULL);
    if (!NT_SUCCESS(ntStatus) || ioStatus.Information != 25344u) goto Cleanup;
    Bc250FirmwareSha256(firmware, 25344u, digest);
    if (!Bc250FirmwareDigestEqual(digest, expectedDigest)) goto Cleanup;
    RtlMoveMemory(firmware, (PUCHAR)firmware + 0x100u, 25088u);
    RtlZeroMemory((PUCHAR)firmware + 25088u, 0x100u);
    Bc250FirmwareSha256(firmware, 25088u, digest);
    if (!Bc250FirmwareDigestEqual(digest, expectedPayloadDigest)) goto Cleanup;
    ringPa = MmGetPhysicalAddress(ring);
    commandPa = MmGetPhysicalAddress(command);
    fencePa = MmGetPhysicalAddress(fence);
    firmwarePa = MmGetPhysicalAddress(firmware);
    if (ringPa.QuadPart == 0 || commandPa.QuadPart == 0 ||
        fencePa.QuadPart == 0 || firmwarePa.QuadPart == 0) goto Cleanup;
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        status = BC250_PSP_LOAD_FW_READ_FAILED;
        goto Cleanup;
    }
    if (!Bc250ActiveVmidRead32(Context, rlcCntl, &RlcEvidence->CntlBefore) ||
        !Bc250ActiveVmidRead32(Context, rlcGpmStat,
            &RlcEvidence->GpmStatBefore)) {
        status = BC250_PSP_LOAD_FW_READ_FAILED;
        goto Unmap;
    }
    RlcEvidence->ExecRomBefore =
        (RlcEvidence->GpmStatBefore & execRomMask) != 0u;
    if (RlcEvidence->ExecRomBefore == 0u) {
        RlcEvidence->AlreadyActive = 1u;
        status = BC250_PSP_LOAD_FW_OK;
        Evidence->FirmwareType = BC250_PSP_LOAD_FW_RLC_TYPE;
        if (Bc250ActiveVmidWrite32(Context, rlcCntl,
                RlcEvidence->CntlBefore | 1u) &&
            Bc250ActiveVmidRead32(Context, rlcCntl,
                &RlcEvidence->CntlAfter) &&
            Bc250ActiveVmidRead32(Context, rlcGpmStat,
                &RlcEvidence->GpmStatAfter)) {
            RlcEvidence->ExecRomAfter =
                (RlcEvidence->GpmStatAfter & execRomMask) != 0u;
            RlcEvidence->EnableF32After = RlcEvidence->CntlAfter & 1u;
            if (RlcEvidence->ExecRomAfter == 0u &&
                RlcEvidence->EnableF32After != 0u) {
                RlcEvidence->PhysicalReady = 1u;
                Evidence->PhysicalReady = 1u;
            }
        }
        goto RestoreRlc;
    }
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250ActiveVmidRead32;
    interfaceData.Write32 = Bc250ActiveVmidWrite32;
    interfaceData.Stall = Bc250ActiveTlbStall;
    interfaceData.Barrier = Bc250ActivePspBarrier;
    interfaceData.Context = Context;
    interfaceData.Identity = BC250_PSP_LOAD_FW_IDENTITY;
    status = Bc250RunPspLoadFirmwareTransaction(
        (uint64_t)ringPa.QuadPart, (uint32_t *)ring,
        (uint64_t)commandPa.QuadPart, (uint32_t *)command,
        (uint64_t)fencePa.QuadPart, (volatile uint32_t *)fence,
        (uint64_t)firmwarePa.QuadPart, 25088u,
        BC250_PSP_LOAD_FW_RLC_TYPE, &interfaceData, 500u, Evidence);
    if (status == BC250_PSP_LOAD_FW_OK && Evidence->PhysicalReady != 0u &&
        Bc250ActiveVmidWrite32(Context, rlcCntl,
            RlcEvidence->CntlBefore | 1u)) {
        KeStallExecutionProcessor(1000u);
    }
    if (status == BC250_PSP_LOAD_FW_OK && Evidence->PhysicalReady != 0u &&
        Bc250ActiveVmidRead32(Context, rlcCntl, &RlcEvidence->CntlAfter) &&
        Bc250ActiveVmidRead32(Context, rlcGpmStat,
            &RlcEvidence->GpmStatAfter)) {
        RlcEvidence->ExecRomAfter =
            (RlcEvidence->GpmStatAfter & execRomMask) != 0u;
        RlcEvidence->EnableF32After = RlcEvidence->CntlAfter & 1u;
        if (RlcEvidence->ExecRomBefore != 0u &&
            RlcEvidence->ExecRomAfter == 0u &&
            RlcEvidence->EnableF32After != 0u)
            RlcEvidence->PhysicalReady = 1u;
    }
RestoreRlc:
    if (Bc250ActiveVmidWrite32(Context, rlcCntl, RlcEvidence->CntlBefore) &&
        Bc250ActiveVmidRead32(Context, rlcCntl, &cntlReadback) &&
        cntlReadback == RlcEvidence->CntlBefore)
        RlcEvidence->CntlRestorePassed = 1u;
Unmap:
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK &&
        status == BC250_PSP_LOAD_FW_OK) {
        status = BC250_PSP_LOAD_FW_RESTORE_FAILED;
        Evidence->PhysicalReady = 0u;
        RlcEvidence->PhysicalReady = 0u;
    }
    if (Evidence->CreatePassed != 0u && Evidence->DestroyPassed == 0u)
        safeToFree = 0u;
Cleanup:
    if (file != NULL) ZwClose(file);
    if (safeToFree != 0u) {
        if (firmware != NULL) MmFreeContiguousMemory(firmware);
        if (fence != NULL) MmFreeContiguousMemory(fence);
        if (command != NULL) MmFreeContiguousMemory(command);
        if (ring != NULL) MmFreeContiguousMemory(ring);
    } else {
        Context->DeferredPspLoadRing = ring;
        Context->DeferredPspLoadCommand = command;
        Context->DeferredPspLoadFence = fence;
        Context->DeferredPspLoadFirmware = firmware;
    }
    return status;
#else
    UNREFERENCED_PARAMETER(Context);
    if (Evidence != NULL) RtlZeroMemory(Evidence, sizeof(*Evidence));
    if (RlcEvidence != NULL) RtlZeroMemory(RlcEvidence, sizeof(*RlcEvidence));
    return BC250_PSP_LOAD_FW_BAD_ARGUMENT;
#endif
}

static VOID Bc250ActiveUnregisterAdapter(
    BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    uint32_t index;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        if (g_Bc250AdapterDirectory[index] == Context) {
            g_Bc250AdapterDirectory[index] = NULL;
            break;
        }
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
}

static NTSTATUS Bc250ObjectStatusToNtStatus(
    BC250_MCDM_OBJECT_STATUS Status)
{
    switch (Status) {
    case BC250_MCDM_OBJECT_OK:
        return STATUS_SUCCESS;
    case BC250_MCDM_OBJECT_NO_SPACE:
        return STATUS_INSUFFICIENT_RESOURCES;
    case BC250_MCDM_OBJECT_DUPLICATE_OWNER:
        return STATUS_OBJECT_NAME_COLLISION;
    case BC250_MCDM_OBJECT_STALE_HANDLE:
    case BC250_MCDM_OBJECT_INVALID_HANDLE:
    case BC250_MCDM_OBJECT_TYPE_MISMATCH:
    case BC250_MCDM_OBJECT_OWNER_MISMATCH:
        return STATUS_INVALID_HANDLE;
    case BC250_MCDM_OBJECT_CHILDREN_LIVE:
        return STATUS_DEVICE_BUSY;
    default:
        return STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS Bc250ActiveCreateProcessObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    const void *ExternalOwner,
    BC250_MCDM_HANDLE *Handle)
{
    BC250_MCDM_OBJECT_STATUS status;

    if (Handle != NULL) *Handle = NULL;
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        ExternalOwner == NULL || Handle == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Context->ObjectLock);
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        ExReleasePushLockExclusive(&Context->ObjectLock);
        KeLeaveCriticalRegion();
        return STATUS_DEVICE_NOT_READY;
    }
    status = Bc250McdmObjectCreateProcess(
        &Context->Objects, ExternalOwner, Handle);
    ExReleasePushLockExclusive(&Context->ObjectLock);
    KeLeaveCriticalRegion();
    return Bc250ObjectStatusToNtStatus(status);
}

static NTSTATUS Bc250ActiveCreateProcessLifecycle(
    HANDLE AdapterContext,
    const void *ExternalOwner,
    HANDLE *ProcessHandle)
{
    return Bc250ActiveCreateProcessObject(
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext,
        ExternalOwner,
        (BC250_MCDM_HANDLE *)ProcessHandle);
}

static NTSTATUS Bc250ActiveDestroyProcessLifecycle(
    HANDLE AdapterContext,
    HANDLE ProcessHandle)
{
    return Bc250ActiveDestroyProcessObject(
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext,
        (BC250_MCDM_HANDLE)ProcessHandle);
}

NTSTATUS Bc250ActiveDestroyProcessObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE Handle)
{
    BC250_MCDM_OBJECT_STATUS status;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        Handle == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Context->ObjectLock);
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        ExReleasePushLockExclusive(&Context->ObjectLock);
        KeLeaveCriticalRegion();
        return STATUS_DEVICE_NOT_READY;
    }
    if (InterlockedCompareExchange(&Context->GpuvaSdmaBrokerReady, 1, 1) == 1) {
        (void)Bc250GpuvaSdmaBrokerRevokeForObject(&Context->GpuvaSdmaBroker,
            Handle, NULL, NULL);
    }
    status = Bc250McdmObjectDestroy(&Context->Objects, Handle,
        BC250_MCDM_OBJECT_PROCESS, Handle);
    ExReleasePushLockExclusive(&Context->ObjectLock);
    KeLeaveCriticalRegion();
    return Bc250ObjectStatusToNtStatus(status);
}

NTSTATUS Bc250ActiveCreateDeviceObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE ProcessHandle,
    BC250_MCDM_HANDLE *DeviceHandle)
{
    BC250_MCDM_OBJECT_STATUS status;

    if (DeviceHandle != NULL) *DeviceHandle = NULL;
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        ProcessHandle == NULL || DeviceHandle == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Context->ObjectLock);
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        status = BC250_MCDM_OBJECT_BAD_REGISTRY;
    } else {
        status = Bc250McdmObjectCreateChild(&Context->Objects,
            BC250_MCDM_OBJECT_DEVICE, ProcessHandle, ProcessHandle,
            DeviceHandle);
    }
    ExReleasePushLockExclusive(&Context->ObjectLock);
    KeLeaveCriticalRegion();
    if (status == BC250_MCDM_OBJECT_BAD_REGISTRY) {
        return STATUS_DEVICE_NOT_READY;
    }
    return Bc250ObjectStatusToNtStatus(status);
}

NTSTATUS Bc250ActiveDestroyDeviceObject(
    BC250_MCDM_HANDLE DeviceHandle)
{
    uint32_t index;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    if (DeviceHandle == NULL) return STATUS_INVALID_PARAMETER;
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[index];
        BC250_MCDM_OBJECT_RECORD record;
        BC250_MCDM_OBJECT_STATUS status;
        if (context == NULL) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        status = Bc250McdmObjectLookup(&context->Objects, DeviceHandle,
            BC250_MCDM_OBJECT_DEVICE, NULL, &record);
        if (status != BC250_MCDM_OBJECT_INVALID_HANDLE) {
            if (status == BC250_MCDM_OBJECT_OK &&
                InterlockedCompareExchange(&context->IsActive, 1, 1) != 1) {
                result = STATUS_DEVICE_NOT_READY;
            } else if (status == BC250_MCDM_OBJECT_OK) {
                status = Bc250McdmObjectDestroy(&context->Objects,
                    DeviceHandle, BC250_MCDM_OBJECT_DEVICE,
                    record.OwnerProcess);
                result = Bc250ObjectStatusToNtStatus(status);
            } else {
                result = Bc250ObjectStatusToNtStatus(status);
            }
            ExReleasePushLockExclusive(&context->ObjectLock);
            break;
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveCreateDeviceLifecycle(
    HANDLE AdapterContext,
    HANDLE ProcessHandle,
    HANDLE *DeviceHandle)
{
    return Bc250ActiveCreateDeviceObject(
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext,
        (BC250_MCDM_HANDLE)ProcessHandle,
        (BC250_MCDM_HANDLE *)DeviceHandle);
}

static NTSTATUS Bc250ActiveDestroyDeviceLifecycle(HANDLE DeviceHandle)
{
    return Bc250ActiveDestroyDeviceObject((BC250_MCDM_HANDLE)DeviceHandle);
}

NTSTATUS Bc250ActiveCreateContextObject(
    BC250_MCDM_HANDLE DeviceHandle,
    UINT NodeOrdinal,
    BC250_MCDM_HANDLE *ContextHandle,
    DXGK_CONTEXTINFO *ContextInfo)
{
    uint32_t index;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    if (ContextHandle != NULL) *ContextHandle = NULL;
    if (ContextInfo != NULL) RtlZeroMemory(ContextInfo, sizeof(*ContextInfo));
    if (DeviceHandle == NULL || ContextHandle == NULL || ContextInfo == NULL ||
        NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[index];
        BC250_MCDM_OBJECT_RECORD device;
        BC250_MCDM_OBJECT_STATUS status;
        if (context == NULL) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        status = Bc250McdmObjectLookup(&context->Objects, DeviceHandle,
            BC250_MCDM_OBJECT_DEVICE, NULL, &device);
        if (status != BC250_MCDM_OBJECT_INVALID_HANDLE) {
            if (status != BC250_MCDM_OBJECT_OK) {
                result = Bc250ObjectStatusToNtStatus(status);
            } else if (InterlockedCompareExchange(
                    &context->IsActive, 1, 1) != 1 ||
                InterlockedCompareExchange(
                    &context->ContextAbiReady, 1, 1) != 1) {
                result = STATUS_DEVICE_NOT_READY;
            } else {
                status = Bc250McdmObjectCreateChild(&context->Objects,
                    BC250_MCDM_OBJECT_CONTEXT, DeviceHandle,
                    device.OwnerProcess, ContextHandle);
                result = Bc250ObjectStatusToNtStatus(status);
                if (NT_SUCCESS(result)) *ContextInfo = context->ContextAbi;
            }
            ExReleasePushLockExclusive(&context->ObjectLock);
            break;
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

NTSTATUS Bc250ActiveDestroyContextObject(
    BC250_MCDM_HANDLE ContextHandle)
{
    uint32_t index;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    if (ContextHandle == NULL) return STATUS_INVALID_PARAMETER;
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[index];
        BC250_MCDM_OBJECT_RECORD record;
        BC250_MCDM_OBJECT_STATUS status;
        if (context == NULL) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        status = Bc250McdmObjectLookup(&context->Objects, ContextHandle,
            BC250_MCDM_OBJECT_CONTEXT, NULL, &record);
        if (status != BC250_MCDM_OBJECT_INVALID_HANDLE) {
            if (status == BC250_MCDM_OBJECT_OK &&
                InterlockedCompareExchange(&context->IsActive, 1, 1) == 1) {
                status = Bc250McdmObjectDestroy(&context->Objects,
                    ContextHandle, BC250_MCDM_OBJECT_CONTEXT,
                    record.OwnerProcess);
                result = Bc250ObjectStatusToNtStatus(status);
            } else if (status == BC250_MCDM_OBJECT_OK) {
                result = STATUS_DEVICE_NOT_READY;
            } else {
                result = Bc250ObjectStatusToNtStatus(status);
            }
            ExReleasePushLockExclusive(&context->ObjectLock);
            break;
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveCreateContextLifecycle(
    HANDLE DeviceHandle,
    UINT NodeOrdinal,
    HANDLE *ContextHandle,
    DXGK_CONTEXTINFO *ContextInfo)
{
    return Bc250ActiveCreateContextObject(
        (BC250_MCDM_HANDLE)DeviceHandle, NodeOrdinal,
        (BC250_MCDM_HANDLE *)ContextHandle, ContextInfo);
}

static NTSTATUS Bc250ActiveDestroyContextLifecycle(HANDLE ContextHandle)
{
    return Bc250ActiveDestroyContextObject(
        (BC250_MCDM_HANDLE)ContextHandle);
}

NTSTATUS Bc250ActiveSetRootPageTableObject(
    BC250_DXGK_ACTIVE_CONTEXT *context,
    BC250_MCDM_HANDLE ContextHandle,
    uint32_t SegmentId,
    uint64_t Address,
    uint32_t NumEntries,
    uint32_t MaxEntries)
{
    BC250_MCDM_OBJECT_STATUS status;

    if (context == NULL || context->Signature != BC250_ACTIVE_SIGNATURE ||
        ContextHandle == NULL || MaxEntries == 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((SegmentId != 0u && SegmentId != 1u) ||
        ((SegmentId == 0u) != (Address == 0u)) ||
        (SegmentId == 1u &&
         (Address >= context->AllocationSegmentBytes ||
          (UINT64)NumEntries * sizeof(UINT64) >
            context->AllocationSegmentBytes - Address))) {
        return STATUS_INVALID_PARAMETER;
    }
    if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1 ||
        InterlockedCompareExchange(
            &context->RootPageTableAbiReady, 1, 1) != 1 ||
        context->RootPageTableMaxEntries != MaxEntries) {
        return STATUS_DEVICE_NOT_READY;
    }
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&context->ObjectLock);
    if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1 ||
        InterlockedCompareExchange(
            &context->RootPageTableAbiReady, 1, 1) != 1) {
        ExReleasePushLockExclusive(&context->ObjectLock);
        KeLeaveCriticalRegion();
        return STATUS_DEVICE_NOT_READY;
    }
    status = Bc250McdmObjectSetRootPageTable(&context->Objects,
        (BC250_MCDM_HANDLE)ContextHandle, SegmentId, Address,
        NumEntries, MaxEntries);
    ExReleasePushLockExclusive(&context->ObjectLock);
    KeLeaveCriticalRegion();
    return Bc250ObjectStatusToNtStatus(status);
}

static NTSTATUS Bc250ActiveSetRootPageTableLifecycle(
    HANDLE AdapterContext,
    HANDLE ContextHandle,
    UINT32 SegmentId,
    UINT64 Address,
    UINT32 NumEntries,
    UINT32 MaxEntries)
{
    return Bc250ActiveSetRootPageTableObject(
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext,
        (BC250_MCDM_HANDLE)ContextHandle,
        SegmentId, Address, NumEntries, MaxEntries);
}

NTSTATUS Bc250ActiveIssueGpuvaSdmaLease(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE OpenAllocation,
    uint64_t GpuVa,
    uint64_t ByteCount,
    uint64_t *LeaseId)
{
    BC250_GPUVA_SDMA_BROKER_STATUS status;

    if (LeaseId != NULL) *LeaseId = 0ull;
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        OwnerProcess == NULL || OpenAllocation == NULL || LeaseId == NULL ||
        InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1 ||
        InterlockedCompareExchange(&Context->GpuvaSdmaBrokerReady, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Context->ObjectLock);
    status = Bc250GpuvaSdmaBrokerIssueLease(&Context->GpuvaSdmaBroker,
        &Context->Objects, OwnerProcess, OpenAllocation, GpuVa, ByteCount,
        LeaseId);
    ExReleasePushLockExclusive(&Context->ObjectLock);
    KeLeaveCriticalRegion();
    if (status == BC250_GPUVA_SDMA_BROKER_OK) return STATUS_SUCCESS;
    if (status == BC250_GPUVA_SDMA_BROKER_MAPPING_NOT_READY) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (status == BC250_GPUVA_SDMA_BROKER_NO_SPACE) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS Bc250ActiveValidateGpuvaSdmaLeasePair(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    uint64_t SourceLeaseId,
    uint64_t DestinationLeaseId,
    uint64_t FenceGpuVa,
    uint64_t ByteCount)
{
    BC250_GPUVA_SDMA_BROKER_STATUS status;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1 ||
        InterlockedCompareExchange(&Context->GpuvaSdmaBrokerReady, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }
    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&Context->ObjectLock);
    status = Bc250GpuvaSdmaBrokerValidatePair(&Context->GpuvaSdmaBroker,
        SourceLeaseId, DestinationLeaseId, FenceGpuVa, ByteCount);
    ExReleasePushLockShared(&Context->ObjectLock);
    KeLeaveCriticalRegion();
    return status == BC250_GPUVA_SDMA_BROKER_OK ? STATUS_SUCCESS :
        STATUS_INVALID_PARAMETER;
}

static NTSTATUS Bc250ActiveCreateAllocationLifecycle(
    HANDLE AdapterContext,
    DXGKARG_CREATEALLOCATION *CreateAllocation)
{
    uint32_t directoryIndex;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (directoryIndex = 0u;
         directoryIndex < BC250_ACTIVE_MAX_ADAPTERS;
         ++directoryIndex) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[directoryIndex];
        UINT index;
        UINT created = 0u;
        if (context != (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1 ||
            InterlockedCompareExchange(
                &context->AllocationAbiReady, 1, 1) != 1) {
            result = STATUS_DEVICE_NOT_READY;
        } else {
            result = STATUS_SUCCESS;
            for (index = 0u; index < CreateAllocation->NumAllocations; ++index) {
                BC250_ALLOCATION_PRIVATE_V1 request;
                DXGK_ALLOCATIONINFO *info =
                    &CreateAllocation->pAllocationInfo[index];
                if (Bc250AllocationAbiParse(info->pPrivateDriverData,
                        info->PrivateDriverDataSize, &request) !=
                        BC250_ALLOCATION_ABI_OK ||
                    request.ByteSize > (uint64_t)(SIZE_T)~(SIZE_T)0 ||
                    context->AllocationSegmentSet != 1u ||
                    context->AllocationSegmentBytes == 0u ||
                    request.ByteSize > context->AllocationSegmentBytes) {
                    result = STATUS_INVALID_PARAMETER;
                    break;
                }
            }
            for (index = 0u;
                 NT_SUCCESS(result) && index < CreateAllocation->NumAllocations;
                 ++index) {
                BC250_ALLOCATION_PRIVATE_V1 request;
                BC250_MCDM_HANDLE handle = NULL;
                BC250_MCDM_OBJECT_STATUS status;
                DXGK_ALLOCATIONINFO *info =
                    &CreateAllocation->pAllocationInfo[index];
                (void)Bc250AllocationAbiParse(info->pPrivateDriverData,
                    info->PrivateDriverDataSize, &request);
                status = Bc250McdmObjectCreateAdapterAllocation(
                    &context->Objects, info, request.AllocationToken,
                    request.ByteSize,
                    request.Alignment, &handle);
                result = Bc250ObjectStatusToNtStatus(status);
                if (!NT_SUCCESS(result)) break;
                info->Size = (SIZE_T)request.ByteSize;
                info->PitchAlignedSize = (SIZE_T)request.ByteSize;
                info->Alignment = (UINT)request.Alignment;
                info->PreferredSegment.Value = 0u;
                info->PreferredSegment.SegmentId0 = 1u;
                info->SupportedReadSegmentSet =
                    context->AllocationSegmentSet;
                info->SupportedWriteSegmentSet =
                    context->AllocationSegmentSet;
                info->EvictionSegmentSet = 0u;
                info->PhysicalAdapterIndex = 0u;
                info->FlagsWddm2.Value = 0u;
                info->FlagsWddm2.CpuVisible = 1u;
                info->hAllocation = handle;
                ++created;
            }
            if (!NT_SUCCESS(result)) {
                while (created != 0u) {
                    DXGK_ALLOCATIONINFO *info =
                        &CreateAllocation->pAllocationInfo[--created];
                    (void)Bc250McdmObjectDestroy(&context->Objects,
                        info->hAllocation, BC250_MCDM_OBJECT_ALLOCATION, NULL);
                    info->hAllocation = NULL;
                }
            }
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
        break;
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveDestroyAllocationLifecycle(
    HANDLE AdapterContext,
    const DXGKARG_DESTROYALLOCATION *DestroyAllocation)
{
    uint32_t directoryIndex;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (directoryIndex = 0u;
         directoryIndex < BC250_ACTIVE_MAX_ADAPTERS;
         ++directoryIndex) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[directoryIndex];
        UINT index, other;
        if (context != (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1) {
            result = STATUS_DEVICE_NOT_READY;
        } else {
            result = STATUS_SUCCESS;
            for (index = 0u; index < DestroyAllocation->NumAllocations; ++index) {
                HANDLE handle = DestroyAllocation->pAllocationList[index];
                if (handle == NULL) {
                    result = STATUS_INVALID_PARAMETER;
                    break;
                }
                for (other = 0u; other < index; ++other) {
                    if (DestroyAllocation->pAllocationList[other] == handle) {
                        result = STATUS_INVALID_PARAMETER;
                        break;
                    }
                }
                if (!NT_SUCCESS(result)) break;
            }
            for (index = 0u;
                 NT_SUCCESS(result) && index < DestroyAllocation->NumAllocations;
                 ++index) {
                BC250_MCDM_OBJECT_RECORD record;
                BC250_MCDM_OBJECT_STATUS status;
                HANDLE handle = DestroyAllocation->pAllocationList[index];
                status = Bc250McdmObjectLookup(&context->Objects, handle,
                    BC250_MCDM_OBJECT_ALLOCATION, NULL, &record);
                result = Bc250ObjectStatusToNtStatus(status);
                if (!NT_SUCCESS(result)) break;
                if (record.ChildCount != 0u) {
                    result = STATUS_DEVICE_BUSY;
                    break;
                }
            }
            for (index = 0u;
                 NT_SUCCESS(result) && index < DestroyAllocation->NumAllocations;
                 ++index) {
                BC250_MCDM_OBJECT_STATUS status = Bc250McdmObjectDestroy(
                    &context->Objects,
                    DestroyAllocation->pAllocationList[index],
                    BC250_MCDM_OBJECT_ALLOCATION, NULL);
                result = Bc250ObjectStatusToNtStatus(status);
            }
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
        break;
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveOpenAllocationLifecycle(
    HANDLE DeviceContext,
    const DXGKARG_OPENALLOCATION *OpenAllocation)
{
    uint32_t directoryIndex;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (directoryIndex = 0u;
         directoryIndex < BC250_ACTIVE_MAX_ADAPTERS;
         ++directoryIndex) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[directoryIndex];
        BC250_MCDM_OBJECT_RECORD deviceRecord;
        BC250_MCDM_OBJECT_STATUS objectStatus;
        UINT index, other, opened = 0u;
        if (context == NULL) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        objectStatus = Bc250McdmObjectLookup(&context->Objects,
            DeviceContext, BC250_MCDM_OBJECT_DEVICE, NULL, &deviceRecord);
        if (objectStatus != BC250_MCDM_OBJECT_OK) {
            ExReleasePushLockExclusive(&context->ObjectLock);
            continue;
        }
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1 ||
            InterlockedCompareExchange(
                &context->AllocationAbiReady, 1, 1) != 1) {
            result = STATUS_DEVICE_NOT_READY;
        } else {
            result = STATUS_SUCCESS;
            for (index = 0u; index < OpenAllocation->NumAllocations; ++index) {
                BC250_ALLOCATION_PRIVATE_V1 request;
                DXGK_OPENALLOCATIONINFO *info =
                    &OpenAllocation->pOpenAllocation[index];
                if (info->hAllocation == 0u ||
                    Bc250AllocationAbiParse(info->pPrivateDriverData,
                        info->PrivateDriverDataSize, &request) !=
                        BC250_ALLOCATION_ABI_OK) {
                    result = STATUS_INVALID_PARAMETER;
                    break;
                }
                for (other = 0u; other < index; ++other) {
                    BC250_ALLOCATION_PRIVATE_V1 previous;
                    DXGK_OPENALLOCATIONINFO *prior =
                        &OpenAllocation->pOpenAllocation[other];
                    (void)Bc250AllocationAbiParse(prior->pPrivateDriverData,
                        prior->PrivateDriverDataSize, &previous);
                    if (prior->hAllocation == info->hAllocation ||
                        previous.AllocationToken == request.AllocationToken) {
                        result = STATUS_INVALID_PARAMETER;
                        break;
                    }
                }
                if (!NT_SUCCESS(result)) break;
            }
            for (index = 0u;
                 NT_SUCCESS(result) && index < OpenAllocation->NumAllocations;
                 ++index) {
                BC250_ALLOCATION_PRIVATE_V1 request;
                BC250_MCDM_HANDLE allocation = NULL, openHandle = NULL;
                DXGK_OPENALLOCATIONINFO *info =
                    &OpenAllocation->pOpenAllocation[index];
                (void)Bc250AllocationAbiParse(info->pPrivateDriverData,
                    info->PrivateDriverDataSize, &request);
                objectStatus = Bc250McdmObjectFindAllocationByToken(
                    &context->Objects, request.AllocationToken, &allocation);
                if (objectStatus == BC250_MCDM_OBJECT_OK) {
                    BC250_MCDM_OBJECT_RECORD allocationRecord;
                    objectStatus = Bc250McdmObjectLookup(&context->Objects,
                        allocation, BC250_MCDM_OBJECT_ALLOCATION, NULL,
                        &allocationRecord);
                    if (objectStatus == BC250_MCDM_OBJECT_OK &&
                        (allocationRecord.AllocationBytes != request.ByteSize ||
                         allocationRecord.AllocationAlignment !=
                            request.Alignment)) {
                        result = STATUS_INVALID_PARAMETER;
                        break;
                    }
                }
                if (objectStatus == BC250_MCDM_OBJECT_OK) {
                    objectStatus = Bc250McdmObjectOpenAllocation(
                        &context->Objects, DeviceContext, allocation,
                        (const void *)(ULONG_PTR)info->hAllocation,
                        &openHandle);
                }
                result = Bc250ObjectStatusToNtStatus(objectStatus);
                if (!NT_SUCCESS(result)) break;
                info->hDeviceSpecificAllocation = openHandle;
                ++opened;
            }
            if (!NT_SUCCESS(result)) {
                while (opened != 0u) {
                    DXGK_OPENALLOCATIONINFO *info =
                        &OpenAllocation->pOpenAllocation[--opened];
                    (void)Bc250McdmObjectDestroy(&context->Objects,
                        info->hDeviceSpecificAllocation,
                        BC250_MCDM_OBJECT_OPEN_ALLOCATION,
                        deviceRecord.OwnerProcess);
                    info->hDeviceSpecificAllocation = NULL;
                }
            }
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
        break;
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveCloseAllocationLifecycle(
    HANDLE DeviceContext,
    const DXGKARG_CLOSEALLOCATION *CloseAllocation)
{
    uint32_t directoryIndex;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (directoryIndex = 0u;
         directoryIndex < BC250_ACTIVE_MAX_ADAPTERS;
         ++directoryIndex) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[directoryIndex];
        BC250_MCDM_OBJECT_RECORD deviceRecord;
        BC250_MCDM_OBJECT_STATUS objectStatus;
        UINT index, other;
        if (context == NULL) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        objectStatus = Bc250McdmObjectLookup(&context->Objects,
            DeviceContext, BC250_MCDM_OBJECT_DEVICE, NULL, &deviceRecord);
        if (objectStatus != BC250_MCDM_OBJECT_OK) {
            ExReleasePushLockExclusive(&context->ObjectLock);
            continue;
        }
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1) {
            result = STATUS_DEVICE_NOT_READY;
        } else {
            result = STATUS_SUCCESS;
            for (index = 0u; index < CloseAllocation->NumAllocations; ++index) {
                BC250_MCDM_OBJECT_RECORD openRecord;
                HANDLE handle = CloseAllocation->pOpenHandleList[index];
                if (handle == NULL) {
                    result = STATUS_INVALID_PARAMETER;
                    break;
                }
                for (other = 0u; other < index; ++other) {
                    if (CloseAllocation->pOpenHandleList[other] == handle) {
                        result = STATUS_INVALID_PARAMETER;
                        break;
                    }
                }
                if (!NT_SUCCESS(result)) break;
                objectStatus = Bc250McdmObjectLookup(&context->Objects,
                    handle, BC250_MCDM_OBJECT_OPEN_ALLOCATION,
                    deviceRecord.OwnerProcess, &openRecord);
                result = Bc250ObjectStatusToNtStatus(objectStatus);
                if (!NT_SUCCESS(result)) break;
                if (openRecord.Parent != DeviceContext) {
                    result = STATUS_INVALID_HANDLE;
                    break;
                }
            }
            for (index = 0u;
                 NT_SUCCESS(result) && index < CloseAllocation->NumAllocations;
                 ++index) {
                if (InterlockedCompareExchange(
                        &context->GpuvaSdmaBrokerReady, 1, 1) == 1) {
                    (void)Bc250GpuvaSdmaBrokerRevokeForObject(
                        &context->GpuvaSdmaBroker, NULL,
                        CloseAllocation->pOpenHandleList[index], NULL);
                }
                objectStatus = Bc250McdmObjectDestroy(&context->Objects,
                    CloseAllocation->pOpenHandleList[index],
                    BC250_MCDM_OBJECT_OPEN_ALLOCATION,
                    deviceRecord.OwnerProcess);
                result = Bc250ObjectStatusToNtStatus(objectStatus);
            }
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
        break;
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveCollectDebugLifecycle(
    HANDLE AdapterContext,
    const DXGKARG_COLLECTDBGINFO *CollectDebugInfo)
{
    BC250_MCDM_DEBUG_SNAPSHOT_V1 snapshot;
    uint32_t directoryIndex;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    if (CollectDebugInfo->BufferSize < sizeof(snapshot)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    RtlZeroMemory(&snapshot, sizeof(snapshot));
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (directoryIndex = 0u;
         directoryIndex < BC250_ACTIVE_MAX_ADAPTERS;
         ++directoryIndex) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[directoryIndex];
        BC250_PROBE_FIELD lastField = (BC250_PROBE_FIELD)0;
        ULONG lastValue = 0u;
        if (context != (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        snapshot.Magic = BC250_MCDM_DEBUG_MAGIC;
        snapshot.Version = BC250_MCDM_DEBUG_VERSION;
        snapshot.StructSize = sizeof(snapshot);
        snapshot.Reason = CollectDebugInfo->Reason;
        snapshot.StateFlags = BC250_MCDM_DEBUG_ADAPTER_REGISTERED;
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) == 1) {
            snapshot.StateFlags |= BC250_MCDM_DEBUG_ACTIVE;
        }
        if (InterlockedCompareExchange(&context->ContextAbiReady, 1, 1) == 1) {
            snapshot.StateFlags |= BC250_MCDM_DEBUG_CONTEXT_ABI_READY;
        }
        if (InterlockedCompareExchange(
                &context->AllocationAbiReady, 1, 1) == 1) {
            snapshot.StateFlags |= BC250_MCDM_DEBUG_ALLOCATION_READY;
        }
        snapshot.FirstFailureStatus =
            (uint32_t)Bc250ProbeGetFirstFailure();
        snapshot.ProbeSequence = Bc250ProbeGetOffline(&lastField, &lastValue);
        snapshot.LastProbeField = (uint32_t)lastField;
        snapshot.LastProbeValue = lastValue;
        snapshot.ObjectLiveCount = context->Objects.LiveCount;
        snapshot.ObjectNextSlot = context->Objects.NextSlot;
        snapshot.SubmissionsCount = context->SubmissionsCount;
        snapshot.ResetsCount = context->ResetsCount;
        RtlCopyMemory(CollectDebugInfo->pBuffer, &snapshot, sizeof(snapshot));
        if (CollectDebugInfo->pExtension != NULL) {
            RtlZeroMemory(CollectDebugInfo->pExtension,
                sizeof(*CollectDebugInfo->pExtension));
            CollectDebugInfo->pExtension->BucketingKey =
                BC250_MCDM_DEBUG_MAGIC;
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
        result = STATUS_SUCCESS;
        break;
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

NTSTATUS APIENTRY Bc250DxgkDdiAddDeviceActive(
    CONST PDEVICE_OBJECT PhysicalDeviceObject,
    PVOID *MiniportDeviceContext)
{
    BC250_DXGK_ACTIVE_CONTEXT *context;
    NTSTATUS status;

    Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_ENTERED, 1);
    if (MiniportDeviceContext != NULL) *MiniportDeviceContext = NULL;
    if (PhysicalDeviceObject == NULL || MiniportDeviceContext == NULL) {
        Bc250ProbeRecord(
            BC250_PROBE_ADD_DEVICE_STATUS, (ULONG)STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    context = (BC250_DXGK_ACTIVE_CONTEXT *)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(*context), BC250_ACTIVE_POOL_TAG);
    if (context == NULL) {
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS,
            (ULONG)STATUS_INSUFFICIENT_RESOURCES);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    status = Bc250DxgkReadonlyInitialize(&context->Core);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS, (ULONG)status);
        return status;
    }

    context->Signature = BC250_ACTIVE_SIGNATURE;
    context->PhysicalDeviceObject = (PDEVICE_OBJECT)PhysicalDeviceObject;
    context->Core.PhysicalDeviceObject = (PDEVICE_OBJECT)PhysicalDeviceObject;
    RtlZeroMemory(&context->Display, sizeof(context->Display));
    ExInitializePushLock(&context->ObjectLock);
    if (Bc250McdmObjectRegistryInitialize(&context->Objects) !=
        BC250_MCDM_OBJECT_OK) {
        ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS,
            (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (Bc250GpuvaSdmaBrokerInitialize(&context->GpuvaSdmaBroker,
            ((uint64_t)(KeQueryPerformanceCounter(NULL).QuadPart) | 1ull)) !=
        BC250_GPUVA_SDMA_BROKER_OK) {
        ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS,
            (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    InterlockedExchange(&context->GpuvaSdmaBrokerReady, 1);
    InterlockedExchange(&context->IsActive, 0);
    RtlZeroMemory(&context->ContextAbi, sizeof(context->ContextAbi));
    InterlockedExchange(&context->ContextAbiReady, 0);
    InterlockedExchange(&context->AllocationAbiReady, 0);
    InterlockedExchange(&context->RootPageTableAbiReady, 0);
    context->AllocationSegmentBytes = 0u;
    context->AllocationSegmentSet = 0u;
    context->RootPageTableMaxEntries = 0u;
    context->PostDisplayStatus = BC250_POST_DISPLAY_BAD_ARGUMENT;
    context->PostDisplayCallbackStatus = STATUS_NOT_SUPPORTED;
    RtlZeroMemory(&context->PostDisplayReservation,
                  sizeof(context->PostDisplayReservation));
    RtlZeroMemory(&context->PostDisplayEvidence,
                  sizeof(context->PostDisplayEvidence));
    context->RuntimeInventoryStatus = BC250_RUNTIME_INVENTORY_BAD_ARGUMENT;
    RtlZeroMemory(&context->RuntimeInventoryInput,
                  sizeof(context->RuntimeInventoryInput));
    RtlZeroMemory(&context->RuntimeInventoryReport,
                  sizeof(context->RuntimeInventoryReport));
    RtlZeroMemory(&context->SealedRuntime,
                  sizeof(context->SealedRuntime));
    context->RuntimeStageStatus = STATUS_NOT_SUPPORTED;
    context->SecondPreflightStage = BC250_DXGK_STAGE_EMPTY;
    context->VramProbeStatus = BC250_VRAM_PROBE_BAD_ARGUMENT;
    RtlZeroMemory(&context->VramProbeEvidence,
                  sizeof(context->VramProbeEvidence));
    context->TlbTransactionStatus = BC250_TLB_TRANSACTION_BAD_ARGUMENT;
    RtlZeroMemory(&context->TlbTransactionEvidence,
                  sizeof(context->TlbTransactionEvidence));
    context->GcvmBackingStatus = BC250_GCVM_BACKING_BAD_ARGUMENT;
    RtlZeroMemory(&context->GcvmBackingEvidence,
                  sizeof(context->GcvmBackingEvidence));
    context->GcvmVmid8Status = BC250_GCVM_VMID8_BAD_ARGUMENT;
    RtlZeroMemory(&context->GcvmVmid8Evidence,
                  sizeof(context->GcvmVmid8Evidence));
    context->PspRingStatus = BC250_PSP_RING_BAD_ARGUMENT;
    RtlZeroMemory(&context->PspRingEvidence,
                  sizeof(context->PspRingEvidence));
    context->DeferredPspRingVirtual = NULL;
    context->PspLoadFwStatus = BC250_PSP_LOAD_FW_BAD_ARGUMENT;
    RtlZeroMemory(&context->PspLoadFwEvidence,
                  sizeof(context->PspLoadFwEvidence));
    context->PspRlcLoadStatus = BC250_PSP_LOAD_FW_BAD_ARGUMENT;
    RtlZeroMemory(&context->PspRlcLoadEvidence,
                  sizeof(context->PspRlcLoadEvidence));
    RtlZeroMemory(&context->RlcLoadEvidence,
                  sizeof(context->RlcLoadEvidence));
    context->PfpDirectLoadStatus = BC250_PFP_DIRECT_LOAD_BAD_ARGUMENT;
    RtlZeroMemory(&context->PfpDirectLoadEvidence,
                  sizeof(context->PfpDirectLoadEvidence));
    context->HqdPreflightStatus = BC250_HQD_PREFLIGHT_BAD_ARGUMENT;
    RtlZeroMemory(&context->HqdPreflightSnapshot,
                  sizeof(context->HqdPreflightSnapshot));
    context->HqdPreflightReadCount = 0u;
    context->MecHqdSelectorStatus = BC250_HQD_SELECTOR_BAD_ARGUMENT;
    RtlZeroMemory(&context->MecHqdSelectorEvidence,
                  sizeof(context->MecHqdSelectorEvidence));
    context->MecHqdBankProbeStatus = BC250_HQD_BANK_PROBE_BAD_ARGUMENT;
    RtlZeroMemory(&context->MecHqdBankProbeEvidence,
                  sizeof(context->MecHqdBankProbeEvidence));
    context->HqdProgramStatus = BC250_HQD_PROGRAM_BAD_ARGUMENT;
    RtlZeroMemory(&context->HqdProgramEvidence,
                  sizeof(context->HqdProgramEvidence));
    context->HqdWptrDiagStatus = BC250_HQD_WPTR_DIAG_BAD_ARGUMENT;
    RtlZeroMemory(&context->HqdWptrDiagEvidence,
                  sizeof(context->HqdWptrDiagEvidence));
    context->HqdActiveStatus = BC250_HQD_ACTIVE_BAD_ARGUMENT;
    RtlZeroMemory(&context->HqdActiveEvidence,
                  sizeof(context->HqdActiveEvidence));
    context->HqdActiveEvidence.GfxOffApplicable = 0u;
    context->HqdActiveEvidence.GfxOffGuardPassed = 1u;
    context->HqdActiveEvidence.GfxOffRestorePassed = 1u;
    context->SdmaF32Status = BC250_SDMA_F32_BAD_ARGUMENT;
    RtlZeroMemory(&context->SdmaF32Evidence,
                  sizeof(context->SdmaF32Evidence));
    context->SdmaRingWorkStatus = BC250_SDMA_RING_WORK_BAD_ARGUMENT;
    RtlZeroMemory(&context->SdmaRingWorkEvidence,
                  sizeof(context->SdmaRingWorkEvidence));
    context->DeferredPspLoadRing = NULL;
    context->DeferredPspLoadCommand = NULL;
    context->DeferredPspLoadFence = NULL;
    context->DeferredPspLoadFirmware = NULL;
    context->SubmissionsCount = 0;
    context->ResetsCount = 0;

    status = Bc250ActiveRegisterAdapter(context);
    if (!NT_SUCCESS(status)) {
        context->Signature = 0u;
        ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
        return status;
    }

    *MiniportDeviceContext = context;
    Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS, (ULONG)STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceActive(
    CONST PVOID MiniportDeviceContext,
    PDXGK_START_INFO DxgkStartInfo,
    PDXGKRNL_INTERFACE DxgkInterface,
    PULONG NumberOfVideoPresentSources,
    PULONG NumberOfChildren)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)MiniportDeviceContext;
    NTSTATUS preflightStatus;

    Bc250ProbeRecord(BC250_PROBE_START_DEVICE_ENTERED, 1);
    if (context == NULL ||
        context->Signature != BC250_ACTIVE_SIGNATURE ||
        DxgkStartInfo == NULL ||
        DxgkInterface == NULL ||
        NumberOfVideoPresentSources == NULL ||
        NumberOfChildren == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Executa somente a auditoria readonly. O contexto nasce sem runtime
     * staged: ownership exclusivo, inventario de VRAM, reservas herdadas e
     * manifesto precisam vir de produtores fisicos no lifecycle PnP. Ate
     * esses produtores existirem, StartDevice deve falhar fechado.
     */
    preflightStatus = Bc250DxgkDdiStartDeviceReadonly(
        &context->Core,
        DxgkStartInfo,
        DxgkInterface,
        NumberOfVideoPresentSources,
        NumberOfChildren);

    if (preflightStatus == STATUS_NOT_SUPPORTED &&
        context->Core.LastStage == BC250_DXGK_STAGE_RUNTIME_MISSING &&
        context->Core.ResolvedBarCount != 0u &&
        context->Core.TopologyEvidence.AddressSpace.SnapshotGeneration != 0u) {
        context->PostDisplayStatus = Bc250AcquirePostDisplayReservation(
            &context->Core.DxgkInterface,
            context->Core.ResolvedBars,
            context->Core.ResolvedBarCount,
            BC250_PCI_VRAM_APERTURE_BAR_INDEX,
            context->Core.TopologyEvidence.AddressSpace.SnapshotGeneration,
            &context->PostDisplayReservation,
            &context->PostDisplayEvidence,
            &context->PostDisplayCallbackStatus);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_STATUS,
            (ULONG)context->PostDisplayStatus);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_CALLBACK_STATUS,
            (ULONG)context->PostDisplayCallbackStatus);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_USED_CALLBACK2,
            context->PostDisplayEvidence.UsedOwnershipCallback2);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_FRAMEBUFFER_STATE,
            context->PostDisplayEvidence.FrameBufferState);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_TARGET_ID,
            context->PostDisplayEvidence.TargetId);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_WIDTH,
            context->PostDisplayEvidence.Width);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_HEIGHT,
            context->PostDisplayEvidence.Height);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_PITCH,
            context->PostDisplayEvidence.Pitch);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_PHYSICAL_LOW32,
            (ULONG)context->PostDisplayEvidence.PhysicalStart);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_PHYSICAL_HIGH32,
            (ULONG)(context->PostDisplayEvidence.PhysicalStart >> 32));
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_BYTES,
            (ULONG)context->PostDisplayEvidence.Length);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_VRAM_OFFSET_LOW32,
            (ULONG)context->PostDisplayEvidence.VramOffset);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_VRAM_OFFSET_HIGH32,
            (ULONG)(context->PostDisplayEvidence.VramOffset >> 32));
        {
            DXGK_DISPLAY_INFORMATION postDisplayInfo;
            RtlZeroMemory(&postDisplayInfo, sizeof(postDisplayInfo));
            postDisplayInfo.Width = context->PostDisplayEvidence.Width;
            postDisplayInfo.Height = context->PostDisplayEvidence.Height;
            postDisplayInfo.Pitch = context->PostDisplayEvidence.Pitch;
            postDisplayInfo.ColorFormat = D3DDDIFMT_A8R8G8B8;
            postDisplayInfo.PhysicAddress.QuadPart = (LONGLONG)context->PostDisplayEvidence.PhysicalStart;
            postDisplayInfo.TargetId = 0;
            postDisplayInfo.AcpiId = 0;
            preflightStatus = Bc250DisplayInit(
                &context->Core.DxgkInterface, &postDisplayInfo);
            if (!NT_SUCCESS(preflightStatus)) {
                Bc250DisplayTeardown();
                Bc250ProbeRecordFirstFailure(preflightStatus);
                return preflightStatus;
            }
        }

#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)
        /*
         * Production display image: StartDevice ends after inheriting the GOP
         * scanout.  Compute/GCVM/CP/HQD transactions are deliberately absent
         * from this compiled path; a registry value must never be able to turn
         * destructive laboratory bring-up on during PnP display ownership.
         */
        Bc250ProbeRecord(BC250_PROBE_SKIP_HW_BRINGUP, 1u);
        *NumberOfVideoPresentSources = 1;
        *NumberOfChildren = 1;
        InterlockedExchange(&context->ContextAbiReady, 0);
        InterlockedExchange(&context->AllocationAbiReady, 0);
        InterlockedExchange(&context->RootPageTableAbiReady, 0);
        InterlockedExchange(&context->IsActive, 1);
        return STATUS_SUCCESS;
    }

    InterlockedExchange(&context->IsActive, 0);
    InterlockedExchange(&context->ContextAbiReady, 0);
    InterlockedExchange(&context->AllocationAbiReady, 0);
    InterlockedExchange(&context->RootPageTableAbiReady, 0);
    Bc250ProbeRecordFirstFailure(preflightStatus);
    return preflightStatus;
#else
        {
            ULONG skipHw = Bc250ActiveReadTunable(L"SkipHwBringup", 1);
            Bc250ProbeRecord(BC250_PROBE_SKIP_HW_BRINGUP, skipHw);
            if (skipHw != 0) {
                if (NumberOfVideoPresentSources != NULL) *NumberOfVideoPresentSources = 1;
                if (NumberOfChildren != NULL) *NumberOfChildren = 1;
                InterlockedExchange(&context->IsActive, 1);
                InterlockedExchange(&context->ContextAbiReady, 1);
                InterlockedExchange(&context->AllocationAbiReady, 1);
                InterlockedExchange(&context->RootPageTableAbiReady, 1);
                return STATUS_SUCCESS;
            }
        }

        context->RuntimeInventoryInput.EvidenceFlags =
            BC250_RUNTIME_EVIDENCE_SCANOUT_PROBED |
            BC250_RUNTIME_EVIDENCE_DISCOVERY_TMR_PROBED |
            BC250_RUNTIME_EVIDENCE_PLATFORM_CARVEOUTS_PROBED;
        if (context->PostDisplayStatus == BC250_POST_DISPLAY_OK) {
            context->RuntimeInventoryInput.HasScanout = 1u;
            context->RuntimeInventoryInput.ScanoutReservation =
                context->PostDisplayReservation;
        }
        if (context->Core.LastDiscoveryTmrStatus ==
            BC250_DISCOVERY_TMR_OK) {
            context->RuntimeInventoryInput.HasDiscoveryTmr = 1u;
            context->RuntimeInventoryInput.DiscoveryTmrEvidence =
                context->Core.DiscoveryTmrEvidence;
        }
        if (context->Core.TopologyEvidence.AddressSpace.TotalBytes >
            context->Core.TopologyEvidence.AddressSpace.CpuVisibleBytes) {
            BC250_INHERITED_VRAM_RESERVATION *opaque =
                &context->RuntimeInventoryInput.PlatformCarveouts[0];
            opaque->Offset =
                context->Core.TopologyEvidence.AddressSpace.CpuVisibleBytes;
            opaque->Length =
                context->Core.TopologyEvidence.AddressSpace.TotalBytes -
                context->Core.TopologyEvidence.AddressSpace.CpuVisibleBytes;
            opaque->Alignment = 0x1000u;
            opaque->Role = BC250_VRAM_ROLE_PLATFORM_RESERVED;
            opaque->Flags = BC250_VRAM_REGION_PERSISTENT;
            context->RuntimeInventoryInput.PlatformCarveoutCount = 1u;
        }
        context->RuntimeInventoryStatus = Bc250BuildRuntimeVramInventory(
            &context->RuntimeInventoryInput,
            context->Core.TopologyEvidence.AddressSpace.TotalBytes,
            context->Core.TopologyEvidence.AddressSpace.CpuVisibleBytes,
            context->Core.TopologyEvidence.AddressSpace.SnapshotGeneration,
            &context->RuntimeInventoryReport);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_STATUS,
            (ULONG)context->RuntimeInventoryStatus);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_EVIDENCE_FLAGS,
            context->RuntimeInventoryReport.EvidenceFlags);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_RESERVATION_COUNT,
            context->RuntimeInventoryReport.ReservationCount);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_RESERVED_LOW32,
            (ULONG)context->RuntimeInventoryReport.TotalReservedBytes);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_RESERVED_HIGH32,
            (ULONG)(context->RuntimeInventoryReport.TotalReservedBytes >> 32));
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_COMPLETE,
            context->RuntimeInventoryStatus == BC250_RUNTIME_INVENTORY_OK ?
                1u : 0u);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_FREE_LOW32,
            (ULONG)context->RuntimeInventoryReport.TotalFreeBytes);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_FREE_HIGH32,
            (ULONG)(context->RuntimeInventoryReport.TotalFreeBytes >> 32));
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_CPU_FREE_LOW32,
            (ULONG)context->RuntimeInventoryReport.CpuVisibleFreeBytes);
        if (context->RuntimeInventoryStatus == BC250_RUNTIME_INVENTORY_OK) {
            RtlZeroMemory(&context->SealedRuntime,
                sizeof(context->SealedRuntime));
            context->SealedRuntime.ReservationCount =
                context->RuntimeInventoryReport.ReservationCount;
            RtlCopyMemory(context->SealedRuntime.Reservations,
                context->RuntimeInventoryReport.Reservations,
                sizeof(context->RuntimeInventoryReport.Reservations));
            context->SealedRuntime.VramApertureBarIndex =
                BC250_PCI_VRAM_APERTURE_BAR_INDEX;
            context->SealedRuntime.MmioBarIndex = BC250_PCI_MMIO_BAR_INDEX;
            context->SealedRuntime.TopologyReaderIdentityFlags =
                BC250_TOPOLOGY_READER_IDENTITY_REQUIRED;
            context->SealedRuntime.RuntimeOwnershipFlags =
                context->RuntimeInventoryReport.OwnershipEvidenceFlags;
            context->SealedRuntime.SnapshotGeneration =
                context->RuntimeInventoryReport.SnapshotGeneration;
            context->SealedRuntime.ManifestCount =
                BC250_CP_BOOTSTRAP_FIRMWARE_COUNT;
            Bc250GetCpFirmwareManifestContract(
                &context->SealedRuntime.Policy,
                context->SealedRuntime.Manifest);
            context->RuntimeStageStatus = Bc250DxgkReadonlyStageRuntime(
                &context->Core, &context->SealedRuntime);
            Bc250ProbeRecord(BC250_PROBE_RUNTIME_STAGE_STATUS,
                (ULONG)context->RuntimeStageStatus);
            if (NT_SUCCESS(context->RuntimeStageStatus)) {
                preflightStatus = Bc250DxgkDdiStartDeviceReadonly(
                    &context->Core, DxgkStartInfo, DxgkInterface,
                    NumberOfVideoPresentSources, NumberOfChildren);
                context->SecondPreflightStage = context->Core.LastStage;
                Bc250ProbeRecord(BC250_PROBE_SECOND_PREFLIGHT_STAGE,
                    context->SecondPreflightStage);
                Bc250ProbeRecord(BC250_PROBE_SECOND_READONLY_GATE_PASSED,
                    context->Core.ReadonlyGatePassed);
                if (context->Core.ReadonlyGatePassed == 1u &&
                    context->Core.LastStage == BC250_DXGK_STAGE_PREFLIGHT_READY) {
                    context->HqdPreflightStatus =
                        Bc250ActiveProbeHqdReadonly(
                            context, &context->HqdPreflightSnapshot,
                            &context->HqdPreflightReadCount);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PREFLIGHT_STATUS,
                        (ULONG)context->HqdPreflightStatus);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PREFLIGHT_READ_COUNT,
                        context->HqdPreflightReadCount);
                    Bc250ProbeRecord(BC250_PROBE_HQD_STABLE_PASSES,
                        context->HqdPreflightSnapshot.StablePasses);
                    Bc250ProbeRecord(BC250_PROBE_HQD_SELECTOR,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_SELECTOR_BEFORE]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_ACTIVE]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_MQD_BASE_LO,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_MQD_BASE_LO]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_MQD_BASE_HI,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_MQD_BASE_HI]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_VMID,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_VMID]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PQ_BASE_LO,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_BASE_LO]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PQ_BASE_HI,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_BASE_HI]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PQ_RPTR,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_RPTR]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_DOORBELL_CONTROL,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_DOORBELL_CONTROL]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PQ_CONTROL,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_CONTROL]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PQ_WPTR_LO,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_WPTR_LO]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PQ_WPTR_HI,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_WPTR_HI]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_INACTIVE,
                        context->HqdPreflightSnapshot.HqdWasInactive);
                    if (context->HqdPreflightStatus ==
                            BC250_HQD_PREFLIGHT_SELECTOR_NOT_COMPUTE &&
                        context->HqdPreflightSnapshot.HqdWasInactive != 0u) {
                        context->MecHqdSelectorStatus =
                            Bc250ActiveProbeMecHqdSelected(
                                context, &context->MecHqdSelectorEvidence);
                    }
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_SELECTOR_STATUS,
                        (ULONG)context->MecHqdSelectorStatus);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_ORIGINAL_SELECTOR,
                        context->MecHqdSelectorEvidence.OriginalSelector);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_APPLIED_SELECTOR,
                        context->MecHqdSelectorEvidence.AppliedSelector);
                    Bc250ProbeRecord(
                        BC250_PROBE_MEC_HQD_APPLIED_READBACK_FIRST,
                        context->MecHqdSelectorEvidence
                            .AppliedSelectorReadbackFirst);
                    Bc250ProbeRecord(
                        BC250_PROBE_MEC_HQD_APPLIED_READBACK_LAST,
                        context->MecHqdSelectorEvidence
                            .AppliedSelectorReadbackLast);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_RESTORED_SELECTOR,
                        context->MecHqdSelectorEvidence.RestoredSelector);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_READ_CALLS,
                        context->MecHqdSelectorEvidence.ReadCalls);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_WRITE_CALLS,
                        context->MecHqdSelectorEvidence.WriteCalls);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_APPLY_READBACK,
                        context->MecHqdSelectorEvidence.ApplyReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_RESTORE,
                        context->MecHqdSelectorEvidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_PHYSICAL_READY,
                        context->MecHqdSelectorEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_STATUS,
                        (ULONG)context->MecHqdSelectorEvidence.HqdStatus);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_STABLE_PASSES,
                        context->MecHqdSelectorEvidence.HqdSnapshot.StablePasses);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_ACTIVE,
                        context->MecHqdSelectorEvidence.HqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_ACTIVE]);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_MQD_BASE_LO,
                        context->MecHqdSelectorEvidence.HqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_MQD_BASE_LO]);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_MQD_BASE_HI,
                        context->MecHqdSelectorEvidence.HqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_MQD_BASE_HI]);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_PQ_BASE_LO,
                        context->MecHqdSelectorEvidence.HqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_BASE_LO]);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_PQ_BASE_HI,
                        context->MecHqdSelectorEvidence.HqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_BASE_HI]);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_PQ_RPTR,
                        context->MecHqdSelectorEvidence.HqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_RPTR]);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_PQ_WPTR_LO,
                        context->MecHqdSelectorEvidence.HqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_WPTR_LO]);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_PQ_WPTR_HI,
                        context->MecHqdSelectorEvidence.HqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_WPTR_HI]);
                    if (context->MecHqdSelectorStatus ==
                            BC250_HQD_SELECTOR_READBACK_FAILED &&
                        context->MecHqdSelectorEvidence.RestorePassed != 0u) {
                        context->MecHqdBankProbeStatus =
                            Bc250ActiveProbeMecHqdBankIsolation(
                                context, &context->MecHqdBankProbeEvidence);
                    }
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_PROBE_STATUS,
                        (ULONG)context->MecHqdBankProbeStatus);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_ORIGINAL_SELECTOR,
                        context->MecHqdBankProbeEvidence.OriginalSelector);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK0_ACTIVE,
                        context->MecHqdBankProbeEvidence.Bank0Active);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK0_PRIORITY,
                        context->MecHqdBankProbeEvidence.Bank0Priority);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_CANDIDATE_ACTIVE,
                        context->MecHqdBankProbeEvidence.CandidateActive);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_CANDIDATE_PRIORITY,
                        context->MecHqdBankProbeEvidence.CandidatePriority);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_POISON_PRIORITY,
                        context->MecHqdBankProbeEvidence.PoisonPriority);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_POISON_READBACK,
                        context->MecHqdBankProbeEvidence.PoisonReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK0_CROSS_READBACK,
                        context->MecHqdBankProbeEvidence.Bank0CrossReadback);
                    Bc250ProbeRecord(
                        BC250_PROBE_HQD_BANK_CANDIDATE_CROSS_READBACK,
                        context->MecHqdBankProbeEvidence
                            .CandidateCrossReadback);
                    Bc250ProbeRecord(
                        BC250_PROBE_HQD_BANK_CANDIDATE_RESTORE_READBACK,
                        context->MecHqdBankProbeEvidence
                            .CandidateRestoreReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK0_FINAL_READBACK,
                        context->MecHqdBankProbeEvidence.Bank0FinalReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_FINAL_SELECTOR,
                        context->MecHqdBankProbeEvidence.FinalSelector);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_READ_CALLS,
                        context->MecHqdBankProbeEvidence.ReadCalls);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_WRITE_CALLS,
                        context->MecHqdBankProbeEvidence.WriteCalls);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_POISON_PASSED,
                        context->MecHqdBankProbeEvidence.PoisonReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_ISOLATION_PASSED,
                        context->MecHqdBankProbeEvidence.BankIsolationPassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_RESTORE_PASSED,
                        context->MecHqdBankProbeEvidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_PHYSICAL_READY,
                        context->MecHqdBankProbeEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_CAPTURE_PASSED,
                        context->MecHqdBankProbeEvidence.HqdCapturePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_STABLE_PASSES,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.StablePasses);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_INACTIVE,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.HqdWasInactive);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_SELECTOR_OBSERVED,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_SELECTOR_BEFORE]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_ACTIVE,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_ACTIVE]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_MQD_BASE_LO,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_MQD_BASE_LO]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_MQD_BASE_HI,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_MQD_BASE_HI]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_VMID,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_VMID]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_PQ_BASE_LO,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_BASE_LO]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_PQ_BASE_HI,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_BASE_HI]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_PQ_RPTR,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_RPTR]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_DOORBELL_CONTROL,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_DOORBELL_CONTROL]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_PQ_CONTROL,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_CONTROL]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_PQ_WPTR_LO,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_WPTR_LO]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_PQ_WPTR_HI,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_WPTR_HI]);
                    if (context->MecHqdBankProbeStatus ==
                            BC250_HQD_BANK_PROBE_OK &&
                        context->MecHqdBankProbeEvidence.PhysicalReady != 0u) {
                        context->HqdProgramStatus =
                            Bc250ActiveRunInactiveHqdProgramTransaction(
                                context, &context->HqdProgramEvidence);
                    }
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_STATUS,
                        (ULONG)context->HqdProgramStatus);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_ORIGINAL_SELECTOR,
                        context->HqdProgramEvidence.OriginalSelector);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_SELECTED_SELECTOR,
                        context->HqdProgramEvidence.SelectedSelector);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_RESTORED_SELECTOR,
                        context->HqdProgramEvidence.RestoredSelector);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_READ_CALLS,
                        context->HqdProgramEvidence.ReadCalls);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_WRITE_CALLS,
                        context->HqdProgramEvidence.WriteCalls);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MEMORY_PASSED,
                        context->HqdProgramEvidence.MemoryVerificationPassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_READBACK_PASSED,
                        context->HqdProgramEvidence.ApplyReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_ACTIVE_ZERO,
                        context->HqdProgramEvidence.ActiveRemainedZero);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_RESTORE_PASSED,
                        context->HqdProgramEvidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MEM_RESTORE_PASSED,
                        context->HqdProgramEvidence.MemoryRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_PHYSICAL_READY,
                        context->HqdProgramEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MQD_BASE_LO,
                        context->HqdProgramEvidence.AppliedValues[0]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MQD_BASE_HI,
                        context->HqdProgramEvidence.AppliedValues[1]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_PQ_BASE_LO,
                        context->HqdProgramEvidence.AppliedValues[11]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_PQ_BASE_HI,
                        context->HqdProgramEvidence.AppliedValues[12]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_PQ_CONTROL,
                        context->HqdProgramEvidence.AppliedValues[14]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_ACTIVE_READBACK,
                        context->HqdProgramEvidence.ActiveReadbackPass1);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MISMATCH_MASK,
                        context->HqdProgramEvidence.MismatchMask);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MISMATCH_OFFSET,
                        context->HqdProgramEvidence.FirstMismatchOffset);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MISMATCH_WRITTEN,
                        context->HqdProgramEvidence.FirstMismatchWritten);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MISMATCH_READ1,
                        context->HqdProgramEvidence.FirstMismatchRead1);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MISMATCH_READ2,
                        context->HqdProgramEvidence.FirstMismatchRead2);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_READBACK_MQD_BASE_LO,
                        context->HqdProgramEvidence.ReadbackPass1[0]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_READBACK_MQD_BASE_HI,
                        context->HqdProgramEvidence.ReadbackPass1[1]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_READBACK_PQ_BASE_LO,
                        context->HqdProgramEvidence.ReadbackPass1[11]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_READBACK_PQ_BASE_HI,
                        context->HqdProgramEvidence.ReadbackPass1[12]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_READBACK_PQ_CONTROL,
                        context->HqdProgramEvidence.ReadbackPass1[14]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_STATUS,
                        (ULONG)context->HqdActiveStatus);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_SELECTED_SELECTOR,
                        context->HqdActiveEvidence.SelectedSelector);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BEFORE,
                        context->HqdActiveEvidence.ActiveBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_APPLIED,
                        context->HqdActiveEvidence.ActiveApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_AFTER,
                        context->HqdActiveEvidence.ActiveAfter);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_BEFORE,
                        context->HqdActiveEvidence.RptrBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_AFTER,
                        context->HqdActiveEvidence.RptrAfter);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_APPLIED,
                        context->HqdActiveEvidence.WptrApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FENCE_BEFORE,
                        context->HqdActiveEvidence.FenceBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FENCE_AFTER,
                        context->HqdActiveEvidence.FenceAfter);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_SCRATCH_BEFORE,
                        context->HqdActiveEvidence.ScratchBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_SCRATCH_AFTER,
                        context->HqdActiveEvidence.ScratchAfter);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_POLL_ITERATIONS,
                        context->HqdActiveEvidence.PollIterations);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_TRIAD_PASSED,
                        context->HqdActiveEvidence.TriadPassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RESTORE_PASSED,
                        context->HqdActiveEvidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEM_RESTORE_PASSED,
                        context->HqdActiveEvidence.MemoryRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PHYSICAL_READY,
                        context->HqdActiveEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_RUNG,
                        context->HqdActiveEvidence.DoorbellRung);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_INDEX,
                        context->HqdActiveEvidence.DoorbellIndex);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_OFFSET,
                        context->HqdActiveEvidence.DoorbellOffset);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_VALUE_APPLIED,
                        context->HqdActiveEvidence.DoorbellValueApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_LOWER_BEFORE,
                        context->HqdActiveEvidence.DoorbellRangeLowerBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_LOWER_APPLIED,
                        context->HqdActiveEvidence.DoorbellRangeLowerApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_LOWER_READBACK,
                        context->HqdActiveEvidence.DoorbellRangeLowerReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_UPPER_BEFORE,
                        context->HqdActiveEvidence.DoorbellRangeUpperBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_UPPER_APPLIED,
                        context->HqdActiveEvidence.DoorbellRangeUpperApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_UPPER_READBACK,
                        context->HqdActiveEvidence.DoorbellRangeUpperReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_BEFORE,
                        context->HqdActiveEvidence.CpPqStatusBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_APPLIED,
                        context->HqdActiveEvidence.CpPqStatusApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_READBACK,
                        context->HqdActiveEvidence.CpPqStatusReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_BEFORE,
                        context->HqdActiveEvidence.WptrPollCntlBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_APPLIED,
                        context->HqdActiveEvidence.WptrPollCntlApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_READBACK,
                        context->HqdActiveEvidence.WptrPollCntlReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_SELECTOR_RELEASED_BEFORE_KICK,
                        context->HqdActiveEvidence.SelectorReleasedBeforeKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.CpPqStatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_CONTROL_AFTER_KICK,
                        context->HqdActiveEvidence.DoorbellControlAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_AFTER_KICK,
                        context->HqdActiveEvidence.WptrAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_AFTER_KICK,
                        context->HqdActiveEvidence.RptrAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_INITIAL_WPTR_LO,
                        context->HqdActiveEvidence.InitialWptrLo);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_INITIAL_WPTR_HI,
                        context->HqdActiveEvidence.InitialWptrHi);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_APPLIED,
                        context->HqdActiveEvidence.BaselineWptrApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_READBACK,
                        context->HqdActiveEvidence.BaselineWptrReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_RESTORED,
                        context->HqdActiveEvidence.BaselineWptrRestored);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_RESTORE_POLLS,
                        context->HqdActiveEvidence.BaselineWptrRestorePolls);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_RESTORE_PASSED,
                        context->HqdActiveEvidence.BaselineWptrRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_KICK_APPLIED,
                        context->HqdActiveEvidence.DirectWptrKickApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_BEFORE,
                        context->HqdActiveEvidence.DirectWptrBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_READBACK,
                        context->HqdActiveEvidence.DirectWptrReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_RPTR_AFTER_KICK,
                        context->HqdActiveEvidence.DirectRptrAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_RESTORED,
                        context->HqdActiveEvidence.DirectWptrRestored);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_RESTORE_POLLS,
                        context->HqdActiveEvidence.DirectWptrRestorePolls);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_RESTORE_PASSED,
                        context->HqdActiveEvidence.DirectWptrRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_MMIO_LAST,
                        context->HqdActiveEvidence.RptrMmioLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_MMIO_LAST,
                        context->HqdActiveEvidence.WptrMmioLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQ_STATUS0_POLL_LAST,
                        context->HqdActiveEvidence.HqStatus0PollLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQD_ERROR_POLL_LAST,
                        context->HqdActiveEvidence.HqdErrorPollLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_BEFORE,
                        context->HqdActiveEvidence.RlcCpSchedulersBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_APPLIED,
                        context->HqdActiveEvidence.RlcCpSchedulersApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_READBACK,
                        context->HqdActiveEvidence.RlcCpSchedulersReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_AFTER_KICK,
                        context->HqdActiveEvidence.RlcCpSchedulersAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_RESTORED,
                        context->HqdActiveEvidence.RlcCpSchedulersRestored);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_RESTORE_PASSED,
                        context->HqdActiveEvidence.RlcCpSchedulersRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQ_STATUS0_AFTER_KICK,
                        context->HqdActiveEvidence.HqStatus0AfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQ_CONTROL0_AFTER_KICK,
                        context->HqdActiveEvidence.HqControl0AfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQD_ERROR_AFTER_KICK,
                        context->HqdActiveEvidence.HqdErrorAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_UTCL1_ERROR_AFTER_KICK,
                        context->HqdActiveEvidence.CpcUtcl1ErrorAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_UTCL1_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.CpcUtcl1StatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_UTCL1_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.CpfUtcl1StatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_INT_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.CpcIntStatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_ME1_PIPE0_INT_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.Me1Pipe0IntStatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_ME2_PIPE1_INT_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.Me2Pipe1IntStatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_ME2_PIPE0_INT_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.Me2Pipe0IntStatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PQ_BASE_LO,
                        context->HqdActiveEvidence.PqBaseLoApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PQ_BASE_HI,
                        context->HqdActiveEvidence.PqBaseHiApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_ADDR_LO,
                        context->HqdActiveEvidence.WptrPollAddrLoApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_ADDR_HI,
                        context->HqdActiveEvidence.WptrPollAddrHiApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_SHADOW,
                        context->HqdActiveEvidence.WptrShadowApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_MEC_CNTL_READBACK,
                        context->HqdActiveEvidence.CpMecCntlReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_PC_FIRST,
                        context->HqdActiveEvidence.MecPcFirst);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_PC_LAST,
                        context->HqdActiveEvidence.MecPcLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_PC_TRANSITIONS,
                        context->HqdActiveEvidence.MecPcTransitions);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_PC_SAMPLES,
                        context->HqdActiveEvidence.MecPcSamples);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PQ_CONTROL_OBSERVED,
                        context->HqdActiveEvidence.PqControlObserved);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HDP_INVALIDATE_APPLIED,
                        context->HqdActiveEvidence.HdpInvalidateApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_L2_BEFORE,
                        context->HqdActiveEvidence.GcvmL2Cntl2Before);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_L2_APPLIED,
                        context->HqdActiveEvidence.GcvmL2Cntl2Applied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_L2_READBACK,
                        context->HqdActiveEvidence.GcvmL2Cntl2Readback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_REQ_APPLIED,
                        context->HqdActiveEvidence.GcvmInvalidateRequestApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_REQ_READBACK,
                        context->HqdActiveEvidence.GcvmInvalidateRequestReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_ACK_BEFORE,
                        context->HqdActiveEvidence.GcvmInvalidateAckBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_ACK_AFTER,
                        context->HqdActiveEvidence.GcvmInvalidateAckAfter);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_POLLS,
                        context->HqdActiveEvidence.GcvmInvalidatePolls);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_COHERENCY_PASSED,
                        context->HqdActiveEvidence.CoherencyPassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_APPLICABLE,
                        context->HqdActiveEvidence.GfxOffApplicable);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_STATE_BEFORE,
                        context->HqdActiveEvidence.GfxOffStateBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_STATE_GUARDED,
                        context->HqdActiveEvidence.GfxOffStateGuarded);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_STATE_AFTER,
                        context->HqdActiveEvidence.GfxOffStateAfter);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_DISALLOW_RESPONSE,
                        context->HqdActiveEvidence.GfxOffDisallowResponse);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_ALLOW_RESPONSE,
                        context->HqdActiveEvidence.GfxOffAllowResponse);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_GUARD_PASSED,
                        context->HqdActiveEvidence.GfxOffGuardPassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_RESTORE_PASSED,
                        context->HqdActiveEvidence.GfxOffRestorePassed);
                    context->VramProbeStatus = Bc250RunVramProbeTransaction(
                        &context->Core.DxgkInterface,
                        context->Core.ResolvedBars,
                        context->Core.ResolvedBarCount,
                        BC250_PCI_VRAM_APERTURE_BAR_INDEX,
                        context->Core.StartDevice.Preflight.FirmwarePlans[0]
                            .Binding.Offset,
                        &context->VramProbeEvidence);
                    Bc250ProbeRecord(BC250_PROBE_VRAM_PROBE_STATUS,
                        (ULONG)context->VramProbeStatus);
                    Bc250ProbeRecord(BC250_PROBE_VRAM_PROBE_OFFSET_LOW32,
                        (ULONG)context->VramProbeEvidence.Offset);
                    Bc250ProbeRecord(BC250_PROBE_VRAM_PROBE_BYTES,
                        context->VramProbeEvidence.Bytes);
                    Bc250ProbeRecord(BC250_PROBE_VRAM_PROBE_READBACK_PASSED,
                        context->VramProbeEvidence.WriteReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_VRAM_PROBE_RESTORE_PASSED,
                        context->VramProbeEvidence.RestorePassed);
                    if (context->VramProbeStatus == BC250_VRAM_PROBE_OK &&
                        context->VramProbeEvidence.WriteReadbackPassed != 0u &&
                        context->VramProbeEvidence.RestorePassed != 0u) {
                        context->GcvmBackingStatus =
                            Bc250RunGcvmBackingTransaction(
                                &context->Core.StartDevice.Owned.Bootstrap
                                    .Allocator,
                                context->Core.StartDevice.Owned.Bootstrap
                                    .AddressSpace.VramBaseOffset,
                                &context->Core.DxgkInterface,
                                context->Core.ResolvedBars,
                                context->Core.ResolvedBarCount,
                                BC250_PCI_VRAM_APERTURE_BAR_INDEX,
                                Bc250ActiveExecuteGcvmBacking,
                                context,
                                &context->GcvmBackingEvidence);
                    }
                    Bc250ProbeRecord(BC250_PROBE_TLB_TRANSACTION_STATUS,
                        (ULONG)context->TlbTransactionStatus);
                    Bc250ProbeRecord(BC250_PROBE_TLB_VMID,
                        context->TlbTransactionEvidence.Vmid);
                    Bc250ProbeRecord(BC250_PROBE_TLB_ENGINE_ID,
                        context->TlbTransactionEvidence.EngineId);
                    Bc250ProbeRecord(BC250_PROBE_TLB_REQUEST_VALUE,
                        context->TlbTransactionEvidence.RequestValue);
                    Bc250ProbeRecord(BC250_PROBE_TLB_RANGE_LOW_BEFORE,
                        context->TlbTransactionEvidence.RangeLowBefore);
                    Bc250ProbeRecord(BC250_PROBE_TLB_RANGE_HIGH_BEFORE,
                        context->TlbTransactionEvidence.RangeHighBefore);
                    Bc250ProbeRecord(BC250_PROBE_TLB_RANGE_LOW_PROGRAMMED,
                        context->TlbTransactionEvidence.RangeLowProgrammed);
                    Bc250ProbeRecord(BC250_PROBE_TLB_RANGE_HIGH_PROGRAMMED,
                        context->TlbTransactionEvidence.RangeHighProgrammed);
                    Bc250ProbeRecord(BC250_PROBE_TLB_REQUEST_DUMMY_READ,
                        context->TlbTransactionEvidence.RequestDummyRead);
                    Bc250ProbeRecord(BC250_PROBE_TLB_ACK_BEFORE,
                        context->TlbTransactionEvidence.AckBefore);
                    Bc250ProbeRecord(BC250_PROBE_TLB_ACK_AFTER,
                        context->TlbTransactionEvidence.AckAfter);
                    Bc250ProbeRecord(BC250_PROBE_TLB_POLL_COUNT,
                        context->TlbTransactionEvidence.PollCount);
                    Bc250ProbeRecord(BC250_PROBE_TLB_REQUEST_WRITTEN,
                        context->TlbTransactionEvidence.RequestWritten);
                    Bc250ProbeRecord(BC250_PROBE_TLB_DUMMY_READ_COMPLETED,
                        context->TlbTransactionEvidence.DummyReadCompleted);
                    Bc250ProbeRecord(BC250_PROBE_TLB_ACK_OBSERVED,
                        context->TlbTransactionEvidence.AckObserved);
                    Bc250ProbeRecord(BC250_PROBE_TLB_RANGE_RESTORE_PASSED,
                        context->TlbTransactionEvidence.RangeRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_BACKING_STATUS,
                        (ULONG)context->GcvmBackingStatus);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_PT_OFFSET_LOW32,
                        (ULONG)context->GcvmBackingEvidence.PageTableOffset);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_PT_PHYSICAL_LOW32,
                        (ULONG)context->GcvmBackingEvidence.PageTablePhysical);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_PT_PHYSICAL_HIGH32,
                        (ULONG)(context->GcvmBackingEvidence.PageTablePhysical >> 32));
                    Bc250ProbeRecord(BC250_PROBE_GCVM_TARGET_OFFSET_LOW32,
                        (ULONG)context->GcvmBackingEvidence.TargetOffset);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_FIRST_PTE_LOW32,
                        (ULONG)context->GcvmBackingEvidence.FirstPte);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_FIRST_PTE_HIGH32,
                        (ULONG)(context->GcvmBackingEvidence.FirstPte >> 32));
                    Bc250ProbeRecord(BC250_PROBE_GCVM_BACKING_OWNED,
                        context->GcvmBackingEvidence.OwnershipPassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_BACKING_READBACK,
                        context->GcvmBackingEvidence.WriteReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_BACKING_RESTORE,
                        context->GcvmBackingEvidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_VMID8_STATUS,
                        (ULONG)context->GcvmVmid8Status);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_VMID8_CNTL_BEFORE,
                        context->GcvmVmid8Evidence.ContextControlBefore);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_VMID8_READBACK,
                        context->GcvmVmid8Evidence.ProgramReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_VMID8_TLB_PASSED,
                        context->GcvmVmid8Evidence.TlbFlushPassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_VMID8_RESTORE,
                        context->GcvmVmid8Evidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_INERT_PHYSICAL_READY,
                        context->GcvmVmid8Evidence.PhysicalReady != 0u &&
                        context->GcvmBackingEvidence.ExecutePassed != 0u &&
                        context->GcvmBackingEvidence.RestorePassed != 0u);
                    if (context->GcvmBackingStatus == BC250_GCVM_BACKING_OK &&
                        context->GcvmVmid8Evidence.PhysicalReady != 0u) {
                        context->PspRingStatus = Bc250ActiveProbePspRing(
                            context, &context->PspRingEvidence);
                    }
                    if (context->PspRingStatus == BC250_PSP_RING_OK &&
                        context->PspRingEvidence.PhysicalReady != 0u) {
                        context->PspLoadFwStatus = Bc250ActiveLoadSdmaFirmware(
                            context, &context->PspLoadFwEvidence);
                        if (context->PspLoadFwStatus ==
                                BC250_PSP_LOAD_FW_OK &&
                            context->SdmaRingWorkStatus ==
                                BC250_SDMA_RING_WORK_OK) {
                            context->PfpDirectLoadStatus =
                                Bc250ActiveRunPfpDirectLoad(
                                    context,
                                    &context->PfpDirectLoadEvidence);
                            if (context->PfpDirectLoadStatus ==
                                    BC250_PFP_DIRECT_LOAD_OK &&
                                context->PfpDirectLoadEvidence.PhysicalReady != 0u) {
                                context->HqdActiveStatus =
                                    Bc250ActiveRunActiveHqdSubmissionTransaction(
                                        context, &context->HqdActiveEvidence);
                                Bc250ActiveRecordHqdSubmission(context);
                            }
                        }
                    }
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_STATUS,
                        (ULONG)context->PspRingStatus);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_PHYSICAL_LOW32,
                        (ULONG)context->PspRingEvidence.RingPhysical);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_C2PMSG64_BEFORE,
                        context->PspRingEvidence.C2pmsg64Before);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_CREATE_ACK,
                        context->PspRingEvidence.CreateAck);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_DESTROY_ACK,
                        context->PspRingEvidence.DestroyAck);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_CREATE_POLLS,
                        context->PspRingEvidence.CreatePolls);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_DESTROY_POLLS,
                        context->PspRingEvidence.DestroyPolls);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_CREATE_PASSED,
                        context->PspRingEvidence.CreatePassed);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_DESTROY_PASSED,
                        context->PspRingEvidence.DestroyPassed);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_RESTORE_PASSED,
                        context->PspRingEvidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_PHYSICAL_READY,
                        context->PspRingEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_STATUS,
                        (ULONG)context->PspLoadFwStatus);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_BYTES,
                        context->PspLoadFwEvidence.FirmwareBytes);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_TYPE,
                        context->PspLoadFwEvidence.FirmwareType);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_RING_LOW,
                        (ULONG)context->PspLoadFwEvidence.RingPhysical);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_RING_HIGH,
                        (ULONG)(context->PspLoadFwEvidence.RingPhysical >> 32));
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_COMMAND_LOW,
                        (ULONG)context->PspLoadFwEvidence.CommandPhysical);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_COMMAND_HIGH,
                        (ULONG)(context->PspLoadFwEvidence.CommandPhysical >> 32));
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_FENCE_LOW,
                        (ULONG)context->PspLoadFwEvidence.FencePhysical);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_FENCE_HIGH,
                        (ULONG)(context->PspLoadFwEvidence.FencePhysical >> 32));
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_PAYLOAD_LOW,
                        (ULONG)context->PspLoadFwEvidence.FirmwarePhysical);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_PAYLOAD_HIGH,
                        (ULONG)(context->PspLoadFwEvidence.FirmwarePhysical >> 32));
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_CREATE_ACK,
                        context->PspLoadFwEvidence.CreateAck);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_FENCE_EXPECTED,
                        context->PspLoadFwEvidence.FenceExpected);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_FENCE_OBSERVED,
                        context->PspLoadFwEvidence.FenceObserved);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_FENCE_POLLS,
                        context->PspLoadFwEvidence.FencePolls);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_RESPONSE_STATUS,
                        context->PspLoadFwEvidence.ResponseStatus);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_CHECKSUM_BEFORE,
                        context->PspLoadFwEvidence.ChecksumBefore);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_CHECKSUM_AFTER,
                        context->PspLoadFwEvidence.ChecksumAfter);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_DESTROY_ACK,
                        context->PspLoadFwEvidence.DestroyAck);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_FENCE_PASSED,
                        context->PspLoadFwEvidence.FencePassed);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_RESPONSE_PASSED,
                        context->PspLoadFwEvidence.ResponsePassed);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_CHECKSUM_PASSED,
                        context->PspLoadFwEvidence.ChecksumPassed);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_DESTROY_PASSED,
                        context->PspLoadFwEvidence.DestroyPassed);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_RESTORE_PASSED,
                        context->PspLoadFwEvidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_PHYSICAL_READY,
                        context->PspLoadFwEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RLC_LOAD_STATUS,
                        (ULONG)context->PspRlcLoadStatus);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RLC_LOAD_BYTES,
                        context->PspRlcLoadEvidence.FirmwareBytes);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RLC_LOAD_TYPE,
                        context->PspRlcLoadEvidence.FirmwareType);
                    Bc250ProbeRecord(
                        BC250_PROBE_PSP_RLC_LOAD_FENCE_EXPECTED,
                        context->PspRlcLoadEvidence.FenceExpected);
                    Bc250ProbeRecord(
                        BC250_PROBE_PSP_RLC_LOAD_FENCE_OBSERVED,
                        context->PspRlcLoadEvidence.FenceObserved);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RLC_LOAD_FENCE_POLLS,
                        context->PspRlcLoadEvidence.FencePolls);
                    Bc250ProbeRecord(
                        BC250_PROBE_PSP_RLC_LOAD_RESPONSE_STATUS,
                        context->PspRlcLoadEvidence.ResponseStatus);
                    Bc250ProbeRecord(
                        BC250_PROBE_PSP_RLC_LOAD_DESTROY_PASSED,
                        context->PspRlcLoadEvidence.DestroyPassed);
                    Bc250ProbeRecord(
                        BC250_PROBE_PSP_RLC_LOAD_RESTORE_PASSED,
                        context->PspRlcLoadEvidence.RestorePassed);
                    Bc250ProbeRecord(
                        BC250_PROBE_PSP_RLC_LOAD_PHYSICAL_READY,
                        context->PspRlcLoadEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_RLC_CNTL_BEFORE,
                        context->RlcLoadEvidence.CntlBefore);
                    Bc250ProbeRecord(BC250_PROBE_RLC_GPM_BEFORE,
                        context->RlcLoadEvidence.GpmStatBefore);
                    Bc250ProbeRecord(BC250_PROBE_RLC_CNTL_AFTER,
                        context->RlcLoadEvidence.CntlAfter);
                    Bc250ProbeRecord(BC250_PROBE_RLC_GPM_AFTER,
                        context->RlcLoadEvidence.GpmStatAfter);
                    Bc250ProbeRecord(BC250_PROBE_RLC_EXEC_ROM_BEFORE,
                        context->RlcLoadEvidence.ExecRomBefore);
                    Bc250ProbeRecord(BC250_PROBE_RLC_EXEC_ROM_AFTER,
                        context->RlcLoadEvidence.ExecRomAfter);
                    Bc250ProbeRecord(BC250_PROBE_RLC_ENABLE_F32_AFTER,
                        context->RlcLoadEvidence.EnableF32After);
                    Bc250ProbeRecord(BC250_PROBE_RLC_CNTL_RESTORE,
                        context->RlcLoadEvidence.CntlRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_RLC_ALREADY_ACTIVE,
                        context->RlcLoadEvidence.AlreadyActive);
                    Bc250ProbeRecord(BC250_PROBE_RLC_PHYSICAL_READY,
                        context->RlcLoadEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_STATUS,
                        (ULONG)context->PfpDirectLoadStatus);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_MC_LOW,
                        (ULONG)context->PfpDirectLoadEvidence.FirmwareMcAddress);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_MC_HIGH,
                        (ULONG)(context->PfpDirectLoadEvidence.FirmwareMcAddress >> 32));
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_BYTES,
                        context->PfpDirectLoadEvidence.FirmwareBytes);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_JT_DWORDS,
                        context->PfpDirectLoadEvidence.JumpTableDwords);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_VERSION,
                        context->PfpDirectLoadEvidence.UcodeVersion);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_STAGED_DWORDS,
                        context->PfpDirectLoadEvidence.StagedDwords);
                    Bc250ProbeRecord(
                        BC250_PROBE_PFP_DIRECT_STAGING_MISMATCHES,
                        context->PfpDirectLoadEvidence.StagingMismatches);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_ME_CNTL_BEFORE,
                        context->PfpDirectLoadEvidence.CpMeCntlBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_ME_CNTL_APPLIED,
                        context->PfpDirectLoadEvidence.CpMeCntlApplied);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_ME_CNTL_AFTER,
                        context->PfpDirectLoadEvidence.CpMeCntlAfter);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RLC_CNTL_BEFORE,
                        context->PfpDirectLoadEvidence.RlcCntlBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RLC_CNTL_APPLIED,
                        context->PfpDirectLoadEvidence.RlcCntlApplied);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_PC_BEFORE,
                        context->PfpDirectLoadEvidence.PfpPcBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_PC_FIRST,
                        context->PfpDirectLoadEvidence.PfpPcFirst);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_PC_LAST,
                        context->PfpDirectLoadEvidence.PfpPcLast);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_PC_TRANSITIONS,
                        context->PfpDirectLoadEvidence.PfpPcTransitions);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_PC_SAMPLES,
                        context->PfpDirectLoadEvidence.PfpPcSamples);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_ME_PC_BEFORE,
                        context->PfpDirectLoadEvidence.MePcBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_ME_PC_LAST,
                        context->PfpDirectLoadEvidence.MePcLast);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_CE_PC_BEFORE,
                        context->PfpDirectLoadEvidence.CePcBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_CE_PC_LAST,
                        context->PfpDirectLoadEvidence.CePcLast);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_FAULT_BEFORE,
                        context->PfpDirectLoadEvidence.FaultStatusBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_FAULT_AFTER,
                        context->PfpDirectLoadEvidence.FaultStatusAfter);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_FAULT_ADDR_LOW,
                        context->PfpDirectLoadEvidence.FaultAddressLow);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_FAULT_ADDR_HIGH,
                        context->PfpDirectLoadEvidence.FaultAddressHigh);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_PORTS_PROGRAMMED,
                        context->PfpDirectLoadEvidence.WriteOnlyPortsProgrammed);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RB0_ACTIVE_BEFORE,
                        context->PfpDirectLoadEvidence.Rb0ActiveBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RB0_ACTIVE_APPLIED,
                        context->PfpDirectLoadEvidence.Rb0ActiveApplied);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RB0_ACTIVE_AFTER,
                        context->PfpDirectLoadEvidence.Rb0ActiveAfter);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RB0_RPTR_BEFORE,
                        context->PfpDirectLoadEvidence.Rb0RptrBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RB0_RPTR_AFTER,
                        context->PfpDirectLoadEvidence.Rb0RptrAfter);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RB0_WPTR_APPLIED,
                        context->PfpDirectLoadEvidence.Rb0WptrApplied);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RB0_FENCE_BEFORE,
                        context->PfpDirectLoadEvidence.Rb0FenceBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RB0_FENCE_AFTER,
                        context->PfpDirectLoadEvidence.Rb0FenceAfter);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_REG_RESTORE,
                        context->PfpDirectLoadEvidence.RegisterRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_MEM_RESTORE,
                        context->PfpDirectLoadEvidence.MemoryRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_PHYSICAL_READY,
                        context->PfpDirectLoadEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_STATUS,
                        (ULONG)context->SdmaF32Status);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_CLOCK_BEFORE,
                        context->SdmaF32Evidence.ClockBefore);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_CONTROL_BEFORE,
                        context->SdmaF32Evidence.ControlBefore);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_CONTROL_APPLIED,
                        context->SdmaF32Evidence.ControlApplied);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_F32_BEFORE,
                        context->SdmaF32Evidence.F32Before);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_F32_APPLIED,
                        context->SdmaF32Evidence.F32Applied);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_PC_FIRST,
                        context->SdmaF32Evidence.PcFirst);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_PC_LAST,
                        context->SdmaF32Evidence.PcLast);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_PC_TRANSITIONS,
                        context->SdmaF32Evidence.PcTransitions);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_APPLY_READBACK,
                        context->SdmaF32Evidence.ApplyReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_RESTORE,
                        context->SdmaF32Evidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_PHYSICAL_READY,
                        context->SdmaF32Evidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_WORK_STATUS,
                        (ULONG)context->SdmaRingWorkStatus);
                    Bc250ProbeRecord(
                        BC250_PROBE_SDMA_RING_FIXED_RESERVATION_STATUS,
                        context->SdmaRingWorkEvidence.FixedReservationStatus);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_ALLOCATION_MODE,
                        context->SdmaRingWorkEvidence.AllocationMode);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_RING_LOW,
                        (ULONG)context->SdmaRingWorkEvidence.RingMcAddress);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_RING_HIGH,
                        (ULONG)(context->SdmaRingWorkEvidence.RingMcAddress >> 32));
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_WORK_LOW,
                        (ULONG)context->SdmaRingWorkEvidence.WorkMcAddress);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_WORK_HIGH,
                        (ULONG)(context->SdmaRingWorkEvidence.WorkMcAddress >> 32));
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_RPTR_BEFORE,
                        context->SdmaRingWorkEvidence.RptrBefore);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_RPTR_AFTER,
                        context->SdmaRingWorkEvidence.RptrAfter);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_WPTR,
                        context->SdmaRingWorkEvidence.WptrPublished);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_WPTR_REGISTER,
                        context->SdmaRingWorkEvidence.WptrRegisterAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_WPTR_SHADOW,
                        context->SdmaRingWorkEvidence.WptrShadowAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_RB_CNTL,
                        context->SdmaRingWorkEvidence.RbCntlApplied);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_HEADER,
                        context->SdmaRingWorkEvidence.RingHeaderReadback);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_RPTR_REGISTER,
                        context->SdmaRingWorkEvidence.RptrRegisterAfter);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_POLL_CNTL,
                        context->SdmaRingWorkEvidence.WptrPollCntlApplied);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_FENCE_POISON,
                        context->SdmaRingWorkEvidence.FencePoison);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_FENCE_EXPECTED,
                        context->SdmaRingWorkEvidence.FenceExpected);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_FENCE_OBSERVED,
                        context->SdmaRingWorkEvidence.FenceObserved);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_POLLS,
                        context->SdmaRingWorkEvidence.Polls);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_COMPARED_DWORDS,
                        context->SdmaRingWorkEvidence.ComparedDwords);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_MISMATCHES,
                        context->SdmaRingWorkEvidence.MismatchCount);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_STATUS_BEFORE,
                        context->SdmaRingWorkEvidence.StatusBefore);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_STATUS_KICK,
                        context->SdmaRingWorkEvidence.StatusKick);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_STATUS_AFTER,
                        context->SdmaRingWorkEvidence.StatusAfter);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_PC_KICK,
                        context->SdmaRingWorkEvidence.PcKick);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_STATUS1_AFTER,
                        context->SdmaRingWorkEvidence.Status1After);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_STATUS2_AFTER,
                        context->SdmaRingWorkEvidence.Status2After);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_STATUS3_AFTER,
                        context->SdmaRingWorkEvidence.Status3After);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_ERROR_LOG_AFTER,
                        context->SdmaRingWorkEvidence.ErrorLogAfter);
                    Bc250ProbeRecord(
                        BC250_PROBE_SDMA_RING_UTCL1_RD_STATUS_AFTER,
                        context->SdmaRingWorkEvidence.Utcl1ReadStatusAfter);
                    Bc250ProbeRecord(
                        BC250_PROBE_SDMA_RING_UTCL1_WR_STATUS_AFTER,
                        context->SdmaRingWorkEvidence.Utcl1WriteStatusAfter);
                    Bc250ProbeRecord(
                        BC250_PROBE_SDMA_RING_UTCL1_RD_XNACK0_AFTER,
                        context->SdmaRingWorkEvidence.Utcl1ReadXnack0After);
                    Bc250ProbeRecord(
                        BC250_PROBE_SDMA_RING_UTCL1_RD_XNACK1_AFTER,
                        context->SdmaRingWorkEvidence.Utcl1ReadXnack1After);
                    Bc250ProbeRecord(
                        BC250_PROBE_SDMA_RING_UTCL1_WR_XNACK0_AFTER,
                        context->SdmaRingWorkEvidence.Utcl1WriteXnack0After);
                    Bc250ProbeRecord(
                        BC250_PROBE_SDMA_RING_UTCL1_WR_XNACK1_AFTER,
                        context->SdmaRingWorkEvidence.Utcl1WriteXnack1After);
                    Bc250ProbeRecord(
                        BC250_PROBE_SDMA_RING_UTCL1_TIMEOUT_AFTER,
                        context->SdmaRingWorkEvidence.Utcl1TimeoutAfter);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_REG_RESTORE,
                        context->SdmaRingWorkEvidence.RegisterRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_MEM_RESTORE,
                        context->SdmaRingWorkEvidence.MemoryRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_PHYSICAL_READY,
                        context->SdmaRingWorkEvidence.PhysicalReady);
                    {
                        BC250_MCDM_RUNTIME_CAPS caps;
                        NTSTATUS capsStatus = Bc250ActiveQueryRuntimeCaps(
                            context, &caps);
                        Bc250ProbeRecord(BC250_PROBE_RUNTIME_CAPS_STATUS,
                            (ULONG)capsStatus);
                        Bc250ProbeRecord(BC250_PROBE_RUNTIME_CAPS_READY,
                            caps.Ready);
                        Bc250ProbeRecord(BC250_PROBE_RUNTIME_CAPS_BASE_LOW32,
                            (ULONG)caps.LocalSegmentBaseOffset);
                        Bc250ProbeRecord(BC250_PROBE_RUNTIME_CAPS_LOCAL_LOW32,
                            (ULONG)caps.LocalSegmentBytes);
                        Bc250ProbeRecord(BC250_PROBE_RUNTIME_CAPS_NODE_COUNT,
                            caps.NodeCount);
                        Bc250ProbeRecord(BC250_PROBE_MCDM_FULL_CAPS_READY,
                            caps.FullMcdmReady);
                        Bc250ProbeRecord(BC250_PROBE_MCDM_SEGMENT4_READY,
                            caps.Ready);
                        if (NT_SUCCESS(capsStatus) && caps.Ready != 0u) {
                            context->AllocationSegmentBytes =
                                caps.LocalSegmentBytes;
                            context->AllocationSegmentSet = 1u;
                        }
                        Bc250ProbeRecord(
                            BC250_PROBE_ALLOCATION_SEGMENT_SET,
                            context->AllocationSegmentSet);
                        Bc250ProbeRecord(
                            BC250_PROBE_ALLOCATION_MAX_LOW32,
                            (ULONG)context->AllocationSegmentBytes);
                        Bc250ProbeRecord(
                            BC250_PROBE_GPUVA_DESCRIPTOR_READY,
                            caps.GpuVaReady != 0u &&
                            caps.GpuVaBitCount != 0u &&
                            caps.PageTableLevelCount != 0u);
                        Bc250ProbeRecord(
                            BC250_PROBE_ROOT_PAGE_TABLE_ABI_READY,
                            (ULONG)InterlockedCompareExchange(
                                &context->RootPageTableAbiReady, 1, 1));
                        Bc250ProbeRecord(
                            BC250_PROBE_ROOT_PAGE_TABLE_MAX_ENTRIES,
                            context->RootPageTableMaxEntries);
                        Bc250ProbeRecord(
                            BC250_PROBE_PAGING_CPU_UPDATE_READY,
                            caps.GpuVaReady != 0u &&
                            caps.PagingReady != 0u);
                        Bc250ProbeRecord(
                            BC250_PROBE_TLB_FLUSH_PHYSICAL_READY,
                            context->TlbTransactionEvidence.PhysicalReady);
                    }
                    if (context->PspRingStatus == BC250_PSP_RING_OK &&
                        context->PspRingEvidence.PhysicalReady != 0u &&
                        context->PspLoadFwStatus == BC250_PSP_LOAD_FW_OK &&
                        context->SdmaRingWorkStatus == BC250_SDMA_RING_WORK_OK) {
                        if (NumberOfVideoPresentSources != NULL) *NumberOfVideoPresentSources = 1;
                        if (NumberOfChildren != NULL) *NumberOfChildren = 1;
                        InterlockedExchange(&context->IsActive, 1);
                        InterlockedExchange(&context->ContextAbiReady, 1);
                        InterlockedExchange(&context->AllocationAbiReady, 1);
                        InterlockedExchange(&context->RootPageTableAbiReady, 1);
                        return STATUS_SUCCESS;
                    }
                }
            }
        }
    }

    InterlockedExchange(&context->IsActive, 0);
    InterlockedExchange(&context->ContextAbiReady, 0);
    InterlockedExchange(&context->AllocationAbiReady, 0);
    InterlockedExchange(&context->RootPageTableAbiReady, 0);
    Bc250ProbeRecordFirstFailure(preflightStatus);
    return preflightStatus;
#endif
}

NTSTATUS APIENTRY Bc250DxgkDdiStopDeviceActive(
    CONST PVOID MiniportDeviceContext)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)MiniportDeviceContext;
    uint32_t destroyedObjects;
    Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_ENTERED, 1);
    if (context == NULL ||
        context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    InterlockedExchange(&context->IsActive, 0);
    InterlockedExchange(&context->ContextAbiReady, 0);
    InterlockedExchange(&context->AllocationAbiReady, 0);
    InterlockedExchange(&context->RootPageTableAbiReady, 0);
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&context->ObjectLock);
    destroyedObjects = 0u;
    (void)Bc250McdmObjectRegistryTeardown(
        &context->Objects, &destroyedObjects);
    ExReleasePushLockExclusive(&context->ObjectLock);
    KeLeaveCriticalRegion();
    Bc250DisplayTeardown();
    {
        NTSTATUS status = Bc250DxgkDdiStopDeviceReadonly(&context->Core);
        Bc250ProbeRecordFirstFailure(status);
        return status;
    }
}

NTSTATUS APIENTRY Bc250DxgkDdiRemoveDeviceActive(
    CONST PVOID MiniportDeviceContext)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)MiniportDeviceContext;
    NTSTATUS status;

    Bc250ProbeRecord(BC250_PROBE_REMOVE_DEVICE_ENTERED, 1);
    if (context == NULL ||
        context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    status = Bc250DxgkDdiStopDeviceActive(context);
    Bc250ActiveUnregisterAdapter(context);
    context->Signature = 0;
    context->PhysicalDeviceObject = NULL;
    context->Core.PhysicalDeviceObject = NULL;
    ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
    Bc250ProbeRecord(BC250_PROBE_REMOVE_DEVICE_STATUS, (ULONG)status);
    return status;
}

NTSTATUS Bc250BuildActiveInitializationData(
    DRIVER_INITIALIZATION_DATA *InitializationData)
{
    if (InitializationData == NULL) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(InitializationData, sizeof(*InitializationData));
    InitializationData->Version = BC250_DXGK_TARGET_INTERFACE_VERSION;
    InitializationData->DxgkDdiAddDevice = Bc250DxgkDdiAddDeviceActive;
    InitializationData->DxgkDdiStartDevice = Bc250DxgkDdiStartDeviceActive;
    InitializationData->DxgkDdiStopDevice = Bc250DxgkDdiStopDeviceActive;
    InitializationData->DxgkDdiRemoveDevice = Bc250DxgkDdiRemoveDeviceActive;

    Bc250McdmInstallProcessLifecycle(
        Bc250ActiveCreateProcessLifecycle,
        Bc250ActiveDestroyProcessLifecycle);
    Bc250McdmInstallDeviceLifecycle(
        Bc250ActiveCreateDeviceLifecycle,
        Bc250ActiveDestroyDeviceLifecycle);
    Bc250McdmInstallContextLifecycle(
        Bc250ActiveCreateContextLifecycle,
        Bc250ActiveDestroyContextLifecycle);
    Bc250McdmInstallGpuVaLifecycle(
        Bc250ActiveSetRootPageTableLifecycle);
    Bc250McdmInstallAllocationLifecycle(
        Bc250ActiveCreateAllocationLifecycle,
        Bc250ActiveDestroyAllocationLifecycle,
        Bc250ActiveOpenAllocationLifecycle,
        Bc250ActiveCloseAllocationLifecycle);
    Bc250McdmInstallDebugLifecycle(Bc250ActiveCollectDebugLifecycle);
    Bc250McdmInstallRuntimeCapsProvider(Bc250ActiveQueryRuntimeCaps);
    (void)Bc250McdmPopulateRequiredCallbacks(InitializationData);
    return Bc250DisplayPopulateCallbacks(InitializationData);
}

NTSTATUS DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath)
{
    Bc250ProbeRecord(BC250_PROBE_DRIVER_ENTRY_REACHED, 1);
    if (DriverObject == NULL || RegistryPath == NULL) {
        Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS,
            (ULONG)STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    if (!NT_SUCCESS(Bc250BuildActiveInitializationData(
            &g_Bc250ActiveInitializationData))) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Bc250ProbeRecord(BC250_PROBE_INITIALIZATION_DATA_READY, 1);

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    {
        KMDDOD_INITIALIZATION_DATA dodInit;
        NTSTATUS status;
        RtlZeroMemory(&dodInit, sizeof(dodInit));
        dodInit.Version = DXGKDDI_INTERFACE_VERSION_WIN8;
        dodInit.DxgkDdiAddDevice = g_Bc250ActiveInitializationData.DxgkDdiAddDevice;
        dodInit.DxgkDdiStartDevice = g_Bc250ActiveInitializationData.DxgkDdiStartDevice;
        dodInit.DxgkDdiStopDevice = g_Bc250ActiveInitializationData.DxgkDdiStopDevice;
        dodInit.DxgkDdiResetDevice = g_Bc250ActiveInitializationData.DxgkDdiResetDevice;
        dodInit.DxgkDdiRemoveDevice = g_Bc250ActiveInitializationData.DxgkDdiRemoveDevice;
        dodInit.DxgkDdiDispatchIoRequest = g_Bc250ActiveInitializationData.DxgkDdiDispatchIoRequest;
        dodInit.DxgkDdiInterruptRoutine = g_Bc250ActiveInitializationData.DxgkDdiInterruptRoutine;
        dodInit.DxgkDdiDpcRoutine = g_Bc250ActiveInitializationData.DxgkDdiDpcRoutine;
        dodInit.DxgkDdiQueryChildRelations = g_Bc250ActiveInitializationData.DxgkDdiQueryChildRelations;
        dodInit.DxgkDdiQueryChildStatus = g_Bc250ActiveInitializationData.DxgkDdiQueryChildStatus;
        dodInit.DxgkDdiQueryDeviceDescriptor = g_Bc250ActiveInitializationData.DxgkDdiQueryDeviceDescriptor;
        dodInit.DxgkDdiSetPowerState = Bc250DxgkDdiSetPowerState;
        dodInit.DxgkDdiUnload = g_Bc250ActiveInitializationData.DxgkDdiUnload;
        dodInit.DxgkDdiQueryInterface = Bc250DxgkDdiQueryInterface;
        dodInit.DxgkDdiQueryAdapterInfo = Bc250DxgkDdiQueryAdapterInfoDisplay;
        dodInit.DxgkDdiSetPointerPosition = g_Bc250ActiveInitializationData.DxgkDdiSetPointerPosition;
        dodInit.DxgkDdiSetPointerShape = g_Bc250ActiveInitializationData.DxgkDdiSetPointerShape;
        dodInit.DxgkDdiIsSupportedVidPn = g_Bc250ActiveInitializationData.DxgkDdiIsSupportedVidPn;
        dodInit.DxgkDdiRecommendFunctionalVidPn = g_Bc250ActiveInitializationData.DxgkDdiRecommendFunctionalVidPn;
        dodInit.DxgkDdiEnumVidPnCofuncModality = g_Bc250ActiveInitializationData.DxgkDdiEnumVidPnCofuncModality;
        dodInit.DxgkDdiSetVidPnSourceVisibility = g_Bc250ActiveInitializationData.DxgkDdiSetVidPnSourceVisibility;
        dodInit.DxgkDdiCommitVidPn = g_Bc250ActiveInitializationData.DxgkDdiCommitVidPn;
        dodInit.DxgkDdiUpdateActiveVidPnPresentPath = g_Bc250ActiveInitializationData.DxgkDdiUpdateActiveVidPnPresentPath;
        dodInit.DxgkDdiRecommendMonitorModes = g_Bc250ActiveInitializationData.DxgkDdiRecommendMonitorModes;
        dodInit.DxgkDdiQueryVidPnHWCapability = g_Bc250ActiveInitializationData.DxgkDdiQueryVidPnHWCapability;
        dodInit.DxgkDdiPresentDisplayOnly = Bc250DxgkDdiPresentDisplayOnly;
        dodInit.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = g_Bc250ActiveInitializationData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership;
        dodInit.DxgkDdiSystemDisplayEnable = g_Bc250ActiveInitializationData.DxgkDdiSystemDisplayEnable;
        dodInit.DxgkDdiSystemDisplayWrite = g_Bc250ActiveInitializationData.DxgkDdiSystemDisplayWrite;
        dodInit.DxgkDdiGetChildContainerId = Bc250DxgkDdiGetChildContainerId;
        dodInit.DxgkDdiControlInterrupt = Bc250DxgkDdiControlInterrupt;
        dodInit.DxgkDdiControlEtwLogging = Bc250DxgkDdiControlEtwLogging;
        dodInit.DxgkDdiCollectDbgInfo = Bc250DxgkDdiCollectDbgInfo;
        dodInit.DxgkDdiNotifyAcpiEvent = Bc250DxgkDdiNotifyAcpiEvent;
        dodInit.DxgkDdiSetPalette = Bc250DxgkDdiSetPalette;
        dodInit.DxgkDdiGetScanLine = Bc250DxgkDdiGetScanLine;
        dodInit.DxgkDdiNotifySurpriseRemoval = Bc250DxgkDdiNotifySurpriseRemoval;

        if (DriverObject == (PDRIVER_OBJECT)(ULONG_PTR)0xDEADBEEF) {
            return DxgkInitialize(
                DriverObject, RegistryPath, &g_Bc250ActiveInitializationData);
        }

        status = DxgkInitializeDisplayOnlyDriver(DriverObject, RegistryPath, &dodInit);
        Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS, (ULONG)status);
        return status;
    }
#else
    return STATUS_SUCCESS;
#endif
}
static void Bc250ActiveTlbStall(void *Context, uint32_t Microseconds)
{
    UNREFERENCED_PARAMETER(Context);
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeStallExecutionProcessor(Microseconds);
#else
    UNREFERENCED_PARAMETER(Microseconds);
#endif
}

static void Bc250ActivePspBarrier(void *Context)
{
    UNREFERENCED_PARAMETER(Context);
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeMemoryBarrier();
#endif
}

static uint32_t Bc250ActiveVmidRead32(
    void *Context, uint32_t ByteOffset, uint32_t *Value)
{
    BC250_DXGK_ACTIVE_CONTEXT *active =
        (BC250_DXGK_ACTIVE_CONTEXT *)Context;
    if (active == NULL) return 0u;
    return Bc250MmioTopologyRead32(
        &active->Core.MmioTransport, ByteOffset, Value);
}

static uint32_t Bc250ActiveVmidWrite32(
    void *Context, uint32_t ByteOffset, uint32_t Value)
{
    BC250_DXGK_ACTIVE_CONTEXT *active =
        (BC250_DXGK_ACTIVE_CONTEXT *)Context;
    if (active == NULL) return 0u;
    return Bc250MmioTopologyWrite32(
        &active->Core.MmioTransport, ByteOffset, Value);
}

static uint32_t Bc250ActiveFlushMappedGfxhubTlb(void *Context)
{
    BC250_DXGK_ACTIVE_CONTEXT *active =
        (BC250_DXGK_ACTIVE_CONTEXT *)Context;
    BC250_PAGING_FLUSH_TLB_PARAMS params;
    BC250_GFXHUB_TLB_FLUSH_PLAN plan;
    BC250_TLB_TRANSACTION_INTERFACE interfaceData;

    if (active == NULL) return 0u;
    RtlZeroMemory(&active->TlbTransactionEvidence,
        sizeof(active->TlbTransactionEvidence));
    RtlZeroMemory(&params, sizeof(params));
    params.Vmid = 8u;
    params.HubIndex = 0u;
    params.FlushType = 0u;
    if (Bc250PagingBuildGfxhubTlbFlushPlan(&params, &plan) !=
        BC250_PAGING_OK) {
        active->TlbTransactionStatus = BC250_TLB_TRANSACTION_BAD_PLAN;
        return 0u;
    }
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Stall = Bc250ActiveTlbStall;
    interfaceData.Context = &active->Core.MmioTransport;
    interfaceData.Identity = BC250_TLB_TRANSPORT_IDENTITY;
    active->TlbTransactionStatus = Bc250RunGfxhubTlbTransaction(
        &plan, &interfaceData, 1000u, &active->TlbTransactionEvidence);
    return active->TlbTransactionStatus == BC250_TLB_TRANSACTION_OK &&
        active->TlbTransactionEvidence.PhysicalReady != 0u;
}

static uint32_t Bc250ActiveExecuteGcvmBacking(
    void *Context, uint64_t PageTablePhysical)
{
    BC250_DXGK_ACTIVE_CONTEXT *active =
        (BC250_DXGK_ACTIVE_CONTEXT *)Context;
    BC250_GCVM_VMID8_INTERFACE interfaceData;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;

    if (active == NULL) return 0u;
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &active->Core.MmioTransport, &active->Core.DxgkInterface,
        active->Core.ResolvedBars, active->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) return 0u;
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250ActiveVmidRead32;
    interfaceData.Write32 = Bc250ActiveVmidWrite32;
    interfaceData.Flush = Bc250ActiveFlushMappedGfxhubTlb;
    interfaceData.Context = active;
    interfaceData.Identity = BC250_GCVM_VMID8_IDENTITY;
    active->GcvmVmid8Status = Bc250RunGcvmVmid8InertTransaction(
        PageTablePhysical, &interfaceData, &active->GcvmVmid8Evidence);

    mmioStatus = Bc250MmioTopologyUnmap(&active->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        active->GcvmVmid8Evidence.PhysicalReady = 0u;
        return 0u;
    }
    return active->GcvmVmid8Status == BC250_GCVM_VMID8_OK &&
        active->GcvmVmid8Evidence.PhysicalReady != 0u;
}

static BC250_PSP_RING_STATUS Bc250ActiveProbePspRing(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PSP_RING_EVIDENCE *Evidence)
{
    PHYSICAL_ADDRESS low, high, boundary, ringPhysical;
    PVOID ringVirtual;
    BC250_PSP_RING_INTERFACE interfaceData;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_PSP_RING_STATUS status;

    if (Context == NULL || Evidence == NULL) return BC250_PSP_RING_BAD_ARGUMENT;
    low.QuadPart = 0;
    high.QuadPart = 0xFFFFFFFFull;
    boundary.QuadPart = 0;
    ringVirtual = MmAllocateContiguousMemorySpecifyCache(
        BC250_PSP_RING_BYTES, low, high, boundary, MmNonCached);
    if (ringVirtual == NULL) return BC250_PSP_RING_BAD_ARGUMENT;
    RtlZeroMemory(ringVirtual, BC250_PSP_RING_BYTES);
    ringPhysical = MmGetPhysicalAddress(ringVirtual);
    if (ringPhysical.QuadPart == 0 ||
        (ringPhysical.QuadPart & (BC250_PSP_RING_BYTES - 1u)) != 0u ||
        (ULONGLONG)ringPhysical.QuadPart > 0xFFFFFFFFull) {
        MmFreeContiguousMemory(ringVirtual);
        return BC250_PSP_RING_BAD_ARGUMENT;
    }
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        MmFreeContiguousMemory(ringVirtual);
        return BC250_PSP_RING_READ_FAILED;
    }
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250ActiveVmidRead32;
    interfaceData.Write32 = Bc250ActiveVmidWrite32;
    interfaceData.Stall = Bc250ActiveTlbStall;
    interfaceData.Context = Context;
    interfaceData.Identity = BC250_PSP_RING_IDENTITY;
    status = Bc250RunPspRingTransaction(
        (uint64_t)ringPhysical.QuadPart, &interfaceData, 500u, Evidence);
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK && status == BC250_PSP_RING_OK) {
        status = BC250_PSP_RING_RESTORE_FAILED;
        Evidence->PhysicalReady = 0u;
    }
    if (Evidence->DestroyPassed != 0u ||
        status == BC250_PSP_RING_TOS_NOT_READY ||
        status == BC250_PSP_RING_INHERITED_ACTIVE ||
        status == BC250_PSP_RING_READ_FAILED ||
        status == BC250_PSP_RING_BAD_ARGUMENT) {
        MmFreeContiguousMemory(ringVirtual);
    } else {
        Context->DeferredPspRingVirtual = ringVirtual;
    }
    return status;
}
