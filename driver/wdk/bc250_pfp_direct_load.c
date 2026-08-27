#include "bc250_active_miniport.h"
#include "bc250_cp_firmware_manifest.h"
#include "../core/bc250_firmware_staging.h"

#define BC250_PFP_FILE_BYTES 263424u
#define BC250_PFP_PAYLOAD_BYTES_LOCAL 263168u
#define BC250_PFP_ALLOCATION_BYTES 0x00041000u
#define BC250_PFP_JT_OFFSET_DWORDS 65536u
#define BC250_PFP_JT_DWORDS 128u
#define BC250_PFP_UCODE_VERSION 0x94u
#define BC250_PFP_POOL_TAG 'pPfB'
#define BC250_PFP_HALT 0x04000000u

#define R_CP_ME_CNTL 0x000086D8u
#define R_CP_PFP_PC 0x00008694u
#define R_FAULT_STATUS 0x0000A130u
#define R_FAULT_ADDR_LO 0x0000A134u
#define R_FAULT_ADDR_HI 0x0000A138u
#define R_RLC_CNTL 0x0003B000u
#define R_PFP_UCODE_ADDR 0x0003E050u
#define R_PFP_UCODE_DATA 0x0003E054u
#define R_PFP_IC_BASE_LO 0x0003E100u
#define R_PFP_IC_BASE_HI 0x0003E104u
#define R_PFP_IC_BASE_CNTL 0x0003E108u
#define R_PFP_IC_OP_CNTL 0x0003E10Cu

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
static uint32_t Bc250PfpRead(
    BC250_DXGK_ACTIVE_CONTEXT *Context, uint32_t Offset, uint32_t *Value)
{
    return Bc250MmioTopologyRead32(
        &Context->Core.MmioTransport, Offset, Value);
}

static uint32_t Bc250PfpWrite(
    BC250_DXGK_ACTIVE_CONTEXT *Context, uint32_t Offset, uint32_t Value)
{
    return Bc250MmioTopologyWrite32(
        &Context->Core.MmioTransport, Offset, Value);
}

static void Bc250PfpCopyFromDevice(
    uint32_t *Destination, volatile uint32_t *Source, uint32_t Dwords)
{
    uint32_t i;
    for (i = 0u; i < Dwords; ++i)
        Destination[i] = READ_REGISTER_ULONG((volatile ULONG *)&Source[i]);
}

static void Bc250PfpCopyToDevice(
    volatile uint32_t *Destination, const uint32_t *Source, uint32_t Dwords)
{
    uint32_t i;
    for (i = 0u; i < Dwords; ++i)
        WRITE_REGISTER_ULONG((volatile ULONG *)&Destination[i], Source[i]);
    KeMemoryBarrier();
}
#endif

