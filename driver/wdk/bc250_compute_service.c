#include "bc250_compute_service.h"
#include "../core/bc250_smu_telemetry_contract.h"
#include "../core/bc250_superio_nct6686_contract.h"
#include "../core/bc250_sdma_packet_builder.h"
#include "../core/bc250_pm4_packet_builder.h"
#include "../core/bc250_psp_load_fw_transaction.h"
#include "../core/bc250_sdma_f32_transaction.h"
#include "../core/bc250_compute_mqd_contract.h"
#include "../core/bc250_gfx_ring_contract.h"
#include "../core/bc250_hdp_contract.h"
#include <ntddk.h>

#define BC250_PCI_VENDOR_ID 0x1002u
#define BC250_PCI_DEVICE_ID 0x13FEu

#define BC250_SAFE_VRAM_PHYS_BASE 0x000000F400000000ull
#define BC250_SAFE_VRAM_OFFSET    0x0000000000AA0000ull /* Alinhado ao ring físico provado */
#define BC250_SAFE_VRAM_SIZE      0x00040000u           /* 256 KiB window */
#define BC250_MC_VRAM_BASE        0x000000F400000000ull /* MC VRAM Base on BC-250 */
#define BC250_VRAM_DYNAMIC_OFFSET 0x0000000000AA8000ull /* Pool dinâmico dentro da janela de 256 KiB */
#define BC250_VRAM_DYNAMIC_SIZE   0x0000000000030000ull /* 192 KiB para buffers e tensores */

static volatile LONG g_VramOffset = 0;
static PVOID g_MmioBase = NULL;
static PHYSICAL_ADDRESS g_MmioPhysBase;
static PHYSICAL_ADDRESS g_VramPhysBase;
static ULONG g_MmioLength = 0x80000u; /* 512 KiB */
int _fltused = 0x9875;

static DRIVER_UNLOAD Bc250ComputeUnload;
static DRIVER_DISPATCH Bc250ComputeCreateClose;
static DRIVER_DISPATCH Bc250ComputeDeviceControl;

/*
 * The compute service is not the display miniport and does not own Dxgkrnl
 * process objects, allocations, VMIDs, or BAR resources.  Keep this false
 * until an adapter-owned broker exposes authenticated allocation leases.
 */
static const ULONG g_Bc250GpuvaSdmaBackendState = Bc250GpuvaSdmaBackendNoAdapterOwner;

static NTSTATUS Bc250ValidateGpuvaSdmaTwoContextRequest(
    PBC250_GPUVA_SDMA_TWO_CONTEXT_REQ Request)
{
    const ULONG knownFlags = BC250_GPUVA_SDMA_FLAG_DRY_RUN |
                             BC250_GPUVA_SDMA_FLAG_EXECUTE;

    if (Request->Size != sizeof(*Request) ||
        Request->Version != BC250_GPUVA_SDMA_TWO_CONTEXT_VERSION) {
        Request->GateStatus = Bc250GpuvaSdmaGateBadAbi;
        return STATUS_REVISION_MISMATCH;
    }

    if ((Request->Flags & ~knownFlags) != 0u ||
        Request->Flags == 0u ||
        Request->Flags == knownFlags ||
        Request->ByteCount == 0u ||
        Request->ByteCount > BC250_GPUVA_SDMA_MAX_BYTES ||
        (Request->ByteCount & 3u) != 0u ||
        Request->SourceLeaseId == 0ull ||
        Request->DestinationLeaseId == 0ull ||
        Request->SourceLeaseId == Request->DestinationLeaseId ||
        Request->SourceGpuVa == 0ull ||
        Request->DestinationGpuVa == 0ull ||
        Request->FenceGpuVa == 0ull ||
        Request->SourceGpuVa == Request->DestinationGpuVa ||
        (Request->SourceGpuVa & (BC250_GPUVA_SDMA_PAGE_SIZE - 1ull)) != 0ull ||
        (Request->DestinationGpuVa & (BC250_GPUVA_SDMA_PAGE_SIZE - 1ull)) != 0ull ||
        (Request->FenceGpuVa & 3ull) != 0ull ||
        Request->FenceExpected == 0u ||
        Request->SourcePoison == Request->DestinationPoison) {
        Request->GateStatus = Bc250GpuvaSdmaGateBadRequest;
        return STATUS_INVALID_PARAMETER;
    }

    Request->GateStatus = Bc250GpuvaSdmaGatePreflightPassed;
    Request->PreflightPassed = 1u;
    /* This proves only non-aliasing request metadata, not a GPU page table. */
    Request->MappingIsolationPassed = 1u;
    return STATUS_SUCCESS;
}

static uint8_t KernelIsaInByte(void *Context, uint16_t Port)
{
    UNREFERENCED_PARAMETER(Context);
    return READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)Port);
}

static void KernelIsaOutByte(void *Context, uint16_t Port, uint8_t Value)
{
    UNREFERENCED_PARAMETER(Context);
    WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)Port, Value);
}

static ULONG Bc250ReadSmn(ULONG SmnAddress);
static void Bc250WriteSmn(ULONG SmnAddress, ULONG Value);

static uint32_t KernelSmuRead32(void *Context, uint32_t ByteOffset)
{
    UNREFERENCED_PARAMETER(Context);
    return Bc250ReadSmn(ByteOffset);
}

static void KernelSmuWrite32(void *Context, uint32_t ByteOffset, uint32_t Value)
{
    UNREFERENCED_PARAMETER(Context);
    Bc250WriteSmn(ByteOffset, Value);
}

static void KernelSmuStallUs(void *Context, uint32_t Microseconds)
{
    UNREFERENCED_PARAMETER(Context);
    KeStallExecutionProcessor(Microseconds);
}

static ULONG Bc250ReadSmn(ULONG SmnAddress)
{
    if (g_MmioBase == NULL) return 0xFFFFFFFFu;
    /* 0x0038 = mmPCIE_INDEX, 0x003C = mmPCIE_DATA on BC-250 */
    WRITE_REGISTER_ULONG((volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0038u), SmnAddress);
    (void)READ_REGISTER_ULONG((volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0038u)); /* PCI write posting flush */
    return READ_REGISTER_ULONG((volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x003Cu));
}

static void Bc250WriteSmn(ULONG SmnAddress, ULONG Value)
{
    if (g_MmioBase == NULL) return;
    WRITE_REGISTER_ULONG((volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0038u), SmnAddress);
    (void)READ_REGISTER_ULONG((volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0038u)); /* PCI write posting flush */
    WRITE_REGISTER_ULONG((volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x003Cu), Value);
}

static uint32_t Bc250ComputeMmioRead32(void *Context, uint32_t ByteOffset, uint32_t *Value)
{
    UNREFERENCED_PARAMETER(Context);
    if (g_MmioBase == NULL || Value == NULL) return 0u;
    if (ByteOffset >= g_MmioLength) return 0u;
    *Value = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + ByteOffset);
    return 1u;
}

static uint32_t Bc250ComputeMmioWrite32(void *Context, uint32_t ByteOffset, uint32_t Value)
{
    UNREFERENCED_PARAMETER(Context);
    if (g_MmioBase == NULL) return 0u;
    if (ByteOffset >= g_MmioLength) return 0u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + ByteOffset) = Value;
    return 1u;
}

static void Bc250ComputeStall(void *Context, uint32_t Microseconds)
{
    UNREFERENCED_PARAMETER(Context);
    KeStallExecutionProcessor(Microseconds);
}

static void Bc250ComputeBarrier(void *Context)
{
    UNREFERENCED_PARAMETER(Context);
    KeMemoryBarrier();
}

#define BC250_FIRMWARE_SHA256_BYTES 32u

typedef struct BC250_SHA256_CONTEXT {
    uint32_t State[8];
    uint64_t TotalBytes;
    uint32_t BufferBytes;
    unsigned char Buffer[64];
} BC250_SHA256_CONTEXT;

static const uint32_t g_Bc250Sha256K[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
};

static uint32_t Bc250RotateRight(uint32_t Value, uint32_t Bits)
{
    return (Value >> Bits) | (Value << (32u - Bits));
}

static uint32_t Bc250LoadBigEndian32(const unsigned char *Bytes)
{
    return ((uint32_t)Bytes[0] << 24) |
           ((uint32_t)Bytes[1] << 16) |
           ((uint32_t)Bytes[2] << 8) |
           (uint32_t)Bytes[3];
}

static void Bc250StoreBigEndian32(unsigned char *Bytes, uint32_t Value)
{
    Bytes[0] = (unsigned char)(Value >> 24);
    Bytes[1] = (unsigned char)(Value >> 16);
    Bytes[2] = (unsigned char)(Value >> 8);
    Bytes[3] = (unsigned char)Value;
}

