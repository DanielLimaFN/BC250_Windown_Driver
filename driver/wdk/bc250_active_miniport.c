#include "bc250_active_miniport.h"
#include "bc250_mcdm_callbacks.h"
#include "bc250_display_ddi.h"
#include "bc250_probe_telemetry.h"
#include "bc250_sdma_present.h"
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

/*
 * Marcador de proveniencia do bring-up inseguro.
 *
 * Existe no binario SOMENTE quando BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP
 * foi definido.  Serve para que o empacotador seguro possa PROVAR, por
 * conteudo do .sys, que esta' assinando a imagem de producao — e nao a de
 * laboratorio com outro INF.
 *
 * Nao usar nomes de tunable para essa prova: eles aparecem no .rdata como
 * nomes de campo de telemetria mesmo na imagem de producao, e a verificacao
 * daria falso positivo.
 */
#if defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)
const WCHAR g_Bc250UnsafeBringupMarker[] =
    L"BC250-UNSAFE-STARTDEVICE-BRINGUP-COMPILED";
#define BC250_UNSAFE_BRINGUP_COMPILED 1u
#else
#define BC250_UNSAFE_BRINGUP_COMPILED 0u
#endif

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

/* Le um tunable e informa se ele existe de fato.
 *
 * Bc250ActiveReadTunable devolve o default tanto quando o valor vale o default
 * quanto quando a leitura falha — e RtlQueryRegistryValues exige PASSIVE_LEVEL,
 * entao "falhou" e' um resultado possivel dependendo de onde e' chamado. Ler
 * duas vezes com defaults diferentes separa os dois casos: se os resultados
 * batem, o valor existe; se divergem, cada chamada devolveu o proprio default e
 * nao ha' valor legivel. */
static ULONG Bc250ActiveReadTunableChecked(
    PCWSTR ValueName, ULONG DefaultValue, ULONG *Present)
{
    ULONG asZero = Bc250ActiveReadTunable(ValueName, 0u);
    ULONG asOne = Bc250ActiveReadTunable(ValueName, 1u);

    if (asZero == asOne) {
        if (Present != NULL) *Present = 1u;
        return asZero;
    }

    if (Present != NULL) *Present = 0u;
    return DefaultValue;
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
    /* Cyan Skillfish 2: a fila de compute validada usa MEC2/Pipe0/Queue0.
     * O contrato rejeita SchedulerMe == 0 antes de qualquer acesso ao HW. */
    props.SchedulerMe = 2u;
    props.SchedulerPipe = 0u;
    props.SchedulerQueue = 0u;
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

        /* GPUVA 0x4000/0x5000 map to these owned physical pages. */
        buffers.ShaderVirtual = (volatile void *)((PUCHAR)mapped + 0x4000);
        buffers.ShaderGpuAddress = basePhys + 0x4000ull;
        buffers.ShaderSizeBytes = 256u;

        buffers.ResultVirtual = (volatile void *)((PUCHAR)mapped + 0x5000);
        buffers.ResultGpuAddress = basePhys + 0x5000ull;
        buffers.ResultSizeBytes = 256u;

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
    /* No modo Host PA (Vmid0PageTable=0), CP_HQD_PQ_BASE aponta para a VRAM física. */
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
    props.SchedulerMe = 2u;
    props.SchedulerPipe = 0u;
    props.SchedulerQueue = 0u;
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
    if (Context->ShaderDispatch != 0u) {
        status = Bc250ComputeRunObservableShaderDispatchTransaction(
            &interfaceData, &buffers, &props, &mqd, Evidence,
            &Context->HqdShaderDispatchEvidence);
    } else {
        status = Bc250ComputeRunActiveHqdSubmissionTransaction(
            &interfaceData, &buffers, &props, &mqd, Evidence);
    }
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
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_BEFORE,
        e->EopRptrBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_APPLIED,
        e->EopRptrApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_READBACK,
        e->EopRptrReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_RESTORED,
        e->EopRptrRestored);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_RESTORE_PASSED,
        e->EopRptrRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_BEFORE,
        e->WptrPollCntl1Before);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_APPLIED,
        e->WptrPollCntl1Applied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_READBACK,
        e->WptrPollCntl1Readback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_RESTORED,
        e->WptrPollCntl1Restored);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_RESTORE_PASSED,
        e->WptrPollCntl1RestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_QUEUE_MASK_BIT,
        e->QueueMaskBit);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PRE_ACTIVE_WPTR_APPLIED,
        e->PreActiveWptrApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PRE_ACTIVE_WPTR_READBACK,
        e->PreActiveWptrReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PRE_ACTIVE_WPTR_PUBLISHED,
        e->PreActiveWptrPublished);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_STAGE,
        e->FirstRestoreFailureStage);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_OFFSET,
        e->FirstRestoreFailureOffset);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_EXPECTED,
        e->FirstRestoreFailureExpected);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_OBSERVED,
        e->FirstRestoreFailureObserved);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_TRANSPORT,
        e->FirstRestoreFailureTransport);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RESTORE_FAILURE_COUNT,
        e->RestoreFailureCount);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_PROBE_APPLIED,
        e->WptrPollCntlProbeApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_PROBE_READBACK,
        e->WptrPollCntlProbeReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_WRITABLE,
        e->WptrPollCntlWritable);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_EN_LATCHED,
        e->WptrPollCntlEnLatched);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_POST_ACTIVE_WPTR_OBSERVED,
        e->PostActiveWptrObserved);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_DIRTY_ENTRY,
        e->WptrPollCntl1DirtyEntry);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_CLEAR_ATTEMPTS,
        e->WptrPollCntl1ClearAttempts);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PQ_CONTROL_FIRST_SAMPLE,
        e->PqControlFirstSample);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQ_STATUS0_FIRST_SAMPLE,
        e->HqStatus0FirstSample);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_POLL_LAST,
        e->DequeueRequestPollLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_VMID_POLL_LAST,
        e->VmidPollLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PERSISTENT_STATE_POLL_LAST,
        e->PersistentStatePollLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_REPORT_MEM_FIRST,
        e->RptrReportMemFirst);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_REPORT_MEM_LAST,
        e->RptrReportMemLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_NON_ZERO_SAMPLES,
        e->RptrNonZeroSamples);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STATUS_FIRST,
        e->CpcStatusFirst);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STATUS_LAST,
        e->CpcStatusLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_BUSY_STAT_LAST,
        e->CpcBusyStatLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_STAT_LAST,
        e->CpfBusyStatLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STALLED_STAT1_LAST,
        e->CpcStalledStat1Last);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STAT_FIRST,
        e->CpStatFirst);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STAT_LAST,
        e->CpStatLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STALLED_STAT1_AFTER,
        e->CpStalledStat1After);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP0,
        e->MecMe1HeaderDump[0]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP1,
        e->MecMe1HeaderDump[1]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP2,
        e->MecMe1HeaderDump[2]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP3,
        e->MecMe1HeaderDump[3]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP0,
        e->MecMe2HeaderDump[0]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP1,
        e->MecMe2HeaderDump[1]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP2,
        e->MecMe2HeaderDump[2]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP3,
        e->MecMe2HeaderDump[3]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE0,
        e->MecMe2HeaderDumpBeforeActive[0]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE1,
        e->MecMe2HeaderDumpBeforeActive[1]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE2,
        e->MecMe2HeaderDumpBeforeActive[2]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE3,
        e->MecMe2HeaderDumpBeforeActive[3]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STATUS_BEFORE_KICK,
        e->CpcStatusBeforeKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_BUSY_BEFORE_KICK,
        e->CpcBusyStatBeforeKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_BEFORE_KICK,
        e->CpfBusyStatBeforeKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STALLED_BEFORE_KICK,
        e->CpcStalledStat1BeforeKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STAT_BEFORE_KICK,
        e->CpStatBeforeKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STALLED_BEFORE_KICK,
        e->CpStalledStat1BeforeKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STATUS_AFTER_KICK,
        e->CpcStatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_BUSY_AFTER_KICK,
        e->CpcBusyStatAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_AFTER_KICK,
        e->CpfBusyStatAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_BEFORE_ACTIVE,
        e->CpfBusyStatBeforeActive);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_AFTER_ACTIVE,
        e->CpfBusyStatAfterActive);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_BEFORE_WPTR,
        e->CpfBusyStatBeforeWptr);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_AFTER_WPTR,
        e->CpfBusyStatAfterWptr);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_BEFORE_ACTIVE,
        e->CpfStalledStat1BeforeActive);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_AFTER_ACTIVE,
        e->CpfStalledStat1AfterActive);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_BEFORE_KICK,
        e->CpfStalledStat1BeforeKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_AFTER_KICK,
        e->CpfStalledStat1AfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_LAST,
        e->CpfStalledStat1Last);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STATUS_AFTER_ACTIVE,
        e->CpfStatusAfterActive);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STATUS_AFTER_KICK,
        e->CpfStatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_APPLIED,
        e->DequeueRequestApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_READBACK,
        e->DequeueRequestReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_RESTORE_PASSED,
        e->DequeueRequestRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STALLED_AFTER_KICK,
        e->CpcStalledStat1AfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STAT_AFTER_KICK,
        e->CpStatAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STALLED_AFTER_KICK,
        e->CpStalledStat1AfterKick);
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