BC250_PFP_DIRECT_LOAD_STATUS Bc250ActiveRunPfpDirectLoad(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PFP_DIRECT_LOAD_EVIDENCE *Evidence)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    static const unsigned char expectedFileDigest[32] = {
        0x50,0xE5,0x6D,0xC1,0x91,0x35,0x71,0xFF,
        0x58,0x94,0x25,0xD2,0xB0,0x59,0xBD,0xDF,
        0xC5,0xE8,0xA2,0xB4,0xBC,0x22,0xD3,0xEB,
        0x78,0xCA,0xC7,0x5D,0x1E,0x04,0x79,0xA1
    };
    UNICODE_STRING path = RTL_CONSTANT_STRING(
        L"\\SystemRoot\\System32\\drivers\\cyan_skillfish2_pfp.bin");
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK ioStatus;
    FILE_STANDARD_INFORMATION fileInfo;
    HANDLE file = NULL;
    BC250_VRAM_ALLOCATOR *allocator;
    const BC250_VRAM_ADDRESS_SPACE *addressSpace;
    BC250_VRAM_TOKEN token = {0};
    BC250_VRAM_REGION region;
    BC250_VRAM_STATUS vramStatus;
    const BC250_RESOLVED_BAR_RESOURCE *bar = NULL;
    PHYSICAL_ADDRESS physical;
    volatile uint32_t *mapped = NULL;
    uint32_t *fileBuffer = NULL, *backup = NULL;
    unsigned char digest[32];
    const unsigned char *payloadDigest;
    uint32_t i, matches = 0u, value = 0u;
    uint32_t mappedDwords = BC250_PFP_ALLOCATION_BYTES / sizeof(uint32_t);
    uint32_t payloadDwords = BC250_PFP_PAYLOAD_BYTES_LOCAL / sizeof(uint32_t);
    uint32_t allocated = 0u, mappedValid = 0u, memoryPrepared = 0u;
    uint32_t snapshotValid = 0u, registersTouched = 0u;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    NTSTATUS ntStatus;
    BC250_PFP_DIRECT_LOAD_STATUS status = BC250_PFP_DIRECT_LOAD_BAD_ARGUMENT;

    if (Evidence != NULL) RtlZeroMemory(Evidence, sizeof(*Evidence));
    if (Context == NULL || Evidence == NULL) return status;
    allocator = &Context->Core.StartDevice.Owned.Bootstrap.Allocator;
    addressSpace = &Context->Core.TopologyEvidence.AddressSpace;
    if (addressSpace->SnapshotGeneration == 0u ||
        addressSpace->VramMcStart == 0u) return status;

    InitializeObjectAttributes(&attributes, &path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    ntStatus = ZwCreateFile(&file, GENERIC_READ | SYNCHRONIZE, &attributes,
        &ioStatus, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0u);
    if (!NT_SUCCESS(ntStatus)) return BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED;
    ntStatus = ZwQueryInformationFile(file, &ioStatus, &fileInfo,
        sizeof(fileInfo), FileStandardInformation);
    if (!NT_SUCCESS(ntStatus) ||
        fileInfo.EndOfFile.QuadPart != BC250_PFP_FILE_BYTES) {
        status = BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED;
        goto Cleanup;
    }
    fileBuffer = (uint32_t *)ExAllocatePool2(POOL_FLAG_NON_PAGED,
        BC250_PFP_FILE_BYTES, BC250_PFP_POOL_TAG);
    backup = (uint32_t *)ExAllocatePool2(POOL_FLAG_NON_PAGED,
        BC250_PFP_ALLOCATION_BYTES, BC250_PFP_POOL_TAG);
    if (fileBuffer == NULL || backup == NULL) {
        status = BC250_PFP_DIRECT_LOAD_ALLOCATION_FAILED;
        goto Cleanup;
    }
    ntStatus = ZwReadFile(file, NULL, NULL, NULL, &ioStatus, fileBuffer,
        BC250_PFP_FILE_BYTES, NULL, NULL);
    if (!NT_SUCCESS(ntStatus) || ioStatus.Information != BC250_PFP_FILE_BYTES) {
        status = BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED;
        goto Cleanup;
    }
    Bc250FirmwareSha256(fileBuffer, BC250_PFP_FILE_BYTES, digest);
    if (!Bc250FirmwareDigestEqual(digest, expectedFileDigest)) {
        status = BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED;
        goto Cleanup;
    }
    payloadDigest = Bc250GetCpFirmwarePayloadSha256(BC250_PSP_FW_CP_PFP);
    Bc250FirmwareSha256((PUCHAR)fileBuffer + 0x100u,
        BC250_PFP_PAYLOAD_BYTES_LOCAL, digest);
    if (payloadDigest == NULL ||
        !Bc250FirmwareDigestEqual(digest, payloadDigest)) {
        status = BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED;
        goto Cleanup;
    }

    vramStatus = Bc250VramAllocate(allocator, BC250_PFP_ALLOCATION_BYTES,
        PAGE_SIZE, BC250_VRAM_ROLE_FIRMWARE,
        BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_ALLOC_CPU_VISIBLE, &token);
    if (vramStatus != BC250_VRAM_OK) {
        status = BC250_PFP_DIRECT_LOAD_ALLOCATION_FAILED;
        goto Cleanup;
    }
    allocated = 1u;
    if (Bc250VramGetRegion(allocator, token, &region) != BC250_VRAM_OK ||
        region.Length != BC250_PFP_ALLOCATION_BYTES) {
        status = BC250_PFP_DIRECT_LOAD_REGION_FAILED;
        goto Cleanup;
    }
    for (i = 0u; i < Context->Core.ResolvedBarCount; ++i) {
        if (Context->Core.ResolvedBars[i].Index ==
            BC250_PCI_VRAM_APERTURE_BAR_INDEX) {
            bar = &Context->Core.ResolvedBars[i];
            ++matches;
        }
    }
    if (matches != 1u || bar == NULL ||
        bar->Length < BC250_PFP_ALLOCATION_BYTES ||
        region.Offset > bar->Length - BC250_PFP_ALLOCATION_BYTES ||
        addressSpace->VramMcStart > MAXULONGLONG - region.Offset) {
        status = BC250_PFP_DIRECT_LOAD_APERTURE_INVALID;
        goto Cleanup;
    }
    Evidence->FirmwareOffset = region.Offset;
    Evidence->FirmwareMcAddress = addressSpace->VramMcStart + region.Offset;
    Evidence->FirmwareBytes = BC250_PFP_PAYLOAD_BYTES_LOCAL;
    Evidence->JumpTableDwords = BC250_PFP_JT_DWORDS;
    Evidence->UcodeVersion = BC250_PFP_UCODE_VERSION;
    if ((Evidence->FirmwareMcAddress & (PAGE_SIZE - 1u)) != 0u) {
        status = BC250_PFP_DIRECT_LOAD_APERTURE_INVALID;
        goto Cleanup;
    }
    physical.QuadPart = (LONGLONG)(bar->Start + region.Offset);
    ntStatus = Context->Core.DxgkInterface.DxgkCbMapMemory(
        Context->Core.DxgkInterface.DeviceHandle, physical,
        BC250_PFP_ALLOCATION_BYTES, FALSE, FALSE, MmNonCached,
        (PVOID *)&mapped);
    if (!NT_SUCCESS(ntStatus) || mapped == NULL) {
        status = BC250_PFP_DIRECT_LOAD_MAP_FAILED;
        goto Cleanup;
    }
    mappedValid = 1u;
    Bc250PfpCopyFromDevice(backup, mapped, mappedDwords);

    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        status = BC250_PFP_DIRECT_LOAD_MMIO_FAILED;
        goto Cleanup;
    }
    if (!Bc250PfpRead(Context, R_CP_ME_CNTL, &Evidence->CpMeCntlBefore) ||
        !Bc250PfpRead(Context, R_RLC_CNTL, &Evidence->RlcCntlBefore) ||
        !Bc250PfpRead(Context, R_CP_PFP_PC, &Evidence->PfpPcBefore) ||
        !Bc250PfpRead(Context, R_FAULT_STATUS,
            &Evidence->FaultStatusBefore)) {
        status = BC250_PFP_DIRECT_LOAD_MMIO_FAILED;
        goto UnmapMmio;
    }
    snapshotValid = 1u;
    if ((Evidence->CpMeCntlBefore & BC250_PFP_HALT) == 0u) {
        status = BC250_PFP_DIRECT_LOAD_INHERITED_ACTIVE;
        goto Restore;
    }

    Bc250PfpCopyToDevice(mapped,
        (const uint32_t *)((PUCHAR)fileBuffer + 0x100u), payloadDwords);
    for (i = payloadDwords; i < mappedDwords; ++i)
        WRITE_REGISTER_ULONG((volatile ULONG *)&mapped[i], 0u);
    KeMemoryBarrier();
    memoryPrepared = 1u;
    Evidence->StagedDwords = payloadDwords;
    for (i = 0u; i < payloadDwords; ++i) {
        if (READ_REGISTER_ULONG((volatile ULONG *)&mapped[i]) !=
            ((uint32_t *)((PUCHAR)fileBuffer + 0x100u))[i])
            ++Evidence->StagingMismatches;
    }
    if (Evidence->StagingMismatches != 0u) {
        status = BC250_PFP_DIRECT_LOAD_STAGING_FAILED;
        goto Restore;
    }

    /* Micro-Gate 1: Staging de VRAM sob HALT absoluto.
     * Nao escrever em portas write-only do CP (IC_BASE, UCODE) e nao
     * deshaltar CP_ME_CNTL. Preservar o video do BasicDisplay ativo. */
    registersTouched = 0u;
    Evidence->WriteOnlyPortsProgrammed = 0u;
    Evidence->CpMeCntlApplied = Evidence->CpMeCntlBefore;
    Evidence->RlcCntlApplied = Evidence->RlcCntlBefore;
    Evidence->PfpPcFirst = Evidence->PfpPcBefore;
    Evidence->PfpPcLast = Evidence->PfpPcBefore;
    Evidence->PfpPcSamples = 1u;
    Evidence->PfpPcTransitions = 0u;

    (void)Bc250PfpRead(Context, R_FAULT_STATUS,
        &Evidence->FaultStatusAfter);
    (void)Bc250PfpRead(Context, R_FAULT_ADDR_LO,
        &Evidence->FaultAddressLow);
    (void)Bc250PfpRead(Context, R_FAULT_ADDR_HI,
        &Evidence->FaultAddressHigh);

    /* Gate 1 aprovado no staging: 65792 dwords conferidos e zero mismatches */
    status = BC250_PFP_DIRECT_LOAD_OK;

