#include "../wdk/bc250_mmio_topology_transport.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

static unsigned long g_failureLine;
#define CHECK(Expression) do { if (!(Expression)) { g_failureLine = __LINE__; return 0; } } while (0)
#define BC250_TEST_MMIO_BYTES 0x00060000u

static unsigned char g_mmio[BC250_TEST_MMIO_BYTES];
static uint32_t g_mapCalls;
static uint32_t g_unmapCalls;
static uint32_t g_failMap;
static uint32_t g_failUnmap;

static NTSTATUS APIENTRY Bc250TestMapMemory(
    HANDLE DeviceHandle, PHYSICAL_ADDRESS TranslatedAddress, ULONG Length,
    BOOLEAN InIoSpace, BOOLEAN MapToUserMode, MEMORY_CACHING_TYPE CacheType,
    PVOID *VirtualAddress)
{
    ++g_mapCalls;
    if (DeviceHandle != (HANDLE)(ULONG_PTR)0x250u ||
        TranslatedAddress.QuadPart != 0xF0000000ll ||
        Length != BC250_TEST_MMIO_BYTES || InIoSpace != FALSE ||
        MapToUserMode != FALSE || CacheType != MmNonCached ||
        VirtualAddress == NULL) return STATUS_INVALID_PARAMETER;
    if (g_failMap != 0) return STATUS_DEVICE_NOT_READY;
    *VirtualAddress = g_mmio;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250TestUnmapMemory(
    HANDLE DeviceHandle, PVOID VirtualAddress)
{
    ++g_unmapCalls;
    if (DeviceHandle != (HANDLE)(ULONG_PTR)0x250u ||
        VirtualAddress != g_mmio) return STATUS_INVALID_PARAMETER;
    return g_failUnmap != 0 ? STATUS_DEVICE_NOT_READY : STATUS_SUCCESS;
}

static int Bc250Prepare(
    DXGKRNL_INTERFACE *Interface,
    BC250_RESOLVED_BAR_RESOURCE *Bar,
    BC250_MMIO_TOPOLOGY_CONTEXT *Context)
{
    uint32_t *value;

    memset(g_mmio, 0, sizeof(g_mmio));
    g_mapCalls = g_unmapCalls = g_failMap = g_failUnmap = 0;
    memset(Interface, 0, sizeof(*Interface));
    Interface->Size = sizeof(*Interface);
    Interface->DeviceHandle = (HANDLE)(ULONG_PTR)0x250u;
    Interface->DxgkCbMapMemory = Bc250TestMapMemory;
    Interface->DxgkCbUnmapMemory = Bc250TestUnmapMemory;
    memset(Bar, 0, sizeof(*Bar));
    Bar->Index = 5u;
    Bar->Start = 0xF0000000ull;
    Bar->Length = BC250_TEST_MMIO_BYTES;
    CHECK(Bc250MmioTopologyInitialize(Context) == BC250_MMIO_TOPOLOGY_OK);
    value = (uint32_t *)(void *)(g_mmio +
        BC250_RCC_CONFIG_MEMSIZE_BYTE_OFFSET);
    *value = 0x200u;
    value = (uint32_t *)(void *)(g_mmio +
        BC250_GCMC_VM_FB_LOCATION_BASE_BYTE_OFFSET);
    *value = 0xF400u;
    value = (uint32_t *)(void *)(g_mmio +
        BC250_GCMC_VM_FB_LOCATION_TOP_BYTE_OFFSET);
    *value = 0xF41Fu;
    value = (uint32_t *)(void *)(g_mmio +
        BC250_GCMC_VM_FB_OFFSET_BYTE_OFFSET);
    *value = 0x450u;
    value = (uint32_t *)(void *)(g_mmio +
        BC250_DRIVER_SCRATCH_0_BYTE_OFFSET);
    *value = 0x1FFF0000u;
    value = (uint32_t *)(void *)(g_mmio +
        BC250_DRIVER_SCRATCH_1_BYTE_OFFSET);
    *value = 0u;
    value = (uint32_t *)(void *)(g_mmio +
        BC250_DRIVER_SCRATCH_2_BYTE_OFFSET);
    *value = 0x10000u;
#define SET_REG(Offset, Value) do { \
    value = (uint32_t *)(void *)(g_mmio + (Offset)); \
    *value = (Value); \
} while (0)
    SET_REG(BC250_REG_GCVM_PT_BASE_LO, 0x11111111u);
    SET_REG(BC250_REG_GCVM_PT_BASE_HI, 0x22222222u);
    SET_REG(BC250_REG_GCVM_PT_START_LO, 0x33333333u);
    SET_REG(BC250_REG_GCVM_PT_START_HI, 0x44444444u);
    SET_REG(BC250_REG_GCVM_PT_END_LO, 0x55555555u);
    SET_REG(BC250_REG_GCVM_PT_END_HI, 0x66666666u);
    SET_REG(BC250_REG_CP_RB0_BASE, 0x77777777u);
    SET_REG(BC250_REG_CP_RB0_BASE_HI, 0x88888888u);
    SET_REG(BC250_REG_CP_RB0_CNTL, 0x99999999u);
    SET_REG(BC250_REG_SDMA0_RB_BASE, 0xAAAAAAAAu);
    SET_REG(BC250_REG_SDMA0_RB_BASE_HI, 0xBBBBBBBBu);
    SET_REG(BC250_REG_SDMA0_RB_CNTL, 0xCCCCCCCCu);
    SET_REG(BC250_REG_CP_RB0_RPTR, 0x10u);
    SET_REG(BC250_REG_CP_RB0_RPTR_ADDR_LO, 0x20u);
    SET_REG(BC250_REG_CP_RB0_RPTR_ADDR_HI, 0x30u);
    SET_REG(BC250_REG_CP_RB0_WPTR, 0x40u);
    SET_REG(BC250_REG_CP_RB0_WPTR_HI, 0x50u);
    SET_REG(BC250_REG_SDMA0_RB_RPTR, 0x60u);
    SET_REG(BC250_REG_SDMA0_RB_RPTR_HI, 0x70u);
    SET_REG(BC250_REG_SDMA0_RB_WPTR, 0x80u);
    SET_REG(BC250_REG_SDMA0_RB_WPTR_HI, 0x90u);
    SET_REG(BC250_REG_SDMA0_RB_RPTR_ADDR_LO, 0xA0u);
    SET_REG(BC250_REG_SDMA0_RB_RPTR_ADDR_HI, 0xB0u);
    SET_REG(BC250_REG_PSP_C2PMSG_35, 0x35u);
    SET_REG(BC250_REG_PSP_C2PMSG_36, 0x36u);
    SET_REG(BC250_REG_PSP_C2PMSG_37, 0x37u);
    SET_REG(BC250_REG_PSP_C2PMSG_81, 0x81u);
    SET_REG(BC250_REG_PSP_C2PMSG_64, 0x64u);
    SET_REG(BC250_REG_PSP_C2PMSG_67, 0x67u);
    SET_REG(BC250_REG_PSP_C2PMSG_69, 0x69u);
    SET_REG(BC250_REG_PSP_C2PMSG_70, 0x70u);
    SET_REG(BC250_REG_PSP_C2PMSG_71, 0x71u);
    SET_REG(BC250_HQD_MMIO_ACTIVE, 0x250u);
    SET_REG(BC250_HQD_MMIO_CC_GC_SHADER_ARRAY_CONFIG, 0xFFF80000u);
    SET_REG(BC250_HQD_MMIO_SPI_PG_STATIC_WGP_MASK, 0x0000FFFFu);
    SET_REG(BC250_HQD_MMIO_RLC_PG_ALWAYS_ON_WGP_MASK, 0x00000003u);
    SET_REG(BC250_HQD_MMIO_RLC_PG_CNTL, 0x00000001u);
#undef SET_REG
    return 1;
}