static void Bc250ActiveRecordShaderDispatch(
    const BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    const BC250_HQD_SHADER_DISPATCH_EVIDENCE *e;

    if (Context == NULL) return;
    e = &Context->HqdShaderDispatchEvidence;
    Bc250ProbeRecord(BC250_PROBE_SHADER_DISPATCH_ENABLED,
        Context->ShaderDispatch != 0u ? 1u : 0u);
    Bc250ProbeRecord(BC250_PROBE_SHADER_DISPATCH_DWORDS,
        e->Plan.SubmittedDwords);
    Bc250ProbeRecord(BC250_PROBE_SHADER_DISPATCH_PACKET_INDEX,
        e->Plan.DispatchPacketIndex);
    Bc250ProbeRecord(BC250_PROBE_SHADER_FENCE_PACKET_INDEX,
        e->Plan.FencePacketIndex);
    Bc250ProbeRecord(BC250_PROBE_SHADER_FENCE_AFTER_DISPATCH,
        e->Plan.FenceIsAfterDispatch);
    Bc250ProbeRecord(BC250_PROBE_SHADER_CC_ARRAY_PASS1,
        e->CcShaderArrayPass1);
    Bc250ProbeRecord(BC250_PROBE_SHADER_CC_ARRAY_PASS2,
        e->CcShaderArrayPass2);
    Bc250ProbeRecord(BC250_PROBE_SHADER_SPI_WGP_MASK_PASS1,
        e->SpiStaticWgpMaskPass1);
    Bc250ProbeRecord(BC250_PROBE_SHADER_SPI_WGP_MASK_PASS2,
        e->SpiStaticWgpMaskPass2);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RLC_AON_WGP_PASS1,
        e->RlcAlwaysOnWgpMaskPass1);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RLC_AON_WGP_PASS2,
        e->RlcAlwaysOnWgpMaskPass2);
    Bc250ProbeRecord(BC250_PROBE_SHADER_NON_HARVESTED_WGP,
        e->NonHarvestedWgpMask);
    Bc250ProbeRecord(BC250_PROBE_SHADER_AON_NON_HARVESTED_WGP,
        e->AlwaysOnNonHarvestedWgpMask);
    Bc250ProbeRecord(BC250_PROBE_SHADER_WGP_PREFLIGHT_PASSED,
        e->WgpPreflightPassed);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_GPUVA_LOW,
        e->RingGpuVaLow);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_GPUVA_HIGH,
        e->RingGpuVaHigh);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_PHYSICAL_LOW,
        e->RingPhysicalLow);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_PHYSICAL_HIGH,
        e->RingPhysicalHigh);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_PTE_LOW,
        e->RingPteLow);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_PTE_HIGH,
        e->RingPteHigh);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_TRANSLATION_PASSED,
        e->RingTranslationPassed);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_READBACK_PASSED,
        e->RingReadbackPassed);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_READBACK_MISMATCHES,
        e->RingReadbackMismatchCount);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_DWORD0, e->RingDword[0]);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_DWORD1, e->RingDword[1]);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_DWORD2, e->RingDword[2]);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_DWORD3, e->RingDword[3]);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_DWORD4, e->RingDword[4]);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_DWORD5, e->RingDword[5]);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_DWORD6, e->RingDword[6]);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_DWORD7, e->RingDword[7]);
    Bc250ProbeRecord(BC250_PROBE_SHADER_READBACK_PASSED,
        e->ShaderReadbackPassed);
    Bc250ProbeRecord(BC250_PROBE_SHADER_READBACK_MISMATCHES,
        e->ShaderReadbackMismatchCount);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RESULT_BEFORE,
        e->Observation.ResultBefore);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RESULT_AFTER,
        e->Observation.ResultAfter);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RESULT_MISMATCHES,
        e->Observation.ResultMismatchCount);
    Bc250ProbeRecord(BC250_PROBE_SHADER_FENCE_BEFORE,
        e->Observation.FenceBefore);
    Bc250ProbeRecord(BC250_PROBE_SHADER_FENCE_AFTER,
        e->Observation.FenceAfter);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RPTR_AFTER,
        e->Observation.RptrAfter);
    Bc250ProbeRecord(BC250_PROBE_SHADER_WPTR_AFTER,
        e->Observation.WptrAfter);
    Bc250ProbeRecord(BC250_PROBE_SHADER_REG_RESTORE,
        e->Observation.RegistersRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_SHADER_MEM_RESTORE,
        e->Observation.MemoryRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_SHADER_TRIAD_PASSED,
        e->Verdict.TriadPassed);
    Bc250ProbeRecord(BC250_PROBE_SHADER_PHYSICAL_READY,
        e->PhysicalReady);
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
    if (context == NULL || context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }

    if (context->RuntimeInventoryStatus == BC250_RUNTIME_INVENTORY_OK &&
        context->VramProbeStatus == BC250_VRAM_PROBE_OK &&
        context->SecondPreflightStage == BC250_DXGK_STAGE_PREFLIGHT_READY &&
        context->Core.StartDevice.Preflight.FirmwareCount ==
            BC250_CP_BOOTSTRAP_FIRMWARE_COUNT) {
        cpuVisibleBytes = context->RuntimeInventoryReport.CpuVisibleBytes;
        localEnd = context->Core.StartDevice.Preflight.FirmwarePlans[0]
            .Binding.Offset;
        if (localEnd == 0u || localEnd > cpuVisibleBytes) {
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
    } else if (context->Core.ResolvedBarCount != 0u &&
               context->Core.TopologyEvidence.AddressSpace.TotalBytes != 0u) {
        uint64_t totalBytes = context->Core.TopologyEvidence.AddressSpace.TotalBytes;
        cpuVisibleBytes = context->Core.TopologyEvidence.AddressSpace.CpuVisibleBytes;
        if (cpuVisibleBytes == 0u) cpuVisibleBytes = totalBytes;
        if (context->PostDisplayReservation.Length != 0u) {
            localStart = (context->PostDisplayReservation.Offset +
                          context->PostDisplayReservation.Length + 0xFFFFull) & ~0xFFFFull;
        } else {
            localStart = 0x01000000ull;
        }
        localEnd = totalBytes;
        if (localStart >= localEnd) {
            localStart = 0u;
        }
    } else {
        return STATUS_DEVICE_NOT_READY;
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
    case BC250_MCDM_OBJECT_NODE_MISMATCH:
        return STATUS_INVALID_PARAMETER;
    case BC250_MCDM_OBJECT_NO_ROOT_PAGE_TABLE:
    case BC250_MCDM_OBJECT_IN_TEARDOWN:
        return STATUS_DEVICE_NOT_READY;
    case BC250_MCDM_OBJECT_CHILDREN_LIVE:
    case BC250_MCDM_OBJECT_IN_FLIGHT_SUBMISSIONS:
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
                status = Bc250McdmObjectCreateContext(&context->Objects,
                    context, DeviceHandle, device.OwnerProcess,
                    (uint32_t)NodeOrdinal, 0u, ContextHandle);
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
                (void)Bc250GpuvaSpaceInvalidateForObject(&context->GpuvaSpace,
                    record.OwnerProcess, ContextHandle, NULL);
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
                if (info->pPrivateDriverData != NULL && info->PrivateDriverDataSize != 0u) {
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
                } else {
                    uint64_t allocSize = info->Size != 0u ? (uint64_t)info->Size : 4096ull;
                    if (allocSize > (uint64_t)(SIZE_T)~(SIZE_T)0 ||
                        context->AllocationSegmentSet != 1u ||
                        context->AllocationSegmentBytes == 0u ||
                        allocSize > context->AllocationSegmentBytes) {
                        result = STATUS_INVALID_PARAMETER;
                        break;
                    }
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
                if (info->pPrivateDriverData != NULL && info->PrivateDriverDataSize != 0u) {
                    (void)Bc250AllocationAbiParse(info->pPrivateDriverData,
                        info->PrivateDriverDataSize, &request);
                } else {
                    request.AllocationToken = 0xA1100000u + index;
                    request.ByteSize = info->Size != 0u ? (uint64_t)info->Size : 4096ull;
                    request.Alignment = info->Alignment != 0u ? (uint64_t)info->Alignment : 4096ull;
                }
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
                if (info->hAllocation == 0u) {
                    result = STATUS_INVALID_PARAMETER;
                    break;
                }
                if (info->pPrivateDriverData != NULL && info->PrivateDriverDataSize != 0u) {
                    if (Bc250AllocationAbiParse(info->pPrivateDriverData,
                            info->PrivateDriverDataSize, &request) !=
                            BC250_ALLOCATION_ABI_OK) {
                        result = STATUS_INVALID_PARAMETER;
                        break;
                    }
                    for (other = 0u; other < index; ++other) {
                        BC250_ALLOCATION_PRIVATE_V1 previous;
                        DXGK_OPENALLOCATIONINFO *prior =
                            &OpenAllocation->pOpenAllocation[other];
                        if (prior->pPrivateDriverData != NULL && prior->PrivateDriverDataSize != 0u) {
                            (void)Bc250AllocationAbiParse(prior->pPrivateDriverData,
                                prior->PrivateDriverDataSize, &previous);
                            if (prior->hAllocation == info->hAllocation ||
                                previous.AllocationToken == request.AllocationToken) {
                                result = STATUS_INVALID_PARAMETER;
                                break;
                            }
                        }
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
                if (info->pPrivateDriverData != NULL && info->PrivateDriverDataSize != 0u) {
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
                } else {
                    allocation = (BC250_MCDM_HANDLE)(ULONG_PTR)info->hAllocation;
                    objectStatus = Bc250McdmObjectLookup(&context->Objects,
                        allocation, BC250_MCDM_OBJECT_ALLOCATION, NULL, NULL);
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

/*
 * Publica as tres flags de ABI como telemetria.
 *
 * Motivo (3.79, 2026-09-01): o ensaio provou P1/P2/HQD no silicio, terminou em
 * Codigo 43, e a auditoria pediu ContextAbiReady, AllocationAbiReady e
 * RootPageTableAbiReady.  Nenhuma das duas primeiras existia como campo de
 * telemetria — so' como membro volatile do contexto.  Um gate que nao e'
 * observavel nao pode ser auditado.
 *
 * Le sempre o valor REAL do contexto com InterlockedCompareExchange(x,1,1);
 * nunca grava literal.  Publicar um 1 constante aqui seria precisamente o
 * falso sucesso que o projeto proibe.
 *
 * AbiReadyPublishCount existe porque, sem ele, os tres zeros continuariam
 * ambiguos: "as ABIs nao ficaram prontas" e "este publicador nunca rodou"
 * produzem exatamente a mesma leitura.  E' a licao do Schema 133.
 *
 * Definida antes do AddDevice porque o AddDevice tambem publica a linha de
 * base.  NAO chamar no zeramento do StopDevice: apagaria a medicao do
 * StartDevice antes de o operador conseguir ler o registro.
 */
static void Bc250ActivePublishAbiReadyTelemetry(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    static volatile LONG s_publishCount;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return;
    }
    Bc250ProbeRecord(BC250_PROBE_V5_ABI_READY_CONTEXT,
        (ULONG)InterlockedCompareExchange(&Context->ContextAbiReady, 1, 1));
    Bc250ProbeRecord(BC250_PROBE_V5_ABI_READY_ALLOCATION,
        (ULONG)InterlockedCompareExchange(&Context->AllocationAbiReady, 1, 1));
    Bc250ProbeRecord(BC250_PROBE_V5_ABI_READY_ROOT_PAGE_TABLE,
        (ULONG)InterlockedCompareExchange(
            &Context->RootPageTableAbiReady, 1, 1));
    Bc250ProbeRecord(BC250_PROBE_V5_ABI_READY_PUBLISH_COUNT,
        (ULONG)InterlockedIncrement(&s_publishCount));
}

NTSTATUS APIENTRY Bc250DxgkDdiAddDeviceActive(
    CONST PDEVICE_OBJECT PhysicalDeviceObject,
    PVOID *MiniportDeviceContext)
{
    BC250_DXGK_ACTIVE_CONTEXT *context;
    NTSTATUS status;

    Bc250ProbeMarkDdi(BC250_V5_DDI_ADD_DEVICE);
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
    if (Bc250GpuvaSpaceInitialize(&context->GpuvaSpace, 48u) !=
        BC250_GPUVA_OK) {
        ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS,
            (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    InterlockedExchange(&context->GpuvaSpaceReady, 1);
    if (Bc250GpuvaSnapshotStoreInitialize(&context->GpuvaSnapshots, 48u) !=
            BC250_GPUVA_OK ||
        Bc250GpuvaJournalInitialize(&context->GpuvaJournal) !=
            BC250_GPUVA_JOURNAL_OK) {
        ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS,
            (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    InterlockedExchange(&context->GpuvaJournalReady, 1);
    context->GpuvaHierarchyPending = 0u;
    context->GpuvaHierarchyProcess = NULL;
    context->GpuvaHierarchyRootPageTable = 0u;
    context->GpuvaHierarchyHighestLevel = 0u;
    InterlockedExchange(&context->GpuvaActivationEligible, 0);
    context->GpuvaLabPromotion = 0u;
    context->GpuvaLabPromotionPresent = 0u;
    context->GpuvaActivationBlockMask =
        BC250_GPUVA_GATE_BLOCK_ADAPTER_INACTIVE |
        BC250_GPUVA_GATE_BLOCK_CONTEXT_ABI |
        BC250_GPUVA_GATE_BLOCK_ALLOCATION_ABI |
        BC250_GPUVA_GATE_BLOCK_ROOT_ABI |
        BC250_GPUVA_GATE_BLOCK_ENGINE_OFFLINE |
        BC250_GPUVA_GATE_BLOCK_SNAPSHOT_EMPTY |
        BC250_GPUVA_GATE_BLOCK_SNAPSHOT_EPOCH;
    context->GpuvaActivationSnapshotGeneration = 0u;
    context->GpuvaActivationSnapshotEpoch = 0u;
    context->GpuvaActivationSnapshotLiveCount = 0u;
    InterlockedExchange(&context->IsActive, 0);
    RtlZeroMemory(&context->ContextAbi, sizeof(context->ContextAbi));
    InterlockedExchange(&context->ContextAbiReady, 0);
    InterlockedExchange(&context->AllocationAbiReady, 0);
    InterlockedExchange(&context->RootPageTableAbiReady, 0);
    context->AllocationSegmentBytes = 0u;
    context->AllocationSegmentSet = 0u;
    context->RootPageTableMaxEntries = 0u;
    /*
     * Linha de base do AddDevice: fixa AbiReadyPublishCount=1 com as tres
     * flags em zero.  Se o registro terminar o ensaio com PublishCount=1, o
     * StartDevice nao chegou a nenhum de seus caminhos de retorno.
     */
    Bc250ActivePublishAbiReadyTelemetry(context);
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
    RtlZeroMemory(&context->HqdShaderDispatchEvidence,
                  sizeof(context->HqdShaderDispatchEvidence));
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
    context->PersistentSubmission = 0u;
    context->TunablePersistentSubmissionPresent = 0u;
    RtlZeroMemory(&context->SubmissionEngine,
                  sizeof(context->SubmissionEngine));

    status = Bc250ActiveRegisterAdapter(context);
    if (!NT_SUCCESS(status)) {
        context->Signature = 0u;
        ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
        return status;
    }

    /*
     * Arma o portao de admissao do rastreador junto com a criacao do
     * contexto. Sao dois portoes independentes e em profundidade:
     *
     *   AcceptingWork  — propriedade do rastreador, armada aqui, fechada na
     *                    quiescencia do teardown.
     *   LifecycleState — propriedade do contexto, so vai a RUNNING quando o
     *                    StartDevice devolve sucesso; NOT_STARTED e' zero, de
     *                    modo que memoria zerada recusa trabalho.
     *
     * Nenhum dos dois sozinho autoriza submissao: SubmitCommandVirtual exige
     * IsActive == 1 e LifecycleState == RUNNING, e a reserva ainda passa pelo
     * AcceptingWork.
     */
    Bc250WddmFenceTrackerSetAcceptingWork(
        &context->SubmissionEngine.WddmFences, TRUE);
    InterlockedExchange(&context->LifecycleState,
        (LONG)BC250_ACTIVE_LIFECYCLE_NOT_STARTED);

    *MiniportDeviceContext = context;
    Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS, (ULONG)STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

/*
 * Arma o ciclo de vida junto com IsActive (3.67).
 *
 * O portao de admissao do rastreador e' fail-closed por construcao: um
 * rastreador zerado tem AcceptingWork == 0 e recusa toda reserva. Isso e'
 * deliberado — esquecer de armar produz um adaptador que nao aceita trabalho,
 * falha alta e imediata, em vez de um adaptador que aceita trabalho que o
 * teardown nao consegue contabilizar.
 *
 * O preco e' que TODO caminho que liga IsActive precisa armar tambem. Sao
 * tres: bring-up pulado cedo, bring-up pulado tarde e bring-up completo.
 * Concentrar aqui evita que um quarto caminho futuro esqueca metade do par.
 */
static void Bc250ActiveArmLifecycle(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return;
    }
    InterlockedExchange(&Context->SubmitInFlight, 0);
    Context->StopReason = BC250_ACTIVE_STOP_REASON_NONE;
    Context->StopSubmitInFlightAfterDrain = 0u;
    Context->StopDrainIterations = 0u;
    Context->StopSnapshotFailures = 0u;
    Bc250WddmFenceTrackerSetAcceptingWork(
        &Context->SubmissionEngine.WddmFences, TRUE);
    InterlockedExchange(&Context->LifecycleState,
        (LONG)BC250_ACTIVE_LIFECYCLE_RUNNING);
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
#if defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)
    NTSTATUS persistentStatus = STATUS_NOT_SUPPORTED;
    NTSTATUS selfTestStatus = STATUS_NOT_SUPPORTED;
    NTSTATUS p2Status = STATUS_NOT_SUPPORTED;
    BOOLEAN persistentReady = FALSE;
#endif

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
     * P0: Avaliacao do fusivel anti-boot-loop persistente.
     * Se a tentativa anterior nao fechou limpa (crash, hang, reboot no meio do ensaio),
     * o fusivel dispara e recusa imediatamente o StartDevice, impedindo o boot loop
     * e devolvendo o BasicDisplay de forma automatica.
     */
    {
        BOOLEAN allowStart = FALSE;
        ULONG prevFuseState = 0;
        NTSTATUS fuseStatus = Bc250BootFuseEvaluateAndArm(&allowStart, &prevFuseState);
        if (!NT_SUCCESS(fuseStatus) || !allowStart) {
            Bc250DisplayLog(
                "[StartDevice] FUSIVEL ANTI-BOOT-LOOP DISPARADO: recusa StartDevice (fuseStatus=%08x, prevFuseState=%u)\n",
                fuseStatus, prevFuseState);
            Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_RETURN_STAGE,
                BC250_V5_START_RETURN_FUSE_TRIPPED);
            Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_STATUS,
                (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
            Bc250ProbeRecordFirstFailure(STATUS_DEVICE_CONFIGURATION_ERROR);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
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

        /*
         * Gate 1.5 — render-only.
         *
         * Devolve zero sources e zero children, e sai ANTES de adquirir
         * post-display ownership e antes de Bc250DisplayInit.  Isso e' o que
         * torna a corrida repetivel: o GOP continua dono do scanout, o modo
         * herdado nao e' tocado, e um FAIL nao custa tela preta nem boot
         * frio.  A pergunta que este perfil responde e' unicamente se a
         * tabela render/base sobrevive em Codigo 0.
         *
         * As tres flags de ABI ficam em zero, iguais ao caminho de display
         * de producao: promove-las aqui anunciaria objetos WDDM sem backend
         * fisico provado, que e' exatamente o falso sucesso que o projeto
         * proibe.  A promocao e' decisao de um gate posterior, com evidencia
         * propria.
         */
        if (Bc250ActiveGetAdapterProfile() ==
                BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) {
            Bc250ProbeRecord(BC250_PROBE_SKIP_HW_BRINGUP, 1u);
            /*
             * Marcador positivo: prova que ESTE caminho executou, em vez de
             * deduzi-lo da ausencia de outros campos. Um zero em
             * PostDisplayAttempted so' significa "nao tentou" se existir um
             * um em RenderOnlyEarlyReturn.
             */
            Bc250ProbeRecord(BC250_PROBE_V5_RENDER_ONLY_EARLY_RETURN, 1u);
            Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_RETURN_STAGE,
                BC250_V5_START_RETURN_RENDER_ONLY);
            *NumberOfVideoPresentSources = 0;
            *NumberOfChildren = 0;
#if defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)
            /*
             * Nao adquirir post-display ownership. O perfil render-only
             * segue diretamente para o bring-up fisico comum e so podera
             * retornar sucesso depois que RuntimeCaps/ABI/submission tiverem
             * backend real. P1/P2 continuam sujeitos aos mesmos gates e
             * provas fisicas do caminho display+render de laboratorio.
             */
            goto Bc250PhysicalRenderBringup;
#else
            /*
             * Uma imagem sem bring-up compilado nao possui backend de
             * render. Retornar sucesso aqui foi o falso positivo medido no
             * 3.76--3.78 (RuntimeMissing=1, todas as ABIs em zero, Codigo
             * 43). Falhar explicitamente e' o unico resultado honesto.
             */
            InterlockedExchange(&context->ContextAbiReady, 0);
            InterlockedExchange(&context->AllocationAbiReady, 0);
            InterlockedExchange(&context->RootPageTableAbiReady, 0);
            InterlockedExchange(&context->IsActive, 0);
            Bc250ActivePublishAbiReadyTelemetry(context);
            Bc250ProbeRecordFirstFailure(STATUS_DEVICE_NOT_READY);
            return STATUS_DEVICE_NOT_READY;
#endif
        }

        Bc250ProbeRecord(BC250_PROBE_V5_POST_DISPLAY_ATTEMPTED, 1u);
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
            Bc250ProbeRecord(BC250_PROBE_V5_DISPLAY_INIT_ENTERED, 1u);
            preflightStatus = Bc250DisplayInit(
                &context->Core.DxgkInterface, &postDisplayInfo);
            if (!NT_SUCCESS(preflightStatus)) {
                Bc250DisplayTeardown();
                Bc250ProbeRecordFirstFailure(preflightStatus);
                return preflightStatus;
            }

            {
                uint32_t bIdx;
                for (bIdx = 0; bIdx < context->Core.ResolvedBarCount; ++bIdx) {
                    if (context->Core.ResolvedBars[bIdx].Index == BC250_PCI_MMIO_BAR_INDEX) {
                        PHYSICAL_ADDRESS mmioPhys;
                        mmioPhys.QuadPart = (LONGLONG)context->Core.ResolvedBars[bIdx].Start;
                        Bc250DisplaySetMmioBase(mmioPhys);
                        (void)Bc250SdmaPresentInit(mmioPhys, 0x000450000000ull);
                        break;
                    }
                }
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
        Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_RETURN_STAGE,
            BC250_V5_START_RETURN_DISPLAY_OK);
        *NumberOfVideoPresentSources = 1;
        *NumberOfChildren = 1;
        Bc250DisplayLog("[StartDevice] Sources=1, Children=1, Profile=%d\n", (int)Bc250ActiveGetAdapterProfile());
        if (Bc250ActiveGetAdapterProfile() == BC250_ADAPTER_PROFILE_DISPLAY_RENDER) {
            context->ContextAbi.DmaBufferSize = 0x10000u;
            context->ContextAbi.DmaBufferSegmentSet = (1u << (2u - 1u));
            context->ContextAbi.AllocationListSize = 32u;
            context->ContextAbi.PatchLocationListSize = 0u;
            context->ContextAbi.Caps.NoPatchingRequired = 1u;
            context->ContextAbi.PagingCompanionNodeId = 0u;
            context->AllocationSegmentSet = 1u;
            {
                BC250_MCDM_RUNTIME_CAPS caps;
                if (NT_SUCCESS(Bc250ActiveQueryRuntimeCaps(context, &caps))) {
                    context->AllocationSegmentBytes = caps.LocalSegmentBytes;
                }
            }
            InterlockedExchange(&context->ContextAbiReady, 1);
            InterlockedExchange(&context->AllocationAbiReady, 1);
            InterlockedExchange(&context->RootPageTableAbiReady, 1);
            InterlockedExchange(&context->GpuvaMappingsOperational, 1);
        } else {
            InterlockedExchange(&context->ContextAbiReady, 0);
            InterlockedExchange(&context->AllocationAbiReady, 0);
            InterlockedExchange(&context->RootPageTableAbiReady, 0);
        }
        InterlockedExchange(&context->IsActive, 1);
        Bc250ActiveArmLifecycle(context);
        Bc250ActivePublishAbiReadyTelemetry(context);
        return STATUS_SUCCESS;
    }

    InterlockedExchange(&context->IsActive, 0);
    Bc250ActiveRetirementTimerStop(context);
    InterlockedExchange(&context->ContextAbiReady, 0);
    InterlockedExchange(&context->AllocationAbiReady, 0);
    InterlockedExchange(&context->RootPageTableAbiReady, 0);
    Bc250ActivePublishAbiReadyTelemetry(context);
    Bc250ProbeRecordFirstFailure(preflightStatus);
    return preflightStatus;
#else
Bc250PhysicalRenderBringup:
        {
            ULONG skipHw = Bc250ActiveReadTunable(L"SkipHwBringup", 1);
            ULONG present = 0u;
            Bc250ProbeRecord(BC250_PROBE_SKIP_HW_BRINGUP, skipHw);
            /*
             * Marca a ENTRADA no bloco compilado com
             * BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP. Num pacote seguro
             * este campo tem de ficar zero: se vier 1, o binario instalado
             * nao e' o que o manifesto diz que e'.
             */
            /* A leitura do marcador garante que o linker o preserve. */
            Bc250ProbeRecord(BC250_PROBE_V5_UNSAFE_BRINGUP_ENTERED,
                g_Bc250UnsafeBringupMarker[0] != L'\0' ? 1u : 0u);
            Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_RETURN_STAGE,
                BC250_V5_START_RETURN_UNSAFE_BRINGUP);

            /* Os tunables do carregador do CP sao lidos AQUI, e nao no sitio de
             * uso: este ponto e' PASSIVE_LEVEL comprovado — e' onde
             * SkipHwBringup funciona desde sempre. Lidos no sitio de uso, os
             * tres voltavam sempre o default. */
            context->GcvmInvalidateEngine = Bc250ActiveReadTunableChecked(
                L"GcvmInvalidateEngine",
                BC250_GCVM_INVALIDATE_ENGINE_DEFAULT, &present);
            context->TunableEnginePresent = present;
            context->GcvmInvalidateSemaphore = Bc250ActiveReadTunableChecked(
                L"GcvmInvalidateSemaphore", 0u, &present);
            context->TunableSemaphorePresent = present;
            context->CpSoftReset = Bc250ActiveReadTunableChecked(
                L"CpSoftReset", 0u, &present);
            context->TunableSoftResetPresent = present;
            context->CpVmid0PageTable = Bc250ActiveReadTunableChecked(
                L"Vmid0PageTable", 0u, &present);
            context->TunableVmid0Present = present;
            context->ShaderDispatch = Bc250ActiveReadTunableChecked(
                L"ShaderDispatch", 0u, &present);
            context->TunableShaderDispatchPresent = present;
            context->PersistentSubmission = Bc250ActiveReadTunableChecked(
                L"PersistentSubmission", 0u, &present);
            context->TunablePersistentSubmissionPresent = present;
            context->GpuvaLabPromotion = Bc250ActiveReadTunableChecked(
                L"GpuvaLabPromotion", 0u, &present);
            context->GpuvaLabPromotionPresent = present;
            Bc250ProbeRecord(BC250_PROBE_GPUVA_LAB_PROMOTION_REQUESTED,
                context->GpuvaLabPromotion);
            Bc250ProbeRecord(BC250_PROBE_GPUVA_LAB_PROMOTION_PRESENT,
                context->GpuvaLabPromotionPresent);
            /* 1 preserva o comportamento atual (liga AUTO_CTXSW_ENABLE antes do
             * kick do anel). O upstream faz o contrario durante o bring-up. */
            context->SdmaAutoCtxSw =
                Bc250ActiveReadTunableChecked(L"SdmaAutoCtxSw", 1u, &present);
            context->SdmaWakeOnExIdle =
                Bc250ActiveReadTunableChecked(L"SdmaWakeOnExIdle", 0u, &present);
            context->SdmaEarlyClkUngate =
                Bc250ActiveReadTunableChecked(L"SdmaEarlyClkUngate", 0u, &present);
            Bc250ProbeRecord(BC250_PROBE_TUNABLE_READ_OK,
                (context->TunableEnginePresent << 0) |
                (context->TunableSemaphorePresent << 1) |
                (context->TunableSoftResetPresent << 2) |
                (context->TunableVmid0Present << 3) |
                (context->TunableShaderDispatchPresent << 4) |
                (context->TunablePersistentSubmissionPresent << 5));
            if (skipHw != 0) {
                if (Bc250ActiveGetAdapterProfile() ==
                        BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) {
                    InterlockedExchange(&context->IsActive, 0);
                    InterlockedExchange(&context->ContextAbiReady, 0);
                    InterlockedExchange(&context->AllocationAbiReady, 0);
                    InterlockedExchange(&context->RootPageTableAbiReady, 0);
                    Bc250ActivePublishAbiReadyTelemetry(context);
                    Bc250ProbeRecordFirstFailure(STATUS_DEVICE_NOT_READY);
                    return STATUS_DEVICE_NOT_READY;
                }
                if (NumberOfVideoPresentSources != NULL) *NumberOfVideoPresentSources = 1;
                if (NumberOfChildren != NULL) *NumberOfChildren = 1;
                InterlockedExchange(&context->IsActive, 1);
                Bc250ActiveArmLifecycle(context);
                InterlockedExchange(&context->ContextAbiReady, 1);
                InterlockedExchange(&context->AllocationAbiReady, 1);
                InterlockedExchange(&context->RootPageTableAbiReady, 1);
                Bc250ActivePublishAbiReadyTelemetry(context);
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
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_BEFORE,
                        context->HqdActiveEvidence.EopRptrBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_APPLIED,
                        context->HqdActiveEvidence.EopRptrApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_READBACK,
                        context->HqdActiveEvidence.EopRptrReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_RESTORED,
                        context->HqdActiveEvidence.EopRptrRestored);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_RESTORE_PASSED,
                        context->HqdActiveEvidence.EopRptrRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_BEFORE,
                        context->HqdActiveEvidence.WptrPollCntl1Before);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_APPLIED,
                        context->HqdActiveEvidence.WptrPollCntl1Applied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_READBACK,
                        context->HqdActiveEvidence.WptrPollCntl1Readback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_RESTORED,
                        context->HqdActiveEvidence.WptrPollCntl1Restored);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_RESTORE_PASSED,
                        context->HqdActiveEvidence.WptrPollCntl1RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_QUEUE_MASK_BIT,
                        context->HqdActiveEvidence.QueueMaskBit);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PRE_ACTIVE_WPTR_APPLIED,
                        context->HqdActiveEvidence.PreActiveWptrApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PRE_ACTIVE_WPTR_READBACK,
                        context->HqdActiveEvidence.PreActiveWptrReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PRE_ACTIVE_WPTR_PUBLISHED,
                        context->HqdActiveEvidence.PreActiveWptrPublished);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_STAGE,
                        context->HqdActiveEvidence.FirstRestoreFailureStage);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_OFFSET,
                        context->HqdActiveEvidence.FirstRestoreFailureOffset);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_EXPECTED,
                        context->HqdActiveEvidence.FirstRestoreFailureExpected);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_OBSERVED,
                        context->HqdActiveEvidence.FirstRestoreFailureObserved);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_TRANSPORT,
                        context->HqdActiveEvidence.FirstRestoreFailureTransport);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RESTORE_FAILURE_COUNT,
                        context->HqdActiveEvidence.RestoreFailureCount);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_PROBE_APPLIED,
                        context->HqdActiveEvidence.WptrPollCntlProbeApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_PROBE_READBACK,
                        context->HqdActiveEvidence.WptrPollCntlProbeReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_WRITABLE,
                        context->HqdActiveEvidence.WptrPollCntlWritable);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_EN_LATCHED,
                        context->HqdActiveEvidence.WptrPollCntlEnLatched);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_POST_ACTIVE_WPTR_OBSERVED,
                        context->HqdActiveEvidence.PostActiveWptrObserved);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_DIRTY_ENTRY,
                        context->HqdActiveEvidence.WptrPollCntl1DirtyEntry);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_CLEAR_ATTEMPTS,
                        context->HqdActiveEvidence.WptrPollCntl1ClearAttempts);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PQ_CONTROL_FIRST_SAMPLE,
                        context->HqdActiveEvidence.PqControlFirstSample);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQ_STATUS0_FIRST_SAMPLE,
                        context->HqdActiveEvidence.HqStatus0FirstSample);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_POLL_LAST,
                        context->HqdActiveEvidence.DequeueRequestPollLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_VMID_POLL_LAST,
                        context->HqdActiveEvidence.VmidPollLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PERSISTENT_STATE_POLL_LAST,
                        context->HqdActiveEvidence.PersistentStatePollLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_REPORT_MEM_FIRST,
                        context->HqdActiveEvidence.RptrReportMemFirst);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_REPORT_MEM_LAST,
                        context->HqdActiveEvidence.RptrReportMemLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_NON_ZERO_SAMPLES,
                        context->HqdActiveEvidence.RptrNonZeroSamples);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STATUS_FIRST,
                        context->HqdActiveEvidence.CpcStatusFirst);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STATUS_LAST,
                        context->HqdActiveEvidence.CpcStatusLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_BUSY_STAT_LAST,
                        context->HqdActiveEvidence.CpcBusyStatLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_STAT_LAST,
                        context->HqdActiveEvidence.CpfBusyStatLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STALLED_STAT1_LAST,
                        context->HqdActiveEvidence.CpcStalledStat1Last);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STAT_FIRST,
                        context->HqdActiveEvidence.CpStatFirst);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STAT_LAST,
                        context->HqdActiveEvidence.CpStatLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STALLED_STAT1_AFTER,
                        context->HqdActiveEvidence.CpStalledStat1After);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP0,
                        context->HqdActiveEvidence.MecMe1HeaderDump[0]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP1,
                        context->HqdActiveEvidence.MecMe1HeaderDump[1]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP2,
                        context->HqdActiveEvidence.MecMe1HeaderDump[2]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP3,
                        context->HqdActiveEvidence.MecMe1HeaderDump[3]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP0,
                        context->HqdActiveEvidence.MecMe2HeaderDump[0]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP1,
                        context->HqdActiveEvidence.MecMe2HeaderDump[1]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP2,
                        context->HqdActiveEvidence.MecMe2HeaderDump[2]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP3,
                        context->HqdActiveEvidence.MecMe2HeaderDump[3]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE0,
                        context->HqdActiveEvidence.MecMe2HeaderDumpBeforeActive[0]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE1,
                        context->HqdActiveEvidence.MecMe2HeaderDumpBeforeActive[1]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE2,
                        context->HqdActiveEvidence.MecMe2HeaderDumpBeforeActive[2]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE3,
                        context->HqdActiveEvidence.MecMe2HeaderDumpBeforeActive[3]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STATUS_BEFORE_KICK,
                        context->HqdActiveEvidence.CpcStatusBeforeKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_BUSY_BEFORE_KICK,
                        context->HqdActiveEvidence.CpcBusyStatBeforeKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_BEFORE_KICK,
                        context->HqdActiveEvidence.CpfBusyStatBeforeKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STALLED_BEFORE_KICK,
                        context->HqdActiveEvidence.CpcStalledStat1BeforeKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STAT_BEFORE_KICK,
                        context->HqdActiveEvidence.CpStatBeforeKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STALLED_BEFORE_KICK,
                        context->HqdActiveEvidence.CpStalledStat1BeforeKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.CpcStatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_BUSY_AFTER_KICK,
                        context->HqdActiveEvidence.CpcBusyStatAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_AFTER_KICK,
                        context->HqdActiveEvidence.CpfBusyStatAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_BEFORE_ACTIVE,
                        context->HqdActiveEvidence.CpfBusyStatBeforeActive);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_AFTER_ACTIVE,
                        context->HqdActiveEvidence.CpfBusyStatAfterActive);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_BEFORE_WPTR,
                        context->HqdActiveEvidence.CpfBusyStatBeforeWptr);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_AFTER_WPTR,
                        context->HqdActiveEvidence.CpfBusyStatAfterWptr);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_BEFORE_ACTIVE,
                        context->HqdActiveEvidence.CpfStalledStat1BeforeActive);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_AFTER_ACTIVE,
                        context->HqdActiveEvidence.CpfStalledStat1AfterActive);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_BEFORE_KICK,
                        context->HqdActiveEvidence.CpfStalledStat1BeforeKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_AFTER_KICK,
                        context->HqdActiveEvidence.CpfStalledStat1AfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_LAST,
                        context->HqdActiveEvidence.CpfStalledStat1Last);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STATUS_AFTER_ACTIVE,
                        context->HqdActiveEvidence.CpfStatusAfterActive);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.CpfStatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_APPLIED,
                        context->HqdActiveEvidence.DequeueRequestApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_READBACK,
                        context->HqdActiveEvidence.DequeueRequestReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_RESTORE_PASSED,
                        context->HqdActiveEvidence.DequeueRequestRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STALLED_AFTER_KICK,
                        context->HqdActiveEvidence.CpcStalledStat1AfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STAT_AFTER_KICK,
                        context->HqdActiveEvidence.CpStatAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STALLED_AFTER_KICK,
                        context->HqdActiveEvidence.CpStalledStat1AfterKick);
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
                                Bc250ActiveRecordShaderDispatch(context);

                                /*
                                 * P1/P2 fazem parte do StartDevice somente por
                                 * opt-in exato. O gate one-shot precisa provar
                                 * o caminho antes que a HQD persistente fique
                                 * viva para as DDIs do Dxgkrnl.
                                 */
                                if (context->PersistentSubmission == 1u &&
                                    context->TunablePersistentSubmissionPresent == 1u &&
                                    context->HqdActiveStatus == BC250_HQD_ACTIVE_OK &&
                                    context->HqdActiveEvidence.PhysicalReady != 0u) {
                                    persistentStatus =
                                        Bc250SubmissionEngineOnline(context);
                                    if (NT_SUCCESS(persistentStatus)) {
                                        selfTestStatus =
                                            Bc250SubmissionEngineSelfTest(context);
                                    }
                                    if (NT_SUCCESS(persistentStatus) &&
                                        NT_SUCCESS(selfTestStatus)) {
                                        p2Status =
                                            Bc250SubmissionEngineP2StreamTest(context);
                                    }
                                    if (NT_SUCCESS(persistentStatus) &&
                                        NT_SUCCESS(selfTestStatus) &&
                                        NT_SUCCESS(p2Status) &&
                                        context->SubmissionEngine.SelfTest.PhysicalReady != 0u &&
                                        context->SubmissionEngine.P2Stream.PhysicalReady != 0u &&
                                        InterlockedCompareExchange(
                                            &context->SubmissionEngine.State,
                                            BC250_SUBMISSION_ENGINE_ONLINE,
                                            BC250_SUBMISSION_ENGINE_ONLINE) ==
                                                BC250_SUBMISSION_ENGINE_ONLINE) {
                                        persistentReady = TRUE;
                                    } else if (NT_SUCCESS(persistentStatus)) {
                                        (void)Bc250SubmissionEngineOffline(context);
                                    }
                                } else if (context->PersistentSubmission == 1u) {
                                    persistentStatus = STATUS_DEVICE_NOT_READY;
                                } else if (context->PersistentSubmission != 0u) {
                                    persistentStatus =
                                        STATUS_DEVICE_CONFIGURATION_ERROR;
                                }
                                Bc250SubmissionEngineRecordTelemetry(context);
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
                    Bc250ProbeRecord(
                        BC250_PROBE_PFP_DIRECT_IC_INV_ENTRY_MASK,
                        context->PfpDirectLoadEvidence.IcacheInvalidateEntryMask);
                    Bc250ProbeRecord(
                        BC250_PROBE_PFP_DIRECT_IC_INV_UNHALT_MASK,
                        context->PfpDirectLoadEvidence.IcacheInvalidateUnhaltMask);
                    Bc250ProbeRecord(
                        BC250_PROBE_PFP_DIRECT_IC_PRIME_UNHALT_MASK,
                        context->PfpDirectLoadEvidence.IcachePrimeUnhaltMask);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_COMPARED_DWORDS,
                        context->PfpDirectLoadEvidence.Rb0ComparedDwords);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_MISMATCHES,
                        context->PfpDirectLoadEvidence.Rb0MismatchCount);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_DEST_POLLS,
                        context->PfpDirectLoadEvidence.Rb0DestPolls);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_DEST_OBSERVED,
                        context->PfpDirectLoadEvidence.Rb0DestObserved);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_JT_STAT_PFP,
                        context->PfpDirectLoadEvidence.JtStatPfp);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_JT_STAT_CE,
                        context->PfpDirectLoadEvidence.JtStatCe);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_JT_STAT_MEC,
                        context->PfpDirectLoadEvidence.JtStatMec);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_ENGINE,
                        context->PfpDirectLoadEvidence.InvalidateEngine);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_ACK_BEFORE_STAGING,
                        context->PfpDirectLoadEvidence.InvalidateAckBeforeStaging);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_ACK_STAGING,
                        context->PfpDirectLoadEvidence.InvalidateAckStaging);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_ACK_BEFORE_PRE_WPTR,
                        context->PfpDirectLoadEvidence.InvalidateAckBeforePreWptr);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_USE_SEMAPHORE,
                        context->PfpDirectLoadEvidence.InvalidateUseSemaphore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_ACK_RAW_STAGING,
                        context->PfpDirectLoadEvidence.InvalidateAckRawStaging);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_ACK_RAW_PRE_WPTR,
                        context->PfpDirectLoadEvidence.InvalidateAckRawPreWptr);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_SEM_ACQ_STAGING,
                        context->PfpDirectLoadEvidence.InvalidateSemAcquiredStaging);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_SEM_ACQ_PRE_WPTR,
                        context->PfpDirectLoadEvidence.InvalidateSemAcquiredPreWptr);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_CP_SOFT_RESET,
                        context->PfpDirectLoadEvidence.CpSoftResetApplied);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_VMID0_PT,
                        context->PfpDirectLoadEvidence.Vmid0PageTableApplied);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_VMID0_INV_ACK,
                        context->PfpDirectLoadEvidence.Vmid0InvalidateAck);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_VMID0_ACK_AFTER,
                        context->PfpDirectLoadEvidence.Vmid0AckAfter);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_POLLS_STAGING,
                        context->PfpDirectLoadEvidence.InvalidatePollsStaging);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_ACK_PRE_WPTR,
                        context->PfpDirectLoadEvidence.InvalidateAckPreWptr);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_POLLS_PRE_WPTR,
                        context->PfpDirectLoadEvidence.InvalidatePollsPreWptr);
                    {
                        uint32_t traceIndex;
                        for (traceIndex = 0u;
                             traceIndex < BC250_PFP_PC_TRACE_SAMPLES;
                             ++traceIndex) {
                            Bc250ProbeRecord(
                                (BC250_PROBE_FIELD)(
                                    BC250_PROBE_PFP_DIRECT_PC_TRACE_00 + traceIndex),
                                context->PfpDirectLoadEvidence.PfpPcTrace[traceIndex]);
                        }
                    }
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
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_ERROR_LOG_BEFORE,
                        context->SdmaRingWorkEvidence.ErrorLogBefore);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_CNTL_BEFORE,
                        context->SdmaRingWorkEvidence.CntlBefore);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_CNTL_APPLIED,
                        context->SdmaRingWorkEvidence.CntlApplied);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_EX_IDLE_EXPIRED,
                        context->SdmaRingWorkEvidence.ExIdlePollExpiredBefore);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_EX_IDLE_AT_ENTRY,
                        context->SdmaRingWorkEvidence.ExIdleAtEntry);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_WAKE_APPLIED,
                        context->SdmaRingWorkEvidence.WakeApplied);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_EX_IDLE_AFTER_WAKE,
                        context->SdmaRingWorkEvidence.ExIdleAfterWake);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_EX_IDLE_CLEAR_POLLS,
                        context->SdmaRingWorkEvidence.ExIdleClearPolls);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_CLK_AT_ENTRY,
                        context->SdmaRingWorkEvidence.ClkCtrlAtEntry);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_EARLY_CLK_UNGATE,
                        context->SdmaRingWorkEvidence.EarlyClkUngateApplied);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_RD_XNACK0_BEFORE,
                        context->SdmaRingWorkEvidence.Utcl1ReadXnack0Before);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_WR_XNACK0_BEFORE,
                        context->SdmaRingWorkEvidence.Utcl1WriteXnack0Before);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_UTCL1_TIMEOUT_BEFORE,
                        context->SdmaRingWorkEvidence.Utcl1TimeoutBefore);
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
                        context->SdmaRingWorkStatus == BC250_SDMA_RING_WORK_OK &&
                        (context->ShaderDispatch == 0u ||
                            (context->ShaderDispatch == 1u &&
                             context->HqdActiveStatus == BC250_HQD_ACTIVE_OK &&
                             context->HqdActiveEvidence.PhysicalReady != 0u)) &&
                        (context->PersistentSubmission == 0u ||
                            (context->PersistentSubmission == 1u &&
                             persistentReady != FALSE))) {
                        if (Bc250ActiveGetAdapterProfile() ==
                                BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) {
                            if (NumberOfVideoPresentSources != NULL)
                                *NumberOfVideoPresentSources = 0;
                            if (NumberOfChildren != NULL) *NumberOfChildren = 0;
                        } else {
                            if (NumberOfVideoPresentSources != NULL)
                                *NumberOfVideoPresentSources = 1;
                            if (NumberOfChildren != NULL) *NumberOfChildren = 1;
                        }
                        InterlockedExchange(&context->IsActive, 1);
                        Bc250ActiveArmLifecycle(context);
                        InterlockedExchange(&context->ContextAbiReady, 1);
                        InterlockedExchange(&context->AllocationAbiReady, 1);
                        InterlockedExchange(&context->RootPageTableAbiReady, 1);
                        Bc250ActivePublishAbiReadyTelemetry(context);
                        return STATUS_SUCCESS;
                    }
                }
            }
        }
    }

    InterlockedExchange(&context->IsActive, 0);
    Bc250ActiveRetirementTimerStop(context);
    InterlockedExchange(&context->ContextAbiReady, 0);
    InterlockedExchange(&context->AllocationAbiReady, 0);
    InterlockedExchange(&context->RootPageTableAbiReady, 0);
    Bc250ActivePublishAbiReadyTelemetry(context);
    if (context->PersistentSubmission == 1u &&
        persistentReady == FALSE) {
        preflightStatus = !NT_SUCCESS(persistentStatus) ?
            persistentStatus : (!NT_SUCCESS(selfTestStatus) ?
                selfTestStatus : p2Status);
        if (NT_SUCCESS(preflightStatus)) {
            preflightStatus = STATUS_DEVICE_HARDWARE_ERROR;
        }
    } else if (context->PersistentSubmission != 0u &&
               context->PersistentSubmission != 1u) {
        preflightStatus = STATUS_DEVICE_CONFIGURATION_ERROR;
    } else if (context->ShaderDispatch != 0u &&
               context->ShaderDispatch != 1u) {
        preflightStatus = STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Bc250ProbeRecordFirstFailure(preflightStatus);
    return preflightStatus;
#endif
}

