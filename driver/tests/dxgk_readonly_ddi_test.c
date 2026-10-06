#include "../wdk/bc250_dxgk_readonly_ddi.h"
#include "../wdk/bc250_cp_firmware_manifest.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

/* P1b.2: o helper Bc250VramCpuMapMemory referencia o mapeador da DRAM. Nesta
 * suite a P1b nunca e' armada (traducao identidade), entao o ramo da DRAM nao
 * roda; o stub falha fechado se um dia rodar. */
static PVOID Bc250TestMapIoSpaceEx(PHYSICAL_ADDRESS PhysicalAddress, SIZE_T NumberOfBytes, ULONG Protect)
{
    UNREFERENCED_PARAMETER(PhysicalAddress);
    UNREFERENCED_PARAMETER(NumberOfBytes);
    UNREFERENCED_PARAMETER(Protect);
    return NULL;
}
static VOID Bc250TestUnmapIoSpace(PVOID BaseAddress, SIZE_T NumberOfBytes)
{
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(NumberOfBytes);
}
PVOID (*__imp_MmMapIoSpaceEx)(PHYSICAL_ADDRESS, SIZE_T, ULONG) = Bc250TestMapIoSpaceEx;
VOID (*__imp_MmUnmapIoSpace)(PVOID, SIZE_T) = Bc250TestUnmapIoSpace;

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
    volatile unsigned char *destination = (volatile unsigned char *)Destination;
    const volatile unsigned char *source = (const volatile unsigned char *)Source;
    size_t index;
    for (index = 0; index < Length; ++index) destination[index] = source[index];
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)

static BC250_DXGK_READONLY_CONTEXT g_context;
static BC250_DXGK_READONLY_INPUT g_runtime;
static DXGKRNL_INTERFACE g_interface;
static DXGK_START_INFO g_startInfo;
static __declspec(align(8)) unsigned char g_resourceBlob[512];
static ULONG g_callbackCount;
static ULONG g_pciReadCount;
static ULONG g_mapCount;
static ULONG g_unmapCount;
static ULONG g_failUnmap;
static unsigned char g_mmio[0x00080000u];
static PDXGKDDI_START_DEVICE g_startDdi = Bc250DxgkDdiStartDeviceReadonly;
static PDXGKDDI_STOP_DEVICE g_stopDdi = Bc250DxgkDdiStopDeviceReadonly;

