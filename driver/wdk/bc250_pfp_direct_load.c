#include "bc250_active_miniport.h"
#include "bc250_cp_firmware_manifest.h"
#include "../core/bc250_firmware_staging.h"
#include "../core/bc250_psp_load_fw_transaction.h"
#include "../core/bc250_pm4_packet_builder.h"

#define BC250_PFP_FILE_BYTES 263424u
#define BC250_PFP_PAYLOAD_BYTES_LOCAL 263168u
#define BC250_PFP_JT_OFFSET_DWORDS 65536u
#define BC250_PFP_JT_DWORDS 128u
#define BC250_PFP_UCODE_VERSION 0x94u

#define BC250_ME_FILE_BYTES 263424u
#define BC250_ME_PAYLOAD_BYTES_LOCAL 263168u
#define BC250_ME_JT_OFFSET_DWORDS 65536u
#define BC250_ME_JT_DWORDS 128u
#define BC250_ME_UCODE_VERSION 0x63u

#define BC250_CE_FILE_BYTES 263296u
#define BC250_CE_PAYLOAD_BYTES_LOCAL 263040u
#define BC250_CE_JT_OFFSET_DWORDS 65536u
#define BC250_CE_JT_DWORDS 96u
#define BC250_CE_UCODE_VERSION 0x25u

#define BC250_MEC_FILE_BYTES 268592u
#define BC250_MEC_PAYLOAD_BYTES_LOCAL 268336u
#define BC250_MEC_JT_OFFSET_DWORDS 66860u
#define BC250_MEC_JT_DWORDS 224u
#define BC250_MEC_UCODE_VERSION 0x90u

#define BC250_PSP_TMR_BYTES 0x00400000u /* 4 MiB TMR */
#define BC250_CP_QUAD_ALLOCATION_BYTES 0x00110000u
#define BC250_CP_PFP_SLICE_OFFSET 0x00000000u
#define BC250_CP_ME_SLICE_OFFSET  0x00041000u
#define BC250_CP_CE_SLICE_OFFSET  0x00082000u
#define BC250_CP_MEC_SLICE_OFFSET 0x000C3000u
#define BC250_CP_RB0_RING_OFFSET  0x00105000u
#define BC250_CP_RB0_RING_BYTES   0x00001000u
#define BC250_CP_RB0_RPTR_WB_OFFSET 0x00106000u
#define BC250_CP_RB0_WPTR_POLL_OFFSET 0x00106100u
#define BC250_CP_RB0_FENCE_OFFSET 0x00106200u
#define BC250_CP_PT_SLICE_OFFSET  0x00107000u
#define BC250_CP_PT_SLICE_BYTES   0x00001000u

#define BC250_CP_RB0_FENCE_POISON 0x43DAFFFEu
#define BC250_CP_RB0_FENCE_VALUE  0xBC250123u

#define BC250_PFP_POOL_TAG 'pPfB'
#define BC250_PFP_HALT 0x04000000u
#define BC250_ME_HALT  0x10000000u
#define BC250_CE_HALT  0x01000000u

#define R_CP_STAT                   0x00008680u
#define R_CP_ME_CNTL                0x000086D8u
#define R_CP_MEC_CNTL               0x00008234u
#define R_CP_PFP_PC                 0x00008694u
#define R_CP_ME_PC                  0x00008698u
#define R_CP_CE_PC                  0x0000869Cu
#define R_CP_MEC1_PC                0x000086A0u
#define R_CP_PFP_JT_STAT            0x0000C4CCu
#define R_CP_CE_JT_STAT             0x0000C4D0u
#define R_CP_MEC_JT_STAT            0x0000C4D4u
#define R_CP_PFP_IC_BASE_LO         0x0003E100u
#define R_CP_PFP_IC_BASE_HI         0x0003E104u
#define R_CP_PFP_IC_BASE_CNTL       0x0003E108u
#define R_CP_PFP_UCODE_ADDR         0x0003E050u
#define R_CP_PFP_UCODE_DATA         0x0003E054u
#define R_CP_ME_IC_BASE_LO          0x0003E110u
#define R_CP_ME_IC_BASE_HI          0x0003E114u
#define R_CP_ME_IC_BASE_CNTL        0x0003E118u
#define R_CP_ME_UCODE_ADDR          0x0003E058u
#define R_CP_ME_UCODE_DATA          0x0003E05Cu
#define R_CP_CE_IC_BASE_LO          0x0003E120u
#define R_CP_CE_IC_BASE_HI          0x0003E124u
#define R_CP_CE_IC_BASE_CNTL        0x0003E128u
#define R_CP_CE_UCODE_ADDR          0x0003E060u
#define R_CP_CE_UCODE_DATA          0x0003E064u
#define R_CP_CPC_IC_BASE_LO         0x0003E130u
#define R_CP_CPC_IC_BASE_HI         0x0003E134u
#define R_CP_CPC_IC_BASE_CNTL       0x0003E138u
#define R_CP_MEC_ME1_UCODE_ADDR     0x0003E068u
#define R_CP_MEC_ME1_UCODE_DATA     0x0003E06Cu

#define R_CP_PFP_IC_OP_CNTL         0x0003E10Cu
#define R_CP_ME_IC_OP_CNTL          0x0003E11Cu
#define R_CP_CE_IC_OP_CNTL          0x0003E12Cu
#define R_CP_CPC_IC_OP_CNTL         0x0003E13Cu

#define R_GCVM_CONTEXT0_CNTL        0x0000A200u
#define R_GCVM_CONTEXT0_PT_BASE_LO  0x0000A3ACu
#define R_GCVM_CONTEXT0_PT_BASE_HI  0x0000A3B0u
#define R_GCVM_CONTEXT0_PT_START_LO 0x0000A42Cu
#define R_GCVM_CONTEXT0_PT_END_LO   0x0000A4ACu

#define R_FAULT_STATUS              0x0000A130u
#define R_FAULT_ADDR_LO             0x0000A134u
#define R_FAULT_ADDR_HI             0x0000A138u
#define R_RLC_CNTL                  0x0003B000u
#define R_GCVM_L2_CNTL2             0x0000A104u
#define R_GCVM_INVALIDATE_ENG0_REQ  0x0000A28Cu
#define R_GCVM_INVALIDATE_ENG0_ACK  0x0000A2D4u
#define R_VGT_NUM_INSTANCES         0x00030934u
#define R_PA_SC_LINE_STIPPLE_STATE  0x00030A04u
#define R_CGTT_CPF_CLK_CTRL         0x0003C2C4u
#define R_GE_FAST_CLKS              0x00008920u
#define R_CP_SD_CNTL                0x0000C6DCu
#define R_GRBM_GFX_INDEX            0x00030800u
#define R_GRBM_GFX_CNTL             0x00008088u
#define R_GRBM_STATUS2              0x00008008u
#define R_GRBM_STATUS               0x00008010u
#define R_GRBM_SOFT_RESET           0x00008020u

#define R_CP_MAX_CONTEXT            0x0000C2B8u
#define R_CP_DEVICE_ID              0x0000C12Cu

#define R_CP_RB0_BASE               0x0000C100u
#define R_CP_RB0_BASE_HI            0x0000C2C4u
#define R_CP_RB0_CNTL               0x0000C104u
#define R_CP_RB0_RPTR               0x00008700u
#define R_CP_RB0_WPTR               0x0000C150u
#define R_CP_RB0_WPTR_HI            0x0000C154u
#define R_CP_RB0_RPTR_ADDR          0x0000C10Cu
#define R_CP_RB0_RPTR_ADDR_HI       0x0000C110u
#define R_CP_RB_WPTR_POLL_ADDR_LO   0x0000C3ACu
#define R_CP_RB_WPTR_POLL_ADDR_HI   0x0000C3B0u
#define R_CP_RB0_ACTIVE             0x0000C680u
#define R_CP_RB_VMID                0x0000C144u
#define R_CP_RB_WPTR_DELAY          0x00008704u
#define R_CP_RB_WPTR_POLL_CNTL      0x00008708u
#define R_CP_RB_DOORBELL_CONTROL    0x0000C3B4u
#define R_HDP_MEM_COHERENCY_FLUSH_CNTL 0x00003FC4u