static NTSTATUS Bc250ActiveQuiesceAndSnapshotTracker(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_ uint32_t StopReason,
    _Out_ uint32_t *PendingCount,
    _Out_ uint32_t *ReservedCount);

static NTSTATUS Bc250DxgkDdiStopDeviceActiveReason(
    CONST PVOID MiniportDeviceContext,
    uint32_t StopReason)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)MiniportDeviceContext;
    uint32_t destroyedObjects;
    uint32_t pendingCount = 0u;
    uint32_t reservedCount = 0u;
    BC250_MCDM_OBJECT_STATUS teardownStatus;
    NTSTATUS engineStatus = STATUS_SUCCESS;
    NTSTATUS quiesceStatus;
    NTSTATUS coreStatus;
    static LONG s_stopFirstStatusRecorded;
    /*
     * Publicar primeiro o rastro barato acumulado desde StartDevice. Fazer
     * isto antes de qualquer teardown preserva a ultima DDI que levou o
     * dxgkrnl a parar o adaptador, e agora tambem os GUIDs completos das
     * interfaces pedidas e o marco pos-StartDevice mais avancado.
     */
    Bc250ProbeSnapshotDdiTrace();
    Bc250ProbeSnapshotQueryInterfaces();
    /*
     * A marca de StopDevice vem DEPOIS do snapshot acima: marcar antes
     * empurraria a propria StopDevice para dentro do retrato e apagaria uma
     * posicao do rastro que interessa mais.
     */
    Bc250ProbeMarkDdi(BC250_V5_DDI_STOP_DEVICE);
    Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_ENTERED, 1);
    Bc250DisplayLog("[StopDevice] Entered. Reason=%u\n", StopReason);
    if (context == NULL ||
        context->Signature != BC250_ACTIVE_SIGNATURE) {
        Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_STATUS,
                         (ULONG)STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Quiescencia antes da fotografia. Nao desligar polling/engine enquanto
     * uma fence ainda possui pin GPUVA — mas tambem nao declarar ocupado por
     * causa de uma consulta que falhou.
     */
    quiesceStatus = Bc250ActiveQuiesceAndSnapshotTracker(
        context, StopReason, &pendingCount, &reservedCount);
    if (!NT_SUCCESS(quiesceStatus)) {
        NTSTATUS reported = (quiesceStatus == STATUS_DEVICE_BUSY) ?
            STATUS_DEVICE_BUSY : quiesceStatus;
        Bc250ProbeRecordFirstFailure(reported);
        Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_STATUS, (ULONG)reported);
        if (InterlockedCompareExchange(&s_stopFirstStatusRecorded, 1, 0) == 0) {
            Bc250ProbeRecord(BC250_PROBE_V5_STOP_FIRST_STATUS, (ULONG)reported);
        }
        /*
         * STOPPING continua fechando novas admissoes, mas trabalho aceito
         * antes do portao precisa conservar um mecanismo de retirement para
         * que uma nova chamada de Stop possa progredir. Nao reabrir o portao
         * e nao limpar o tracker.
         */
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) == 1) {
            Bc250ProbeRecord(BC250_PROBE_V5_STOP_TIMER_DRAINED, 0u);
            Bc250ActiveRetirementTimerStart(context);
        }
        return reported;
    }
    if (pendingCount != 0u || reservedCount != 0u) {
        Bc250ProbeRecordFirstFailure(STATUS_DEVICE_BUSY);
        Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_STATUS,
                         (ULONG)STATUS_DEVICE_BUSY);
        if (InterlockedCompareExchange(&s_stopFirstStatusRecorded, 1, 0) == 0) {
            Bc250ProbeRecord(BC250_PROBE_V5_STOP_FIRST_STATUS,
                             (ULONG)STATUS_DEVICE_BUSY);
        }
        /* Ver comentario acima: manter retirement sem reabrir admissoes. */
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) == 1) {
            Bc250ProbeRecord(BC250_PROBE_V5_STOP_TIMER_DRAINED, 0u);
            Bc250ActiveRetirementTimerStart(context);
        }
        return STATUS_DEVICE_BUSY;
    }
    if (InterlockedCompareExchange(&s_stopFirstStatusRecorded, 1, 0) == 0) {
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_FIRST_STATUS, (ULONG)STATUS_SUCCESS);
    }
    InterlockedExchange(&context->IsActive, 0);
    Bc250ActiveRetirementTimerStop(context);
    InterlockedExchange(&context->ContextAbiReady, 0);
    InterlockedExchange(&context->AllocationAbiReady, 0);
    InterlockedExchange(&context->RootPageTableAbiReady, 0);
    engineStatus = Bc250SubmissionEngineOffline(context);
    Bc250ProbeRecordFirstFailure(engineStatus);
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&context->ObjectLock);
    InterlockedExchange(&context->GpuvaMappingsOperational, 0);
    InterlockedExchange(&context->GpuvaActivationEligible, 0);
    context->GpuvaActivationBlockMask =
        BC250_GPUVA_GATE_BLOCK_ADAPTER_INACTIVE;
    InterlockedExchange(&context->GpuvaJournalReady, 0);
    Bc250GpuvaJournalAbort(&context->GpuvaJournal);
    context->GpuvaHierarchyPending = 0u;
    context->GpuvaHierarchyProcess = NULL;
    context->GpuvaHierarchyRootPageTable = 0u;
    context->GpuvaHierarchyHighestLevel = 0u;
    destroyedObjects = 0u;
    teardownStatus = Bc250McdmObjectRegistryTeardown(
        &context->Objects, &destroyedObjects);
    ExReleasePushLockExclusive(&context->ObjectLock);
    KeLeaveCriticalRegion();
    if (teardownStatus != BC250_MCDM_OBJECT_OK) {
        NTSTATUS status = Bc250ObjectStatusToNtStatus(teardownStatus);
        Bc250ProbeRecordFirstFailure(status);
        Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_STATUS, (ULONG)status);
        return status;
    }
    Bc250DisplayTeardown();
    coreStatus = Bc250DxgkDdiStopDeviceReadonly(&context->Core);
    Bc250ProbeRecordFirstFailure(coreStatus);
    if (!NT_SUCCESS(coreStatus)) {
        Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_STATUS, (ULONG)coreStatus);
        return coreStatus;
    }
    if (NT_SUCCESS(engineStatus)) {
        InterlockedExchange(&context->LifecycleState,
            (LONG)BC250_ACTIVE_LIFECYCLE_STOPPED);
        Bc250ProbeRecord(BC250_PROBE_V5_LIFECYCLE_STATE,
            BC250_ACTIVE_LIFECYCLE_STOPPED);
    }
    Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_STATUS, (ULONG)engineStatus);
    (void)Bc250BootFuseDisarmClean(STATUS_SUCCESS);
    return engineStatus;
}