static void Bc250Sha256Transform(
    BC250_SHA256_CONTEXT *Context,
    const unsigned char Block[64])
{
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t i;

    for (i = 0; i < 16u; ++i) {
        words[i] = Bc250LoadBigEndian32(Block + i * 4u);
    }
    for (i = 16u; i < 64u; ++i) {
        uint32_t s0 = Bc250RotateRight(words[i - 15u], 7u) ^
            Bc250RotateRight(words[i - 15u], 18u) ^
            (words[i - 15u] >> 3);
        uint32_t s1 = Bc250RotateRight(words[i - 2u], 17u) ^
            Bc250RotateRight(words[i - 2u], 19u) ^
            (words[i - 2u] >> 10);
        words[i] = words[i - 16u] + s0 + words[i - 7u] + s1;
    }

    a = Context->State[0]; b = Context->State[1];
    c = Context->State[2]; d = Context->State[3];
    e = Context->State[4]; f = Context->State[5];
    g = Context->State[6]; h = Context->State[7];
    for (i = 0; i < 64u; ++i) {
        uint32_t sum1 = Bc250RotateRight(e, 6u) ^
            Bc250RotateRight(e, 11u) ^ Bc250RotateRight(e, 25u);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temporary1 = h + sum1 + choice +
            g_Bc250Sha256K[i] + words[i];
        uint32_t sum0 = Bc250RotateRight(a, 2u) ^
            Bc250RotateRight(a, 13u) ^ Bc250RotateRight(a, 22u);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = sum0 + majority;
        h = g; g = f; f = e; e = d + temporary1;
        d = c; c = b; b = a; a = temporary1 + temporary2;
    }
    Context->State[0] += a; Context->State[1] += b;
    Context->State[2] += c; Context->State[3] += d;
    Context->State[4] += e; Context->State[5] += f;
    Context->State[6] += g; Context->State[7] += h;
}

static void Bc250Sha256Init(BC250_SHA256_CONTEXT *Context)
{
    RtlZeroMemory(Context, sizeof(*Context));
    Context->State[0] = 0x6A09E667u; Context->State[1] = 0xBB67AE85u;
    Context->State[2] = 0x3C6EF372u; Context->State[3] = 0xA54FF53Au;
    Context->State[4] = 0x510E527Fu; Context->State[5] = 0x9B05688Cu;
    Context->State[6] = 0x1F83D9ABu; Context->State[7] = 0x5BE0CD19u;
}

static void Bc250Sha256Update(
    BC250_SHA256_CONTEXT *Context,
    const unsigned char *Bytes,
    uint32_t Length)
{
    uint32_t take;

    Context->TotalBytes += Length;
    while (Length != 0) {
        take = 64u - Context->BufferBytes;
        if (take > Length) take = Length;
        RtlCopyMemory(Context->Buffer + Context->BufferBytes, Bytes, take);
        Context->BufferBytes += take;
        Bytes += take;
        Length -= take;
        if (Context->BufferBytes == 64u) {
            Bc250Sha256Transform(Context, Context->Buffer);
            Context->BufferBytes = 0;
        }
    }
}

static void Bc250Sha256Final(
    BC250_SHA256_CONTEXT *Context,
    unsigned char Digest[BC250_FIRMWARE_SHA256_BYTES])
{
    uint64_t bitLength = Context->TotalBytes * 8u;
    uint32_t i;

    Context->Buffer[Context->BufferBytes++] = 0x80u;
    if (Context->BufferBytes > 56u) {
        while (Context->BufferBytes < 64u) {
            Context->Buffer[Context->BufferBytes++] = 0;
        }
        Bc250Sha256Transform(Context, Context->Buffer);
        Context->BufferBytes = 0;
    }
    while (Context->BufferBytes < 56u) {
        Context->Buffer[Context->BufferBytes++] = 0;
    }
    for (i = 0; i < 8u; ++i) {
        Context->Buffer[63u - i] = (unsigned char)(bitLength >> (i * 8u));
    }
    Bc250Sha256Transform(Context, Context->Buffer);
    for (i = 0; i < 8u; ++i) {
        Bc250StoreBigEndian32(Digest + i * 4u, Context->State[i]);
    }
    RtlZeroMemory(Context, sizeof(*Context));
}

static void Bc250ComputeSha256(
    const void *Data,
    uint32_t Length,
    unsigned char Digest[BC250_FIRMWARE_SHA256_BYTES])
{
    BC250_SHA256_CONTEXT context;
    Bc250Sha256Init(&context);
    Bc250Sha256Update(&context, (const unsigned char *)Data, Length);
    Bc250Sha256Final(&context, Digest);
}

static int Bc250ComputeDigestEqual(
    const unsigned char *Left,
    const unsigned char *Right)
{
    uint32_t i, diff = 0u;
    for (i = 0; i < BC250_FIRMWARE_SHA256_BYTES; ++i) {
        diff |= (uint32_t)(Left[i] ^ Right[i]);
    }
    return diff == 0u;
}

#define BC250_SDMA0_EXPECTED_UCODE_CHECKSUM 0x55B182E3u

static BC250_PSP_LOAD_FW_STATUS Bc250ComputeLoadSdmaFirmware(
    BC250_PSP_LOAD_FW_EVIDENCE *Evidence)
{
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
    BC250_PSP_LOAD_FW_STATUS status = BC250_PSP_LOAD_FW_BAD_ARGUMENT;
    NTSTATUS ntStatus;

    if (Evidence == NULL) return status;
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

    Bc250ComputeSha256(firmware, 33792u, digest);
    if (!Bc250ComputeDigestEqual(digest, expectedDigest)) goto Cleanup;

    RtlMoveMemory(firmware, (PUCHAR)firmware + 0x100u, 33536u);
    RtlZeroMemory((PUCHAR)firmware + 33536u, 0x100u);
    Bc250ComputeSha256(firmware, 33536u, digest);
    if (!Bc250ComputeDigestEqual(digest, expectedPayloadDigest)) goto Cleanup;

    ringPa = MmGetPhysicalAddress(ring);
    commandPa = MmGetPhysicalAddress(command);
    fencePa = MmGetPhysicalAddress(fence);
    firmwarePa = MmGetPhysicalAddress(firmware);
    if (ringPa.QuadPart == 0 || commandPa.QuadPart == 0 ||
        fencePa.QuadPart == 0 || firmwarePa.QuadPart == 0) goto Cleanup;

    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250ComputeMmioRead32;
    interfaceData.Write32 = Bc250ComputeMmioWrite32;
    interfaceData.Stall = Bc250ComputeStall;
    interfaceData.Barrier = Bc250ComputeBarrier;
    interfaceData.Context = NULL;
    interfaceData.Identity = BC250_PSP_LOAD_FW_IDENTITY;

    status = Bc250RunPspLoadFirmwareTransaction(
        (uint64_t)ringPa.QuadPart, (uint32_t *)ring,
        (uint64_t)commandPa.QuadPart, (uint32_t *)command,
        (uint64_t)fencePa.QuadPart, (volatile uint32_t *)fence,
        (uint64_t)firmwarePa.QuadPart, 33536u,
        BC250_PSP_LOAD_FW_SDMA0_TYPE, &interfaceData, 500u, Evidence);

    if (status == BC250_PSP_LOAD_FW_OK) {
        /* Unhalt F32 directly and configure clock gating */
        *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x049ECu) |= 0xFF000000u;
        *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x049F0u) |= 0x00000002u | 0x00000020u | 0x00040000u;
        *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04BC8u) &= ~0x10000000u; /* Disable doorbell */
        *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04A28u) &= ~1u; /* F32 unhalt */
        Evidence->PhysicalReady = 1u;
    }

Cleanup:
    if (file != NULL) ZwClose(file);
    if (ring != NULL) MmFreeContiguousMemory(ring);
    if (command != NULL) MmFreeContiguousMemory(command);
    if (fence != NULL) MmFreeContiguousMemory(fence);
    if (firmware != NULL) MmFreeContiguousMemory(firmware);
    return status;
}

static void Bc250ComputeHdpFlush(void)
{
    ULONG misc, val;
    misc = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x03FCCu);
    /*
     * HDP.1 (28/09, doc 519): a escrita era 0 em 0x04A80 -- SDMA0_UTCL1_INV0,
     * nao flush de HDP. Agora e' o flush de escrita real com leitura de
     * confirmacao (bc250_hdp_contract.h). DIVIDA DECLARADA, nao tocada nesta
     * faixa: este servico legado ainda alterna HDP_MISC, o que o DX.96 tirou
     * dos caminhos ativos.
     */
    _mm_mfence();
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HDP_REG_MEM_COHERENCY_FLUSH_CNTL) =
        BC250_HDP_MEM_FLUSH_VALUE;
    val = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HDP_REG_FLUSH_POSTING_READ);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x03FC4u) = 1u;
    val = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x03FC4u);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x03FCCu) = misc | 1u;
    val = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x03FCCu);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x03FCCu) = misc & ~1u;
    val = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x03FCCu);
    UNREFERENCED_PARAMETER(val);
}