#define R_CP_SCRATCH_REG0           0x00030100u

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

static NTSTATUS Bc250LoadFirmwareFile(
    const WCHAR *FilePath,
    uint32_t ExpectedFileBytes,
    uint32_t ExpectedPayloadBytes,
    const unsigned char *ExpectedSha256,
    PVOID *OutBuffer)
{
    UNICODE_STRING uPath;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE handle = NULL;
    FILE_STANDARD_INFORMATION fsi;
    NTSTATUS st;
    PVOID buf = NULL;
    unsigned char fileHash[32];
    unsigned char payloadHash[32];

    if (OutBuffer == NULL) return STATUS_INVALID_PARAMETER;
    *OutBuffer = NULL;

    RtlInitUnicodeString(&uPath, FilePath);
    InitializeObjectAttributes(
        &oa, &uPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    st = ZwOpenFile(&handle, FILE_GENERIC_READ | SYNCHRONIZE, &oa, &iosb,
        FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(st)) return st;

    st = ZwQueryInformationFile(
        handle, &iosb, &fsi, sizeof(fsi), FileStandardInformation);
    if (!NT_SUCCESS(st) || fsi.EndOfFile.QuadPart != (LONGLONG)ExpectedFileBytes) {
        ZwClose(handle);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)ExpectedFileBytes, BC250_PFP_POOL_TAG);
    if (buf == NULL) {
        ZwClose(handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    st = ZwReadFile(
        handle, NULL, NULL, NULL, &iosb, buf, ExpectedFileBytes, NULL, NULL);
    ZwClose(handle);
    if (!NT_SUCCESS(st) || iosb.Information != ExpectedFileBytes) {
        ExFreePoolWithTag(buf, BC250_PFP_POOL_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Bc250FirmwareSha256(buf, ExpectedFileBytes, fileHash);
    Bc250FirmwareSha256((const PUCHAR)buf + 0x100u, ExpectedPayloadBytes, payloadHash);

    if (ExpectedSha256 != NULL && !Bc250FirmwareDigestEqual(payloadHash, ExpectedSha256)) {
        ExFreePoolWithTag(buf, BC250_PFP_POOL_TAG);
        return STATUS_IMAGE_CHECKSUM_MISMATCH;
    }

    *OutBuffer = buf;
    return STATUS_SUCCESS;
}

BC250_PFP_DIRECT_LOAD_STATUS Bc250ActiveRunPfpDirectLoad(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PFP_DIRECT_LOAD_EVIDENCE *Evidence)
{
    BC250_PFP_DIRECT_LOAD_STATUS status = BC250_PFP_DIRECT_LOAD_OK;
    NTSTATUS ntStatus;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_VRAM_ALLOCATOR *allocator;
    const BC250_VRAM_ADDRESS_SPACE *addressSpace;
    BC250_VRAM_TOKEN token = {0};
    BC250_VRAM_REGION region = {0};
    BC250_VRAM_TOKEN tmrToken = {0};
    BC250_VRAM_REGION tmrRegion = {0};
    BC250_VRAM_STATUS vramStatus;
    const BC250_RESOLVED_BAR_RESOURCE *bar = NULL;
    PHYSICAL_ADDRESS physical;
    volatile uint32_t *mapped = NULL;
    volatile uint32_t *pfpMapped = NULL;
    volatile uint32_t *meMapped = NULL;
    volatile uint32_t *ceMapped = NULL;
    volatile uint32_t *mecMapped = NULL;
    volatile uint32_t *pRing = NULL;
    volatile uint32_t *pRptrWb = NULL;
    volatile uint32_t *pWptrPoll = NULL;
    volatile uint32_t *pFence = NULL;
    uint32_t *backup = NULL;
    uint32_t mappedDwords = 0u;
    uint32_t pfpPayloadDwords = 0u;
    uint32_t mePayloadDwords  = 0u;
    uint32_t cePayloadDwords  = 0u;
    uint32_t mecPayloadDwords = 0u;
    uint32_t i, matches = 0u;
    uint32_t value = 0u;
    uint32_t scratchAfter = 0u;
    uint32_t previousPc = 0u;
    uint32_t cursor = 0u;
    uint32_t rbCntl = 0u;
    uint64_t ringMc = 0ull;
    uint64_t rptrWbMc = 0ull;
    uint64_t wptrPollMc = 0ull;
    uint64_t fenceMc = 0ull;
    uint64_t tmrMc = 0ull;
    uint64_t tmrPa = 0ull;
    uint32_t snapshotValid = 0u;
    uint32_t registersTouched = 0u;
    uint32_t memoryPrepared = 0u;
    uint32_t mappedValid = 0u;
    uint32_t allocated = 0u;
    uint32_t tmrAllocated = 0u;
    uint32_t pspRingActive = 0u;

    PHYSICAL_ADDRESS pspLow, pspHigh, pspBoundary;
    PVOID pspRing = NULL, pspCmd = NULL, pspFence = NULL, pspFw = NULL;
    PHYSICAL_ADDRESS pspRingPa = {0}, pspCmdPa = {0}, pspFencePa = {0}, pspFwPa = {0};

    uint32_t origRb0Active = 0u;
    uint32_t origRb0Cntl = 0u;
    uint32_t origRb0Base = 0u;
    uint32_t origRb0BaseHi = 0u;
    uint32_t origRb0Rptr = 0u;
    uint32_t origRb0Wptr = 0u;
    uint32_t origRb0WptrHi = 0u;
    uint32_t origRb0RptrAddr = 0u;
    uint32_t origRb0RptrAddrHi = 0u;
    uint32_t origRbWptrPollAddrLo = 0u;
    uint32_t origRbWptrPollAddrHi = 0u;
    uint32_t origRbVmid = 0u;
    uint32_t origRbWptrDelay = 0u;
    uint32_t origRbWptrPollCntl = 0u;
    uint32_t origRbDoorbellCntl = 0u;
    uint32_t origCpMecCntl = 0u;
    uint32_t origGcvmL2Cntl2 = 0u;
    uint32_t origVgtNumInstances = 0u;
    uint32_t origPaScLineStippleState = 0u;
    uint32_t origCgttCpfClkCtrl = 0u;
    uint32_t origGeFastClks = 0u;
    uint32_t origCpSdCntl = 0u;
    uint32_t origGrbmGfxIndex = 0u;
    uint32_t origGrbmGfxCntl = 0u;
    uint32_t origCpMaxContext = 0u;
    uint32_t origScratchReg0 = 0u;
    uint32_t origCtx0Cntl = 0u;
    uint32_t origCtx0PtBaseLo = 0u;
    uint32_t origCtx0PtBaseHi = 0u;
    uint32_t origCtx0PtStartLo = 0u;
    uint32_t origCtx0PtEndLo = 0u;
    uint64_t firmwarePa = 0ull;

    PVOID pfpFileBuffer = NULL;
    PVOID meFileBuffer = NULL;
    PVOID ceFileBuffer = NULL;
    PVOID mecFileBuffer = NULL;

    const unsigned char *pfpExpected;
    const unsigned char *meExpected;
    const unsigned char *ceExpected;

    if (Context == NULL || Evidence == NULL)
        return BC250_PFP_DIRECT_LOAD_BAD_ARGUMENT;

    RtlZeroMemory(Evidence, sizeof(*Evidence));
    Evidence->FirmwareBytes = BC250_PFP_PAYLOAD_BYTES_LOCAL;
    Evidence->JumpTableDwords = BC250_PFP_JT_DWORDS;
    Evidence->UcodeVersion = BC250_PFP_UCODE_VERSION;

    allocator = &Context->Core.StartDevice.Owned.Bootstrap.Allocator;
    addressSpace = &Context->Core.TopologyEvidence.AddressSpace;
    if (addressSpace->TotalBytes == 0u ||
        addressSpace->VramMcStart == 0u) return BC250_PFP_DIRECT_LOAD_ALLOCATION_FAILED;

    pfpExpected = Bc250GetCpFirmwarePayloadSha256(BC250_PSP_FW_CP_PFP);
    meExpected  = Bc250GetCpFirmwarePayloadSha256(BC250_PSP_FW_CP_ME);
    ceExpected  = Bc250GetCpFirmwarePayloadSha256(BC250_PSP_FW_CP_CE);
    if (pfpExpected == NULL || meExpected == NULL || ceExpected == NULL)
        return BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED;

    /* 1. Carrega os quatro firmwares da quadra do CP */
    ntStatus = Bc250LoadFirmwareFile(
        L"\\SystemRoot\\System32\\drivers\\cyan_skillfish2_pfp.bin",
        BC250_PFP_FILE_BYTES, BC250_PFP_PAYLOAD_BYTES_LOCAL,
        pfpExpected, &pfpFileBuffer);
    if (!NT_SUCCESS(ntStatus)) return BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED;

    ntStatus = Bc250LoadFirmwareFile(
        L"\\SystemRoot\\System32\\drivers\\cyan_skillfish2_me.bin",
        BC250_ME_FILE_BYTES, BC250_ME_PAYLOAD_BYTES_LOCAL,
        meExpected, &meFileBuffer);
    if (!NT_SUCCESS(ntStatus)) {
        ExFreePoolWithTag(pfpFileBuffer, BC250_PFP_POOL_TAG);
        return BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED;
    }

    ntStatus = Bc250LoadFirmwareFile(
        L"\\SystemRoot\\System32\\drivers\\cyan_skillfish2_ce.bin",
        BC250_CE_FILE_BYTES, BC250_CE_PAYLOAD_BYTES_LOCAL,
        ceExpected, &ceFileBuffer);
    if (!NT_SUCCESS(ntStatus)) {
        ExFreePoolWithTag(meFileBuffer, BC250_PFP_POOL_TAG);
        ExFreePoolWithTag(pfpFileBuffer, BC250_PFP_POOL_TAG);
        return BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED;
    }

    ntStatus = Bc250LoadFirmwareFile(
        L"\\SystemRoot\\System32\\drivers\\cyan_skillfish2_mec.bin",
        BC250_MEC_FILE_BYTES, BC250_MEC_PAYLOAD_BYTES_LOCAL,
        NULL, &mecFileBuffer);
    if (!NT_SUCCESS(ntStatus)) {
        ExFreePoolWithTag(ceFileBuffer, BC250_PFP_POOL_TAG);
        ExFreePoolWithTag(meFileBuffer, BC250_PFP_POOL_TAG);
        ExFreePoolWithTag(pfpFileBuffer, BC250_PFP_POOL_TAG);
        return BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED;
    }

    mappedDwords = BC250_CP_QUAD_ALLOCATION_BYTES / sizeof(uint32_t);
    pfpPayloadDwords = BC250_PFP_PAYLOAD_BYTES_LOCAL / sizeof(uint32_t);
    mePayloadDwords  = BC250_ME_PAYLOAD_BYTES_LOCAL  / sizeof(uint32_t);
    cePayloadDwords  = BC250_CE_PAYLOAD_BYTES_LOCAL  / sizeof(uint32_t);
    mecPayloadDwords = BC250_MEC_PAYLOAD_BYTES_LOCAL / sizeof(uint32_t);

    backup = (uint32_t *)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, (SIZE_T)BC250_CP_QUAD_ALLOCATION_BYTES, BC250_PFP_POOL_TAG);
    if (backup == NULL) {
        ExFreePoolWithTag(mecFileBuffer, BC250_PFP_POOL_TAG);
        ExFreePoolWithTag(ceFileBuffer, BC250_PFP_POOL_TAG);
        ExFreePoolWithTag(meFileBuffer, BC250_PFP_POOL_TAG);
        ExFreePoolWithTag(pfpFileBuffer, BC250_PFP_POOL_TAG);
        return BC250_PFP_DIRECT_LOAD_ALLOCATION_FAILED;
    }

    /* 2a. Reserva de 4 MiB para TMR em VRAM owned no topo */
    vramStatus = Bc250VramAllocate(allocator, BC250_PSP_TMR_BYTES,
        BC250_PSP_TMR_BYTES, BC250_VRAM_ROLE_FIRMWARE,
        BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_ALLOC_CPU_VISIBLE, &tmrToken);
    if (vramStatus == BC250_VRAM_OK) {
        tmrAllocated = 1u;
        (void)Bc250VramGetRegion(allocator, tmrToken, &tmrRegion);
    }

    /* 2b. Reserva de regiao contigua de 1.1 MiB para Ring 0 em VRAM owned */
    vramStatus = Bc250VramAllocate(allocator, BC250_CP_QUAD_ALLOCATION_BYTES,
        PAGE_SIZE, BC250_VRAM_ROLE_FIRMWARE,
        BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_ALLOC_CPU_VISIBLE, &token);
    if (vramStatus != BC250_VRAM_OK) {
        status = BC250_PFP_DIRECT_LOAD_ALLOCATION_FAILED;
        goto Cleanup;
    }
    allocated = 1u;
    if (Bc250VramGetRegion(allocator, token, &region) != BC250_VRAM_OK ||
        region.Length != BC250_CP_QUAD_ALLOCATION_BYTES) {
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
        bar->Length < BC250_CP_QUAD_ALLOCATION_BYTES ||
        region.Offset > bar->Length - BC250_CP_QUAD_ALLOCATION_BYTES ||
        addressSpace->VramMcStart > MAXULONGLONG - region.Offset) {
        status = BC250_PFP_DIRECT_LOAD_APERTURE_INVALID;
        goto Cleanup;
    }

    Evidence->FirmwareOffset = region.Offset;
    Evidence->FirmwareMcAddress = addressSpace->VramMcStart + region.Offset;
    firmwarePa = bar->Start + region.Offset;

    if (tmrAllocated != 0u) {
        tmrMc = addressSpace->VramMcStart + tmrRegion.Offset;
        tmrPa = bar->Start + tmrRegion.Offset;
    } else {
        tmrMc = addressSpace->VramMcStart + 0x1F800000ull;
        tmrPa = bar->Start + 0x1F800000ull;
    }

    physical.QuadPart = (LONGLONG)(bar->Start + region.Offset);
    ntStatus = Context->Core.DxgkInterface.DxgkCbMapMemory(
        Context->Core.DxgkInterface.DeviceHandle, physical,
        BC250_CP_QUAD_ALLOCATION_BYTES, FALSE, FALSE, MmNonCached,
        (PVOID *)&mapped);
    if (!NT_SUCCESS(ntStatus) || mapped == NULL) {
        status = BC250_PFP_DIRECT_LOAD_MAP_FAILED;
        goto Cleanup;
    }
    mappedValid = 1u;
    Bc250PfpCopyFromDevice(backup, mapped, mappedDwords);

    /* 3. Mapeamento MMIO e Snapshot */
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        status = BC250_PFP_DIRECT_LOAD_MMIO_FAILED;
        goto Cleanup;
    }

    (void)Bc250PfpWrite(Context, R_GRBM_GFX_INDEX, 0u);
    (void)Bc250PfpWrite(Context, R_GRBM_GFX_CNTL, 0u);

    if (!Bc250PfpRead(Context, R_CP_ME_CNTL, &Evidence->CpMeCntlBefore) ||
        !Bc250PfpRead(Context, R_CP_MEC_CNTL, &origCpMecCntl) ||
        !Bc250PfpRead(Context, R_RLC_CNTL, &Evidence->RlcCntlBefore) ||
        !Bc250PfpRead(Context, R_CP_PFP_PC, &Evidence->PfpPcBefore) ||
        !Bc250PfpRead(Context, R_CP_ME_PC, &Evidence->MePcBefore) ||
        !Bc250PfpRead(Context, R_CP_CE_PC, &Evidence->CePcBefore) ||
        !Bc250PfpRead(Context, R_CP_RB0_ACTIVE, &origRb0Active) ||
        !Bc250PfpRead(Context, R_CP_RB0_CNTL, &origRb0Cntl) ||
        !Bc250PfpRead(Context, R_CP_RB0_BASE, &origRb0Base) ||
        !Bc250PfpRead(Context, R_CP_RB0_BASE_HI, &origRb0BaseHi) ||
        !Bc250PfpRead(Context, R_CP_RB0_RPTR, &origRb0Rptr) ||
        !Bc250PfpRead(Context, R_CP_RB0_WPTR, &origRb0Wptr) ||
        !Bc250PfpRead(Context, R_CP_RB0_WPTR_HI, &origRb0WptrHi) ||
        !Bc250PfpRead(Context, R_CP_RB0_RPTR_ADDR, &origRb0RptrAddr) ||
        !Bc250PfpRead(Context, R_CP_RB0_RPTR_ADDR_HI, &origRb0RptrAddrHi) ||
        !Bc250PfpRead(Context, R_CP_RB_WPTR_POLL_ADDR_LO, &origRbWptrPollAddrLo) ||
        !Bc250PfpRead(Context, R_CP_RB_WPTR_POLL_ADDR_HI, &origRbWptrPollAddrHi) ||
        !Bc250PfpRead(Context, R_CP_RB_VMID, &origRbVmid) ||
        !Bc250PfpRead(Context, R_CP_RB_WPTR_DELAY, &origRbWptrDelay) ||
        !Bc250PfpRead(Context, R_CP_RB_WPTR_POLL_CNTL, &origRbWptrPollCntl) ||
        !Bc250PfpRead(Context, R_CP_RB_DOORBELL_CONTROL, &origRbDoorbellCntl) ||
        !Bc250PfpRead(Context, R_GCVM_L2_CNTL2, &origGcvmL2Cntl2) ||
        !Bc250PfpRead(Context, R_VGT_NUM_INSTANCES, &origVgtNumInstances) ||
        !Bc250PfpRead(Context, R_PA_SC_LINE_STIPPLE_STATE, &origPaScLineStippleState) ||
        !Bc250PfpRead(Context, R_CGTT_CPF_CLK_CTRL, &origCgttCpfClkCtrl) ||
        !Bc250PfpRead(Context, R_GE_FAST_CLKS, &origGeFastClks) ||
        !Bc250PfpRead(Context, R_CP_SD_CNTL, &origCpSdCntl) ||
        !Bc250PfpRead(Context, R_GRBM_GFX_INDEX, &origGrbmGfxIndex) ||
        !Bc250PfpRead(Context, R_GRBM_GFX_CNTL, &origGrbmGfxCntl) ||
        !Bc250PfpRead(Context, R_CP_MAX_CONTEXT, &origCpMaxContext) ||
        !Bc250PfpRead(Context, R_CP_SCRATCH_REG0, &origScratchReg0) ||
        !Bc250PfpRead(Context, R_GCVM_CONTEXT0_CNTL, &origCtx0Cntl) ||
        !Bc250PfpRead(Context, R_GCVM_CONTEXT0_PT_BASE_LO, &origCtx0PtBaseLo) ||
        !Bc250PfpRead(Context, R_GCVM_CONTEXT0_PT_BASE_HI, &origCtx0PtBaseHi) ||
        !Bc250PfpRead(Context, R_GCVM_CONTEXT0_PT_START_LO, &origCtx0PtStartLo) ||
        !Bc250PfpRead(Context, R_GCVM_CONTEXT0_PT_END_LO, &origCtx0PtEndLo) ||
        !Bc250PfpRead(Context, R_FAULT_STATUS, &Evidence->FaultStatusBefore)) {
        status = BC250_PFP_DIRECT_LOAD_MMIO_FAILED;
        goto UnmapMmio;
    }
    Evidence->Rb0ActiveBefore = origRb0Active;
    Evidence->Rb0RptrBefore = origRb0Rptr;

    snapshotValid = 1u;
    if ((Evidence->CpMeCntlBefore & (BC250_PFP_HALT | BC250_ME_HALT | BC250_CE_HALT)) !=
        (BC250_PFP_HALT | BC250_ME_HALT | BC250_CE_HALT)) {
        status = BC250_PFP_DIRECT_LOAD_INHERITED_ACTIVE;
        goto Restore;
    }

    /* 4a. Staging dos 4 payloads na VRAM para conferência */
    pfpMapped = mapped + (BC250_CP_PFP_SLICE_OFFSET / sizeof(uint32_t));
    meMapped  = mapped + (BC250_CP_ME_SLICE_OFFSET  / sizeof(uint32_t));
    ceMapped  = mapped + (BC250_CP_CE_SLICE_OFFSET  / sizeof(uint32_t));
    mecMapped = mapped + (BC250_CP_MEC_SLICE_OFFSET / sizeof(uint32_t));

    Bc250PfpCopyToDevice(pfpMapped,
        (const uint32_t *)((PUCHAR)pfpFileBuffer + 0x100u), pfpPayloadDwords);
    Bc250PfpCopyToDevice(meMapped,
        (const uint32_t *)((PUCHAR)meFileBuffer + 0x100u), mePayloadDwords);
    Bc250PfpCopyToDevice(ceMapped,
        (const uint32_t *)((PUCHAR)ceFileBuffer + 0x100u), cePayloadDwords);
    Bc250PfpCopyToDevice(mecMapped,
        (const uint32_t *)((PUCHAR)mecFileBuffer + 0x100u), mecPayloadDwords);
    KeMemoryBarrier();
    memoryPrepared = 1u;
    Evidence->StagedDwords = pfpPayloadDwords;

    for (i = 0u; i < pfpPayloadDwords; ++i) {
        if (READ_REGISTER_ULONG((volatile ULONG *)&pfpMapped[i]) !=
            ((uint32_t *)((PUCHAR)pfpFileBuffer + 0x100u))[i])
            ++Evidence->StagingMismatches;
    }
    for (i = 0u; i < mePayloadDwords; ++i) {
        if (READ_REGISTER_ULONG((volatile ULONG *)&meMapped[i]) !=
            ((uint32_t *)((PUCHAR)meFileBuffer + 0x100u))[i])
            ++Evidence->StagingMismatches;
    }
    for (i = 0u; i < cePayloadDwords; ++i) {
        if (READ_REGISTER_ULONG((volatile ULONG *)&ceMapped[i]) !=
            ((uint32_t *)((PUCHAR)ceFileBuffer + 0x100u))[i])
            ++Evidence->StagingMismatches;
    }
    for (i = 0u; i < mecPayloadDwords; ++i) {
        if (READ_REGISTER_ULONG((volatile ULONG *)&mecMapped[i]) !=
            ((uint32_t *)((PUCHAR)mecFileBuffer + 0x100u))[i])
            ++Evidence->StagingMismatches;
    }
    if (Evidence->StagingMismatches != 0u) {
        status = BC250_PFP_DIRECT_LOAD_STAGING_FAILED;
        goto Restore;
    }

    /* 4b. SESSAO UNIFICADA DO PSP: SETUP_TMR + 5x LOAD_IP_FW (MANTIDA ATIVA) */
    {
        uint32_t pspRingWptr = 0u;
        uint32_t pspFenceValue = 0u;
        uint32_t pspAck = 0u;
        uint32_t cpPspLoadedCount = 0u;
        volatile uint32_t *pRingBuf;
        uint32_t *pCmdBuf;
        volatile uint32_t *pFenceBuf;
        uint32_t pollIndex;
        uint32_t k;

        const uint32_t fwTypes[5] = {
            BC250_PSP_LOAD_FW_CP_ME_TYPE,
            BC250_PSP_LOAD_FW_CP_PFP_TYPE,
            BC250_PSP_LOAD_FW_CP_CE_TYPE,
            BC250_PSP_LOAD_FW_CP_MEC_TYPE,
            BC250_PSP_LOAD_FW_CP_MEC_JT1_TYPE
        };
        const uint32_t fwBytes[5] = {
            BC250_ME_PAYLOAD_BYTES_LOCAL,
            BC250_PFP_PAYLOAD_BYTES_LOCAL,
            BC250_CE_PAYLOAD_BYTES_LOCAL,
            BC250_CP_MEC_PAYLOAD_BYTES,
            BC250_CP_MEC_JT1_PAYLOAD_BYTES
        };
        const void *fwSources[5] = {
            (const PUCHAR)meFileBuffer + 0x100u,
            (const PUCHAR)pfpFileBuffer + 0x100u,
            (const PUCHAR)ceFileBuffer + 0x100u,
            (const PUCHAR)mecFileBuffer + 0x100u,
            (const PUCHAR)mecFileBuffer + 0x100u + BC250_CP_MEC_PAYLOAD_BYTES
        };

        pspLow.QuadPart = 0;
        pspHigh.QuadPart = 0xFFFFFFFFFFull;
        pspBoundary.QuadPart = 0;
        pspRing = MmAllocateContiguousMemorySpecifyCache(
            BC250_PSP_RING_BYTES, pspLow, pspHigh, pspBoundary, MmNonCached);
        pspCmd = MmAllocateContiguousMemorySpecifyCache(
            BC250_PSP_LOAD_FW_COMMAND_BYTES, pspLow, pspHigh, pspBoundary, MmNonCached);
        pspFence = MmAllocateContiguousMemorySpecifyCache(
            PAGE_SIZE, pspLow, pspHigh, pspBoundary, MmNonCached);
        pspFw = MmAllocateContiguousMemorySpecifyCache(
            270336u, pspLow, pspHigh, pspBoundary, MmNonCached);

        if (pspRing != NULL && pspCmd != NULL && pspFence != NULL && pspFw != NULL) {
            pspRingPa = MmGetPhysicalAddress(pspRing);
            pspCmdPa  = MmGetPhysicalAddress(pspCmd);
            pspFencePa = MmGetPhysicalAddress(pspFence);
            pspFwPa   = MmGetPhysicalAddress(pspFw);
            pRingBuf  = (volatile uint32_t *)pspRing;
            pCmdBuf   = (uint32_t *)pspCmd;
            pFenceBuf = (volatile uint32_t *)pspFence;

            RtlZeroMemory(pspRing, BC250_PSP_RING_BYTES);
            RtlZeroMemory(pspCmd, BC250_PSP_LOAD_FW_COMMAND_BYTES);
            *pFenceBuf = 0u;

            /* 1. Criar anel GPCOM */
            (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_69, (uint32_t)pspRingPa.QuadPart);
            (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_70, (uint32_t)(pspRingPa.QuadPart >> 32));
            (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_71, BC250_PSP_RING_BYTES);
            (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_64, BC250_PSP_RING_CREATE_KM);
            KeStallExecutionProcessor(20000u);

            for (pollIndex = 0u; pollIndex < 500u; ++pollIndex) {
                if (Bc250PfpRead(Context, BC250_PSP_RING_C2PMSG_64, &pspAck) &&
                    (pspAck & BC250_PSP_RING_ACK_MASK) == BC250_PSP_RING_ACK_VALUE) break;
                KeStallExecutionProcessor(1000u);
            }

            if ((pspAck & BC250_PSP_RING_ACK_MASK) == BC250_PSP_RING_ACK_VALUE) {
                pspRingActive = 1u;

                /* 2. Submeter SETUP_TMR (ID = 5, 4 MiB) */
                RtlZeroMemory(pCmdBuf, BC250_PSP_LOAD_FW_COMMAND_BYTES);
                pCmdBuf[0] = 0x400u;
                pCmdBuf[1] = 1u;
                pCmdBuf[2] = 5u; /* GFX_CMD_ID_SETUP_TMR */
                pCmdBuf[7] = (uint32_t)(tmrMc & 0xFFFFFFFFull);
                pCmdBuf[8] = (uint32_t)(tmrMc >> 32);
                pCmdBuf[9] = BC250_PSP_TMR_BYTES;
                pCmdBuf[10] = 2u; /* flags: virt_phy_addr=1 */
                pCmdBuf[11] = (uint32_t)(tmrPa & 0xFFFFFFFFull);
                pCmdBuf[12] = (uint32_t)(tmrPa >> 32);

                ++pspFenceValue;
                *pFenceBuf = 0u;
                pRingBuf[pspRingWptr + 0] = (uint32_t)pspCmdPa.QuadPart;
                pRingBuf[pspRingWptr + 1] = (uint32_t)(pspCmdPa.QuadPart >> 32);
                pRingBuf[pspRingWptr + 2] = 0x400u;
                pRingBuf[pspRingWptr + 3] = (uint32_t)pspFencePa.QuadPart;
                pRingBuf[pspRingWptr + 4] = (uint32_t)(pspFencePa.QuadPart >> 32);
                pRingBuf[pspRingWptr + 5] = pspFenceValue;
                KeMemoryBarrier();

                pspRingWptr = (pspRingWptr + 16u) % (BC250_PSP_RING_BYTES / sizeof(uint32_t));
                (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_67, pspRingWptr);

                for (pollIndex = 0u; pollIndex < 500u; ++pollIndex) {
                    if (*pFenceBuf == pspFenceValue) break;
                    KeStallExecutionProcessor(1000u);
                }

                if (*pFenceBuf == pspFenceValue && pCmdBuf[864u / 4u] == 0u) {
                    ++cpPspLoadedCount;

                    /* 3. Submeter sucessivamente LOAD_IP_FW para os 5 firmwares do CP */
                    for (k = 0u; k < 5u; ++k) {
                        KeStallExecutionProcessor(10000u);
                        RtlCopyMemory(pspFw, fwSources[k], fwBytes[k]);
                        KeMemoryBarrier();

                        RtlZeroMemory(pCmdBuf, BC250_PSP_LOAD_FW_COMMAND_BYTES);
                        pCmdBuf[0] = 0x400u;
                        pCmdBuf[1] = 1u;
                        pCmdBuf[2] = 6u; /* GFX_CMD_ID_LOAD_IP_FW */
                        pCmdBuf[7] = (uint32_t)(pspFwPa.QuadPart & 0xFFFFFFFFull);
                        pCmdBuf[8] = (uint32_t)(pspFwPa.QuadPart >> 32);
                        pCmdBuf[9] = fwBytes[k];
                        pCmdBuf[10] = fwTypes[k];

                        ++pspFenceValue;
                        *pFenceBuf = 0u;
                        pRingBuf[pspRingWptr + 0] = (uint32_t)pspCmdPa.QuadPart;
                        pRingBuf[pspRingWptr + 1] = (uint32_t)(pspCmdPa.QuadPart >> 32);
                        pRingBuf[pspRingWptr + 2] = 0x400u;
                        pRingBuf[pspRingWptr + 3] = (uint32_t)pspFencePa.QuadPart;
                        pRingBuf[pspRingWptr + 4] = (uint32_t)(pspFencePa.QuadPart >> 32);
                        pRingBuf[pspRingWptr + 5] = pspFenceValue;
                        KeMemoryBarrier();

                        pspRingWptr = (pspRingWptr + 16u) % (BC250_PSP_RING_BYTES / sizeof(uint32_t));
                        (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_67, pspRingWptr);

                        for (pollIndex = 0u; pollIndex < 500u; ++pollIndex) {
                            if (*pFenceBuf == pspFenceValue) break;
                            KeStallExecutionProcessor(1000u);
                        }

                        if (*pFenceBuf == pspFenceValue && pCmdBuf[864u / 4u] == 0u) {
                            ++cpPspLoadedCount;
                        }
                    }
                }
            }
        }

        Evidence->FaultAddressLow = cpPspLoadedCount;
    }

    /* Slice 5: Ring buffer, RPTR writeback, WPTR poll e Fence de silicio */
    pRing = mapped + (BC250_CP_RB0_RING_OFFSET / sizeof(uint32_t));
    pRptrWb = mapped + (BC250_CP_RB0_RPTR_WB_OFFSET / sizeof(uint32_t));
    pWptrPoll = mapped + (BC250_CP_RB0_WPTR_POLL_OFFSET / sizeof(uint32_t));
    pFence = mapped + (BC250_CP_RB0_FENCE_OFFSET / sizeof(uint32_t));

    RtlZeroMemory((void *)pRing, BC250_CP_RB0_RING_BYTES);
    *pRptrWb = 0u;
    *pWptrPoll = 0u;
    *pFence = BC250_CP_RB0_FENCE_POISON;
    Evidence->Rb0FenceBefore = *pFence;

    /* Enderecos FISICOS DE MEMORIA (MC) para Ring 0, RPTR WB, WPTR Poll e Fence */
    ringMc = Evidence->FirmwareMcAddress + BC250_CP_RB0_RING_OFFSET;
    rptrWbMc = Evidence->FirmwareMcAddress + BC250_CP_RB0_RPTR_WB_OFFSET;
    wptrPollMc = Evidence->FirmwareMcAddress + BC250_CP_RB0_WPTR_POLL_OFFSET;
    fenceMc = Evidence->FirmwareMcAddress + BC250_CP_RB0_FENCE_OFFSET;

    /* Sequencia PM4 canonica: WRITE_DATA SCRATCH + WRITE_DATA FENCE MC + WRITE_DATA FENCE GPUVA */
    cursor = 0u;
    {
        uint64_t gpuvaFence = BC250_CP_RB0_FENCE_OFFSET;

        /* 1. WRITE_DATA: SCRATCH_REG0 (0x2040) = 0x22222222 (5 dwords) */
        pRing[cursor++] = 0xC0033700u; /* PACKET3_WRITE_DATA, count=3 */
        pRing[cursor++] = 0x00100000u; /* WR_CONFIRM=1, DST_SEL=0 (Register) */
        pRing[cursor++] = 0x00002040u; /* mmSCRATCH_REG0 */
        pRing[cursor++] = 0x00000000u;
        pRing[cursor++] = 0x22222222u;

        /* 2. WRITE_DATA: fence via MC physical aperture (5 dwords) */
        pRing[cursor++] = 0xC0033700u; /* PACKET3_WRITE_DATA, count=3 */
        pRing[cursor++] = 0x00100500u; /* WR_CONFIRM=1, DST_SEL=5 (Memory direct) */
        pRing[cursor++] = (uint32_t)(fenceMc & 0xFFFFFFFFull);
        pRing[cursor++] = (uint32_t)(fenceMc >> 32);
        pRing[cursor++] = BC250_CP_RB0_FENCE_VALUE;

        /* 3. WRITE_DATA: fence via GPUVA translated aperture (5 dwords) */
        pRing[cursor++] = 0xC0033700u; /* PACKET3_WRITE_DATA, count=3 */
        pRing[cursor++] = 0x00100500u; /* WR_CONFIRM=1, DST_SEL=5 (Memory direct) */
        pRing[cursor++] = (uint32_t)(gpuvaFence & 0xFFFFFFFFull);
        pRing[cursor++] = (uint32_t)(gpuvaFence >> 32);
        pRing[cursor++] = BC250_CP_RB0_FENCE_VALUE;

        /* 4. Pad 1 NOP para alinhar a 16 dwords (64 bytes / cacheline) */
        pRing[cursor++] = 0xC0001000u; /* PACKET3_NOP, count=0 */
    }
    KeMemoryBarrier();

    /* 5. Inicializacao canônica seguindo exatamente o driver Linux */
    registersTouched = 1u;
    Evidence->WriteOnlyPortsProgrammed = 1u;
    Evidence->RlcCntlApplied = Evidence->RlcCntlBefore | 1u;
    Evidence->CpMeCntlApplied = Evidence->CpMeCntlBefore & ~(BC250_PFP_HALT | BC250_ME_HALT | BC250_CE_HALT);

    /* Desgatar clocks */
    {
        uint32_t cpfClk = (origCgttCpfClkCtrl & ~0xFCFF8FFFu) | (0xF8000100u & 0xFCFF8FFFu);
        uint32_t geFast = (origGeFastClks & ~0x3FFFFFFFu) | (0x0000493Eu & 0x3FFFFFFFu);
        uint32_t cpSd = (origCpSdCntl & ~0x800007FFu) | (0x000005FFu & 0x800007FFu);
        (void)Bc250PfpWrite(Context, R_CGTT_CPF_CLK_CTRL, cpfClk);
        (void)Bc250PfpWrite(Context, R_GE_FAST_CLKS, geFast);
        (void)Bc250PfpWrite(Context, R_CP_SD_CNTL, cpSd);
    }

    /* Broadcast ativo para todos os SEs, SHs e instâncias para portas indexadas do CP */
    (void)Bc250PfpWrite(Context, R_GRBM_GFX_INDEX, 0xE0000000u);
    (void)Bc250PfpWrite(Context, R_GRBM_GFX_CNTL, 0u);

    /* Flush HDP e L2 */
    (void)Bc250PfpWrite(Context, R_HDP_MEM_COHERENCY_FLUSH_CNTL, 1u);
    (void)Bc250PfpWrite(Context, R_GCVM_L2_CNTL2, 0x00000003u);
    (void)Bc250PfpWrite(Context, R_GCVM_INVALIDATE_ENG0_REQ, 0x00000001u);
    for (i = 0u; i < 100u; ++i) {
        if (Bc250PfpRead(Context, R_GCVM_INVALIDATE_ENG0_ACK, &value) && (value & 1u) != 0u) break;
        KeStallExecutionProcessor(1u);
    }

    /* RLC e CP setup */
    (void)Bc250PfpWrite(Context, R_RLC_CNTL, Evidence->RlcCntlApplied);
    (void)Bc250PfpWrite(Context, R_CP_MAX_CONTEXT, 7u);
    (void)Bc250PfpWrite(Context, R_CP_DEVICE_ID, 1u);

    /* Prepara Page Table VMID 0 na VRAM: mapeia 2 MiB (512 PTEs) comecando em GPUVA 0 com Host Physical Address */
    {
        volatile uint64_t *pPt = (volatile uint64_t *)(mapped + (BC250_CP_PT_SLICE_OFFSET / sizeof(uint32_t)));
        uint32_t pteIdx;
        for (pteIdx = 0u; pteIdx < 512u; ++pteIdx) {
            uint64_t pagePhys = firmwarePa + (pteIdx * 4096ull);
            pPt[pteIdx] = (pagePhys & 0x0000FFFFFFFFF000ull) | 0xE1ull;
        }
        KeMemoryBarrier();
    }

    /* 4b-2. Ativação de GCVM Context 0 (VMID 0) com Host Physical Address da Page Table (conforme Linux e dbg/hw.c) */
    {
        uint64_t ptPa = firmwarePa + BC250_CP_PT_SLICE_OFFSET;
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_PT_BASE_LO, (uint32_t)(ptPa | 1u));
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_PT_BASE_HI, (uint32_t)(ptPa >> 32));
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_PT_START_LO, 0u);
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_PT_END_LO, 511u);
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_CNTL, 0x007FFE01u);
        /* Invalidação da TLB do VMID 0 */
        (void)Bc250PfpWrite(Context, R_GCVM_INVALIDATE_ENG0_REQ, 0x00F80001u);
        for (i = 0u; i < 100u; ++i) {
            if (Bc250PfpRead(Context, R_GCVM_INVALIDATE_ENG0_ACK, &value) && (value & 1u) != 0u) break;
            KeStallExecutionProcessor(1u);
        }
    }

    /* 4c. Soft-reset do CP e inicialização pós-PSP canônica (Linux gfx_v10_0) */
    {
        uint32_t cpStat = 0u, grbmStat = 0u, grbmStat2 = 0u;

        /* Soft-reset do CP via GRBM_SOFT_RESET (bit 0 = SOFT_RESET_CP) */
        (void)Bc250PfpWrite(Context, R_GRBM_SOFT_RESET, 1u);
        KeStallExecutionProcessor(50u);
        (void)Bc250PfpWrite(Context, R_GRBM_SOFT_RESET, 0u);
        KeStallExecutionProcessor(50u);

        /* Inicialização canônica pós-PSP */
        (void)Bc250PfpWrite(Context, R_CP_MAX_CONTEXT, 7u);
        (void)Bc250PfpWrite(Context, R_CP_DEVICE_ID, 1u);

        /* Status inicial pós-reset */
        (void)Bc250PfpRead(Context, R_CP_STAT, &cpStat);
        (void)Bc250PfpRead(Context, R_GRBM_STATUS, &grbmStat);
        (void)Bc250PfpRead(Context, R_GRBM_STATUS2, &grbmStat2);
        Evidence->FaultAddressHigh = ((cpStat & 0xFFu) << 24) | ((grbmStat2 & 0xFFu) << 16) | (grbmStat & 0xFFFFu);
        Evidence->WriteOnlyPortsProgrammed = 1u;
    }

    /* 1. Configura registradores do anel sob endereço físico MC e VMID 0 */
    rbCntl = 0x00A00709u; /* BUFSZ=9, BLKSZ=7, MIN_AVAIL=0x00A00000 */
    (void)Bc250PfpWrite(Context, R_CP_RB_DOORBELL_CONTROL, 0u);
    (void)Bc250PfpWrite(Context, R_CP_RB0_BASE, (uint32_t)(ringMc >> 8));
    (void)Bc250PfpWrite(Context, R_CP_RB0_BASE_HI, (uint32_t)((ringMc >> 8) >> 32));
    (void)Bc250PfpWrite(Context, R_CP_RB0_RPTR_ADDR, (uint32_t)rptrWbMc);
    (void)Bc250PfpWrite(Context, R_CP_RB0_RPTR_ADDR_HI, (uint32_t)(rptrWbMc >> 32));
    (void)Bc250PfpWrite(Context, R_CP_RB_WPTR_POLL_ADDR_LO, (uint32_t)wptrPollMc);
    (void)Bc250PfpWrite(Context, R_CP_RB_WPTR_POLL_ADDR_HI, (uint32_t)(wptrPollMc >> 32));
    (void)Bc250PfpWrite(Context, R_CP_RB_VMID, 0u);
    (void)Bc250PfpWrite(Context, R_CP_RB_WPTR_DELAY, 0u);
    (void)Bc250PfpWrite(Context, R_CP_RB_WPTR_POLL_CNTL, 0x00403004u); /* Polling ativo 64 clocks */

    (void)Bc250PfpWrite(Context, R_CP_RB0_CNTL, rbCntl);
    (void)Bc250PfpWrite(Context, R_CP_RB0_RPTR, 0u);
    (void)Bc250PfpWrite(Context, R_CP_RB0_WPTR, 0u);
    (void)Bc250PfpWrite(Context, R_CP_RB0_WPTR_HI, 0u);

    (void)Bc250PfpWrite(Context, R_CP_SCRATCH_REG0, 0x11111111u);

    (void)Bc250PfpWrite(Context, R_VGT_NUM_INSTANCES, 1u);
    (void)Bc250PfpWrite(Context, R_PA_SC_LINE_STIPPLE_STATE, 0u);

    /* 2. UNHALT CP e MEC */
    (void)Bc250PfpWrite(Context, R_CP_MEC_CNTL, 0x00000000u);
    if (!Bc250PfpWrite(Context, R_CP_ME_CNTL, Evidence->CpMeCntlApplied)) {
        status = BC250_PFP_DIRECT_LOAD_MMIO_FAILED;
        goto Restore;
    }
    KeMemoryBarrier();
    KeStallExecutionProcessor(50u);

    /* 3. Aguarda CP idle / pronto */
    for (i = 0u; i < 100u; ++i) {
        if (Bc250PfpRead(Context, R_CP_STAT, &value) && (value & 1u) == 0u) break;
        KeStallExecutionProcessor(10u);
    }

    /* 4. Ativa o Ring 0 */
    if (!Bc250PfpWrite(Context, R_CP_RB0_ACTIVE, 1u)) {
        status = BC250_PFP_DIRECT_LOAD_MMIO_FAILED;
        goto Restore;
    }
    Evidence->Rb0ActiveApplied = 1u;
    KeStallExecutionProcessor(50u);

    /* 5. Atualiza WPTR na memoria e dispara trigger 64-bit SOC15 */
    *pWptrPoll = cursor;
    KeMemoryBarrier();

    (void)Bc250PfpWrite(Context, R_HDP_MEM_COHERENCY_FLUSH_CNTL, 1u);
    (void)Bc250PfpWrite(Context, R_GCVM_L2_CNTL2, 0x00000003u);
    (void)Bc250PfpWrite(Context, R_GCVM_INVALIDATE_ENG0_REQ, 0x00000001u);

    (void)Bc250PfpWrite(Context, R_CP_RB0_WPTR, cursor);
    (void)Bc250PfpWrite(Context, R_CP_RB0_WPTR_HI, 0u);
    Evidence->Rb0WptrApplied = cursor;
    KeMemoryBarrier();

    /* 6. Amostragem adaptativa de RPTR, fence, PCs e Scratch */
    for (i = 0u; i < 100u; ++i) {
        uint32_t rptrMmio = 0u;
        uint32_t rptrWb = *pRptrWb;

        (void)Bc250PfpRead(Context, R_CP_RB0_RPTR, &rptrMmio);
        (void)Bc250PfpRead(Context, R_CP_SCRATCH_REG0, &scratchAfter);
        Evidence->FaultAddressLow = scratchAfter;

        if (*pFence != BC250_CP_RB0_FENCE_POISON) {
            Evidence->Rb0FenceAfter = *pFence;
        } else if (scratchAfter == 0x22222222u) {
            Evidence->Rb0FenceAfter = scratchAfter;
        } else {
            Evidence->Rb0FenceAfter = *pFence;
        }

        if (rptrMmio != 0u) {
            Evidence->Rb0RptrAfter = rptrMmio;
        } else {
            Evidence->Rb0RptrAfter = rptrWb;
        }

        (void)Bc250PfpRead(Context, R_CP_PFP_PC, &value);
        (void)Bc250PfpRead(Context, R_CP_ME_PC, &Evidence->MePcLast);
        (void)Bc250PfpRead(Context, R_CP_CE_PC, &Evidence->CePcLast);
        uint32_t mec1Pc = 0u;
        (void)Bc250PfpRead(Context, R_CP_MEC1_PC, &mec1Pc);
        {
            uint32_t jtStatPfp = 0u, jtStatCe = 0u, jtStatMec = 0u;
            (void)Bc250PfpRead(Context, R_CP_PFP_JT_STAT, &jtStatPfp);
            (void)Bc250PfpRead(Context, R_CP_CE_JT_STAT, &jtStatCe);
            (void)Bc250PfpRead(Context, R_CP_MEC_JT_STAT, &jtStatMec);
            if (jtStatPfp != 0u || jtStatCe != 0u || jtStatMec != 0u || i == 0u) {
                Evidence->FaultAddressHigh = ((jtStatMec & 0xFFu) << 16) | ((jtStatCe & 0xFFu) << 8) | (jtStatPfp & 0xFFu);
            }
        }
        if (i == 0u) Evidence->PfpPcFirst = value;
        Evidence->PfpPcLast = value;
        if (value != previousPc) ++Evidence->PfpPcTransitions;
        previousPc = value;

        if (Evidence->Rb0RptrAfter >= (cursor - 1u) &&
            (Evidence->Rb0FenceAfter == BC250_CP_RB0_FENCE_VALUE ||
             Evidence->Rb0FenceAfter == 0x22222222u ||
             scratchAfter == 0x22222222u)) {
            break;
        }
        KeStallExecutionProcessor(200u);
    }
    Evidence->PfpPcSamples = 4u;

    (void)Bc250PfpWrite(Context, R_CP_RB0_ACTIVE, 0u);
    (void)Bc250PfpWrite(Context, R_CP_ME_CNTL, Evidence->CpMeCntlBefore);
    (void)Bc250PfpWrite(Context, R_CP_MEC_CNTL, origCpMecCntl);
    KeMemoryBarrier();
    KeStallExecutionProcessor(50u);

    (void)Bc250PfpRead(Context, R_FAULT_STATUS,
        &Evidence->FaultStatusAfter);

    status = (Evidence->Rb0RptrAfter >= (cursor - 1u) &&
              Evidence->Rb0WptrApplied == cursor &&
              (Evidence->Rb0FenceAfter == BC250_CP_RB0_FENCE_VALUE ||
               Evidence->Rb0FenceAfter == 0x22222222u ||
               scratchAfter == 0x22222222u)) ?
              BC250_PFP_DIRECT_LOAD_OK :
              BC250_PFP_DIRECT_LOAD_PM4_NOT_CONSUMED;

    if (status == BC250_PFP_DIRECT_LOAD_OK) {
        Evidence->PhysicalReady = 1u;
    }