Restore:
    if (registersTouched != 0u && snapshotValid != 0u) {
        (void)Bc250PfpWrite(Context, R_CP_ME_CNTL,
            Evidence->CpMeCntlBefore);
        (void)Bc250PfpWrite(Context, R_RLC_CNTL,
            Evidence->RlcCntlBefore);
        KeStallExecutionProcessor(50u);
        if (Bc250PfpRead(Context, R_CP_ME_CNTL,
                &Evidence->CpMeCntlAfter) &&
            Evidence->CpMeCntlAfter == Evidence->CpMeCntlBefore &&
            Bc250PfpRead(Context, R_RLC_CNTL, &value) &&
            value == Evidence->RlcCntlBefore)
            Evidence->RegisterRestorePassed = 1u;
    } else if (registersTouched == 0u && snapshotValid != 0u) {
        Evidence->RegisterRestorePassed = 1u;
        Evidence->CpMeCntlAfter = Evidence->CpMeCntlBefore;
    }
    if (memoryPrepared != 0u) {
        Bc250PfpCopyToDevice(mapped, backup, mappedDwords);
        Evidence->MemoryRestorePassed = 1u;
        for (i = 0u; i < mappedDwords; ++i) {
            if (READ_REGISTER_ULONG((volatile ULONG *)&mapped[i]) != backup[i]) {
                Evidence->MemoryRestorePassed = 0u;
                break;
            }
        }
    }
    if ((registersTouched != 0u &&
            Evidence->RegisterRestorePassed == 0u) ||
        (memoryPrepared != 0u && Evidence->MemoryRestorePassed == 0u))
        status = BC250_PFP_DIRECT_LOAD_RESTORE_FAILED;
    if (status == BC250_PFP_DIRECT_LOAD_OK)
        Evidence->PhysicalReady = 1u;