static NTSTATUS APIENTRY Bc250TestGetDeviceInformation(
    HANDLE DeviceHandle,
    PDXGK_DEVICE_INFO DeviceInfo)
{
    if (DeviceHandle != (HANDLE)(ULONG_PTR)0x250u || DeviceInfo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    ++g_callbackCount;
    memset(DeviceInfo, 0, sizeof(*DeviceInfo));
    DeviceInfo->TranslatedResourceList =
        (CM_RESOURCE_LIST *)(void *)g_resourceBlob;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250TestReadDeviceSpace(
    HANDLE DeviceHandle, ULONG DataType, PVOID Buffer, ULONG Offset,
    ULONG Length, PULONG BytesRead)
{
    PCI_COMMON_CONFIG config;
    uint32_t capability[3];
    if (DeviceHandle != (HANDLE)(ULONG_PTR)0x250u ||
        DataType != DXGK_WHICHSPACE_CONFIG || Buffer == NULL ||
        BytesRead == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Offset == 0 && Length == PCI_COMMON_HDR_LENGTH) {
        memset(&config, 0, sizeof(config));
        config.VendorID = BC250_PCI_VENDOR_ID;
        config.DeviceID = BC250_PCI_DEVICE_ID;
        config.HeaderType = PCI_DEVICE_TYPE;
        config.u.type0.BaseAddresses[0] = 0xC000000Cu;
        config.u.type0.BaseAddresses[2] = 0xD0000000u;
        config.u.type0.BaseAddresses[4] = 0x0000E001u;
        config.u.type0.BaseAddresses[5] = 0xFE800000u;
        memcpy(Buffer, &config, Length);
    } else if (Offset == BC250_REBAR_CONFIG_START &&
               (Length == sizeof(uint32_t) ||
                Length == 3u * sizeof(uint32_t))) {
        capability[0] = PCI_EXPRESS_RESIZABLE_BAR_CAP_ID | (1u << 16);
        capability[1] = ((1u << 8) | (1u << 9)) << 4;
        capability[2] = (1u << 5) | (8u << 8);
        memcpy(Buffer, capability, Length);
    } else {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesRead = Length;
    ++g_pciReadCount;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250TestMapMemory(
    HANDLE DeviceHandle, PHYSICAL_ADDRESS TranslatedAddress, ULONG Length,
    BOOLEAN InIoSpace, BOOLEAN MapToUserMode, MEMORY_CACHING_TYPE CacheType,
    PVOID *VirtualAddress)
{
    if (DeviceHandle != (HANDLE)(ULONG_PTR)0x250u ||
        TranslatedAddress.QuadPart != 0xFE800000ll ||
        Length != sizeof(g_mmio) || InIoSpace != FALSE ||
        MapToUserMode != FALSE || CacheType != MmNonCached ||
        VirtualAddress == NULL) return STATUS_INVALID_PARAMETER;
    ++g_mapCount;
    *VirtualAddress = g_mmio;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250TestUnmapMemory(
    HANDLE DeviceHandle, PVOID VirtualAddress)
{
    if (DeviceHandle != (HANDLE)(ULONG_PTR)0x250u ||
        VirtualAddress != g_mmio) return STATUS_INVALID_PARAMETER;
    ++g_unmapCount;
    return g_failUnmap != 0 ? STATUS_DEVICE_NOT_READY : STATUS_SUCCESS;
}

static void Bc250TestBuildMmio(void)
{
    uint32_t *value;
    memset(g_mmio, 0, sizeof(g_mmio));
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
}

static void Bc250TestBuildResourceList(void)
{
    CM_RESOURCE_LIST *list;
    CM_FULL_RESOURCE_DESCRIPTOR *full;
    CM_PARTIAL_RESOURCE_DESCRIPTOR *descriptor;

    memset(g_resourceBlob, 0, sizeof(g_resourceBlob));
    list = (CM_RESOURCE_LIST *)(void *)g_resourceBlob;
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
}

static void Bc250TestBuildRuntime(void)
{
    memset(&g_runtime, 0, sizeof(g_runtime));
    g_runtime.ReservationCount = 1;
    g_runtime.Reservations[0].Offset = 0x1FC00000ull;
    g_runtime.Reservations[0].Length = 0x00400000ull;
    g_runtime.Reservations[0].Alignment = 0x00100000ull;
    g_runtime.Reservations[0].Role = BC250_VRAM_ROLE_PLATFORM_RESERVED;
    g_runtime.Reservations[0].Flags = BC250_VRAM_REGION_PERSISTENT;
    g_runtime.VramApertureBarIndex = 0;
    g_runtime.MmioBarIndex = 5;
    g_runtime.TopologyReaderIdentityFlags =
        BC250_TOPOLOGY_READER_IDENTITY_REQUIRED;
    g_runtime.RuntimeOwnershipFlags =
        BC250_OWNERSHIP_EXCLUSIVE_ADAPTER |
        BC250_OWNERSHIP_INVENTORY_COMPLETE;
    g_runtime.SnapshotGeneration = 51;
    g_runtime.Policy.TmrBytes = 0x00400000ull;
    g_runtime.Policy.TmrAlignment = 0x00100000ull;
    g_runtime.Policy.FirmwareAlignment = 0x1000ull;
    g_runtime.ManifestCount = BC250_CP_BOOTSTRAP_FIRMWARE_COUNT;
    g_runtime.Manifest[0].FirmwareType = BC250_PSP_FW_CP_PFP;
    g_runtime.Manifest[1].FirmwareType = BC250_PSP_FW_CP_ME;
    g_runtime.Manifest[2].FirmwareType = BC250_PSP_FW_CP_CE;
    g_runtime.Manifest[3].FirmwareType = BC250_PSP_FW_CP_MEC;
    g_runtime.Manifest[4].FirmwareType = BC250_PSP_FW_CP_MEC_JT1;
    g_runtime.Manifest[0].PayloadBytes = 0x12000u;
    g_runtime.Manifest[1].PayloadBytes = 0x14000u;
    g_runtime.Manifest[2].PayloadBytes = 0x10000u;
    g_runtime.Manifest[3].PayloadBytes = 0x30000u;
    g_runtime.Manifest[4].PayloadBytes = 0x08000u;
}

static int Bc250RunDxgkReadonlyDdiTest(void)
{
    ULONG sources = 99, children = 99;
    NTSTATUS status;
    const unsigned char *firmwareDigest;

    firmwareDigest = Bc250GetCpFirmwarePayloadSha256(BC250_PSP_FW_CP_MEC);
    CHECK(firmwareDigest != NULL && firmwareDigest[0] == 0x76u &&
          firmwareDigest[31] == 0x48u);
    firmwareDigest = Bc250GetCpFirmwarePayloadSha256(
        BC250_PSP_FW_CP_MEC_JT1);
    CHECK(firmwareDigest != NULL && firmwareDigest[0] == 0xF9u &&
          firmwareDigest[31] == 0xD3u);
    CHECK(Bc250GetCpFirmwarePayloadSha256(0u) == NULL);

    Bc250TestBuildResourceList();
    memset(&g_interface, 0, sizeof(g_interface));
    memset(&g_startInfo, 0, sizeof(g_startInfo));
    g_interface.Size = sizeof(g_interface);
    g_interface.DeviceHandle = (HANDLE)(ULONG_PTR)0x250u;
    g_interface.DxgkCbGetDeviceInformation = Bc250TestGetDeviceInformation;
    g_interface.DxgkCbReadDeviceSpace = Bc250TestReadDeviceSpace;
    g_interface.DxgkCbMapMemory = Bc250TestMapMemory;
    g_interface.DxgkCbUnmapMemory = Bc250TestUnmapMemory;
    Bc250TestBuildMmio();
    g_callbackCount = 0;
    g_pciReadCount = 0;
    g_mapCount = 0;
    g_unmapCount = 0;
    g_failUnmap = 0;

    CHECK(Bc250DxgkReadonlyInitialize(&g_context) == STATUS_SUCCESS);
    status = g_startDdi(
        &g_context, &g_startInfo, &g_interface, &sources, &children);
    CHECK(status == STATUS_NOT_SUPPORTED);
    CHECK(sources == 0 && children == 0 && g_callbackCount == 1u);
    CHECK(g_pciReadCount == 3u && g_context.DiscoveredBarCount == 3u);
    CHECK(g_context.LastCmStatus == BC250_CM_RESOURCE_OK);
    CHECK(g_context.LastResourceStatus == BC250_RESOURCE_OK);
    CHECK(g_context.ResolvedBarCount == 3u);
    CHECK(g_mapCount == 1u && g_unmapCount == 1u);
    CHECK(g_context.LastMmioStatus == BC250_MMIO_TOPOLOGY_OK);
    CHECK(g_context.LastTopologyReaderStatus ==
        BC250_VRAM_TOPOLOGY_READER_OK);
    CHECK(g_context.TopologyEvidence.ReadCount == 8u);
    CHECK(g_context.TopologyEvidence.AddressSpace.TotalBytes ==
        0x20000000ull);
    CHECK(g_context.LastDiscoveryTmrStatus == BC250_DISCOVERY_TMR_OK);
    CHECK(g_context.DiscoveryTmrEvidence.ReadCount == 6u);
    CHECK(g_context.DiscoveryTmrEvidence.StablePasses == 2u);
    CHECK(g_context.DiscoveryTmrEvidence.Offset == 0x1FFF0000ull);
    CHECK(g_context.DiscoveryTmrEvidence.Length == 0x10000ull);
    CHECK(g_context.StartDevice.State == BC250_START_DEVICE_EMPTY);
    CHECK(g_context.LastRebarStatus == BC250_REBAR_OK);
    CHECK(g_context.RebarEvidence.SelectedBytes == 0x10000000ull);
    CHECK(g_context.LastStage == BC250_DXGK_STAGE_RUNTIME_MISSING);
    CHECK(g_context.ReadonlyGatePassed == 0u);
    CHECK(g_context.StartDevice.Preflight.FirmwareCount == 0u);
    CHECK(g_stopDdi(&g_context) == STATUS_SUCCESS);

    Bc250TestBuildRuntime();
    g_runtime.RuntimeOwnershipFlags &=
        ~BC250_OWNERSHIP_INVENTORY_COMPLETE;
    CHECK(Bc250DxgkReadonlyStageRuntime(&g_context, &g_runtime) ==
        STATUS_INVALID_PARAMETER);
    CHECK(g_context.RuntimeStaged == 0u);

    Bc250TestBuildRuntime();
    CHECK(Bc250DxgkReadonlyStageRuntime(&g_context, &g_runtime) ==
        STATUS_SUCCESS);
    sources = 99; children = 99;
    status = g_startDdi(
        &g_context, &g_startInfo, &g_interface, &sources, &children);
    CHECK(status == STATUS_NOT_SUPPORTED);
    CHECK(sources == 0 && children == 0 && g_callbackCount == 2u);
    CHECK(g_pciReadCount == 6u && g_context.DiscoveredBarCount == 3u);
    CHECK(g_context.ResolvedBarCount == 3u);
    CHECK(g_context.LastStage == BC250_DXGK_STAGE_PREFLIGHT_READY);
    CHECK(g_mapCount == 2u && g_unmapCount == 2u);
    CHECK(g_context.LastMmioStatus == BC250_MMIO_TOPOLOGY_OK);
    CHECK(g_context.LastTopologyReaderStatus ==
        BC250_VRAM_TOPOLOGY_READER_OK);
    CHECK(g_context.TopologyEvidence.ReadCount == 8u);
    CHECK(g_context.TopologyEvidence.AddressSpace.TotalBytes ==
        0x20000000ull);
    CHECK(g_context.LastDiscoveryTmrStatus == BC250_DISCOVERY_TMR_OK);
    CHECK(g_context.DiscoveryTmrEvidence.ReadCount == 6u);
    CHECK(g_context.DiscoveryTmrEvidence.StablePasses == 2u);
    CHECK(g_context.DiscoveryTmrEvidence.Offset == 0x1FFF0000ull);
    CHECK(g_context.DiscoveryTmrEvidence.Length == 0x10000ull);
    CHECK(g_context.LastRebarStatus == BC250_REBAR_OK);
    CHECK(g_context.RebarEvidence.SelectedBytes == 0x10000000ull);
    CHECK(g_context.ReadonlyGatePassed == 1u);
    CHECK(g_context.StartDevice.State == BC250_START_DEVICE_PREFLIGHT_READY);
    CHECK(g_context.StartDevice.Preflight.TmrPlan.Binding.Offset ==
        0x1F800000ull);
    CHECK(g_stopDdi(&g_context) == STATUS_SUCCESS);
    CHECK(g_context.StartDevice.State == BC250_START_DEVICE_EMPTY);
    CHECK(g_context.RuntimeStaged == 0u);

    /* Test discovery TMR scratch size = 0 (not reported) */
    Bc250TestBuildMmio();
    *(uint32_t *)(void *)(g_mmio + BC250_DRIVER_SCRATCH_2_BYTE_OFFSET) = 0u;
    sources = 99; children = 99;
    status = g_startDdi(
        &g_context, &g_startInfo, &g_interface, &sources, &children);
    CHECK(status == STATUS_NOT_SUPPORTED);
    CHECK(g_context.LastDiscoveryTmrStatus ==
        BC250_DISCOVERY_TMR_NOT_REPORTED);
    CHECK(g_context.DiscoveryTmrEvidence.Length == 0);
    CHECK(g_context.LastStage == BC250_DXGK_STAGE_RUNTIME_MISSING);
    CHECK(g_stopDdi(&g_context) == STATUS_SUCCESS);
    Bc250TestBuildMmio();

    Bc250TestBuildRuntime();
    CHECK(Bc250DxgkReadonlyStageRuntime(&g_context, &g_runtime) ==
        STATUS_SUCCESS);
    g_failUnmap = 1u;
    sources = 99; children = 99;
    status = g_startDdi(
        &g_context, &g_startInfo, &g_interface, &sources, &children);
    CHECK(status == STATUS_DEVICE_CONFIGURATION_ERROR);
    CHECK(sources == 0 && children == 0);
    CHECK(g_context.LastStage == BC250_DXGK_STAGE_MMIO_UNMAP_FAILED);
    CHECK(g_context.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED);

    /*
     * REPRODUCAO OFFLINE DA CORRIDA 08/09 11:48 (Codigo 43).
     *
     * Estado de partida: runtime STAGED e transporte MMIO ainda MAPEADO -- foi
     * exatamente esse o estado em que o StartDevice de laboratorio chamou o
     * segundo preflight, depois de programar a MMU global.
     *
     * Duas coisas sao travadas aqui, e as duas falharam de forma silenciosa na
     * corrida:
     *
     *  1. a guarda de entrada RECUSA, e recusa com STATUS_INVALID_PARAMETER --
     *     NAO com STATUS_NOT_SUPPORTED. Confundir os dois foi o que fez a
     *     corrida parecer "runtime ausente" quando o runtime estava staged;
     *
     *  2. LastStage NAO e' tocado. O chamador que ler apenas o estagio vera o
     *     residuo da chamada ANTERIOR e concluira errado. Por isso o
     *     StartDevice passou a publicar tambem o NTSTATUS cru
     *     (BC250_PROBE_SECOND_PREFLIGHT_STATUS).
     */
    g_failUnmap = 0u;
    CHECK(g_context.RuntimeStaged == 1u);
    CHECK(g_context.LastStage == BC250_DXGK_STAGE_MMIO_UNMAP_FAILED);
    sources = 99; children = 99;
    status = g_startDdi(
        &g_context, &g_startInfo, &g_interface, &sources, &children);
    CHECK(status == STATUS_INVALID_PARAMETER);
    CHECK(status != STATUS_NOT_SUPPORTED);
    CHECK(g_context.LastStage == BC250_DXGK_STAGE_MMIO_UNMAP_FAILED);
    CHECK(g_context.ReadonlyGatePassed == 0u);
    CHECK(g_context.RuntimeStaged == 1u);
    CHECK(sources == 0 && children == 0);

    /*
     * E o contraponto que da sentido ao caso acima: com o MESMO runtime
     * staged, devolver o transporte a vazio faz o preflight ANDAR de verdade --
     * ele passa da guarda e chega ao fim do gate readonly. Sem esta metade, a
     * assercao anterior seria satisfeita por um preflight quebrado de qualquer
     * outra forma.
     */
    CHECK(g_stopDdi(&g_context) == STATUS_SUCCESS);
    CHECK(g_context.MmioTransport.State == BC250_MMIO_TOPOLOGY_EMPTY);
    Bc250TestBuildMmio();
    Bc250TestBuildRuntime();
    CHECK(Bc250DxgkReadonlyStageRuntime(&g_context, &g_runtime) ==
        STATUS_SUCCESS);
    sources = 99; children = 99;
    status = g_startDdi(
        &g_context, &g_startInfo, &g_interface, &sources, &children);
    CHECK(status == STATUS_NOT_SUPPORTED);
    CHECK(g_context.LastStage == BC250_DXGK_STAGE_PREFLIGHT_READY);
    CHECK(g_context.ReadonlyGatePassed == 1u);
    CHECK(g_context.MmioTransport.State == BC250_MMIO_TOPOLOGY_EMPTY);

    CHECK(g_stopDdi(&g_context) == STATUS_SUCCESS);
    CHECK(g_context.MmioTransport.State == BC250_MMIO_TOPOLOGY_EMPTY);
    return 1;
}

void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunDxgkReadonlyDdiTest() ? 0ul : 1ul);
}