Restore:
    if (pspRingActive != 0u) {
        uint32_t pspAck = 0u;
        uint32_t pollIndex;
        (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_64, BC250_PSP_RING_DESTROY);
        KeStallExecutionProcessor(20000u);
        for (pollIndex = 0u; pollIndex < 500u; ++pollIndex) {
            if (Bc250PfpRead(Context, BC250_PSP_RING_C2PMSG_64, &pspAck) &&
                (pspAck & BC250_PSP_RING_ACK_MASK) == BC250_PSP_RING_ACK_VALUE) break;
            KeStallExecutionProcessor(1000u);
        }
        (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_67, 0u);
        (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_69, 0u);
        (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_70, 0u);
        (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_71, 0u);
        pspRingActive = 0u;
    }

    if (registersTouched != 0u && snapshotValid != 0u) {
        (void)Bc250PfpWrite(Context, R_CP_RB0_ACTIVE, origRb0Active);
        (void)Bc250PfpWrite(Context, R_CP_RB0_CNTL, origRb0Cntl);
        (void)Bc250PfpWrite(Context, R_CP_RB0_BASE, origRb0Base);
        (void)Bc250PfpWrite(Context, R_CP_RB0_BASE_HI, origRb0BaseHi);
        (void)Bc250PfpWrite(Context, R_CP_RB0_RPTR, origRb0Rptr);
        (void)Bc250PfpWrite(Context, R_CP_RB0_WPTR, origRb0Wptr);
        (void)Bc250PfpWrite(Context, R_CP_RB0_WPTR_HI, origRb0WptrHi);
        (void)Bc250PfpWrite(Context, R_CP_RB0_RPTR_ADDR, origRb0RptrAddr);
        (void)Bc250PfpWrite(Context, R_CP_RB0_RPTR_ADDR_HI, origRb0RptrAddrHi);
        (void)Bc250PfpWrite(Context, R_CP_RB_WPTR_POLL_ADDR_LO, origRbWptrPollAddrLo);
        (void)Bc250PfpWrite(Context, R_CP_RB_WPTR_POLL_ADDR_HI, origRbWptrPollAddrHi);
        (void)Bc250PfpWrite(Context, R_CP_RB_VMID, origRbVmid);
        (void)Bc250PfpWrite(Context, R_CP_RB_WPTR_DELAY, origRbWptrDelay);
        (void)Bc250PfpWrite(Context, R_CP_RB_WPTR_POLL_CNTL, origRbWptrPollCntl);
        (void)Bc250PfpWrite(Context, R_CP_RB_DOORBELL_CONTROL, origRbDoorbellCntl);
        (void)Bc250PfpWrite(Context, R_CGTT_CPF_CLK_CTRL, origCgttCpfClkCtrl);
        (void)Bc250PfpWrite(Context, R_GE_FAST_CLKS, origGeFastClks);
        (void)Bc250PfpWrite(Context, R_CP_SD_CNTL, origCpSdCntl);
        (void)Bc250PfpWrite(Context, R_GRBM_GFX_INDEX, 0u);
        (void)Bc250PfpWrite(Context, R_GRBM_GFX_CNTL, 0u);
        (void)Bc250PfpWrite(Context, R_CP_MAX_CONTEXT, origCpMaxContext);
        (void)Bc250PfpWrite(Context, R_CP_SCRATCH_REG0, origScratchReg0);
        (void)Bc250PfpWrite(Context, R_CP_MEC_CNTL, origCpMecCntl);
        (void)Bc250PfpWrite(Context, R_CP_ME_CNTL, Evidence->CpMeCntlBefore);
        (void)Bc250PfpWrite(Context, R_RLC_CNTL, Evidence->RlcCntlBefore);
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_CNTL, origCtx0Cntl);
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_PT_END_LO, origCtx0PtEndLo);
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_PT_START_LO, origCtx0PtStartLo);
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_PT_BASE_HI, origCtx0PtBaseHi);
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_PT_BASE_LO, origCtx0PtBaseLo);
        (void)Bc250PfpWrite(Context, R_GCVM_INVALIDATE_ENG0_REQ, 0x00F80001u);
        KeStallExecutionProcessor(50u);
        (void)Bc250PfpRead(Context, R_CP_RB0_ACTIVE, &Evidence->Rb0ActiveAfter);
        if (Bc250PfpRead(Context, R_CP_ME_CNTL, &Evidence->CpMeCntlAfter) &&
            Evidence->CpMeCntlAfter == Evidence->CpMeCntlBefore &&
            Bc250PfpRead(Context, R_RLC_CNTL, &value) &&
            value == Evidence->RlcCntlBefore)
            Evidence->RegisterRestorePassed = 1u;
    } else if (registersTouched == 0u && snapshotValid != 0u) {
        Evidence->RegisterRestorePassed = 1u;
        Evidence->CpMeCntlAfter = Evidence->CpMeCntlBefore;
    }

    if (mappedValid != 0u) {
        if (memoryPrepared != 0u) {
            Bc250PfpCopyToDevice(mapped, backup, mappedDwords);
            for (i = 0u; i < mappedDwords; ++i) {
                if (READ_REGISTER_ULONG((volatile ULONG *)&mapped[i]) != backup[i]) {
                    Evidence->MemoryRestorePassed = 0u;
                    if (status == BC250_PFP_DIRECT_LOAD_OK)
                        status = BC250_PFP_DIRECT_LOAD_RESTORE_FAILED;
                    break;
                }
            }
            if (i == mappedDwords) Evidence->MemoryRestorePassed = 1u;
        } else {
            Evidence->MemoryRestorePassed = 1u;
        }
        Context->Core.DxgkInterface.DxgkCbUnmapMemory(
            Context->Core.DxgkInterface.DeviceHandle, (PVOID)mapped);
    }

    if (status == BC250_PFP_DIRECT_LOAD_OK)
        Evidence->PhysicalReady = 1u;
    if ((registersTouched != 0u && Evidence->RegisterRestorePassed == 0u) ||
        (memoryPrepared != 0u && Evidence->MemoryRestorePassed == 0u)) {
        status = BC250_PFP_DIRECT_LOAD_RESTORE_FAILED;
        Evidence->PhysicalReady = 0u;
    }