static int Bc250RunMmioTopologyTransportTest(void)
{
    static const uint32_t globalMmuOffsets[BC250_GCVM_GLOBAL_MMU_REG_COUNT] = {
        BC250_REG_GCMC_VM_AGP_BASE,
        BC250_REG_GCMC_VM_AGP_BOT,
        BC250_REG_GCMC_VM_AGP_TOP,
        BC250_REG_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR,
        BC250_REG_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR,
        BC250_REG_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB,
        BC250_REG_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB,
        BC250_REG_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_LO,
        BC250_REG_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_HI,
        BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL2,
        BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO,
        BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI,
        BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO,
        BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI,
        BC250_REG_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO,
        BC250_REG_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI,
        BC250_REG_GCVM_INVALIDATE_ENG0_ADDR_RANGE_LO,
        BC250_REG_GCVM_INVALIDATE_ENG0_ADDR_RANGE_HI,
        BC250_REG_GCVM_INVALIDATE_ENG17_ADDR_RANGE_LO,
        BC250_REG_GCVM_INVALIDATE_ENG17_ADDR_RANGE_HI,
        BC250_REG_GCMC_VM_MX_L1_TLB_CNTL,
        BC250_REG_GCVM_L2_CNTL,
        BC250_REG_GCVM_L2_CNTL2,
        BC250_REG_GCVM_L2_CNTL3,
        BC250_REG_GCVM_L2_CNTL4,
        BC250_REG_GCVM_L2_CNTL5
    };
    DXGKRNL_INTERFACE interfaceData;
    BC250_RESOLVED_BAR_RESOURCE bars[2];
    BC250_MMIO_TOPOLOGY_CONTEXT context;
    BC250_VRAM_TOPOLOGY_READER_INTERFACE readerInterface;
    BC250_VRAM_TOPOLOGY_READER_EVIDENCE evidence;
    BC250_DISCOVERY_TMR_EVIDENCE tmrEvidence;
    BC250_PLATFORM_REGISTER_SNAPSHOT platformSnapshot;
    uint32_t value;
    uint32_t index;

    CHECK(Bc250Prepare(&interfaceData, &bars[0], &context));
    CHECK(Bc250MmioTopologyMapReadonly(
        &context, &interfaceData, bars, 1, 5u) == BC250_MMIO_TOPOLOGY_OK);
    CHECK(g_mapCalls == 1u && context.State == BC250_MMIO_TOPOLOGY_MAPPED);
    CHECK(Bc250MmioTopologyRead32(&context, 0x100u, &value) == 0u);
    CHECK(value == 0u && context.ReadCount == 0u);
    CHECK(Bc250MmioTopologyWrite32(&context, 0x100u, 1u) == 0u);
    CHECK(Bc250MmioTopologyWrite32(&context, 0xA3A4u, 0xFFFFFFFFu) == 1u);
    CHECK(Bc250MmioTopologyRead32(&context, 0xA3A4u, &value) == 1u);
    CHECK(value == 0xFFFFFFFFu && context.WriteCount == 1u);
    memset(&readerInterface, 0, sizeof(readerInterface));
    readerInterface.Read32 = Bc250MmioTopologyRead32;
    readerInterface.Context = &context;
    readerInterface.IdentityFlags = BC250_TOPOLOGY_READER_IDENTITY_REQUIRED;
    CHECK(Bc250CaptureVramTopologyReadonly(
        &readerInterface, 0x10000000ull, 81u, &evidence) ==
        BC250_VRAM_TOPOLOGY_READER_OK);
    CHECK(context.ReadCount == 9u && evidence.ReadCount == 8u);
    CHECK(evidence.AddressSpace.TotalBytes == 0x20000000ull);
    CHECK(Bc250CaptureDiscoveryTmrReadonly(
        &readerInterface, evidence.AddressSpace.TotalBytes, 81u,
        &tmrEvidence) == BC250_DISCOVERY_TMR_OK);
    CHECK(context.ReadCount == 15u && tmrEvidence.ReadCount == 6u);
    CHECK(tmrEvidence.Offset == 0x1FFF0000ull &&
          tmrEvidence.Length == 0x10000ull);
    CHECK(Bc250CapturePlatformRegistersReadonly(
        &readerInterface, 81u, &platformSnapshot) ==
        BC250_PLATFORM_SNAPSHOT_OK);
    CHECK(context.ReadCount == 79u && platformSnapshot.ReadCount == 64u);
    CHECK(platformSnapshot.StablePasses == 2u);
    CHECK(platformSnapshot.GcvmPtBaseLo == 0x11111111u);
    CHECK(platformSnapshot.GcvmPtEndHi == 0x66666666u);
    CHECK(platformSnapshot.CpRb0BaseHi == 0x88888888u);
    CHECK(platformSnapshot.Sdma0RbCntl == 0xCCCCCCCCu);
    CHECK(platformSnapshot.CpRb0RptrAddrHi == 0x30u);
    CHECK(platformSnapshot.Sdma0RbRptrAddrHi == 0xB0u);
    CHECK(platformSnapshot.PspC2pmsg35 == 0x35u);
    CHECK(platformSnapshot.PspC2pmsg81 == 0x81u);
    CHECK(platformSnapshot.PspC2pmsg64 == 0x64u);
    CHECK(platformSnapshot.PspC2pmsg71 == 0x71u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_ACTIVE, &value) == 1u);
    CHECK(value == 0x250u && context.ReadCount == 80u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_ACTIVE, 1u) == 1u);
    CHECK(context.WriteCount == 2u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_QUEUE_PRIORITY, &value) == 1u);
    CHECK(value == 0u && context.ReadCount == 81u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_QUEUE_PRIORITY, 7u) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_QUEUE_PRIORITY, &value) == 1u);
    CHECK(value == 7u && context.ReadCount == 82u && context.WriteCount == 3u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_QUEUE_PRIORITY, 0u) == 1u);
    CHECK(context.WriteCount == 4u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_EOP_RPTR, &value) == 1u);
    CHECK(value == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_EOP_RPTR, 0x80000000u) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_EOP_RPTR, &value) == 1u);
    CHECK(value == 0x80000000u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_EOP_RPTR, 0u) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_EOP_RPTR, &value) == 1u);
    CHECK(value == 0u);
    /*
     * CP_PQ_WPTR_POLL_CNTL1 (0xC210) entra na allowlist de leitura e escrita
     * para o candidato 3.30; o offset foi catalogado a partir de
     * mmCP_PQ_WPTR_POLL_CNTL1 = dw 0x1E24 com a base +0x1260 dwords.
     */
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1, &value) == 1u);
    CHECK(value == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1, 1u) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1, &value) == 1u);
    CHECK(value == 1u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1, 0u) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_RLC_CP_SCHEDULERS, &value) == 1u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_RLC_CP_SCHEDULERS, 0xA0u) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_RLC_CP_SCHEDULERS, &value) == 1u);
    CHECK(value == 0xA0u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_GCVM_VMID8_CNTL, &value) == 1u);
    CHECK(value == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_GCVM_VMID8_CNTL, 0x00555403u) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_GCVM_VMID8_CNTL, &value) == 1u);
    CHECK(value == 0x00555403u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_GCVM_VMID8_CNTL, 0u) == 1u);
    {
        const uint32_t computeOffsets[] = {
            BC250_GCVM_VMID9_CNTL, BC250_GCVM_VMID9_PT_BASE_LO,
            BC250_GCVM_VMID9_PT_BASE_HI, BC250_GCVM_VMID9_PT_START_LO,
            BC250_GCVM_VMID9_PT_START_HI, BC250_GCVM_VMID9_PT_END_LO,
            BC250_GCVM_VMID9_PT_END_HI
        };
        uint32_t n;
        for (n = 0u; n < 7u; ++n) {
            CHECK(Bc250MmioTopologyWrite32(&context, computeOffsets[n], n + 1u));
            CHECK(Bc250MmioTopologyRead32(&context, computeOffsets[n], &value));
            CHECK(value == n + 1u);
            CHECK(Bc250MmioTopologyWrite32(&context, computeOffsets[n], 0u));
        }
        /* VMID9_CNTL + 4 e' VMID10_CNTL, agora pertencente ao pool. A antiga
         * assercao chamava um registrador catalogado de "vizinho". */
        CHECK(Bc250VmidPoolIsWritableGcvmRegister(
            BC250_GCVM_VMID9_CNTL + 4u) == 1u);
        CHECK(Bc250VmidPoolIsWritableGcvmRegister(
            BC250_GCVM_VMID9_CNTL + 2u) == 0u);
    }
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CC_GC_SHADER_ARRAY_CONFIG, &value) == 1u);
    CHECK(value == 0xFFF80000u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_SPI_PG_STATIC_WGP_MASK, &value) == 1u);
    CHECK(value == 0x0000FFFFu);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_RLC_PG_ALWAYS_ON_WGP_MASK, &value) == 1u);
    CHECK(value == 0x00000003u);
    /*
     * Candidato 3.34: diagnostico de stall do CP/CPC. Legiveis, e NUNCA
     * gravaveis - sao estado do proprio microengine. Escrever neles seria
     * mexer no que estamos tentando observar.
     */
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CP_CPC_STATUS, &value) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CP_CPC_BUSY_STAT, &value) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CP_CPC_STALLED_STAT1, &value) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CP_CPF_STATUS, &value) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CP_CPF_BUSY_STAT, &value) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CP_CPF_STALLED_STAT1, &value) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CP_MEC_ME1_HEADER_DUMP, &value) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CP_MEC_ME2_HEADER_DUMP, &value) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CP_STALLED_STAT1, &value) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CP_STALLED_STAT2, &value) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CP_BUSY_STAT, &value) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CP_STAT, &value) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CP_MEC1_INSTR_PNTR, &value) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_CP_MEC2_INSTR_PNTR, &value) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_IB_BASE_LO, &value) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_IB_BASE_HI, &value) == 1u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_IB_RPTR, &value) == 1u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_CP_CPC_STATUS, 0u) == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_CP_CPC_BUSY_STAT, 0u) == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_CP_CPC_STALLED_STAT1, 0u) == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_CP_CPF_STATUS, 0u) == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_CP_CPF_BUSY_STAT, 0u) == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_CP_CPF_STALLED_STAT1, 0u) == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_CP_MEC_ME1_HEADER_DUMP, 0u) == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_CP_MEC_ME2_HEADER_DUMP, 0u) == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_CP_STALLED_STAT1, 0u) == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_CP_STALLED_STAT2, 0u) == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_CP_BUSY_STAT, 0u) == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_CP_STAT, 0u) == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_CP_MEC2_INSTR_PNTR, 0u) == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_IB_BASE_LO, 0u) == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_IB_BASE_HI, 0u) == 0u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_IB_RPTR, 0u) == 0u);

    /* CC_GC_SHADER_ARRAY_CONFIG nunca entra na allowlist de escrita (preserva 24 CUs). */
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_CC_GC_SHADER_ARRAY_CONFIG, 0u) == 0u);
    /* Politica WGP/power-gating pertence ao firmware: leitura sim, escrita nunca. */
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_SPI_PG_STATIC_WGP_MASK, 0x0000001Fu) == 0u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_SPI_PG_STATIC_WGP_MASK, &value) == 1u);
    CHECK(value == 0x0000FFFFu);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_RLC_PG_ALWAYS_ON_WGP_MASK, 0x0000001Fu) == 0u);
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_RLC_PG_ALWAYS_ON_WGP_MASK, &value) == 1u);
    CHECK(value == 0x00000003u);
    /*
     * RLC.1 (doc 642): RLC_PG_CNTL passa a ser escrito, como
     * gfx_v10_0_rlc_resume (WREG32 mmRLC_PG_CNTL, 0). As mascaras de WGP acima
     * continuam so' de leitura.
     */
    CHECK(Bc250MmioTopologyRead32(
        &context, BC250_HQD_MMIO_RLC_PG_CNTL, &value) == 1u);
    CHECK(value == 1u);
    CHECK(Bc250MmioTopologyWrite32(
        &context, BC250_HQD_MMIO_RLC_PG_CNTL, 0u) == 1u);
    /* SMUIO_GFX_MISC_CNTL aceita leitura */
    CHECK(Bc250MmioTopologyRead32(
        &context, 0x0005A314u, &value) == 1u);
    for (index = 0; index < BC250_GCVM_GLOBAL_MMU_REG_COUNT; ++index) {
        const uint32_t pattern = 0xBC250000u + index;
        CHECK(Bc250MmioTopologyWrite32(
            &context, globalMmuOffsets[index], pattern) == 1u);
        CHECK(Bc250MmioTopologyRead32(
            &context, globalMmuOffsets[index], &value) == 1u);
        CHECK(value == pattern);
    }
    /* Um vizinho nao catalogado nao e aceito por proximidade. */
    CHECK(Bc250MmioTopologyRead32(&context, 0x0000A188u, &value) == 0u);
    CHECK(Bc250MmioTopologyWrite32(&context, 0x0000A188u, 1u) == 0u);
    CHECK(Bc250MmioTopologyRead32(&context, 0x0000DA60u, &value) == 0u);
    CHECK(Bc250MmioTopologyWrite32(&context, 0x0000DA60u, 1u) == 0u);
    CHECK(Bc250MmioTopologyRead32(&context, 0x0003F040u, &value) == 0u);
    CHECK(Bc250MmioTopologyWrite32(&context, 0x0003F040u, 1u) == 0u);
    CHECK(Bc250MmioTopologyRead32(&context, 0x000032D4u, &value) == 0u);
    CHECK(Bc250MmioTopologyWrite32(&context, 0x000032D4u, 1u) == 0u);
    /* RLC.1 (doc 642): +1 escrita, a do RLC_PG_CNTL. */
    CHECK(context.ReadCount == 148u && context.WriteCount == 52u);
    /*
     * DX.7: o transporte aceita somente os sete offsets exatos de cada VMID
     * do pool. Este bloco fica depois das assercoes historicas de contadores
     * para que a nova cobertura nao reescreva o baseline delas.
     */
    {
        static const uint32_t poolEdges[2] = { 1u, 15u };
        uint32_t edge;
        for (edge = 0u; edge < 2u; ++edge) {
            const uint32_t v = poolEdges[edge];
            const uint32_t pairOffset = v * BC250_GCVM_PT_BASE_STRIDE;
            const uint32_t offsets[7] = {
                BC250_GCVM_CNTL_BASE + v * BC250_GCVM_CNTL_STRIDE,
                BC250_GCVM_PT_BASE_LO_BASE + pairOffset,
                BC250_GCVM_PT_BASE_LO_BASE + pairOffset + 4u,
                BC250_GCVM_PT_START_LO_BASE + pairOffset,
                BC250_GCVM_PT_START_LO_BASE + pairOffset + 4u,
                BC250_GCVM_PT_END_LO_BASE + pairOffset,
                BC250_GCVM_PT_END_LO_BASE + pairOffset + 4u
            };
            uint32_t r;
            for (r = 0u; r < 7u; ++r) {
                CHECK(Bc250MmioTopologyWrite32(&context, offsets[r],
                    0xA5000000u | (v << 8) | r) == 1u);
                CHECK(Bc250MmioTopologyRead32(&context, offsets[r], &value) == 1u);
                CHECK(value == (0xA5000000u | (v << 8) | r));
            }
        }
        /* Desalinhado e primeiro offset alem do VMID15 continuam recusados. */
        CHECK(Bc250MmioTopologyWrite32(&context,
            BC250_GCVM_PT_BASE_LO_BASE + 2u, 1u) == 0u);
        CHECK(Bc250MmioTopologyWrite32(&context,
            BC250_GCVM_PT_END_LO_BASE +
                BC250_GCVM_CONTEXT_COUNT * BC250_GCVM_PT_BASE_STRIDE,
            1u) == 0u);
        /* RLC_SAFE_MODE (0x3B014) aceita leitura e escrita */
        CHECK(Bc250MmioTopologyWrite32(&context, 0x0003B014u, 3u) == 1u);
        CHECK(Bc250MmioTopologyRead32(&context, 0x0003B014u, &value) == 1u);
        CHECK(value == 3u);
        CHECK(Bc250MmioTopologyWrite32(&context, 0x0003B014u, 1u) == 1u);
        CHECK(Bc250MmioTopologyRead32(&context, 0x0003B014u, &value) == 1u);
        CHECK(value == 1u);
    }
    CHECK(Bc250MmioTopologyUnmap(&context) == BC250_MMIO_TOPOLOGY_OK);
    CHECK(g_unmapCalls == 1u && context.State == BC250_MMIO_TOPOLOGY_EMPTY);
    CHECK(context.VirtualBase == NULL);
    CHECK(Bc250MmioTopologyUnmap(&context) == BC250_MMIO_TOPOLOGY_OK);
    CHECK(g_unmapCalls == 1u);

    CHECK(Bc250Prepare(&interfaceData, &bars[0], &context));
    bars[0].Length = BC250_GCMC_VM_FB_LOCATION_TOP_BYTE_OFFSET;
    CHECK(Bc250MmioTopologyMapReadonly(
        &context, &interfaceData, bars, 1, 5u) ==
        BC250_MMIO_TOPOLOGY_BAR_TOO_SHORT);
    CHECK(g_mapCalls == 0u);

    CHECK(Bc250Prepare(&interfaceData, &bars[0], &context));
    bars[1] = bars[0];
    CHECK(Bc250MmioTopologyMapReadonly(
        &context, &interfaceData, bars, 2, 5u) ==
        BC250_MMIO_TOPOLOGY_BAR_NOT_UNIQUE);
    CHECK(g_mapCalls == 0u);

    CHECK(Bc250Prepare(&interfaceData, &bars[0], &context));
    g_failMap = 1u;
    CHECK(Bc250MmioTopologyMapReadonly(
        &context, &interfaceData, bars, 1, 5u) ==
        BC250_MMIO_TOPOLOGY_MAP_FAILED);
    CHECK(g_mapCalls == 1u && context.VirtualBase == NULL);

    CHECK(Bc250Prepare(&interfaceData, &bars[0], &context));
    CHECK(Bc250MmioTopologyMapReadonly(
        &context, &interfaceData, bars, 1, 5u) == BC250_MMIO_TOPOLOGY_OK);
    g_failUnmap = 1u;
    CHECK(Bc250MmioTopologyUnmap(&context) ==
        BC250_MMIO_TOPOLOGY_UNMAP_FAILED);
    CHECK(g_unmapCalls == 1u && context.VirtualBase == g_mmio &&
          context.State == BC250_MMIO_TOPOLOGY_MAPPED);
    g_failUnmap = 0;
    CHECK(Bc250MmioTopologyUnmap(&context) == BC250_MMIO_TOPOLOGY_OK);
    CHECK(g_unmapCalls == 2u && context.VirtualBase == NULL &&
          context.State == BC250_MMIO_TOPOLOGY_EMPTY);
    return 1;
}

void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunMmioTopologyTransportTest() ? 0ul : g_failureLine);
}
