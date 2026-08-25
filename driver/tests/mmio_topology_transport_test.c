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

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#define BC250_TEST_MMIO_BYTES 0x0000B000u

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
    return 1;
}

static int Bc250RunMmioTopologyTransportTest(void)
{
    DXGKRNL_INTERFACE interfaceData;
    BC250_RESOLVED_BAR_RESOURCE bars[2];
    BC250_MMIO_TOPOLOGY_CONTEXT context;
    BC250_VRAM_TOPOLOGY_READER_INTERFACE readerInterface;
    BC250_VRAM_TOPOLOGY_READER_EVIDENCE evidence;
    BC250_DISCOVERY_TMR_EVIDENCE tmrEvidence;
    uint32_t value;

    CHECK(Bc250Prepare(&interfaceData, &bars[0], &context));
    CHECK(Bc250MmioTopologyMapReadonly(
        &context, &interfaceData, bars, 1, 5u) == BC250_MMIO_TOPOLOGY_OK);
    CHECK(g_mapCalls == 1u && context.State == BC250_MMIO_TOPOLOGY_MAPPED);
    CHECK(Bc250MmioTopologyRead32(&context, 0x100u, &value) == 0u);
    CHECK(value == 0u && context.ReadCount == 0u);
    memset(&readerInterface, 0, sizeof(readerInterface));
    readerInterface.Read32 = Bc250MmioTopologyRead32;
    readerInterface.Context = &context;
    readerInterface.IdentityFlags = BC250_TOPOLOGY_READER_IDENTITY_REQUIRED;
    CHECK(Bc250CaptureVramTopologyReadonly(
        &readerInterface, 0x10000000ull, 81u, &evidence) ==
        BC250_VRAM_TOPOLOGY_READER_OK);
    CHECK(context.ReadCount == 8u && evidence.ReadCount == 8u);
    CHECK(evidence.AddressSpace.TotalBytes == 0x20000000ull);
    CHECK(Bc250CaptureDiscoveryTmrReadonly(
        &readerInterface, evidence.AddressSpace.TotalBytes, 81u,
        &tmrEvidence) == BC250_DISCOVERY_TMR_OK);
    CHECK(context.ReadCount == 14u && tmrEvidence.ReadCount == 6u);
    CHECK(tmrEvidence.Offset == 0x1FFF0000ull &&
          tmrEvidence.Length == 0x10000ull);
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
    ExitProcess(Bc250RunMmioTopologyTransportTest() ? 0ul : 1ul);
}