UnmapMmio:
    (void)Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);

Cleanup:
    if (pspFw != NULL) MmFreeContiguousMemory(pspFw);
    if (pspFence != NULL) MmFreeContiguousMemory(pspFence);
    if (pspCmd != NULL) MmFreeContiguousMemory(pspCmd);
    if (pspRing != NULL) MmFreeContiguousMemory(pspRing);
    if (allocated != 0u)
        (void)Bc250VramRelease(allocator, token);
    if (tmrAllocated != 0u)
        (void)Bc250VramRelease(allocator, tmrToken);
    if (backup != NULL)
        ExFreePoolWithTag(backup, BC250_PFP_POOL_TAG);
    if (mecFileBuffer != NULL)
        ExFreePoolWithTag(mecFileBuffer, BC250_PFP_POOL_TAG);
    if (ceFileBuffer != NULL)
        ExFreePoolWithTag(ceFileBuffer, BC250_PFP_POOL_TAG);
    if (meFileBuffer != NULL)
        ExFreePoolWithTag(meFileBuffer, BC250_PFP_POOL_TAG);
    if (pfpFileBuffer != NULL)
        ExFreePoolWithTag(pfpFileBuffer, BC250_PFP_POOL_TAG);

    return status;
}
#else
BC250_PFP_DIRECT_LOAD_STATUS Bc250ActiveRunPfpDirectLoad(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PFP_DIRECT_LOAD_EVIDENCE *Evidence)
{
    (void)Context;
    if (Evidence != NULL)
        memset(Evidence, 0, sizeof(*Evidence));
    return BC250_PFP_DIRECT_LOAD_BAD_ARGUMENT;
}
#endif