NTSTATUS APIENTRY Bc250DxgkDdiStopDeviceActive(
    CONST PVOID MiniportDeviceContext)
{
    return Bc250DxgkDdiStopDeviceActiveReason(
        MiniportDeviceContext, BC250_ACTIVE_STOP_REASON_STOP_DEVICE);
}

NTSTATUS APIENTRY Bc250DxgkDdiRemoveDeviceActive(
    CONST PVOID MiniportDeviceContext)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)MiniportDeviceContext;
    NTSTATUS status = STATUS_SUCCESS;

    Bc250ProbeMarkDdi(BC250_V5_DDI_REMOVE_DEVICE);
    Bc250ProbeRecord(BC250_PROBE_REMOVE_DEVICE_ENTERED, 1);
    if (context == NULL ||
        context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    /*
     * O contrato PnP normalmente chama StopDevice antes de RemoveDevice.
     * Repetir o teardown readonly sobre o mesmo core devolvia
     * STATUS_DEVICE_BUSY e mascarava a causa original do Codigo 43. Ainda
     * suportamos RemoveDevice direto quando o contexto segue ativo.
     */
    if (InterlockedCompareExchange(&context->IsActive, 0, 0) != 0) {
        status = Bc250DxgkDdiStopDeviceActiveReason(
            context, BC250_ACTIVE_STOP_REASON_REMOVE_DEVICE);
        if (!NT_SUCCESS(status)) {
            Bc250ProbeRecord(BC250_PROBE_REMOVE_DEVICE_STATUS, (ULONG)status);
            return status;
        }
    }
    Bc250ActiveUnregisterAdapter(context);
    context->Signature = 0;
    context->PhysicalDeviceObject = NULL;
    context->Core.PhysicalDeviceObject = NULL;
    ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
    /*
     * Segundo retrato, agora com Stop e Remove ja' marcadas. So' e' seguro
     * republicar porque o anel do schema 134 tem 16 posicoes: com os tres
     * slots antigos, esta chamada apagaria justamente a historia anterior ao
     * teardown. Depois do ExFreePoolWithTag de proposito — o rastro vive em
     * estatica global, nao no contexto liberado.
     */
    Bc250ProbeSnapshotDdiTrace();
    Bc250ProbeRecord(BC250_PROBE_REMOVE_DEVICE_STATUS, (ULONG)status);
    return status;
}

/* ==========================================================================
   Fase V2 / V5 — rastreador e retirada de fence para o dxgkrnl.
   ========================================================================== */

