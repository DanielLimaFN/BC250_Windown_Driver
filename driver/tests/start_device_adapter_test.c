#include "../wdk/bc250_start_device_adapter.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#pragma function(memcpy)
void * __cdecl memcpy(void *Destination, const void *Source, size_t Length)
{
    unsigned char *destination = (unsigned char *)Destination;
    const unsigned char *source = (const unsigned char *)Source;
    size_t index;
    for (index = 0; index < Length; ++index) destination[index] = source[index];
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)

static BC250_START_DEVICE_CONTEXT g_context;

static void Bc250TestBuildResourceList(
    unsigned char *Blob,
    size_t BlobBytes,
    CM_RESOURCE_LIST **List)
{
    CM_RESOURCE_LIST *list;
    CM_FULL_RESOURCE_DESCRIPTOR *full;
    CM_PARTIAL_RESOURCE_DESCRIPTOR *descriptor;

    memset(Blob, 0, BlobBytes);
    list = (CM_RESOURCE_LIST *)(void *)Blob;
    list->Count = 1;
    full = &list->List[0];
    full->PartialResourceList.Version = 1;
    full->PartialResourceList.Revision = 1;
    full->PartialResourceList.Count = 3;
    descriptor = &full->PartialResourceList.PartialDescriptors[0];
    descriptor->Type = CmResourceTypeMemory;
    descriptor->u.Memory.Start.QuadPart = 0xC0000000ll;
    descriptor->u.Memory.Length = 0x10200000u;
    descriptor = &full->PartialResourceList.PartialDescriptors[1];
    descriptor->Type = CmResourceTypeMemory;
    descriptor->u.Memory.Start.QuadPart = 0xD0000000ll;
    descriptor->u.Memory.Length = 0x00200000u;
    descriptor = &full->PartialResourceList.PartialDescriptors[2];
    descriptor->Type = CmResourceTypeMemory;
    descriptor->u.Memory.Start.QuadPart = 0xFE800000ll;
    descriptor->u.Memory.Length = 0x00080000u;
    *List = list;
}