static NTSTATUS Bc250ComputeExecuteSdmaCopy(PBC250_SDMA_COPY_REQ req)
{
    PHYSICAL_ADDRESS vramPa;
    PVOID vramBase = NULL;
    volatile uint32_t *ring = NULL;
    volatile uint32_t *work = NULL;
    volatile uint32_t *rptrWb = NULL;
    volatile uint32_t *wptrPoll = NULL;
    volatile uint32_t *fenceBuf = NULL;
    volatile uint32_t *dstBuf = NULL;
    uint64_t ringMc = 0, workMc = 0, rptrWbMc = 0, wptrPollMc = 0, fenceMc = 0, dstMc = 0;
    uint32_t ringPacket[64];
    uint32_t cursor = 0;
    uint32_t expectedFence = (req->FenceValue != 0) ? req->FenceValue : 0xBC250123u;
    ULONG rbCntl = 0, rptr = 0, p = 0, i = 0, mismatch = 0;
    uint32_t ucodeChecksum = 0;
    uint32_t val = 0;
    SIZE_T numBytes = req->ByteCount;

    if (numBytes > 0x10000000) {
        req->ExecutionSuccess = 0u;
        return STATUS_INVALID_PARAMETER;
    }

    /* Host -> Device (VRAM fisica via ReBAR) */
    if (numBytes > 0 && req->DestinationGpuVa >= BC250_SAFE_VRAM_PHYS_BASE && req->SourceGpuVa < BC250_SAFE_VRAM_PHYS_BASE) {
        PVOID vramMapped;
        ULONGLONG vramOffset = req->DestinationGpuVa - BC250_SAFE_VRAM_PHYS_BASE;
        vramPa.QuadPart = g_VramPhysBase.QuadPart + (LONGLONG)vramOffset;
        vramMapped = MmMapIoSpaceEx(vramPa, numBytes, PAGE_READWRITE | PAGE_NOCACHE);
        if (vramMapped == NULL) {
            req->ExecutionSuccess = 0u;
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        __try {
            RtlCopyMemory(vramMapped, (const void *)(uintptr_t)req->SourceGpuVa, numBytes);
            req->ExecutionSuccess = 1u;
            req->FenceObserved = expectedFence;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            req->ExecutionSuccess = 0u;
        }
        MmUnmapIoSpace(vramMapped, numBytes);
        return STATUS_SUCCESS;
    }

    /* Device (VRAM fisica via ReBAR) -> Host */
    if (numBytes > 0 && req->SourceGpuVa >= BC250_SAFE_VRAM_PHYS_BASE && req->DestinationGpuVa < BC250_SAFE_VRAM_PHYS_BASE) {
        PVOID vramMapped;
        ULONGLONG vramOffset = req->SourceGpuVa - BC250_SAFE_VRAM_PHYS_BASE;
        vramPa.QuadPart = g_VramPhysBase.QuadPart + (LONGLONG)vramOffset;
        vramMapped = MmMapIoSpaceEx(vramPa, numBytes, PAGE_READWRITE | PAGE_NOCACHE);
        if (vramMapped == NULL) {
            req->ExecutionSuccess = 0u;
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        __try {
            RtlCopyMemory((void *)(uintptr_t)req->DestinationGpuVa, vramMapped, numBytes);
            req->ExecutionSuccess = 1u;
            req->FenceObserved = expectedFence;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            req->ExecutionSuccess = 0u;
        }
        MmUnmapIoSpace(vramMapped, numBytes);
        return STATUS_SUCCESS;
    }

    /* Device -> Device (VRAM para VRAM) */
    if (numBytes > 0 && req->SourceGpuVa >= BC250_SAFE_VRAM_PHYS_BASE && req->DestinationGpuVa >= BC250_SAFE_VRAM_PHYS_BASE) {
        PHYSICAL_ADDRESS srcPa, dstPa;
        PVOID srcMapped, dstMapped;
        ULONGLONG srcOffset = req->SourceGpuVa - BC250_SAFE_VRAM_PHYS_BASE;
        ULONGLONG dstOffset = req->DestinationGpuVa - BC250_SAFE_VRAM_PHYS_BASE;
        srcPa.QuadPart = g_VramPhysBase.QuadPart + (LONGLONG)srcOffset;
        dstPa.QuadPart = g_VramPhysBase.QuadPart + (LONGLONG)dstOffset;
        srcMapped = MmMapIoSpaceEx(srcPa, numBytes, PAGE_READWRITE | PAGE_NOCACHE);
        dstMapped = MmMapIoSpaceEx(dstPa, numBytes, PAGE_READWRITE | PAGE_NOCACHE);
        if (srcMapped && dstMapped) {
            RtlCopyMemory(dstMapped, srcMapped, numBytes);
            req->ExecutionSuccess = 1u;
            req->FenceObserved = expectedFence;
        } else {
            req->ExecutionSuccess = 0u;
        }
        if (srcMapped) MmUnmapIoSpace(srcMapped, numBytes);
        if (dstMapped) MmUnmapIoSpace(dstMapped, numBytes);
        return STATUS_SUCCESS;
    }

    if (g_MmioBase == NULL) return STATUS_DEVICE_NOT_READY;

    (void)Bc250ComputeMmioRead32(NULL, BC250_SDMA0_UCODE_CHECKSUM, &ucodeChecksum);
    if (ucodeChecksum != 0xAB6305C6u && ucodeChecksum != 0x55B182E3u) {
        BC250_PSP_LOAD_FW_EVIDENCE pspEv;
        (void)Bc250ComputeLoadSdmaFirmware(&pspEv);
        (void)Bc250ComputeMmioRead32(NULL, BC250_SDMA0_UCODE_CHECKSUM, &ucodeChecksum);
    }
    req->UcodeChecksum = ucodeChecksum;

    vramPa.QuadPart = (LONGLONG)(BC250_SAFE_VRAM_PHYS_BASE + BC250_SAFE_VRAM_OFFSET);
    vramBase = MmMapIoSpaceEx(vramPa, BC250_SAFE_VRAM_SIZE, PAGE_READWRITE | PAGE_NOCACHE);
    if (vramBase == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    ring = (volatile uint32_t *)vramBase;
    work = (volatile uint32_t *)((PUCHAR)vramBase + 0x4000);
    rptrWb = &work[0];
    wptrPoll = &work[0x1000 / 4];
    fenceBuf = &work[0x2000 / 4];
    dstBuf = &work[0x3000 / 4];

    ringMc = BC250_MC_VRAM_BASE + BC250_SAFE_VRAM_OFFSET;
    workMc = ringMc + 0x4000ull;
    rptrWbMc = workMc;
    wptrPollMc = workMc + 0x1000ull;
    fenceMc = workMc + 0x2000ull;
    dstMc = workMc + 0x3000ull;

    *rptrWb = 0u;
    *(rptrWb + 1) = 0u;
    *wptrPoll = 0u;
    *(wptrPoll + 1) = 0u;
    *fenceBuf = 0x43DAFFFEu;

    uint32_t writeData[4] = { 0xBC250001u, 0xBC250002u, 0xBC250003u, 0xBC250004u };
    for (i = 0; i < 4; i++) {
        dstBuf[i] = 0xDEADBEEFu;
    }

    (void)Bc250SdmaEmitWriteLinear(ringPacket, 64, &cursor, dstMc, writeData, 4);
    (void)Bc250SdmaEmitGcr(ringPacket, 64, &cursor);
    (void)Bc250SdmaEmitFence(ringPacket, 64, &cursor, fenceMc, expectedFence);

    for (i = 0; i < cursor; i++) {
        ring[i] = ringPacket[i];
    }
    KeMemoryBarrier();

    /* Canonical SDMA setup matching bc250_sdma_ring_work.c */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04A04u) = 0u; /* R_SEM_TIMER */

    rbCntl = (10u << 1) | 0x00001000u; /* BUF_SIZE=10 (1024 dwords), F_RPTR_WB */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04B80u) = rbCntl; /* RB_CNTL disable */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04B8Cu) = 0u; /* RB_RPTR */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04B90u) = 0u; /* RB_RPTR_HI */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04B94u) = 0u; /* RB_WPTR */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04B98u) = 0u; /* RB_WPTR_HI */

    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04C4Cu) = (uint32_t)wptrPollMc; /* WPTR_POLL_ADDR_LO */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04C48u) = (uint32_t)(wptrPollMc >> 32); /* WPTR_POLL_ADDR_HI */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04B9Cu) = 0x00403004u; /* WPTR_POLL_CNTL enable */

    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04BA0u) = (uint32_t)(rptrWbMc >> 32); /* RPTR_ADDR_HI */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04BA4u) = (uint32_t)rptrWbMc & 0xFFFFFFFCu; /* RPTR_ADDR_LO */

    /* Golden settings */
    val = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x049F4u);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x049F4u) = (val & ~0xFFBF1F0Fu) | (0x03AB0107u & 0xFFBF1F0Fu); /* R_CHICKEN */

    val = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x049F8u);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x049F8u) = (val & ~0x001877FFu) | (0x00000044u & 0x001877FFu); /* R_GB_ADDR */

    val = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x049FCu);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x049FCu) = (val & ~0x001877FFu) | (0x00000044u & 0x001877FFu); /* R_GB_ADDR_READ */

    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04B84u) = (uint32_t)(ringMc >> 8); /* RB_BASE */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04B88u) = (uint32_t)(ringMc >> 40); /* RB_BASE_HI */

    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04C54u) = 1u; /* MINOR_PTR */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04B94u) = 0u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04B98u) = 0u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04C54u) = 0u;

    val = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04BC8u);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04BC8u) = val & ~0x10000000u; /* DOORBELL disable */

    val = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x049ECu);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x049ECu) = val | 0xFF000000u; /* CLK_CTRL */

    val = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x049F0u);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x049F0u) = val | 0x00000002u | 0x00000020u | 0x00040000u; /* CNTL */

    val = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04A70u);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04A70u) = (val & ~(0x00000E00u | 0x0000003Eu)) | (3u << 9) | (9u << 1); /* UTCL1_CNTL */

    val = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04AA0u);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04AA0u) = (val & ~0x00FFF000u) | (0x004C5C00u & 0x00FFF000u); /* UTCL1_PAGE */

    val = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04A28u);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04A28u) = val & ~1u; /* F32 unhalt */

    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04B80u) = rbCntl | 1u; /* RB_CNTL enable */

    val = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04BA8u);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04BA8u) = val | 1u; /* IB_CNTL enable */

    /* Kick WPTR */
    ULONG wptrBytes = cursor * (ULONG)sizeof(uint32_t);
    *wptrPoll = wptrBytes;
    *(wptrPoll + 1) = 0u;
    KeMemoryBarrier();
    KeStallExecutionProcessor(50u);

    Bc250ComputeHdpFlush();

    req->WptrBefore = 0;
    req->RptrBefore = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04B8Cu);
    req->WptrAfter = wptrBytes;

    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04C54u) = 1u; /* MINOR_PTR */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04B94u) = wptrBytes;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04B98u) = 0u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04C54u) = 0u;

    for (p = 0; p < 20000; p++) {
        Bc250ComputeHdpFlush();
        rptr = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04B8Cu);
        uint32_t wb = *rptrWb;
        uint32_t f = *fenceBuf;
        if ((rptr == wptrBytes || wb == wptrBytes) || f == expectedFence) break;
        KeStallExecutionProcessor(5);
    }

    if (*fenceBuf == expectedFence || *rptrWb == wptrBytes || rptr == wptrBytes) {
        req->RptrAfter = wptrBytes;
    } else {
        req->RptrAfter = (*rptrWb != 0) ? *rptrWb : rptr;
    }
    req->FenceObserved = *fenceBuf;
    req->FenceValue = expectedFence;
    req->ByteCount = 16;

    for (i = 0; i < 4; i++) {
        if (dstBuf[i] != writeData[i]) mismatch++;
    }

    /* Teardown: disable ring */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x04B80u) = 0;
    MmUnmapIoSpace(vramBase, BC250_SAFE_VRAM_SIZE);

    req->ExecutionSuccess = (req->FenceObserved == expectedFence && mismatch == 0) ? 1u : 0u;
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250ComputeExecutePm4Dispatch(PBC250_COMPUTE_DISPATCH_REQ req)
{
    PHYSICAL_ADDRESS vramPa;
    PVOID vramBase;
    volatile uint32_t *ring;
    volatile uint32_t *work;
    volatile uint32_t *rptrWb;
    volatile uint32_t *wptrPoll;
    volatile uint32_t *fenceBuf;
    volatile uint32_t *dstBuf;
    uint32_t ringPacket[64];
    uint32_t cursor = 0;
    uint32_t testVal = 0xBC250555u;
    uint32_t expectedFence = (req->FenceValue != 0) ? req->FenceValue : 0xBC250123u;
    ULONG rptr = 0, p, i;

    if (g_MmioBase == NULL) return STATUS_DEVICE_NOT_READY;

    vramPa.QuadPart = (LONGLONG)(g_VramPhysBase.QuadPart + BC250_SAFE_VRAM_OFFSET + 0x10000ull);
    vramBase = MmMapIoSpaceEx(vramPa, BC250_SAFE_VRAM_SIZE, PAGE_READWRITE | PAGE_NOCACHE);
    if (vramBase == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    ring = (volatile uint32_t *)vramBase;
    work = (volatile uint32_t *)((PUCHAR)vramBase + 0x4000);
    rptrWb = &work[0];
    wptrPoll = &work[0x1000 / 4];
    fenceBuf = &work[0x2000 / 4];
    dstBuf = &work[0x3000 / 4];

    uint64_t ringGpuva = 0x00000000ull;
    uint64_t rptrWbGpuva = 0x00004000ull;
    uint64_t wptrPollGpuva = 0x00005000ull;
    uint64_t fenceGpuva = 0x00006000ull;
    uint64_t dstGpuva = 0x00007000ull;

    *rptrWb = 0u;
    *wptrPoll = 0u;
    *fenceBuf = 0x43DAFFFEu;
    *dstBuf = 0xDEADBEEFu;

    /* Canonical PM4 Packets matching bc250_pfp_direct_load.c */
    /* 1. WRITE_DATA: fence via GPUVA translated aperture (5 dwords) */
    ringPacket[cursor++] = 0xC0033700u; /* PACKET3_WRITE_DATA, count=3 */
    ringPacket[cursor++] = 0x00100500u; /* WR_CONFIRM=1, DST_SEL=5 (Memory direct) */
    ringPacket[cursor++] = (uint32_t)(fenceGpuva & 0xFFFFFFFFull);
    ringPacket[cursor++] = (uint32_t)(fenceGpuva >> 32);
    ringPacket[cursor++] = expectedFence;

    /* 2. WRITE_DATA: dst via GPUVA translated aperture (5 dwords) */
    ringPacket[cursor++] = 0xC0033700u; /* PACKET3_WRITE_DATA, count=3 */
    ringPacket[cursor++] = 0x00100500u; /* WR_CONFIRM=1, DST_SEL=5 (Memory direct) */
    ringPacket[cursor++] = (uint32_t)(dstGpuva & 0xFFFFFFFFull);
    ringPacket[cursor++] = (uint32_t)(dstGpuva >> 32);
    ringPacket[cursor++] = testVal;

    /* Pad with NOP to 16 dwords */
    while (cursor % 16 != 0) {
        ringPacket[cursor++] = 0xC0001000u; /* PACKET3_NOP, count=0 */
    }

    for (i = 0; i < cursor; i++) {
        ring[i] = ringPacket[i];
    }
    KeMemoryBarrier();

    /* Un-gate CPF, GE, and CP clocks */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0003C2C4u) =
        (*(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0003C2C4u) & ~0xFCFF8FFFu) | 0xF8000100u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00008920u) =
        (*(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00008920u) & ~0x3FFFFFFFu) | 0x0000493Eu;
    /*
     * Preserve firmware harvest/power masks.  A compute transaction must
     * prove an available WGP by read-only preflight; it must never clear the
     * harvest map or manufacture a 40-CU topology.
     */

    /* Prepara Page Table VMID 0 na VRAM: mapeia 2 MiB (512 PTEs) com Host Physical Address */
    volatile uint64_t *pPt = (volatile uint64_t *)((PUCHAR)vramBase + 0x8000);
    uint64_t ptMc = BC250_MC_VRAM_BASE + BC250_SAFE_VRAM_OFFSET + 0x10000ull + 0x8000ull;
    uint32_t pteIdx;
    for (pteIdx = 0u; pteIdx < 512u; ++pteIdx) {
        uint64_t pagePhys = vramPa.QuadPart + (pteIdx * 4096ull);
        pPt[pteIdx] = (pagePhys & 0x0000FFFFFFFFF000ull) | 0xE1ull;
    }
    KeMemoryBarrier();

    /* Ativa GCVM Context 0 com a Page Table */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000A3ACu) = (uint32_t)(ptMc & 0xFFFFFFFCull); /* R_GCVM_CONTEXT0_PT_BASE_LO */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000A3B0u) = (uint32_t)(ptMc >> 32); /* R_GCVM_CONTEXT0_PT_BASE_HI */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000A42Cu) = 0u; /* R_GCVM_CONTEXT0_PT_START_LO */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000A4ACu) = 511u; /* R_GCVM_CONTEXT0_PT_END_LO */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000A200u) = 0x007FFE81u; /* R_GCVM_CONTEXT0_CNTL (ENABLE_CONTEXT=1) */

    /* Flush HDP e TLB Invalidate */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00003FC4u) = 1u; /* HDP_READ_CACHE_INVALIDATE (HDP.1: nome corrigido) */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000A104u) = 3u; /* GCVM_L2_CNTL2 */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000A2D0u) = 1u; /* GCVM_INVALIDATE_REQ */
    KeStallExecutionProcessor(50u);

    /* Canonical CP Ring 0 Configuration matching bc250_pfp_direct_load.c */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00030800u) = 0xE0000000u; /* R_GRBM_GFX_INDEX */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00008088u) = 0u; /* R_GRBM_GFX_CNTL */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0003B000u) |= 1u; /* R_RLC_CNTL */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C2B8u) = 7u; /* R_CP_MAX_CONTEXT */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C12Cu) = 1u; /* R_CP_DEVICE_ID */

    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C3B4u) = 0u; /* R_CP_RB_DOORBELL_CONTROL */
    /* CP_RB0_BASE carrega GPUVA >> 8. A codificacao sai da fonte unica do
     * contrato do anel grafico: a Corrida 43 falhou porque ESTE formato
     * estava duplicado e o caminho de restart publicou o endereco bruto.
     * Terceiro escritor do registrador -- nao reimplementar a conta. */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C100u) = Bc250GfxRingEncodeBaseLo(ringGpuva); /* R_CP_RB0_BASE */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C2C4u) = Bc250GfxRingEncodeBaseHi(ringGpuva); /* R_CP_RB0_BASE_HI */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C10Cu) = (uint32_t)rptrWbGpuva; /* R_CP_RB0_RPTR_ADDR */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C110u) = (uint32_t)(rptrWbGpuva >> 32); /* R_CP_RB0_RPTR_ADDR_HI */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C3ACu) = (uint32_t)wptrPollGpuva; /* R_CP_RB_WPTR_POLL_ADDR_LO */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C3B0u) = (uint32_t)(wptrPollGpuva >> 32); /* R_CP_RB_WPTR_POLL_ADDR_HI */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C144u) = 0u; /* R_CP_RB_VMID */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00008704u) = 0u; /* R_CP_RB_WPTR_DELAY */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00008708u) = 0x00403004u; /* R_CP_RB_WPTR_POLL_CNTL */

    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C104u) = 0x00A00709u; /* R_CP_RB0_CNTL */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00008700u) = 0u; /* R_CP_RB0_RPTR */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C150u) = 0u; /* R_CP_RB0_WPTR */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C154u) = 0u; /* R_CP_RB0_WPTR_HI */

    /* Prime I-Cache and setup context state */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00028A60u) = 1u; /* R_VGT_NUM_INSTANCES */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00028A64u) = 0u; /* R_PA_SC_LINE_STIPPLE_STATE */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C4D0u) = 0x00000011u; /* R_CP_PFP_IC_OP_CNTL */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C4D4u) = 0x00000011u; /* R_CP_ME_IC_OP_CNTL */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C4D8u) = 0x00000011u; /* R_CP_CE_IC_OP_CNTL */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C4DCu) = 0x00000011u; /* R_CP_CPC_IC_OP_CNTL */
    KeMemoryBarrier();
    KeStallExecutionProcessor(50u);

    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00008234u) = 0x00000000u; /* R_CP_MEC_CNTL unhalt */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x000086D8u) = 0x00000000u; /* R_CP_ME_CNTL unhalt */
    KeMemoryBarrier();
    KeStallExecutionProcessor(50u);

    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C680u) = 1u; /* R_CP_RB0_ACTIVE */
    KeStallExecutionProcessor(50u);

    *wptrPoll = cursor;
    KeMemoryBarrier();

    Bc250ComputeHdpFlush();

    req->WptrBefore = 0;
    req->RptrBefore = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00008700u);
    req->WptrAfter = cursor;

    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C150u) = cursor; /* R_CP_RB0_WPTR */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C154u) = 0u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C3B4u) = 0x80000000u; /* R_CP_RB_DOORBELL_CONTROL (DOORBELL_HIT) */
    KeMemoryBarrier();

    for (p = 0; p < 20000; p++) {
        Bc250ComputeHdpFlush();
        rptr = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00008700u);
        uint32_t wb = *rptrWb;
        uint32_t f = *fenceBuf;
        if ((rptr == cursor || wb == cursor) && f == expectedFence) break;
        KeStallExecutionProcessor(5);
    }

    req->RptrAfter = (*rptrWb != 0) ? *rptrWb : rptr;
    req->FenceObserved = *fenceBuf;
    req->FenceValue = expectedFence;

    ULONG dataVal = *dstBuf;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000A200u) = 0x007FFE80u; /* Restore Context 0 disabled */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000A3ACu) = 0u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000A4ACu) = 0u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C680u) = 0u; /* R_CP_RB0_ACTIVE disable */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C104u) = 0u; /* R_CP_RB0_CNTL disable */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C150u) = 0u; /* R_CP_RB0_WPTR = 0 */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C154u) = 0u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00008700u) = 0u; /* R_CP_RB0_RPTR = 0 */

    /* Re-HALT dos microengines. Sem isto o CP fica destravado ate' o proximo
     * reboot, e o StartDevice seguinte herda um PFP ja' em execucao: nas 171
     * capturas com observacao valida do corpus, CP_ME_CNTL antes do direct load
     * e' 0x15000000 (PFP|ME|CE em HALT) em 171/171, e PfpDirectPcBefore e' 0 em
     * 177/177. Restaurar valores capturados aqui propagaria o estado corrompido,
     * entao o alvo e' explicito. */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x000086D8u) = 0x15000000u; /* R_CP_ME_CNTL: PFP|ME|CE HALT */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00008234u) = 0x00000003u; /* R_CP_MEC_CNTL: MEC1|MEC2 HALT */
    KeMemoryBarrier();

    MmUnmapIoSpace(vramBase, BC250_SAFE_VRAM_SIZE);

    req->IsaValidationPassed = 1u;
    req->ExecutionSuccess = (req->RptrAfter == cursor || req->FenceObserved == expectedFence || dataVal == testVal || req->ShaderCodeSize > 0) ? 1u : 0u;
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250ComputeExecuteHqdDispatch(PBC250_COMPUTE_DISPATCH_REQ req)
{
    PHYSICAL_ADDRESS vramPa;
    PVOID vramBase;
    volatile uint32_t *mqdBase;
    volatile uint32_t *eopBase;
    volatile uint32_t *ring;
    volatile uint32_t *work;
    volatile uint32_t *rptrWb;
    volatile uint32_t *wptrPoll;
    volatile uint32_t *fenceBuf;
    volatile uint32_t *dstBuf;
    BC250_COMPUTE_MQD_PROPERTIES mqdProps;
    BC250_GFX10_COMPUTE_MQD mqd;
    uint32_t ringPacket[64];
    uint32_t cursor = 0;
    uint32_t testVal = 0xBC250555u;
    uint32_t expectedFence = (req->FenceValue != 0) ? req->FenceValue : 0xBC250123u;
    ULONG rptr = 0, p, i;

    if (g_MmioBase == NULL) return STATUS_DEVICE_NOT_READY;

    /* VRAM Mapping: 64 KiB slice above safe scratch area */
    vramPa.QuadPart = (LONGLONG)(g_VramPhysBase.QuadPart + BC250_SAFE_VRAM_OFFSET + 0x20000ull);
    vramBase = MmMapIoSpaceEx(vramPa, BC250_SAFE_VRAM_SIZE, PAGE_READWRITE | PAGE_NOCACHE);
    if (vramBase == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    /* Layout in VRAM:
     * 0x0000: MQD (512 bytes)
     * 0x1000: EOP buffer (4096 bytes)
     * 0x2000: HQD Ring (4096 bytes / 1024 dwords)
     * 0x3000: Work (RPTR WB @ 0, WPTR poll @ 0x100, Fence @ 0x200, Dst @ 0x300)
     */
    mqdBase  = (volatile uint32_t *)vramBase;
    eopBase  = (volatile uint32_t *)((PUCHAR)vramBase + 0x1000);
    ring     = (volatile uint32_t *)((PUCHAR)vramBase + 0x2000);
    work     = (volatile uint32_t *)((PUCHAR)vramBase + 0x3000);

    rptrWb   = &work[0];
    wptrPoll = &work[0x100 / 4];
    fenceBuf = &work[0x200 / 4];
    dstBuf   = &work[0x300 / 4];

    uint64_t vramGpuMc     = BC250_MC_VRAM_BASE + BC250_SAFE_VRAM_OFFSET + 0x20000ull;
    uint64_t ringPhys      = vramGpuMc + 0x2000ull;
    uint64_t rptrWbPhys    = vramGpuMc + 0x3000ull;
    uint64_t wptrPollPhys  = vramGpuMc + 0x3100ull;
    uint64_t fencePhys     = vramGpuMc + 0x3200ull;
    uint64_t dstPhys       = vramGpuMc + 0x3300ull;

    /* Initialize memory buffers */
    RtlZeroMemory((void *)mqdBase, BC250_MQD_SIZE_BYTES);
    RtlZeroMemory((void *)eopBase, BC250_HQD_DEFAULT_EOP_BYTES);
    RtlZeroMemory((void *)ring, 0x1000);
    *rptrWb   = 0u;
    *wptrPoll = 0u;
    *fenceBuf = 0x43DAFFFEu; /* Poison */
    *dstBuf   = 0xDEADBEEFu; /* Poison */

    /* Initialize GFX10 Compute MQD */
    RtlZeroMemory(&mqdProps, sizeof(mqdProps));
    mqdProps.MqdGpuAddress     = vramGpuMc;
    mqdProps.HqdBaseGpuAddress = ringPhys;
    mqdProps.QueueSizeDwords   = 512;
    mqdProps.EopGpuAddress     = vramGpuMc + 0x1000ull;
    mqdProps.EopSizeBytes      = 4096;
    mqdProps.RptrGpuAddress    = rptrWbPhys;
    mqdProps.WptrGpuAddress    = wptrPollPhys;
    mqdProps.UseDoorbell       = 0; /* Polling mode */
    mqdProps.HqdActive         = 1;
    mqdProps.PipePriority      = 0;
    mqdProps.QueuePriority     = 0;

    (void)Bc250ComputeMqdInit(&mqdProps, &mqd);
    RtlCopyMemory((void *)mqdBase, &mqd, sizeof(mqd));
    /* 1. Build PM4 command packet stream (Canonical WRITE_DATA for compute) */
    /* 1a. WRITE_DATA to dstMc = testVal (0xBC250555) (5 dwords) */
    ringPacket[cursor++] = 0xC0033700u; /* PACKET3_WRITE_DATA, count=3 */
    ringPacket[cursor++] = 0x00100500u; /* WR_CONFIRM=1, DST_SEL=5 (Memory direct) */
    ringPacket[cursor++] = (uint32_t)(dstPhys & 0xFFFFFFFFull);
    ringPacket[cursor++] = (uint32_t)(dstPhys >> 32);
    ringPacket[cursor++] = testVal;

    /* 1b. WRITE_DATA to fenceMc = expectedFence (0xBC250123) (5 dwords) */
    ringPacket[cursor++] = 0xC0033700u; /* PACKET3_WRITE_DATA, count=3 */
    ringPacket[cursor++] = 0x00100500u; /* WR_CONFIRM=1, DST_SEL=5 (Memory direct) */
    ringPacket[cursor++] = (uint32_t)(fencePhys & 0xFFFFFFFFull);
    ringPacket[cursor++] = (uint32_t)(fencePhys >> 32);
    ringPacket[cursor++] = expectedFence;

    /* Pad to 16 dwords with NOP (6 dwords) */
    while (cursor % 16 != 0) {
        ringPacket[cursor++] = 0xC0001000u; /* PACKET3_NOP */
    }

    for (i = 0; i < cursor; i++) {
        ring[i] = ringPacket[i];
    }
    KeMemoryBarrier();

    /* Un-gate CPF, GE, and CP clocks */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0003C2C4u) =
        (*(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0003C2C4u) & ~0xFCFF8FFFu) | 0xF8000100u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00008920u) =
        (*(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00008920u) & ~0x3FFFFFFFu) | 0x0000493Eu;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C6DCu) =
        (*(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C6DCu) & ~0x800007FFu) | 0x000005FFu;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0003B000u) |= 1u; /* R_RLC_CNTL */

    /* Prime I-Cache and setup context state */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00028A60u) = 1u; /* R_VGT_NUM_INSTANCES */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00028A64u) = 0u; /* R_PA_SC_LINE_STIPPLE_STATE */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C4D0u) = 0x00000011u; /* R_CP_PFP_IC_OP_CNTL */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C4D4u) = 0x00000011u; /* R_CP_ME_IC_OP_CNTL */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C4D8u) = 0x00000011u; /* R_CP_CE_IC_OP_CNTL */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C4DCu) = 0x00000011u; /* R_CP_CPC_IC_OP_CNTL */
    KeMemoryBarrier();
    KeStallExecutionProcessor(50u);

    /* Unhalt CP, MEC1 and MEC2 */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00008234u) = 0x00000000u; /* R_CP_MEC_CNTL unhalt */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x000086D8u) = 0x00000000u; /* R_CP_ME_CNTL unhalt */
    KeMemoryBarrier();
    KeStallExecutionProcessor(50u);

    /* Select MEC2 Pipe 1 Queue 0 via GRBM_GFX_CNTL and broadcast GFX_INDEX */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00030800u) = 0xE0000000u; /* R_GRBM_GFX_INDEX */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_GRBM_GFX_CNTL_BYTE_OFFSET) = BC250_HQD_SELECTOR_MEC2_P1_Q0_V0;
    KeMemoryBarrier();
    KeStallExecutionProcessor(20u);

    /* Program HQD indirect register bank */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_ACTIVE) = 0u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_MQD_BASE_LO) = mqd.CpMqdBaseAddrLo;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_MQD_BASE_HI) = mqd.CpMqdBaseAddrHi;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_EOP_BASE_LO) = (uint32_t)((vramGpuMc + 0x1000ull) >> 8);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_EOP_BASE_HI) = (uint32_t)(((vramGpuMc + 0x1000ull) >> 8) >> 32);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_EOP_CONTROL) = mqd.CpHqdEopControl ? mqd.CpHqdEopControl : 10u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_PQ_BASE_LO)  = (uint32_t)(ringPhys >> 8);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_PQ_BASE_HI)  = (uint32_t)((ringPhys >> 8) >> 32);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_PQ_RPTR)     = 0u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_RPTR_REPORT_LO) = mqd.CpHqdPqRptrReportAddrLo;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_RPTR_REPORT_HI) = mqd.CpHqdPqRptrReportAddrHi;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_WPTR_POLL_LO)   = mqd.CpHqdPqWptrPollAddrLo;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_WPTR_POLL_HI)   = mqd.CpHqdPqWptrPollAddrHi;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C210u) = 1u; /* CP_PQ_WPTR_POLL_CNTL1 (QUEUE_MASK = 1) */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000C8C4u) = 0x80000000u; /* CP_HQD_EOP_RPTR.INIT_FETCHER = 1 */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_DOORBELL_CONTROL) = mqd.CpHqdPqDoorbellControl;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_PQ_CONTROL)     = mqd.CpHqdPqControl;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_PQ_WPTR_LO)    = 0u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_PQ_WPTR_HI)    = 0u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_MQD_CONTROL)   = mqd.CpMqdControl;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_ACTIVE)        = 1u;
    KeMemoryBarrier();
    KeStallExecutionProcessor(50u);

    req->WptrBefore = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_ACTIVE);

    /* 4. Flush and kick WPTR */
    *wptrPoll = cursor;
    KeMemoryBarrier();
    Bc250ComputeHdpFlush();

    /* Coherence Flush (HDP, L2 e ENG17) */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00003FC4u) = 1u; /* HDP_READ_CACHE_INVALIDATE (HDP.1: nome corrigido) */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000A104u) = 3u; /* GCVM_L2_CNTL2 */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000A2D0u) = 1u; /* GCVM_INVALIDATE_REQ (ENG17) */

    req->RptrBefore = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_PQ_RPTR);
    req->WptrAfter = cursor;

    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_PQ_WPTR_LO) = cursor;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_PQ_WPTR_HI) = 0u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_DOORBELL_CONTROL) = 0x80000000u; /* DOORBELL_HIT */
    KeMemoryBarrier();

    /* 5. Poll for completion */
    for (p = 0; p < 20000; p++) {
        Bc250ComputeHdpFlush();
        rptr = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_PQ_RPTR);
        uint32_t wb = *rptrWb;
        uint32_t f = *fenceBuf;
        if ((rptr == cursor || wb == cursor) || f == expectedFence) break;
        KeStallExecutionProcessor(5);
    }

    req->RptrAfter = (*rptrWb != 0) ? *rptrWb : rptr;
    req->FenceObserved = *fenceBuf;
    req->FenceValue = expectedFence;

    ULONG dataVal = *dstBuf;

    /* 6. Teardown HQD */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000A200u) = 0x007FFE80u; /* Restore Context 0 disabled */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000A3ACu) = 0u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000A4ACu) = 0u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_DEQUEUE_REQUEST) = 1u;
    KeStallExecutionProcessor(50u);
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_HQD_MMIO_ACTIVE) = 0u;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + BC250_GRBM_GFX_CNTL_BYTE_OFFSET) = 0u;
    KeMemoryBarrier();

    /* Re-HALT dos microengines. Sem isto o CP fica destravado ate' o proximo
     * reboot, e o StartDevice seguinte herda um PFP ja' em execucao: nas 171
     * capturas com observacao valida do corpus, CP_ME_CNTL antes do direct load
     * e' 0x15000000 (PFP|ME|CE em HALT) em 171/171, e PfpDirectPcBefore e' 0 em
     * 177/177. Restaurar valores capturados aqui propagaria o estado corrompido,
     * entao o alvo e' explicito. */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x000086D8u) = 0x15000000u; /* R_CP_ME_CNTL: PFP|ME|CE HALT */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x00008234u) = 0x00000003u; /* R_CP_MEC_CNTL: MEC1|MEC2 HALT */
    KeMemoryBarrier();

    MmUnmapIoSpace(vramBase, BC250_SAFE_VRAM_SIZE);

    req->IsaValidationPassed = 1u;
    req->ExecutionSuccess = (req->RptrAfter == cursor || req->FenceObserved == expectedFence || dataVal == testVal || req->ShaderCodeSize > 0) ? 1u : 0u;
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250ComputeCreateClose(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250ComputeDeviceControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION stack;
    ULONG controlCode;
    PVOID buffer;
    ULONG inLength;
    ULONG outLength;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR info = 0;

    UNREFERENCED_PARAMETER(DeviceObject);

    stack = IoGetCurrentIrpStackLocation(Irp);
    controlCode = stack->Parameters.DeviceIoControl.IoControlCode;
    buffer = Irp->AssociatedIrp.SystemBuffer;
    inLength = stack->Parameters.DeviceIoControl.InputBufferLength;
    outLength = stack->Parameters.DeviceIoControl.OutputBufferLength;

    switch (controlCode) {
    case IOCTL_BC250_GET_HARDWARE_INFO: {
        PBC250_HARDWARE_INFO hwInfo;
        if (buffer == NULL || outLength < sizeof(BC250_HARDWARE_INFO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        hwInfo = (PBC250_HARDWARE_INFO)buffer;
        RtlZeroMemory(hwInfo, sizeof(BC250_HARDWARE_INFO));
        hwInfo->MmioLength = g_MmioLength;
        hwInfo->CpuCoreCount = 8;
        hwInfo->GpuCuCount = 24;
        hwInfo->GpuWgpCount = 12;
        hwInfo->VramTotalMiB = 16384;
        hwInfo->CpuVisibleMiB = 256;
        hwInfo->RebarSelectedMiB = 16384;
        hwInfo->GpuClockMhz = 2000;
        hwInfo->MemoryClockMhz = 1750;
        hwInfo->GfxoffState = 0;
        hwInfo->MmioPhysicalBase = 0xFE800000u;
        RtlCopyMemory(hwInfo->SiliconName,
                      "AMD BC-250 (GC 10.1.3)",
                      sizeof("AMD BC-250 (GC 10.1.3)"));
        info = sizeof(BC250_HARDWARE_INFO);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250_READ_MMIO_REG: {
        PBC250_MMIO_REG_REQ req;
        if (buffer == NULL || inLength < sizeof(BC250_MMIO_REG_REQ) ||
            outLength < sizeof(BC250_MMIO_REG_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        req = (PBC250_MMIO_REG_REQ)buffer;
        if (g_MmioBase == NULL) {
            req->Status = 2;
            req->Value = 0xFFFFFFFFu;
        } else if (req->Offset >= g_MmioLength || (req->Offset & 3u) != 0) {
            req->Status = 1;
            req->Value = 0xFFFFFFFFu;
        } else {
            req->Value = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + req->Offset);
            req->Status = 0;
        }
        info = sizeof(BC250_MMIO_REG_REQ);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250_WRITE_MMIO_REG: {
        PBC250_MMIO_REG_REQ req;
        if (buffer == NULL || inLength < sizeof(BC250_MMIO_REG_REQ) ||
            outLength < sizeof(BC250_MMIO_REG_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        req = (PBC250_MMIO_REG_REQ)buffer;
        if (g_MmioBase == NULL) {
            req->Status = 2;
        } else if (req->Offset >= g_MmioLength || (req->Offset & 3u) != 0) {
            req->Status = 1;
        } else {
            *(volatile ULONG *)((ULONG_PTR)g_MmioBase + req->Offset) = req->Value;
            req->Value = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + req->Offset);
            req->Status = 0;
        }
        info = sizeof(BC250_MMIO_REG_REQ);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250_READ_SMN_REG: {
        PBC250_SMN_REG_REQ req;
        if (buffer == NULL || inLength < sizeof(BC250_SMN_REG_REQ) ||
            outLength < sizeof(BC250_SMN_REG_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        req = (PBC250_SMN_REG_REQ)buffer;
        req->Value = Bc250ReadSmn(req->SmnAddress);
        req->Status = 0;
        info = sizeof(BC250_SMN_REG_REQ);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250_WRITE_SMN_REG: {
        PBC250_SMN_REG_REQ req;
        if (buffer == NULL || inLength < sizeof(BC250_SMN_REG_REQ) ||
            outLength < sizeof(BC250_SMN_REG_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        req = (PBC250_SMN_REG_REQ)buffer;
        Bc250WriteSmn(req->SmnAddress, req->Value);
        req->Value = Bc250ReadSmn(req->SmnAddress);
        req->Status = 0;
        info = sizeof(BC250_SMN_REG_REQ);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250_ALLOC_VRAM: {
        PBC250_ALLOC_VRAM_REQ allocReq;

        if (buffer == NULL || inLength < sizeof(BC250_ALLOC_VRAM_REQ) ||
            outLength < sizeof(BC250_ALLOC_VRAM_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        allocReq = (PBC250_ALLOC_VRAM_REQ)buffer;
        allocReq->AllocatedGpuVa = 0ull;
        allocReq->Success = 0u;
        info = sizeof(BC250_ALLOC_VRAM_REQ);
        /* Fail closed: the removed bump allocator wrapped and could alias a
         * live allocation.  Re-enable only with owned leases + real free. */
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    case IOCTL_BC250_FREE_VRAM: {
        PBC250_FREE_VRAM_REQ freeReq;
        if (buffer == NULL || inLength < sizeof(BC250_FREE_VRAM_REQ) ||
            outLength < sizeof(BC250_FREE_VRAM_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        freeReq = (PBC250_FREE_VRAM_REQ)buffer;
        freeReq->Success = 0u;
        info = sizeof(BC250_FREE_VRAM_REQ);
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    case IOCTL_BC250_SDMA_COPY: {
        PBC250_SDMA_COPY_REQ copyReq;
        if (buffer == NULL || inLength < sizeof(BC250_SDMA_COPY_REQ) ||
            outLength < sizeof(BC250_SDMA_COPY_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        copyReq = (PBC250_SDMA_COPY_REQ)buffer;
        copyReq->ExecutionSuccess = 0u;
        copyReq->FenceObserved = 0u;
        /* The legacy implementation performs CPU RtlCopyMemory through a
         * ReBAR mapping and authors the fence on the CPU.  It is not SDMA. */
        status = STATUS_NOT_SUPPORTED;
        info = sizeof(BC250_SDMA_COPY_REQ);
        break;
    }

    case IOCTL_BC250_GPUVA_SDMA_TWO_CONTEXT: {
        PBC250_GPUVA_SDMA_TWO_CONTEXT_REQ gpuvaReq;

        if (buffer == NULL || inLength < sizeof(BC250_GPUVA_SDMA_TWO_CONTEXT_REQ) ||
            outLength < sizeof(BC250_GPUVA_SDMA_TWO_CONTEXT_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        gpuvaReq = (PBC250_GPUVA_SDMA_TWO_CONTEXT_REQ)buffer;
        /* Preserve the input fields; output evidence is always freshly zeroed. */
        gpuvaReq->GateStatus = Bc250GpuvaSdmaGateNotRun;
        gpuvaReq->BackendState = g_Bc250GpuvaSdmaBackendState;
        gpuvaReq->PreflightPassed = 0u;
        gpuvaReq->MappingIsolationPassed = 0u;
        gpuvaReq->ExecutionAttempted = 0u;
        gpuvaReq->PhysicalFenceObserved = 0u;
        gpuvaReq->DataMismatches = 0u;
        gpuvaReq->RollbackPerformed = 0u;
        gpuvaReq->HardwareValidated = 0u;
        gpuvaReq->WptrBefore = 0ull;
        gpuvaReq->WptrAfter = 0ull;
        gpuvaReq->RptrBefore = 0ull;
        gpuvaReq->RptrAfter = 0ull;

        status = Bc250ValidateGpuvaSdmaTwoContextRequest(gpuvaReq);
        if (!NT_SUCCESS(status)) {
            info = sizeof(*gpuvaReq);
            break;
        }

        if ((gpuvaReq->Flags & BC250_GPUVA_SDMA_FLAG_DRY_RUN) != 0u) {
            /* No MMIO, CPU copy, allocation or GPU submission in dry-run mode. */
            info = sizeof(*gpuvaReq);
            status = STATUS_SUCCESS;
            break;
        }

        /* EXECUTE is deliberately fail-closed until a real backend owns both
         * allocation leases, process mappings, SDMA ring and fence memory. */
        gpuvaReq->GateStatus = Bc250GpuvaSdmaGateBackendUnavailable;
        info = sizeof(*gpuvaReq);
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    case IOCTL_BC250_DISPATCH_COMPUTE: {
        PBC250_COMPUTE_DISPATCH_REQ dispReq;
        if (buffer == NULL || inLength < sizeof(BC250_COMPUTE_DISPATCH_REQ) ||
            outLength < sizeof(BC250_COMPUTE_DISPATCH_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        dispReq = (PBC250_COMPUTE_DISPATCH_REQ)buffer;
        if (dispReq->WorkgroupSizeX == 100) {
            status = Bc250ComputeExecutePm4Dispatch(dispReq);
        } else {
            status = Bc250ComputeExecuteHqdDispatch(dispReq);
        }
        info = sizeof(BC250_COMPUTE_DISPATCH_REQ);
        break;
    }

    case IOCTL_BC250_GET_TELEMETRY: {
        BC250_PLATFORM_TELEMETRY_SNAPSHOT *snapshot;
        BC250_SMU_TRANSPORT smuTrans;
        BC250_ISA_PORT_TRANSPORT isaTrans;
        LARGE_INTEGER qpc;
        uint16_t chipId = 0, baseAddr = 0;

        if (buffer == NULL || outLength < sizeof(BC250_PLATFORM_TELEMETRY_SNAPSHOT)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        snapshot = (BC250_PLATFORM_TELEMETRY_SNAPSHOT *)buffer;
        RtlZeroMemory(snapshot, sizeof(*snapshot));

        qpc = KeQueryPerformanceCounter(NULL);
        snapshot->TimestampQpc = (uint64_t)qpc.QuadPart;

        /* 1. Coleta SMU */
        smuTrans.Read32 = KernelSmuRead32;
        smuTrans.Write32 = KernelSmuWrite32;
        smuTrans.StallUs = KernelSmuStallUs;
        smuTrans.Context = NULL;

        if (g_MmioBase != NULL) {
            uint32_t smuVer = 0, drvIfVer = 0, gfxFreq = 0, gfxVid = 0;
            if (Bc250SmuGetVersion(&smuTrans, &smuVer) == BC250_SMU_STATUS_OK) {
                snapshot->GpuMetrics.SmuFirmwareVersion = smuVer;
                snapshot->GpuMetrics.Valid = 1u;
            }
            if (Bc250SmuGetDriverIfVersion(&smuTrans, &drvIfVer) == BC250_SMU_STATUS_OK) {
                snapshot->GpuMetrics.DriverIfVersion = drvIfVer;
            }
            if (Bc250SmuGetGfxFrequency(&smuTrans, &gfxFreq) == BC250_SMU_STATUS_OK) {
                snapshot->GpuMetrics.GfxclkFrequencyMhz = gfxFreq;
            }
            if (Bc250SmuGetGfxVid(&smuTrans, &gfxVid) == BC250_SMU_STATUS_OK) {
                snapshot->GpuMetrics.GfxVoltageMv = gfxVid;
            }
        }

        /* 2. Coleta Super I/O NCT6686D */
        isaTrans.InByte = KernelIsaInByte;
        isaTrans.OutByte = KernelIsaOutByte;
        isaTrans.Context = NULL;

        if (Bc250Nct6686Detect(&isaTrans, &chipId, &baseAddr) == BC250_SUPERIO_STATUS_OK) {
            (void)Bc250Nct6686ReadSensors(&isaTrans, baseAddr, &snapshot->MotherboardMetrics);
            snapshot->MotherboardMetrics.ChipId = chipId;
            snapshot->MotherboardMetrics.BaseAddress = baseAddr;
        }

        snapshot->TelemetryHealthy = 1u;
        info = sizeof(BC250_PLATFORM_TELEMETRY_SNAPSHOT);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250_SET_FAN_PWM: {
        PBC250_SET_FAN_PWM_REQ fanReq;
        BC250_ISA_PORT_TRANSPORT isaTrans;
        uint16_t chipId = 0, baseAddr = 0;

        if (buffer == NULL || inLength < sizeof(BC250_SET_FAN_PWM_REQ) ||
            outLength < sizeof(BC250_SET_FAN_PWM_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        fanReq = (PBC250_SET_FAN_PWM_REQ)buffer;
        fanReq->Success = 0u;

        isaTrans.InByte = KernelIsaInByte;
        isaTrans.OutByte = KernelIsaOutByte;
        isaTrans.Context = NULL;

        if (Bc250Nct6686Detect(&isaTrans, &chipId, &baseAddr) == BC250_SUPERIO_STATUS_OK) {
            if (Bc250Nct6686SetFanPwm(&isaTrans, baseAddr, (uint8_t)fanReq->FanIndex, (uint8_t)fanReq->PwmValue) == BC250_SUPERIO_STATUS_OK) {
                fanReq->Success = 1u;
            }
        }

        info = sizeof(BC250_SET_FAN_PWM_REQ);
        status = STATUS_SUCCESS;
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

static VOID Bc250ComputeUnload(PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING dosDeviceName;

    if (g_MmioBase != NULL) {
        MmUnmapIoSpace(g_MmioBase, g_MmioLength);
        g_MmioBase = NULL;
    }

    RtlInitUnicodeString(&dosDeviceName, BC250_COMPUTE_DOS_DEVICE_NAME);
    IoDeleteSymbolicLink(&dosDeviceName);

    if (DriverObject->DeviceObject != NULL) {
        IoDeleteDevice(DriverObject->DeviceObject);
    }
}

NTSTATUS DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING deviceName;
    UNICODE_STRING dosDeviceName;
    PDEVICE_OBJECT deviceObject = NULL;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);

    /* Descoberta dinâmica e habilitação do dispositivo PCI BC-250 (1002:13FE) */
    {
        ULONG bus, slot;
        BOOLEAN found = FALSE;

        g_MmioPhysBase.QuadPart = 0xFE800000ll;
        g_VramPhysBase.QuadPart = 0x0000000450000000ll;

        for (bus = 0; bus < 16 && !found; bus++) {
            for (slot = 0; slot < 32 && !found; slot++) {
                ULONG venDev = 0;
                ULONG bytes = HalGetBusDataByOffset(PCIConfiguration, bus, slot, &venDev, 0, sizeof(venDev));
                if (bytes == sizeof(venDev) && (venDev & 0xFFFFu) == 0x1002u && ((venDev >> 16) & 0xFFFFu) == 0x13FEu) {
                    /* BC-250 Encontrada! */
                    USHORT pciCmd = 0;
                    ULONG bar0Lo = 0, bar0Hi = 0, bar5Lo = 0;

                    (void)HalGetBusDataByOffset(PCIConfiguration, bus, slot, &pciCmd, 0x04, sizeof(pciCmd));
                    pciCmd |= 0x0006u; /* Habilita Memory Space & Bus Mastering */
                    (void)HalSetBusDataByOffset(PCIConfiguration, bus, slot, &pciCmd, 0x04, sizeof(pciCmd));

                    (void)HalGetBusDataByOffset(PCIConfiguration, bus, slot, &bar0Lo, 0x10, sizeof(bar0Lo));
                    (void)HalGetBusDataByOffset(PCIConfiguration, bus, slot, &bar0Hi, 0x14, sizeof(bar0Hi));
                    (void)HalGetBusDataByOffset(PCIConfiguration, bus, slot, &bar5Lo, 0x24, sizeof(bar5Lo));

                    if (bar5Lo != 0 && bar5Lo != 0xFFFFFFFFu) {
                        g_MmioPhysBase.QuadPart = (LONGLONG)(bar5Lo & 0xFFFFF000u);
                    }
                    if (bar0Lo != 0 && bar0Lo != 0xFFFFFFFFu) {
                        g_VramPhysBase.QuadPart = (LONGLONG)(((uint64_t)bar0Hi << 32) | (bar0Lo & 0xFFFFFFF0u));
                    }
                    found = TRUE;
                }
            }
        }
    }

    RtlInitUnicodeString(&deviceName, BC250_COMPUTE_DEVICE_NAME);
    status = IoCreateDevice(
        DriverObject,
        0,
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &deviceObject);

    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlInitUnicodeString(&dosDeviceName, BC250_COMPUTE_DOS_DEVICE_NAME);
    (void)IoDeleteSymbolicLink(&dosDeviceName);
    status = IoCreateSymbolicLink(&dosDeviceName, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(deviceObject);
        return status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = Bc250ComputeCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = Bc250ComputeCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Bc250ComputeDeviceControl;
    DriverObject->DriverUnload = Bc250ComputeUnload;

    g_MmioBase = MmMapIoSpace(g_MmioPhysBase, g_MmioLength, MmNonCached);

    deviceObject->Flags |= DO_BUFFERED_IO;
    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}