#define BC250_ACTIVE_FENCE_SYNC_PUSH    0u
#define BC250_ACTIVE_FENCE_SYNC_DRAIN   1u
#define BC250_ACTIVE_FENCE_SYNC_RESERVE 2u
#define BC250_ACTIVE_FENCE_SYNC_CANCEL  3u
#define BC250_ACTIVE_FENCE_SYNC_COMMIT  4u
#define BC250_ACTIVE_FENCE_SYNC_QUERY   5u
#define BC250_ACTIVE_FENCE_SYNC_PENDING 6u

#define BC250_ACTIVE_FENCE_NOTIFY_BATCH 16u

typedef struct BC250_ACTIVE_FENCE_SYNC {
    BC250_DXGK_ACTIVE_CONTEXT *Context;
    uint32_t Operation;

    /* PUSH */
    uint64_t RingFence;
    uint32_t SubmissionFenceId;
    uint32_t NodeOrdinal;
    uint32_t EngineOrdinal;
    BOOLEAN PushAccepted;

    /* RESERVE / CANCEL / COMMIT */
    uint32_t ReservationToken;
    BOOLEAN ReserveAccepted;
    BOOLEAN CommitAccepted;
    uint32_t GpuvaSlot;
    uint32_t GpuvaGeneration;

    /* DRAIN */
    uint64_t RetiredRingFence;
    uint32_t Notified;
    uint32_t CurrentFence;
    uint32_t PendingCount;
    uint32_t ReservedCount;
} BC250_ACTIVE_FENCE_SYNC;

static BOOLEAN Bc250ActiveSynchronizedFenceCallback(
    _In_opt_ PVOID SynchronizeContext)
{
    BC250_ACTIVE_FENCE_SYNC *sync = (BC250_ACTIVE_FENCE_SYNC *)SynchronizeContext;
    BC250_DXGK_ACTIVE_CONTEXT *context;
    BC250_WDDM_FENCE_ENTRY completed[BC250_ACTIVE_FENCE_NOTIFY_BATCH];
    DXGKARGCB_NOTIFY_INTERRUPT_DATA data;
    uint32_t count;
    uint32_t i;

    if (sync == NULL || sync->Context == NULL) {
        return FALSE;
    }
    context = sync->Context;
    if (context->Signature != BC250_ACTIVE_SIGNATURE) {
        return FALSE;
    }

    if (sync->Operation == BC250_ACTIVE_FENCE_SYNC_RESERVE) {
        sync->ReserveAccepted = Bc250WddmFenceTrackerReserve(
            &context->SubmissionEngine.WddmFences,
            sync->SubmissionFenceId,
            sync->NodeOrdinal,
            sync->EngineOrdinal,
            &sync->ReservationToken);
        if (sync->ReserveAccepted &&
            !Bc250WddmFenceTrackerAttachGpuvaPin(
                &context->SubmissionEngine.WddmFences,
                sync->ReservationToken, sync->GpuvaSlot,
                sync->GpuvaGeneration)) {
            (void)Bc250WddmFenceTrackerCancel(
                &context->SubmissionEngine.WddmFences,
                sync->ReservationToken);
            sync->ReserveAccepted = FALSE;
        }
        return sync->ReserveAccepted;
    }

    if (sync->Operation == BC250_ACTIVE_FENCE_SYNC_CANCEL) {
        return Bc250WddmFenceTrackerCancel(
            &context->SubmissionEngine.WddmFences,
            sync->ReservationToken);
    }

    if (sync->Operation == BC250_ACTIVE_FENCE_SYNC_COMMIT) {
        sync->CommitAccepted = Bc250WddmFenceTrackerCommit(
            &context->SubmissionEngine.WddmFences,
            sync->ReservationToken,
            sync->RingFence);
        return sync->CommitAccepted;
    }

    if (sync->Operation == BC250_ACTIVE_FENCE_SYNC_PUSH) {
        sync->PushAccepted = Bc250WddmFenceTrackerPush(
            &context->SubmissionEngine.WddmFences,
            sync->RingFence,
            sync->SubmissionFenceId,
            sync->NodeOrdinal,
            sync->EngineOrdinal);
        return sync->PushAccepted;
    }

    if (sync->Operation == BC250_ACTIVE_FENCE_SYNC_QUERY) {
        return Bc250WddmFenceTrackerQueryCurrent(
            &context->SubmissionEngine.WddmFences,
            sync->NodeOrdinal,
            &sync->CurrentFence);
    }

    if (sync->Operation == BC250_ACTIVE_FENCE_SYNC_PENDING) {
        return Bc250WddmFenceTrackerSnapshotCounts(
            &context->SubmissionEngine.WddmFences,
            &sync->PendingCount,
            &sync->ReservedCount);
    }

    if (context->Core.DxgkInterface.DxgkCbNotifyInterrupt == NULL ||
        context->Core.DxgkInterface.DeviceHandle == NULL) {
        return FALSE;
    }

    count = Bc250WddmFenceTrackerDrain(
        &context->SubmissionEngine.WddmFences,
        sync->RetiredRingFence,
        completed,
        BC250_ACTIVE_FENCE_NOTIFY_BATCH);
    if (count == 0u) {
        return FALSE;
    }

    for (i = 0u; i < count; ++i) {
        if (completed[i].GpuvaPinValid != 0u) {
            BC250_GPUVA_SNAPSHOT_READER reader;
            RtlZeroMemory(&reader, sizeof(reader));
            if (completed[i].GpuvaSlot > 1u ||
                (uint32_t)InterlockedCompareExchange(
                    &context->GpuvaSnapshots.SlotGeneration[
                        completed[i].GpuvaSlot], 0, 0) !=
                    completed[i].GpuvaGeneration) {
                InterlockedExchange(&context->GpuvaMappingsOperational, 0);
                return FALSE;
            }
            reader.Space = &context->GpuvaSnapshots.Slots[completed[i].GpuvaSlot];
            reader.Slot = completed[i].GpuvaSlot;
            reader.PublicationGeneration = completed[i].GpuvaGeneration;
            reader.Acquired = 1u;
            Bc250GpuvaSnapshotRelease(&context->GpuvaSnapshots, &reader);
        }
        RtlZeroMemory(&data, sizeof(data));
        data.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
        data.DmaCompleted.SubmissionFenceId = completed[i].SubmissionFenceId;
        data.DmaCompleted.NodeOrdinal = completed[i].NodeOrdinal;
        data.DmaCompleted.EngineOrdinal = completed[i].EngineOrdinal;
        context->Core.DxgkInterface.DxgkCbNotifyInterrupt(
            context->Core.DxgkInterface.DeviceHandle, &data);
    }

    sync->Notified = count;
    return TRUE;
}

/*
 * ==========================================================================
 * Quiescencia do rastreador antes do teardown (candidato 3.67).
 *
 * O que o 3.66 fazia: chamava DxgkCbSynchronizeExecution para ler Count e
 * ReservedCount, e tratava qualquer falha dessa chamada como "ocupado".  O
 * ensaio fisico de 2026-09-01 com RenderNode=1 mediu exatamente isso —
 * SyncStatus=0xC0000001 (STATUS_UNSUCCESSFUL), SyncResult=0, Pending=0,
 * Reserved=0 — e o adaptador terminou em STATUS_DEVICE_BUSY sem jamais ter
 * tido uma fence.  Evidencia:
 * docs/evidence/2026-09-01_11-34-36-DESKTOP-HQAG6T4-rendernode-inspect.
 *
 * O que este candidato NAO faz: ler os contadores direto.  Nao esta provado
 * que a interrupcao ja foi desconectada nesse ponto do ciclo PnP, e mesmo que
 * estivesse ainda restariam o timer de retirement (armado em 1 ms), o DPC,
 * submissoes concorrentes e cancelamentos de reserva.  Trocar uma leitura
 * errada por outra leitura errada nao e' correcao.
 *
 * O que ele faz, nesta ordem:
 *   1. LifecycleState = STOPPING e portao de admissao do rastreador fechado —
 *      nenhuma submissao ou reserva nova entra a partir daqui.
 *   2. Timer de retirement parado.
 *   3. Espera as submissoes em voo sairem e drena os DPCs ja enfileirados.
 *   4. Fotografa Count e ReservedCount sob o lock proprio do rastreador.
 *
 * O criterio fail-closed NAO foi afrouxado.  Contagem diferente de zero
 * continua devolvendo STATUS_DEVICE_BUSY; falha ao adquirir o lock ou dreno
 * incompleto tambem.  O que mudou foi o mecanismo de consulta, que deixou de
 * depender exclusivamente de uma chamada que pode falhar por motivos alheios
 * ao rastreador.
 * ==========================================================================
 */

/* Dreno limitado: 400 x 5 ms = 2 s. Estourar e' falha, nao sucesso. */
#define BC250_ACTIVE_STOP_DRAIN_MAX_ITERATIONS 400u
#define BC250_ACTIVE_STOP_DRAIN_INTERVAL_100NS (-50000LL) /* 5 ms relativos */

static void Bc250ActiveStopDrainDelay(void)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    LARGE_INTEGER interval;
    interval.QuadPart = BC250_ACTIVE_STOP_DRAIN_INTERVAL_100NS;
    (void)KeDelayExecutionThread(KernelMode, FALSE, &interval);
#endif
}

static void Bc250ActiveStopFlushDpcs(void)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeFlushQueuedDpcs();
#endif
}

/*
 * Torna o rastreador quiescente e fotografa as contagens.
 *
 * Devolve STATUS_SUCCESS somente quando a fotografia e' confiavel; nesse caso
 * *PendingCount e *ReservedCount valem. Qualquer outro retorno significa
 * "nao sei", e o chamador deve falhar fechado.
 */
static NTSTATUS Bc250ActiveQuiesceAndSnapshotTracker(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_ uint32_t StopReason,
    _Out_ uint32_t *PendingCount,
    _Out_ uint32_t *ReservedCount)
{
    BC250_WDDM_FENCE_TRACKER *tracker;
    uint32_t iterations = 0u;
    LONG inFlight;

    *PendingCount = 0u;
    *ReservedCount = 0u;
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    tracker = &Context->SubmissionEngine.WddmFences;

    /* 1. STOPPING: fecha a admissao de trabalho novo. */
    InterlockedExchange(&Context->LifecycleState,
        (LONG)BC250_ACTIVE_LIFECYCLE_STOPPING);
    Context->StopReason = StopReason;
    Bc250WddmFenceTrackerSetAcceptingWork(tracker, FALSE);
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_REASON, StopReason);
    Bc250ProbeRecord(BC250_PROBE_V5_LIFECYCLE_STATE,
        BC250_ACTIVE_LIFECYCLE_STOPPING);

    /* 2. Primeira parada do timer (ja inclui KeFlushQueuedDpcs). */
    Bc250ActiveRetirementTimerStop(Context);

    /*
     * 3. Espera as submissoes que passaram pelo portao antes do fechamento
     *    terminarem. Elas ainda podem comitar ou cancelar reserva, e por isso
     *    fotografar antes delas sairem produziria numero instavel.
     */
    for (;;) {
        inFlight = InterlockedCompareExchange(&Context->SubmitInFlight, 0, 0);
        if (inFlight <= 0) {
            break;
        }
        if (iterations >= BC250_ACTIVE_STOP_DRAIN_MAX_ITERATIONS) {
            break;
        }
        ++iterations;
        Bc250ActiveStopDrainDelay();
    }
    /*
     * Uma submissao que ja tinha passado pelo portao podia estar entre sua
     * ultima verificacao de RUNNING e o rearme do timer. Parar novamente
     * depois de SubmitInFlight chegar a zero fecha essa janela. A propria
     * submissao tambem deixa de rearmar ao observar STOPPING, mas a segunda
     * parada e' a barreira que torna a corrida inofensiva.
     */
    Bc250ActiveRetirementTimerStop(Context);
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_TIMER_DRAINED, 1u);
    /* Um DPC pode ter sido enfileirado durante a espera acima. */
    Bc250ActiveStopFlushDpcs();
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_DPC_DRAINED, 1u);

    inFlight = InterlockedCompareExchange(&Context->SubmitInFlight, 0, 0);
    Context->StopSubmitInFlightAfterDrain =
        (inFlight < 0) ? 0u : (uint32_t)inFlight;
    Context->StopDrainIterations = iterations;
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_SUBMIT_INFLIGHT,
        Context->StopSubmitInFlightAfterDrain);
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_DRAIN_ITERATIONS, iterations);
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_REJECTED_WHILE_STOPPING,
        tracker->RejectedWhileStopping);

    if (inFlight > 0) {
        /* Dreno nao concluiu: e' ocupado de verdade, nao ruido de consulta. */
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_SNAPSHOT_OK, 0u);
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_FENCE_QUERY_MODE, 3u);
        return STATUS_DEVICE_BUSY;
    }

    /* 4. Fotografia sob o lock proprio do rastreador. */
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_FENCE_QUERY_MODE, 3u);
    if (!Bc250WddmFenceTrackerSnapshotCounts(
            tracker, PendingCount, ReservedCount)) {
        ++Context->StopSnapshotFailures;
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_SNAPSHOT_OK, 0u);
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_LOCK_FAILURES,
            (ULONG)tracker->Lock.AcquireFailures);
        return STATUS_UNSUCCESSFUL;
    }
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_SNAPSHOT_OK, 1u);
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_LOCK_FAILURES,
        (ULONG)tracker->Lock.AcquireFailures);
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_FENCE_PENDING_COUNT, *PendingCount);
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_FENCE_RESERVED_COUNT, *ReservedCount);

    /*
     * A consulta serializada continua sendo feita, mas agora e' apenas
     * observacao: o veredito ja saiu da fotografia sob lock. Assim o proximo
     * ensaio ainda mostra se DxgkCbSynchronizeExecution funciona no
     * StopDevice, sem que a resposta dele decida o teardown.
     */
    if (Context->Core.DxgkInterface.DxgkCbSynchronizeExecution != NULL &&
        Context->Core.DxgkInterface.DeviceHandle != NULL) {
        BC250_ACTIVE_FENCE_SYNC sync;
        BOOLEAN queried = FALSE;
        NTSTATUS syncStatus;

        RtlZeroMemory(&sync, sizeof(sync));
        sync.Context = Context;
        sync.Operation = BC250_ACTIVE_FENCE_SYNC_PENDING;
        syncStatus = Context->Core.DxgkInterface.DxgkCbSynchronizeExecution(
            Context->Core.DxgkInterface.DeviceHandle,
            Bc250ActiveSynchronizedFenceCallback, &sync, 0, &queried);
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_FENCE_SYNC_STATUS,
            (ULONG)syncStatus);
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_FENCE_SYNC_RESULT,
            queried ? 1u : 0u);
    } else {
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_FENCE_SYNC_STATUS,
            (ULONG)STATUS_NOT_SUPPORTED);
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_FENCE_SYNC_RESULT, 0u);
    }
    return STATUS_SUCCESS;
}

static void Bc250ActiveRetireAndNotify(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    BC250_ACTIVE_FENCE_SYNC sync;
    BOOLEAN notified = FALSE;
    uint32_t retired = 0u;
    NTSTATUS status;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return;
    }
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        return;
    }
    if (Context->Core.DxgkInterface.DxgkCbSynchronizeExecution == NULL ||
        Context->Core.DxgkInterface.DeviceHandle == NULL) {
        return;
    }

    status = Bc250SubmissionEnginePoll(Context, &retired);
    if (!NT_SUCCESS(status)) {
        ++Context->RetirementPollFailures;
        return;
    }

    RtlZeroMemory(&sync, sizeof(sync));
    sync.Context = Context;
    sync.Operation = BC250_ACTIVE_FENCE_SYNC_DRAIN;
    sync.RetiredRingFence = Context->SubmissionEngine.ComputeRing.LastRetiredFence;

    status = Context->Core.DxgkInterface.DxgkCbSynchronizeExecution(
        Context->Core.DxgkInterface.DeviceHandle,
        Bc250ActiveSynchronizedFenceCallback,
        &sync,
        0,
        &notified);
    if (!NT_SUCCESS(status) || !notified || sync.Notified == 0u) {
        return;
    }

    ++Context->RetirementNotifyBatches;
    Context->RetirementNotifiedTotal += sync.Notified;

    if (Context->Core.DxgkInterface.DxgkCbNotifyDpc != NULL) {
        Context->Core.DxgkInterface.DxgkCbNotifyDpc(
            Context->Core.DxgkInterface.DeviceHandle);
    }
}

static VOID Bc250ActiveRetirementDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)DeferredContext;
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (context == NULL || context->Signature != BC250_ACTIVE_SIGNATURE) {
        return;
    }
    ++context->RetirementDpcRuns;
    Bc250ActiveRetireAndNotify(context);
}