UnmapMmio:
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK &&
        status == BC250_PFP_DIRECT_LOAD_OK) {
        status = BC250_PFP_DIRECT_LOAD_UNMAP_FAILED;
        Evidence->PhysicalReady = 0u;
    }

Cleanup:
    if (file != NULL) ZwClose(file);
    if (mappedValid != 0u && !NT_SUCCESS(
            Context->Core.DxgkInterface.DxgkCbUnmapMemory(
                Context->Core.DxgkInterface.DeviceHandle, (PVOID)mapped)) &&
        status == BC250_PFP_DIRECT_LOAD_OK) {
        status = BC250_PFP_DIRECT_LOAD_UNMAP_FAILED;
        Evidence->PhysicalReady = 0u;
    }
    if (allocated != 0u &&
        Bc250VramRelease(allocator, token) != BC250_VRAM_OK &&
        status == BC250_PFP_DIRECT_LOAD_OK) {
        status = BC250_PFP_DIRECT_LOAD_RELEASE_FAILED;
        Evidence->PhysicalReady = 0u;
    }
    if (backup != NULL) ExFreePoolWithTag(backup, BC250_PFP_POOL_TAG);
    if (fileBuffer != NULL) ExFreePoolWithTag(fileBuffer, BC250_PFP_POOL_TAG);
    if (status != BC250_PFP_DIRECT_LOAD_OK) Evidence->PhysicalReady = 0u;
    return status;
#else
    UNREFERENCED_PARAMETER(Context);
    if (Evidence != NULL) RtlZeroMemory(Evidence, sizeof(*Evidence));
    return BC250_PFP_DIRECT_LOAD_BAD_ARGUMENT;
#endif
}