static int Bc250RunStartDeviceAdapterTest(void)
{
    __declspec(align(8)) unsigned char blob[512];
    CM_RESOURCE_LIST *list;
    BC250_PCI_BAR_RESOURCE bars[3];
    BC250_INHERITED_VRAM_RESERVATION reservations[2];
    BC250_START_DEVICE_TOPOLOGY topology;
    BC250_CP_BOOTSTRAP_POLICY policy;
    BC250_CP_FIRMWARE_ENTRY manifest[BC250_CP_BOOTSTRAP_FIRMWARE_COUNT];
    BC250_START_DEVICE_CONTEXT *context = &g_context;
    BC250_CM_RESOURCE_STATUS cmStatus;
    BC250_RESOURCE_STATUS resourceStatus;
    BC250_OWNED_ADAPTER_STATUS ownedStatus;
    BC250_CP_BOOTSTRAP_STATUS bootstrapStatus;
    BC250_VRAM_STATUS vramStatus;
    BC250_ADDRESS_STATUS addressStatus;
    BC250_PSP_CONTRACT_STATUS pspStatus;

    Bc250TestBuildResourceList(blob, sizeof(blob), &list);
    memset(bars, 0, sizeof(bars));
    bars[0].Index = 0; bars[0].Present = 1; bars[0].Base = 0xC0000000ull;
    bars[1].Index = 2; bars[1].Present = 1; bars[1].Base = 0xD0000000ull;
    bars[2].Index = 5; bars[2].Present = 1; bars[2].Base = 0xFE800000ull;
    memset(reservations, 0, sizeof(reservations));
    reservations[0].Offset = 0x1FC00000ull;
    reservations[0].Length = 0x00400000ull;
    reservations[0].Alignment = 0x00100000ull;
    reservations[0].Role = BC250_VRAM_ROLE_PLATFORM_RESERVED;
    reservations[0].Flags = BC250_VRAM_REGION_PERSISTENT;
    reservations[1].Offset = 0x0FC00000ull;
    reservations[1].Length = 0x00400000ull;
    reservations[1].Alignment = 0x1000ull;
    reservations[1].Role = BC250_VRAM_ROLE_PAGE_TABLE;
    reservations[1].Flags = BC250_VRAM_ALLOC_CPU_VISIBLE |
        BC250_VRAM_REGION_PERSISTENT;
    memset(&topology, 0, sizeof(topology));
    topology.VramApertureBarIndex = 0;
    topology.RuntimeEvidenceFlags = BC250_START_RUNTIME_EVIDENCE_REQUIRED;
    topology.VramTotalBytes = 0x20000000ull;
    topology.VramMcStart = 0x000000F400000000ull;
    topology.VramMcEnd = 0x000000F41FFFFFFFull;
    topology.VramBaseOffset = 0x0000000450000000ull;
    topology.SnapshotGeneration = 41;
    CHECK(Bc250StartDeviceInitialize(context) == BC250_START_DEVICE_OK);

    CHECK(Bc250StartDevicePrepareOwnership(
        context, list, bars, RTL_NUMBER_OF(bars), &topology, reservations,
        RTL_NUMBER_OF(reservations), &cmStatus, &resourceStatus, &ownedStatus,
        &bootstrapStatus, &vramStatus) == BC250_START_DEVICE_OK);
    CHECK(context->State == BC250_START_DEVICE_OWNED_SEALED);
    CHECK(context->ResolvedBarCount == 3u);
    CHECK(context->Owned.CpuApertureBytes == 0x10000000ull);
    CHECK(Bc250StartDevicePrepareOwnership(
        context, list, bars, RTL_NUMBER_OF(bars), &topology, reservations,
        RTL_NUMBER_OF(reservations), &cmStatus, &resourceStatus, &ownedStatus,
        &bootstrapStatus, &vramStatus) == BC250_START_DEVICE_WRONG_STATE);

    policy.TmrBytes = 0x00400000ull;
    policy.TmrAlignment = 0x00100000ull;
    policy.FirmwareAlignment = 0x1000ull;
    manifest[0].FirmwareType = BC250_PSP_FW_CP_PFP; manifest[0].PayloadBytes = 0x12000u;
    manifest[1].FirmwareType = BC250_PSP_FW_CP_ME; manifest[1].PayloadBytes = 0x14000u;
    manifest[2].FirmwareType = BC250_PSP_FW_CP_CE; manifest[2].PayloadBytes = 0x10000u;
    manifest[3].FirmwareType = BC250_PSP_FW_CP_MEC; manifest[3].PayloadBytes = 0x30000u;
    manifest[4].FirmwareType = BC250_PSP_FW_CP_MEC_JT1; manifest[4].PayloadBytes = 0x08000u;
    CHECK(Bc250StartDeviceBuildReadonlyPreflight(
        context, &policy, manifest, RTL_NUMBER_OF(manifest) - 1u, &vramStatus,
        &addressStatus, &pspStatus, &bootstrapStatus) ==
        BC250_START_DEVICE_PREFLIGHT_REJECTED);
    CHECK(context->State == BC250_START_DEVICE_OWNED_SEALED);
    CHECK(Bc250StartDeviceBuildReadonlyPreflight(
        context, &policy, manifest, RTL_NUMBER_OF(manifest), &vramStatus,
        &addressStatus, &pspStatus, &bootstrapStatus) == BC250_START_DEVICE_OK);
    CHECK(context->State == BC250_START_DEVICE_PREFLIGHT_READY);
    CHECK(context->Preflight.TmrPlan.Binding.Offset == 0x1F800000ull);
    CHECK(Bc250StartDeviceTeardown(context) == BC250_START_DEVICE_OK);
    CHECK(context->State == BC250_START_DEVICE_EMPTY);
    CHECK(context->ResolvedBarCount == 0u);

    topology.RuntimeEvidenceFlags &= ~BC250_OWNERSHIP_EXCLUSIVE_ADAPTER;
    CHECK(Bc250StartDevicePrepareOwnership(
        context, list, bars, RTL_NUMBER_OF(bars), &topology, reservations,
        RTL_NUMBER_OF(reservations), &cmStatus, &resourceStatus, &ownedStatus,
        &bootstrapStatus, &vramStatus) ==
        BC250_START_DEVICE_MISSING_RUNTIME_EVIDENCE);
    CHECK(context->State == BC250_START_DEVICE_EMPTY);
    return 1;
}

void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunStartDeviceAdapterTest() ? 0ul : 1ul);
}