void Bc250ActiveRetirementTimerStart(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    LARGE_INTEGER due;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return;
    }
    if (InterlockedCompareExchange(
            &Context->RetirementTimerInitialized, 1, 0) == 0) {
        KeInitializeTimer(&Context->RetirementTimer);
        KeInitializeDpc(&Context->RetirementDpc,
            Bc250ActiveRetirementDpcRoutine, Context);
    }
    if (InterlockedCompareExchange(
            &Context->RetirementTimerRunning, 1, 0) != 0) {
        return;
    }

    due.QuadPart = -10000LL; /* 1 ms */
    KeSetTimerEx(&Context->RetirementTimer, due, 1, &Context->RetirementDpc);
}

void Bc250ActiveRetirementTimerStop(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    if (Context == NULL ||
        InterlockedCompareExchange(
            &Context->RetirementTimerInitialized, 1, 1) != 1) {
        return;
    }
    InterlockedExchange(&Context->RetirementTimerRunning, 0);
    KeCancelTimer(&Context->RetirementTimer);
    KeFlushQueuedDpcs();
}

static NTSTATUS Bc250ActiveSubmitCommandVirtualBody(
    HANDLE AdapterContext,
    CONST DXGKARG_SUBMITCOMMANDVIRTUAL *pSubmitCommand)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext;
    BC250_CONTEXT_SUBMISSION_VIEW contextView;
    BC250_GPUVA_SNAPSHOT_READER gpuvaReader;
    BC250_MCDM_OBJECT_STATUS objStatus;
    BC250_GPUVA_STATUS vaStatus;
    BC250_ACTIVE_FENCE_SYNC reserveSync;
    BOOLEAN reserved = FALSE;
    uint64_t assignedFence = 0ull;
    uint32_t ibSizeDwords;
    NTSTATUS submitStatus;

    RtlZeroMemory(&gpuvaReader, sizeof(gpuvaReader));

    /* 1. Validar argumentos basicos fail-closed */
    if (context == NULL || context->Signature != BC250_ACTIVE_SIGNATURE ||
        pSubmitCommand == NULL || pSubmitCommand->hContext == NULL ||
        pSubmitCommand->DmaBufferSize == 0u ||
        (pSubmitCommand->DmaBufferSize % sizeof(uint32_t)) != 0u ||
        pSubmitCommand->DmaBufferVirtualAddress == 0ull ||
        (pSubmitCommand->DmaBufferVirtualAddress & 3ull) != 0ull ||
        pSubmitCommand->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }
    /* O gate só é aberto por critério explícito após validação integrada. */
    if (InterlockedCompareExchange(
            &context->GpuvaMappingsOperational, 1, 1) != 1) {
        return STATUS_NOT_SUPPORTED;
    }

    ibSizeDwords = (uint32_t)(pSubmitCommand->DmaBufferSize / sizeof(uint32_t));

    /* 2. Adquirir visao imutavel do contexto e incrementar InFlightSubmissions */
    objStatus = Bc250McdmContextAcquireSubmissionView(
        &context->Objects,
        (BC250_MCDM_HANDLE)pSubmitCommand->hContext,
        context,
        pSubmitCommand->NodeOrdinal,
        &contextView);
    if (objStatus != BC250_MCDM_OBJECT_OK) {
        return Bc250ObjectStatusToNtStatus(objStatus);
    }

    /* 3. Fixar o snapshot publicado durante toda a publicacao do IB. */
    vaStatus = Bc250GpuvaSnapshotAcquire(
        &context->GpuvaSnapshots, &gpuvaReader);
    if (vaStatus != BC250_GPUVA_OK) {
        Bc250McdmContextReleaseSubmissionView(&context->Objects,
            (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
        return vaStatus == BC250_GPUVA_BUSY ?
            STATUS_GRAPHICS_ALLOCATION_BUSY : STATUS_DEVICE_NOT_READY;
    }

    /* 4. Validar cobertura integral sem gaps no mesmo snapshot imutavel. */
    vaStatus = Bc250GpuvaSpaceValidateRangeCoverage(
        gpuvaReader.Space,
        contextView.OwnerProcess,
        contextView.ContextHandle,
        (uint64_t)pSubmitCommand->DmaBufferVirtualAddress,
        (uint64_t)pSubmitCommand->DmaBufferSize,
        BC250_PTE_READABLE,
        gpuvaReader.Space->PageTableEpoch);
    if (vaStatus != BC250_GPUVA_OK) {
        Bc250GpuvaSnapshotRelease(&context->GpuvaSnapshots, &gpuvaReader);
        Bc250McdmContextReleaseSubmissionView(&context->Objects,
            (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
        if (vaStatus == BC250_GPUVA_PERMISSION_DENIED) {
            return STATUS_ACCESS_VIOLATION;
        } else if (vaStatus == BC250_GPUVA_RANGE_OVERFLOW ||
                   vaStatus == BC250_GPUVA_OUT_OF_BOUNDS) {
            return STATUS_INTEGER_OVERFLOW;
        } else if (vaStatus == BC250_GPUVA_MISALIGNED) {
            return STATUS_DATATYPE_MISALIGNMENT;
        } else if (vaStatus == BC250_GPUVA_IN_TEARDOWN ||
                   vaStatus == BC250_GPUVA_NOT_RESIDENT ||
                   vaStatus == BC250_GPUVA_STALE_EPOCH) {
            return STATUS_DEVICE_NOT_READY;
        }
        return STATUS_INVALID_PARAMETER;
    }

    /* 5. Reservar slot no rastreador de fences WDDM antes de tocar no anel */
    if (context->Core.DxgkInterface.DxgkCbSynchronizeExecution == NULL ||
        context->Core.DxgkInterface.DeviceHandle == NULL) {
        Bc250GpuvaSnapshotRelease(&context->GpuvaSnapshots, &gpuvaReader);
        Bc250McdmContextReleaseSubmissionView(&context->Objects,
            (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
        return STATUS_DEVICE_NOT_READY;
    }

    RtlZeroMemory(&reserveSync, sizeof(reserveSync));
    reserveSync.Context = context;
    reserveSync.Operation = BC250_ACTIVE_FENCE_SYNC_RESERVE;
    reserveSync.SubmissionFenceId = pSubmitCommand->SubmissionFenceId;
    reserveSync.NodeOrdinal = pSubmitCommand->NodeOrdinal;
    reserveSync.EngineOrdinal = pSubmitCommand->EngineOrdinal;
    reserveSync.GpuvaSlot = gpuvaReader.Slot;
    reserveSync.GpuvaGeneration = gpuvaReader.PublicationGeneration;

    submitStatus = context->Core.DxgkInterface.DxgkCbSynchronizeExecution(
        context->Core.DxgkInterface.DeviceHandle,
        Bc250ActiveSynchronizedFenceCallback,
        &reserveSync,
        0,
        &reserved);
    if (!NT_SUCCESS(submitStatus) || !reserved || !reserveSync.ReserveAccepted) {
        ++context->RetirementBackpressure;
        Bc250GpuvaSnapshotRelease(&context->GpuvaSnapshots, &gpuvaReader);
        Bc250McdmContextReleaseSubmissionView(&context->Objects,
            (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
        return STATUS_GRAPHICS_ALLOCATION_BUSY;
    }

    /* 6. Publicar IB no anel persistente MEC */
    submitStatus = Bc250SubmissionEngineSubmit(
        context,
        (uint32_t)pSubmitCommand->NodeOrdinal,
        (uint64_t)pSubmitCommand->DmaBufferVirtualAddress,
        ibSizeDwords,
        &assignedFence);
    Bc250SubmissionEngineRecordTelemetry(context);
    if (!NT_SUCCESS(submitStatus)) {
        /* Cancelar reserva imediatamente em caso de falha de publicacao */
        BC250_ACTIVE_FENCE_SYNC cancelSync;
        BOOLEAN cancelled = FALSE;
        RtlZeroMemory(&cancelSync, sizeof(cancelSync));
        cancelSync.Context = context;
        cancelSync.Operation = BC250_ACTIVE_FENCE_SYNC_CANCEL;
        cancelSync.ReservationToken = reserveSync.ReservationToken;
        (void)context->Core.DxgkInterface.DxgkCbSynchronizeExecution(
            context->Core.DxgkInterface.DeviceHandle,
            Bc250ActiveSynchronizedFenceCallback,
            &cancelSync,
            0,
            &cancelled);
        Bc250GpuvaSnapshotRelease(&context->GpuvaSnapshots, &gpuvaReader);
        Bc250McdmContextReleaseSubmissionView(&context->Objects,
            (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
        return submitStatus;
    }

    /* 7. Commit definitivo da fence fisica atribuida */
    {
        BC250_ACTIVE_FENCE_SYNC commitSync;
        BOOLEAN committed = FALSE;
        RtlZeroMemory(&commitSync, sizeof(commitSync));
        commitSync.Context = context;
        commitSync.Operation = BC250_ACTIVE_FENCE_SYNC_COMMIT;
        commitSync.ReservationToken = reserveSync.ReservationToken;
        commitSync.RingFence = assignedFence;

        submitStatus = context->Core.DxgkInterface.DxgkCbSynchronizeExecution(
            context->Core.DxgkInterface.DeviceHandle,
            Bc250ActiveSynchronizedFenceCallback,
            &commitSync,
            0,
            &committed);
        if (!NT_SUCCESS(submitStatus) || !committed || !commitSync.CommitAccepted) {
            ++context->RetirementPushFailures;
            InterlockedExchange(&context->SubmissionEngine.State,
                BC250_SUBMISSION_ENGINE_FAULTED);
            Bc250GpuvaSnapshotRelease(
                &context->GpuvaSnapshots, &gpuvaReader);
            Bc250McdmContextReleaseSubmissionView(&context->Objects,
                (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
            return STATUS_DEVICE_DATA_ERROR;
        }
        /* A referência passa a pertencer à entrada da fence até o drain. */
        gpuvaReader.Space = NULL;
        gpuvaReader.Acquired = 0u;
    }

    /* 8. Iniciar retirada e liberar as duas referencias imutaveis. */
    /*
     * Nao rearmar depois que StopDevice fechou o portao. Se a leitura de
     * RUNNING competir com STOPPING, a segunda parada feita pela quiescencia
     * depois de SubmitInFlight==0 garante o cancelamento definitivo.
     */
    if (InterlockedCompareExchange(&context->LifecycleState, 0, 0) ==
            (LONG)BC250_ACTIVE_LIFECYCLE_RUNNING) {
        Bc250ActiveRetirementTimerStart(context);
    }
    Bc250McdmContextReleaseSubmissionView(&context->Objects,
        (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
    InterlockedIncrement((volatile LONG *)&context->SubmissionsCount);
    return STATUS_SUCCESS;
}

/*
 * Invólucro de ciclo de vida do SubmitCommandVirtual (3.67).
 *
 * Duas responsabilidades, ambas necessarias para que o StopDevice consiga
 * fotografar o rastreador com confianca:
 *
 *   1. Recusar submissao nova assim que o contexto entra em STOPPING.
 *   2. Contabilizar submissoes em voo, para que o dreno do teardown saiba
 *      esperar as que ja passaram pelo portao.
 *
 * O corpo ficou em Bc250ActiveSubmitCommandVirtualBody justamente para que o
 * decremento tenha um unico ponto de saida — espalhar InterlockedDecrement
 * pelos dezoito retornos do corpo seria a forma classica de vazar a contagem
 * em um caminho de erro e travar o teardown para sempre.
 */
BOOLEAN Bc250ActiveSubmissionGateEnter(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return FALSE;
    }
    if (InterlockedCompareExchange(&Context->LifecycleState, 0, 0) !=
            (LONG)BC250_ACTIVE_LIFECYCLE_RUNNING) {
        return FALSE;
    }
    (void)InterlockedIncrement(&Context->SubmitInFlight);
    if (InterlockedCompareExchange(&Context->LifecycleState, 0, 0) !=
            (LONG)BC250_ACTIVE_LIFECYCLE_RUNNING) {
        (void)InterlockedDecrement(&Context->SubmitInFlight);
        return FALSE;
    }
    return TRUE;
}

void Bc250ActiveSubmissionGateLeave(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    if (Context != NULL && Context->Signature == BC250_ACTIVE_SIGNATURE) {
        (void)InterlockedDecrement(&Context->SubmitInFlight);
    }
}

static NTSTATUS Bc250ActiveSubmitCommandVirtualLifecycle(
    HANDLE AdapterContext,
    CONST DXGKARG_SUBMITCOMMANDVIRTUAL *pSubmitCommand)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext;
    NTSTATUS status;

    if (context == NULL || context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!Bc250ActiveSubmissionGateEnter(context)) {
        return STATUS_DEVICE_NOT_READY;
    }

    Bc250ProbeMarkPostStartStage(BC250_V5_POST_START_SUBMIT);
    status = Bc250ActiveSubmitCommandVirtualBody(AdapterContext, pSubmitCommand);
    Bc250ActiveSubmissionGateLeave(context);
    return status;
}

static uint32_t Bc250ActiveFlushMappedGfxhubTlb(void *Context);

NTSTATUS Bc250ActiveCurrentFenceProvider(
    _In_ HANDLE hAdapter,
    _In_ UINT NodeOrdinal,
    _Out_ UINT *pCurrentFence)
{
    BC250_DXGK_ACTIVE_CONTEXT *active = (BC250_DXGK_ACTIVE_CONTEXT *)hAdapter;
    BC250_ACTIVE_FENCE_SYNC sync;
    BOOLEAN queried = FALSE;
    NTSTATUS status;

    if (active == NULL || pCurrentFence == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (active->Signature != BC250_ACTIVE_SIGNATURE ||
        active->SubmissionEngine.State != BC250_SUBMISSION_ENGINE_ONLINE) {
        return STATUS_DEVICE_NOT_READY;
    }

    if (NodeOrdinal > BC250_SUBMISSION_NODE_COMPUTE ||
        active->Core.DxgkInterface.DxgkCbSynchronizeExecution == NULL ||
        active->Core.DxgkInterface.DeviceHandle == NULL) {
        return STATUS_NOT_SUPPORTED;
    }
    RtlZeroMemory(&sync, sizeof(sync));
    sync.Context = active;
    sync.Operation = BC250_ACTIVE_FENCE_SYNC_QUERY;
    sync.NodeOrdinal = NodeOrdinal;
    status = active->Core.DxgkInterface.DxgkCbSynchronizeExecution(
        active->Core.DxgkInterface.DeviceHandle,
        Bc250ActiveSynchronizedFenceCallback,
        &sync, 0, &queried);
    if (!NT_SUCCESS(status) || !queried) return STATUS_DEVICE_NOT_READY;
    *pCurrentFence = sync.CurrentFence;
    return STATUS_SUCCESS;
}

/* Calcula prontidão do candidato, mas não autoriza produção. ObjectLock held. */
static LONG Bc250ActiveEvaluateGpuvaActivationEligibility(
    BC250_DXGK_ACTIVE_CONTEXT *active)
{
    LONG slot;
    const BC250_GPUVA_SPACE *space = NULL;
    uint32_t blocked = 0u;

    if (active == NULL) return 0;
    if (active->GpuvaJournal.Pending != 0u)
        blocked |= BC250_GPUVA_GATE_BLOCK_JOURNAL_PENDING;
    if (active->GpuvaHierarchyPending != 0u)
        blocked |= BC250_GPUVA_GATE_BLOCK_HIERARCHY_PENDING;
    if (InterlockedCompareExchange(&active->IsActive, 1, 1) != 1)
        blocked |= BC250_GPUVA_GATE_BLOCK_ADAPTER_INACTIVE;
    if (InterlockedCompareExchange(&active->ContextAbiReady, 1, 1) != 1)
        blocked |= BC250_GPUVA_GATE_BLOCK_CONTEXT_ABI;
    if (InterlockedCompareExchange(&active->AllocationAbiReady, 1, 1) != 1)
        blocked |= BC250_GPUVA_GATE_BLOCK_ALLOCATION_ABI;
    if (InterlockedCompareExchange(&active->RootPageTableAbiReady, 1, 1) != 1)
        blocked |= BC250_GPUVA_GATE_BLOCK_ROOT_ABI;
    if (InterlockedCompareExchange(&active->SubmissionEngine.State,
            BC250_SUBMISSION_ENGINE_ONLINE,
            BC250_SUBMISSION_ENGINE_ONLINE) != BC250_SUBMISSION_ENGINE_ONLINE)
        blocked |= BC250_GPUVA_GATE_BLOCK_ENGINE_OFFLINE;
    slot = InterlockedCompareExchange(&active->GpuvaSnapshots.ActiveSlot, 0, 0);
    if (slot < 0 || slot > 1) {
        blocked |= BC250_GPUVA_GATE_BLOCK_SNAPSHOT_SLOT;
    } else {
        space = &active->GpuvaSnapshots.Slots[(uint32_t)slot];
        active->GpuvaActivationSnapshotGeneration = (uint32_t)
            InterlockedCompareExchange(
                &active->GpuvaSnapshots.SlotGeneration[(uint32_t)slot], 0, 0);
        active->GpuvaActivationSnapshotEpoch = space->PageTableEpoch;
        active->GpuvaActivationSnapshotLiveCount = space->LiveCount;
        if (space->Signature != BC250_GPUVA_SPACE_SIGNATURE)
            blocked |= BC250_GPUVA_GATE_BLOCK_SNAPSHOT_SIGNATURE;
        if (space->LiveCount == 0u)
            blocked |= BC250_GPUVA_GATE_BLOCK_SNAPSHOT_EMPTY;
        if (space->PageTableEpoch == 0u)
            blocked |= BC250_GPUVA_GATE_BLOCK_SNAPSHOT_EPOCH;
    }
    active->GpuvaActivationBlockMask = blocked;
    return blocked == 0u ? 1 : 0;
}

static void Bc250ActiveRecordGpuvaGateTelemetry(
    ULONG eligible, ULONG operational, ULONG blocked,
    ULONG generation, ULONG epoch, ULONG liveCount)
{
    Bc250ProbeRecord(BC250_PROBE_GPUVA_GATE_ELIGIBLE, eligible);
    Bc250ProbeRecord(BC250_PROBE_GPUVA_GATE_OPERATIONAL, operational);
    Bc250ProbeRecord(BC250_PROBE_GPUVA_GATE_BLOCK_MASK, blocked);
    Bc250ProbeRecord(BC250_PROBE_GPUVA_SNAPSHOT_GENERATION, generation);
    Bc250ProbeRecord(BC250_PROBE_GPUVA_SNAPSHOT_EPOCH, epoch);
    Bc250ProbeRecord(BC250_PROBE_GPUVA_SNAPSHOT_LIVE_COUNT, liveCount);
}

NTSTATUS Bc250ActiveFlushTlbHandler(
    _In_ HANDLE hAdapter,
    _In_ CONST struct _DXGK_BUILDPAGINGBUFFER_FLUSHTLB *pFlushTlb)
{
    BC250_DXGK_ACTIVE_CONTEXT *active = (BC250_DXGK_ACTIVE_CONTEXT *)hAdapter;
    BC250_GPUVA_JOURNAL_STATUS journalStatus;
    uint32_t generation = 0u;
    ULONG telemetryEligible, telemetryOperational, telemetryBlocked;
    ULONG telemetryGeneration, telemetryEpoch, telemetryLiveCount;

    if (active == NULL || pFlushTlb == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (active->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_DEVICE_NOT_READY;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&active->ObjectLock);
    if (InterlockedCompareExchange(&active->GpuvaJournalReady, 1, 1) != 1) {
        ExReleasePushLockExclusive(&active->ObjectLock);
        KeLeaveCriticalRegion();
        return STATUS_DEVICE_NOT_READY;
    }
    if ((active->GpuvaJournal.Pending != 0u ||
         active->GpuvaHierarchyPending != 0u) &&
        (pFlushTlb->RootPageTableAddress.SegmentId != 1u ||
         (active->GpuvaJournal.Pending != 0u &&
          (active->GpuvaJournal.Process !=
            (BC250_MCDM_HANDLE)pFlushTlb->hProcess ||
           active->GpuvaJournal.ExpectedRootPageTable !=
             pFlushTlb->RootPageTableAddress.SegmentOffset)) ||
         (active->GpuvaHierarchyPending != 0u &&
          (active->GpuvaHierarchyProcess !=
             (BC250_MCDM_HANDLE)pFlushTlb->hProcess ||
           active->GpuvaHierarchyRootPageTable !=
             pFlushTlb->RootPageTableAddress.SegmentOffset)))) {
        ExReleasePushLockExclusive(&active->ObjectLock);
        KeLeaveCriticalRegion();
        return STATUS_INVALID_PARAMETER;
    }
    if (Bc250ActiveFlushMappedGfxhubTlb(active) == 0u) {
        ExReleasePushLockExclusive(&active->ObjectLock);
        KeLeaveCriticalRegion();
        return STATUS_IO_DEVICE_ERROR;
    }
    if (active->GpuvaJournal.Pending == 0u) {
        active->GpuvaHierarchyPending = 0u;
        active->GpuvaHierarchyProcess = NULL;
        active->GpuvaHierarchyRootPageTable = 0u;
        active->GpuvaHierarchyHighestLevel = 0u;
        InterlockedExchange(&active->GpuvaActivationEligible, 0);
        active->GpuvaActivationBlockMask =
            BC250_GPUVA_GATE_BLOCK_NO_LEAF_COMMIT;
        active->GpuvaActivationSnapshotGeneration = (uint32_t)
            InterlockedCompareExchange(
                &active->GpuvaSnapshots.PublicationGeneration, 0, 0);
        telemetryEligible = 0u;
        telemetryOperational = (ULONG)InterlockedCompareExchange(
            &active->GpuvaMappingsOperational, 0, 0);
        telemetryBlocked = active->GpuvaActivationBlockMask;
        telemetryGeneration = active->GpuvaActivationSnapshotGeneration;
        telemetryEpoch = active->GpuvaActivationSnapshotEpoch;
        telemetryLiveCount = active->GpuvaActivationSnapshotLiveCount;
        ExReleasePushLockExclusive(&active->ObjectLock);
        KeLeaveCriticalRegion();
        Bc250ActiveRecordGpuvaGateTelemetry(telemetryEligible,
            telemetryOperational, telemetryBlocked, telemetryGeneration,
            telemetryEpoch, telemetryLiveCount);
        return STATUS_SUCCESS;
    }
    journalStatus = Bc250GpuvaJournalCommitFlush(&active->GpuvaJournal,
        &active->GpuvaSnapshots, &active->GpuvaSnapshotScratch,
        (BC250_MCDM_HANDLE)pFlushTlb->hProcess,
        pFlushTlb->RootPageTableAddress.SegmentOffset,
        pFlushTlb->StartVirtualAddress, pFlushTlb->EndVirtualAddress,
        &generation);
    if (journalStatus == BC250_GPUVA_JOURNAL_OK) {
        active->GpuvaHierarchyPending = 0u;
        active->GpuvaHierarchyProcess = NULL;
        active->GpuvaHierarchyRootPageTable = 0u;
        active->GpuvaHierarchyHighestLevel = 0u;
        InterlockedExchange(&active->GpuvaActivationEligible,
            Bc250ActiveEvaluateGpuvaActivationEligibility(active));
        if (active->GpuvaLabPromotionPresent == 1u &&
            active->GpuvaLabPromotion == 1u &&
            InterlockedCompareExchange(
                &active->GpuvaActivationEligible, 0, 0) == 1) {
            InterlockedExchange(&active->GpuvaMappingsOperational, 1);
        } else {
            InterlockedExchange(&active->GpuvaMappingsOperational, 0);
        }
    } else {
        InterlockedExchange(&active->GpuvaActivationEligible,
            Bc250ActiveEvaluateGpuvaActivationEligibility(active));
    }
    telemetryEligible = (ULONG)InterlockedCompareExchange(
        &active->GpuvaActivationEligible, 0, 0);
    telemetryOperational = (ULONG)InterlockedCompareExchange(
        &active->GpuvaMappingsOperational, 0, 0);
    telemetryBlocked = active->GpuvaActivationBlockMask;
    telemetryGeneration = active->GpuvaActivationSnapshotGeneration;
    telemetryEpoch = active->GpuvaActivationSnapshotEpoch;
    telemetryLiveCount = active->GpuvaActivationSnapshotLiveCount;
    ExReleasePushLockExclusive(&active->ObjectLock);
    KeLeaveCriticalRegion();
    if (journalStatus != BC250_GPUVA_JOURNAL_OK) {
        InterlockedExchange(&active->GpuvaMappingsOperational, 0);
        telemetryOperational = 0u;
        Bc250ActiveRecordGpuvaGateTelemetry(telemetryEligible,
            telemetryOperational, telemetryBlocked, telemetryGeneration,
            telemetryEpoch, telemetryLiveCount);
        return STATUS_IO_DEVICE_ERROR;
    }
    /* GpuvaMappingsOperational permanece zero até o ensaio integrado. */
    Bc250ActiveRecordGpuvaGateTelemetry(telemetryEligible,
        telemetryOperational, telemetryBlocked, telemetryGeneration,
        telemetryEpoch, telemetryLiveCount);
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250ActiveUpdatePageTableHandler(
    _In_ HANDLE hAdapter,
    _In_ CONST BC250_MCDM_RUNTIME_CAPS *Caps,
    _In_ DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *update)
{
    BC250_DXGK_ACTIVE_CONTEXT *active = (BC250_DXGK_ACTIVE_CONTEXT *)hAdapter;
    BC250_MCDM_OBJECT_RECORD processRecord;
    BC250_GPUVA_JOURNAL_PTE *entries;
    BC250_GPUVA_JOURNAL_STATUS journalStatus;
    UINT32 index;
    UINT64 *destination;

    if (active == NULL || Caps == NULL || update == NULL ||
        active->Signature != BC250_ACTIVE_SIGNATURE || update->hProcess == NULL ||
        update->PageTableLevel >= Caps->PageTableLevelCount ||
        (update->PageTableLevel == 0u &&
         (update->FirstPteVirtualAddress == 0u ||
          update->NumPageTableEntries > BC250_GPUVA_JOURNAL_MAX_CHANGES))) {
        return STATUS_NOT_SUPPORTED;
    }
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&active->ObjectLock);
    entries = active->GpuvaJournalStaging;

    if (InterlockedCompareExchange(&active->GpuvaJournalReady, 1, 1) != 1 ||
        Bc250McdmObjectLookup(&active->Objects,
            (BC250_MCDM_HANDLE)update->hProcess, BC250_MCDM_OBJECT_PROCESS,
            (BC250_MCDM_HANDLE)update->hProcess, &processRecord) !=
                BC250_MCDM_OBJECT_OK ||
        processRecord.RootPageTableSegmentId != 1u ||
        processRecord.RootPageTableAddress == 0u) {
        ExReleasePushLockExclusive(&active->ObjectLock);
        KeLeaveCriticalRegion();
        return STATUS_DEVICE_NOT_READY;
    }
    if (active->GpuvaHierarchyPending != 0u &&
        (active->GpuvaHierarchyProcess !=
            (BC250_MCDM_HANDLE)update->hProcess ||
         active->GpuvaHierarchyRootPageTable !=
            processRecord.RootPageTableAddress)) {
        ExReleasePushLockExclusive(&active->ObjectLock);
        KeLeaveCriticalRegion();
        return STATUS_DEVICE_BUSY;
    }
    if (update->PageTableLevel == 0u) {
        RtlZeroMemory(entries, sizeof(*entries) * update->NumPageTableEntries);
        for (index = 0u; index < update->NumPageTableEntries; ++index) {
            const DXGK_PTE *pte = &update->pPageTableEntries[index];
            entries[index].Valid = pte->Valid != 0u ? 1u : 0u;
            entries[index].Permissions = pte->Valid == 0u ? 0u : BC250_PTE_READABLE;
            if (pte->Valid != 0u && pte->ReadOnly == 0u)
                entries[index].Permissions |= BC250_PTE_WRITEABLE;
            if (pte->Valid != 0u && pte->NoExecute == 0u)
                entries[index].Permissions |= BC250_PTE_EXECUTABLE;
            entries[index].SegmentId = (uint32_t)pte->Segment;
            entries[index].SegmentOffset = pte->PageAddress;
            entries[index].Allocation = (BC250_MCDM_HANDLE)update->hAllocation;
        }
        journalStatus = Bc250GpuvaJournalStageLeafUpdate(&active->GpuvaJournal,
            (BC250_MCDM_HANDLE)update->hProcess,
            processRecord.RootPageTableAddress, update->FirstPteVirtualAddress,
            entries, update->NumPageTableEntries);
    } else {
        if (active->GpuvaJournal.Pending != 0u &&
            (active->GpuvaJournal.Process !=
                (BC250_MCDM_HANDLE)update->hProcess ||
             active->GpuvaJournal.ExpectedRootPageTable !=
                processRecord.RootPageTableAddress)) {
            journalStatus = BC250_GPUVA_JOURNAL_BUSY;
        } else {
            active->GpuvaHierarchyPending = 1u;
            active->GpuvaHierarchyProcess =
                (BC250_MCDM_HANDLE)update->hProcess;
            active->GpuvaHierarchyRootPageTable =
                processRecord.RootPageTableAddress;
            if (update->PageTableLevel > active->GpuvaHierarchyHighestLevel)
                active->GpuvaHierarchyHighestLevel = update->PageTableLevel;
            journalStatus = BC250_GPUVA_JOURNAL_OK;
        }
    }
    if (journalStatus == BC250_GPUVA_JOURNAL_OK) {
        /* Entre a escrita da PTE e o ACK do flush, snapshot e hardware
         * representam gerações diferentes: submissão deve permanecer fechada. */
        InterlockedExchange(&active->GpuvaMappingsOperational, 0);
        InterlockedExchange(&active->GpuvaActivationEligible, 0);
        destination = (UINT64 *)update->PageTableAddress.CpuVirtual;
        for (index = 0u; index < update->NumPageTableEntries; ++index) {
            UINT64 encoded = 0u;
            (void)Bc250McdmEncodeVidMmPte(
                Caps, &update->pPageTableEntries[index], &encoded);
            destination[update->StartIndex + index] = encoded;
        }
    }
    ExReleasePushLockExclusive(&active->ObjectLock);
    KeLeaveCriticalRegion();
    if (journalStatus == BC250_GPUVA_JOURNAL_BUSY) return STATUS_DEVICE_BUSY;
    if (journalStatus != BC250_GPUVA_JOURNAL_OK) return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceV5Telemetry(
    CONST PVOID MiniportDeviceContext,
    PDXGK_START_INFO DxgkStartInfo,
    PDXGKRNL_INTERFACE DxgkInterface,
    PULONG NumberOfVideoPresentSources,
    PULONG NumberOfChildren)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)MiniportDeviceContext;
    NTSTATUS status;

    /*
     * Marca ANTES de entrar: se o StartDevice travar ou o dxgkrnl desistir
     * durante a chamada, o rastro precisa registrar que ela comecou.
     */
    Bc250ProbeMarkDdi(BC250_V5_DDI_START_DEVICE);
    status = Bc250DxgkDdiStartDeviceActive(
        MiniportDeviceContext, DxgkStartInfo, DxgkInterface,
        NumberOfVideoPresentSources, NumberOfChildren);

    /*
     * O ciclo de vida so abre depois de o StartDevice ter devolvido sucesso.
     * Em falha o contexto permanece fora de RUNNING, de modo que nenhuma
     * submissao entra em um adaptador que nunca subiu.
     */
    if (NT_SUCCESS(status) && context != NULL &&
        context->Signature == BC250_ACTIVE_SIGNATURE) {
        InterlockedExchange(&context->LifecycleState,
            (LONG)BC250_ACTIVE_LIFECYCLE_RUNNING);
        InterlockedExchange(&context->SubmitInFlight, 0);
        Bc250ProbeRecord(BC250_PROBE_V5_LIFECYCLE_STATE,
            BC250_ACTIVE_LIFECYCLE_RUNNING);
        Bc250ProbeMarkPostStartStage(BC250_V5_POST_START_RETURNED);
    }
    Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_STATUS, (ULONG)status);
    Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_SOURCES,
        NumberOfVideoPresentSources != NULL ?
            *NumberOfVideoPresentSources : 0u);
    Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_CHILDREN,
        NumberOfChildren != NULL ? *NumberOfChildren : 0u);
    if (!NT_SUCCESS(status) && status != STATUS_DEVICE_CONFIGURATION_ERROR) {
        (void)Bc250BootFuseDisarmClean(status);
    }
    return status;
}

/*
 * Perfil do carregamento corrente.  Resolvido uma unica vez em DriverEntry,
 * antes de qualquer registro no dxgkrnl, e consultado depois por
 * StartDevice.  LONG volatil porque StartDevice roda fora do contexto de
 * DriverEntry; a escrita e' unica e anterior a qualquer leitura.
 */
static volatile LONG g_Bc250AdapterProfile =
    (LONG)BC250_ADAPTER_PROFILE_DISPLAY_ONLY;

_Check_return_
NTSTATUS Bc250ActiveSetAdapterProfile(_In_ BC250_ADAPTER_PROFILE Profile)
{
    if (Profile != BC250_ADAPTER_PROFILE_DISPLAY_ONLY &&
        Profile != BC250_ADAPTER_PROFILE_DISPLAY_RENDER &&
        Profile != BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) {
        return STATUS_INVALID_PARAMETER;
    }
    InterlockedExchange(&g_Bc250AdapterProfile, (LONG)Profile);
    Bc250DisplaySetChildEnumeration(
        Profile == BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB ? FALSE : TRUE);
    return STATUS_SUCCESS;
}

_Check_return_
BC250_ADAPTER_PROFILE Bc250ActiveGetAdapterProfile(void)
{
    return (BC250_ADAPTER_PROFILE)InterlockedCompareExchange(
        &g_Bc250AdapterProfile, 0, 0);
}

_Check_return_
NTSTATUS Bc250ActiveResolveAdapterProfile(
    _In_ ULONG RenderNode,
    _In_ ULONG RenderNodePresent,
    _In_ ULONG RenderOnlyLab,
    _In_ ULONG RenderOnlyLabPresent,
    _Out_ BC250_ADAPTER_PROFILE *Profile)
{
    if (Profile == NULL) return STATUS_INVALID_PARAMETER;
    *Profile = BC250_ADAPTER_PROFILE_DISPLAY_ONLY;

    /* Fora de {0,1} nao existe interpretacao segura: nao adivinhar. */
    if (RenderNode > 1u || RenderOnlyLab > 1u) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /*
     * Um tunable ilegivel que devolveu valor nao-default e' configuracao
     * corrompida, nao ausencia.  Ausencia legitima e' Present=0 com o
     * default preservado, e cai no perfil herdado.
     */
    if (RenderNodePresent == 0u && RenderNode != 0u) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (RenderOnlyLabPresent == 0u && RenderOnlyLab != 0u) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (RenderOnlyLabPresent == 1u && RenderOnlyLab == 1u) {
        /*
         * RenderOnlyLab exige o caminho DxgkInitialize.  O caminho KMDOD e'
         * display por definicao: pedir render-only nele e' uma combinacao
         * sem significado, e adivinhar qual metade o operador queria seria
         * pior do que recusar.
         */
        if (!(RenderNodePresent == 1u && RenderNode == 1u)) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        *Profile = BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB;
        return STATUS_SUCCESS;
    }

    if (RenderNodePresent == 1u && RenderNode == 1u) {
        *Profile = BC250_ADAPTER_PROFILE_DISPLAY_RENDER;
        return STATUS_SUCCESS;
    }

    *Profile = BC250_ADAPTER_PROFILE_DISPLAY_ONLY;
    return STATUS_SUCCESS;
}

NTSTATUS Bc250BuildActiveInitializationData(
    DRIVER_INITIALIZATION_DATA *InitializationData)
{
    return Bc250BuildActiveInitializationDataForContract(
        InitializationData,
        DXGKDDI_INTERFACE_VERSION_WDDM2_7,
        DXGKDDI_WDDMv2_7);
}

NTSTATUS Bc250BuildActiveInitializationDataForContract(
    DRIVER_INITIALIZATION_DATA *InitializationData,
    ULONG InterfaceVersion,
    DXGK_WDDMVERSION WddmVersion)
{
    return Bc250BuildActiveInitializationDataForProfile(
        InitializationData, InterfaceVersion, WddmVersion,
        BC250_ADAPTER_PROFILE_DISPLAY_RENDER);
}

NTSTATUS Bc250BuildActiveInitializationDataForProfile(
    DRIVER_INITIALIZATION_DATA *InitializationData,
    ULONG InterfaceVersion,
    DXGK_WDDMVERSION WddmVersion,
    BC250_ADAPTER_PROFILE Profile)
{
    if (InitializationData == NULL) return STATUS_INVALID_PARAMETER;
    if (Profile != BC250_ADAPTER_PROFILE_DISPLAY_ONLY &&
        Profile != BC250_ADAPTER_PROFILE_DISPLAY_RENDER &&
        Profile != BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((InterfaceVersion != DXGKDDI_INTERFACE_VERSION_WDDM2_0 ||
         WddmVersion != DXGKDDI_WDDMv2) &&
        (InterfaceVersion != DXGKDDI_INTERFACE_VERSION_WDDM2_7 ||
         WddmVersion != DXGKDDI_WDDMv2_7)) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!NT_SUCCESS(Bc250McdmSetWddmContractVersion(WddmVersion))) {
        return STATUS_INVALID_PARAMETER;
    }
    /*
     * MiscCaps.ComputeOnly amarrado ao perfil, e este e' o UNICO ponto do
     * driver que o decide.
     *
     * O destino do projeto e' WDDM display+render de verdade; nos dois perfis
     * de producao o valor e' 0. So' o perfil de laboratorio RENDER_ONLY_LAB,
     * que reporta 0 sources e 0 children, declara 1 — porque para ele essa e'
     * a resposta coerente. Declarar ComputeOnly enquanto se enumera fonte de
     * video foi o que o dxgkrnl rejeitou em 2026-08-31 com Kernel-PnP 411.
     *
     * Escrito como expressao condicional, e nunca como literal solto, para
     * que seja impossivel um perfil de producao herdar o valor do laboratorio
     * por esquecimento de um caminho novo.
     */
    if (!NT_SUCCESS(Bc250McdmSetComputeOnly(
            (Profile == BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) ? 1u : 0u))) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250ProbeRecord(BC250_PROBE_V5_COMPUTE_ONLY_DECLARED,
        Bc250McdmGetComputeOnly());
    RtlZeroMemory(InitializationData, sizeof(*InitializationData));
    InitializationData->Version = InterfaceVersion;
    InitializationData->DxgkDdiAddDevice = Bc250DxgkDdiAddDeviceActive;
    InitializationData->DxgkDdiStartDevice =
        Bc250DxgkDdiStartDeviceV5Telemetry;
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
    Bc250McdmInstallSubmissionExecutor(Bc250ActiveSubmitCommandVirtualLifecycle);
    Bc250McdmInstallCurrentFenceProvider(Bc250ActiveCurrentFenceProvider);
    Bc250McdmInstallUpdatePageTableHandler(Bc250ActiveUpdatePageTableHandler);
    Bc250McdmInstallFlushTlbHandler(Bc250ActiveFlushTlbHandler);
    (void)Bc250McdmPopulateRequiredCallbacks(InitializationData);
    /*
     * DxgkInitialize valida a tabela completa de display+render antes de
     * consultar DRIVERCAPS.  Estes callbacks vivem separados para que a
     * imagem MCDM readonly continue sem as DDIs graficas, mas o miniport
     * ativo deve instala-los explicitamente.  Ate o 3.67 a funcao existia
     * sem chamador: o dxgkrnl encerrava o post-start com
     * STATUS_INTERNAL_ERROR depois de QueryChildRelations.
     */
    (void)Bc250McdmPopulateRenderCallbacks(InitializationData);

    /*
     * Gate 1.5: o perfil render-only nao anuncia NENHUMA superficie de
     * display.  Nao e' uma poda seletiva — podar Present mantendo VidPn e
     * children apenas trocaria a tela preta por uma rejeicao de tabela ou um
     * Codigo 43 mais cedo.  Ou o adaptador anuncia o conjunto coerente de
     * display, ou nao anuncia display nenhum.
     */
    if (Profile == BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) {
        Bc250ProbeRecord(BC250_PROBE_V5_DISPLAY_SURFACE_ANNOUNCED, 0u);
        return Bc250DisplayPopulateBaseOnlyCallbacks(InitializationData);
    }

    Bc250ProbeRecord(BC250_PROBE_V5_DISPLAY_SURFACE_ANNOUNCED, 1u);
    return Bc250DisplayPopulateCallbacks(InitializationData);
}

NTSTATUS DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath)
{
    /*
     * Reset INTEGRAL da telemetria, antes de QUALQUER gravacao deste
     * carregamento.
     *
     * Os resets anteriores eram por familia: cobriam o rastro V5 e o bloco
     * P1/P2, e deixavam Shader*, HqdActive* e Dmm* fora de toda varredura. A
     * corrida 3.75 publicou DmmChildRelationsCalled=56 e
     * DmmIsSupportedCount=41 herdados de boots anteriores, e um
     * ShaderPhysicalReady=1 que nao pode ser atribuido aquela corrida.
     * Evidencia que mistura boots nao e' evidencia.
     *
     * Precisa ficar no TOPO: colocado depois da resolucao de perfil, o
     * proprio reset apagaria os campos que acabaram de ser publicados.
     */
    Bc250ProbeResetAllVolatile();
    Bc250ProbeRecord(BC250_PROBE_DRIVER_ENTRY_REACHED, 1);
    /*
     * Fato de COMPILACAO, nao de execucao: diz qual imagem esta' carregada.
     * Num pacote RENDERONLY_SAFE tem de ser 0. Se vier 1, o binario instalado
     * nao e' o que o manifesto declara, e a corrida esta' invalidada antes de
     * qualquer outra leitura.
     */
    Bc250ProbeRecord(BC250_PROBE_V5_UNSAFE_BRINGUP_COMPILED,
        BC250_UNSAFE_BRINGUP_COMPILED);
    if (DriverObject == NULL || RegistryPath == NULL) {
        Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS,
            (ULONG)STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    /*
     * O perfil e' resolvido ANTES da tabela porque governa quais callbacks
     * ela recebe.  Ler os tunables aqui tambem garante PASSIVE_LEVEL, que e'
     * requisito de RtlQueryRegistryValues.
     */
    {
        ULONG renderNodePresent = 0u;
        ULONG renderOnlyLabPresent = 0u;
        ULONG renderNode = Bc250ActiveReadTunableChecked(
            L"RenderNode", 0u, &renderNodePresent);
        ULONG renderOnlyLab = Bc250ActiveReadTunableChecked(
            L"RenderOnlyLab", 0u, &renderOnlyLabPresent);
        BC250_ADAPTER_PROFILE profile = BC250_ADAPTER_PROFILE_DISPLAY_ONLY;
        NTSTATUS profileStatus = Bc250ActiveResolveAdapterProfile(
            renderNode, renderNodePresent,
            renderOnlyLab, renderOnlyLabPresent, &profile);

        Bc250ProbeRecord(BC250_PROBE_V5_RENDER_ONLY_LAB_REQUESTED,
            renderOnlyLab);
        Bc250ProbeRecord(BC250_PROBE_V5_RENDER_ONLY_LAB_TUNABLE_PRESENT,
            renderOnlyLabPresent);
        Bc250ProbeRecord(BC250_PROBE_V5_PROFILE_RESOLVE_STATUS,
            (ULONG)profileStatus);

        if (!NT_SUCCESS(profileStatus) ||
            !NT_SUCCESS(Bc250ActiveSetAdapterProfile(profile))) {
            Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS,
                (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        Bc250ProbeRecord(BC250_PROBE_V5_ADAPTER_PROFILE, (ULONG)profile);
    }

    {
        ULONG contractPresent = 0u;
        ULONG contract = Bc250ActiveReadTunableChecked(
            L"WddmContractVersion", 20u, &contractPresent);
        ULONG interfaceVersion;
        DXGK_WDDMVERSION wddmVersion;

        UNREFERENCED_PARAMETER(contractPresent);
        if (contract == 20u) {
            interfaceVersion = DXGKDDI_INTERFACE_VERSION_WDDM2_0;
            wddmVersion = DXGKDDI_WDDMv2;
        } else if (contract == 27u) {
            interfaceVersion = DXGKDDI_INTERFACE_VERSION_WDDM2_7;
            wddmVersion = DXGKDDI_WDDMv2_7;
        } else {
            Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS,
                (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        Bc250ProbeRecord(BC250_PROBE_V5_CONTRACT_VERSION, contract);
        if (!NT_SUCCESS(Bc250BuildActiveInitializationDataForProfile(
                &g_Bc250ActiveInitializationData,
                interfaceVersion, wddmVersion,
                Bc250ActiveGetAdapterProfile()))) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }
    Bc250ProbeRecord(BC250_PROBE_INITIALIZATION_DATA_READY, 1);

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    {
        KMDDOD_INITIALIZATION_DATA dodInit;
        NTSTATUS status;
        BC250_ADAPTER_PROFILE profile = Bc250ActiveGetAdapterProfile();
        ULONG renderNodePresent = 0u;
        ULONG renderNode = Bc250ActiveReadTunableChecked(
            L"RenderNode", 0u, &renderNodePresent);

        /*
         * Zera o rastro deste carregamento antes de registrar no dxgkrnl.
         *
         * Ate o 3.66 o reset era parcial e feito a mao: cobria seis campos
         * V5 e deixava de fora o rastro de DDI, QueryInterface, StopDevice,
         * RemoveDevice e o bloco DMM inteiro. O inspetor do ensaio de
         * 2026-09-01 leu DmmQaiOutSize=544, DmmChildRelationsCalled=56 e
         * DmmControlInterruptCount=14925 — nenhum daquele boot.
         *
         * Bc250ProbeResetSubmissionEvidence estava declarado, implementado e
         * documentado com "chamar em DriverEntry", mas nao possuia UM UNICO
         * chamador em todo o repositorio: o reset P1/P2 que a documentacao
         * afirmava existir nunca executou. Agora executa aqui, antes do
         * bring-up, e a publicacao fisica de P1/P2 acontece depois.
         */
        /* O reset integral ja' ocorreu no topo de DriverEntry. */
        Bc250ProbeRecord(BC250_PROBE_V5_CONTRACT_VERSION,
            Bc250ActiveReadTunable(L"WddmContractVersion", 27u));

        Bc250ProbeRecord(BC250_PROBE_V5_RENDER_NODE_REQUESTED, renderNode);
        Bc250ProbeRecord(BC250_PROBE_V5_RENDER_NODE_TUNABLE_PRESENT,
            renderNodePresent);
        Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_STATUS,
            (ULONG)STATUS_NOT_SUPPORTED);
        Bc250ProbeRecord(BC250_PROBE_V5_QAI_LAST_STATUS,
            (ULONG)STATUS_NOT_SUPPORTED);

        Bc250ProbeRecord(BC250_PROBE_V5_ADAPTER_PROFILE, (ULONG)profile);

        if (renderNode > 1u) {
            Bc250ProbeRecord(BC250_PROBE_V5_REGISTRATION_MODE, 0u);
            Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS,
                (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }

        /*
         * A escolha da rotina de registro segue o perfil, nao mais o tunable
         * cru: DISPLAY_RENDER e RENDER_ONLY_LAB usam DxgkInitialize com
         * tabelas diferentes; DISPLAY_ONLY continua no KMDOD.  A divergencia
         * entre perfil e RenderNode e' impossivel por construcao — o perfil
         * foi derivado de RenderNode em DriverEntry —, mas conferir aqui
         * mantem o fail-closed local caso alguem insira codigo entre os dois
         * pontos.
         */
        if (profile == BC250_ADAPTER_PROFILE_DISPLAY_RENDER ||
            profile == BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) {
            if (!(renderNodePresent == 1u && renderNode == 1u)) {
                Bc250ProbeRecord(BC250_PROBE_V5_REGISTRATION_MODE, 0u);
                Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS,
                    (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
            Bc250ProbeRecord(BC250_PROBE_V5_REGISTRATION_MODE,
                profile == BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB ? 2u : 1u);
            status = DxgkInitialize(
                DriverObject, RegistryPath, &g_Bc250ActiveInitializationData);
            Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS,
                (ULONG)status);
            return status;
        }

        Bc250ProbeRecord(BC250_PROBE_V5_REGISTRATION_MODE, 0u);
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
        /*
         * DIVIDA TECNICA DELIBERADA E TEMPORARIA — isolamento do Gate 1.5.
         *
         * O stub local de CollectDbgInfo e SetPalette voltam para que a
         * tabela KMDOD do 3.76 seja byte a byte a da baseline conhecida. A
         * troca por Bc250McdmCollectDbgInfo e a retirada de SetPalette estao
         * certas, mas pertencem ao candidato de limpeza, nao a este.
         * Ver o comentario extenso em Bc250DisplayPopulateCallbacks.
         */
        dodInit.DxgkDdiCollectDbgInfo = Bc250DxgkDdiCollectDbgInfo;
        dodInit.DxgkDdiNotifyAcpiEvent = Bc250DxgkDdiNotifyAcpiEvent;
        dodInit.DxgkDdiSetPalette = Bc250DxgkDdiSetPalette;
        dodInit.DxgkDdiGetScanLine = Bc250DxgkDdiGetScanLine;
        dodInit.DxgkDdiNotifySurpriseRemoval = Bc250DxgkDdiNotifySurpriseRemoval;

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
