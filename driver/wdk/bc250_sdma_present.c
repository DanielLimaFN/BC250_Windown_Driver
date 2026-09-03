#include "bc250_sdma_present.h"
#include "bc250_probe_telemetry.h"
#include "bc250_display_ddi.h"

BC250_SDMA_PRESENT_CONTEXT g_Bc250SdmaPresent = { 0 };

#define F_RB_ENABLE         0x00000001u
#define M_RB_SIZE           0x0000003Eu
#define F_RPTR_WB           0x00001000u
#define F_POLL_ENABLE       0x00000001u
#define F_IB_ENABLE         0x00000001u
#define F_F32_HALT          0x00000001u
#define F_DOORBELL_ENABLE   0x00000001u

#if defined(BC250_ENABLE_DXGK_REGISTRATION)

static inline uint32_t Bc250SdmaRegRead(
    _In_ BC250_SDMA_PRESENT_CONTEXT *Context,
    _In_ ULONG RegOffset)
{
    if (Context->MmioVirtual == NULL || RegOffset >= Context->MmioLength) {
        return 0u;
    }
    return *(volatile uint32_t *)(Context->MmioVirtual + RegOffset);
}

static inline void Bc250SdmaRegWrite(
    _In_ BC250_SDMA_PRESENT_CONTEXT *Context,
    _In_ ULONG RegOffset,
    _In_ uint32_t Value)
{
    if (Context->MmioVirtual != NULL && RegOffset < Context->MmioLength) {
        *(volatile uint32_t *)(Context->MmioVirtual + RegOffset) = Value;
    }
}

#endif /* BC250_ENABLE_DXGK_REGISTRATION */

NTSTATUS Bc250SdmaPresentInit(
    _In_ PHYSICAL_ADDRESS MmioBar5Physical,
    _In_ uint64_t ScanoutGpuMcAddress)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    PHYSICAL_ADDRESS lowest, highest, boundary;
    PHYSICAL_ADDRESS sdmaRegPhysical;
    uint32_t clk, rbCntl, value;

    if (g_Bc250SdmaPresent.Initialized) {
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&g_Bc250SdmaPresent, sizeof(g_Bc250SdmaPresent));
    g_Bc250SdmaPresent.MmioBase = MmioBar5Physical;
    g_Bc250SdmaPresent.ScanoutGpuAddress = ScanoutGpuMcAddress;
    g_Bc250SdmaPresent.NextFenceSequence = BC250_SDMA_PRESENT_FENCE_INIT;

    if (MmioBar5Physical.QuadPart == 0) {
        Bc250DisplayLog("[SdmaPresent] Error: BAR5 base address is zero\n");
        return STATUS_INVALID_PARAMETER;
    }

    /* Mapeia a página de registradores do SDMA0 (offset 0x4000 do BAR5, tamanho 4KB) */
    sdmaRegPhysical.QuadPart = MmioBar5Physical.QuadPart + BC250_SDMA0_REG_BASE_OFFSET;
    g_Bc250SdmaPresent.MmioLength = 0x1000u;
    g_Bc250SdmaPresent.MmioVirtual = (volatile UCHAR *)MmMapIoSpace(
        sdmaRegPhysical,
        g_Bc250SdmaPresent.MmioLength,
        MmNonCached);
    if (g_Bc250SdmaPresent.MmioVirtual == NULL) {
        Bc250DisplayLog("[SdmaPresent] Error: Failed to map SDMA0 MMIO at 0x%llX\n",
            sdmaRegPhysical.QuadPart);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Aloca memória contígua para Ring Buffer (4KB) e Work Buffer (4KB) */
    lowest.QuadPart = 0;
    highest.QuadPart = 0xFFFFFFFFull; /* 32-bit físico para compatibilidade */
    boundary.QuadPart = 0;
    g_Bc250SdmaPresent.BufferSize = BC250_SDMA_PRESENT_BUFFER_TOTAL;
    g_Bc250SdmaPresent.BufferVirtual = MmAllocateContiguousMemorySpecifyCache(
        g_Bc250SdmaPresent.BufferSize,
        lowest,
        highest,
        boundary,
        MmNonCached);
    if (g_Bc250SdmaPresent.BufferVirtual == NULL) {
        /* Tenta sem restrição de 32-bit */
        highest.QuadPart = 0xFFFFFFFFFFull;
        g_Bc250SdmaPresent.BufferVirtual = MmAllocateContiguousMemorySpecifyCache(
            g_Bc250SdmaPresent.BufferSize,
            lowest,
            highest,
            boundary,
            MmNonCached);
    }
    if (g_Bc250SdmaPresent.BufferVirtual == NULL) {
        Bc250DisplayLog("[SdmaPresent] Error: Failed to allocate contiguous memory (%u bytes)\n",
            (ULONG)g_Bc250SdmaPresent.BufferSize);
        MmUnmapIoSpace((PVOID)g_Bc250SdmaPresent.MmioVirtual, g_Bc250SdmaPresent.MmioLength);
        g_Bc250SdmaPresent.MmioVirtual = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(g_Bc250SdmaPresent.BufferVirtual, g_Bc250SdmaPresent.BufferSize);
    g_Bc250SdmaPresent.BufferPhysical = MmGetPhysicalAddress(g_Bc250SdmaPresent.BufferVirtual);

    /* Divide o buffer em Ring e Work */
    g_Bc250SdmaPresent.Ring = (volatile uint32_t *)g_Bc250SdmaPresent.BufferVirtual;
    g_Bc250SdmaPresent.RingGpuAddress = (uint64_t)g_Bc250SdmaPresent.BufferPhysical.QuadPart;

    g_Bc250SdmaPresent.Work = (volatile uint32_t *)((PUCHAR)g_Bc250SdmaPresent.BufferVirtual +
        BC250_SDMA_PRESENT_RING_BYTES);
    g_Bc250SdmaPresent.WorkGpuAddress = (uint64_t)g_Bc250SdmaPresent.BufferPhysical.QuadPart +
        BC250_SDMA_PRESENT_RING_BYTES;

    /* Inicializa offsets do work buffer */
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / sizeof(uint32_t)] = 0u;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / sizeof(uint32_t)] = 0u;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / sizeof(uint32_t)] =
        (uint32_t)g_Bc250SdmaPresent.NextFenceSequence;

    /* 
     * Programação do hardware SDMA0 (proveniência: bc250_sdma_ring_work.c no silício):
     * 1. Des-gating de clock
     */
    clk = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_REG_BASE_OFFSET);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_REG_BASE_OFFSET,
        clk | 0xFF000000u);
    KeStallExecutionProcessor(50u);

    /* 2. Desabilita o anel antes de reconfigurar */
    rbCntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_REG_BASE_OFFSET);
    rbCntl = (rbCntl & ~(M_RB_SIZE | F_RB_ENABLE | F_RPTR_WB)) | (10u << 1) | F_RPTR_WB;
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_REG_BASE_OFFSET,
        rbCntl & ~F_RB_ENABLE);

    /* 3. Zera ponteiros e programa endereços base */
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_REG_BASE_OFFSET, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR_HI - BC250_SDMA0_REG_BASE_OFFSET, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_REG_BASE_OFFSET, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_REG_BASE_OFFSET, 0u);

    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RPTR_ADDR_LO - BC250_SDMA0_REG_BASE_OFFSET,
        (uint32_t)(g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_RPTR_OFFSET) & 0xFFFFFFFCu);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RPTR_ADDR_HI - BC250_SDMA0_REG_BASE_OFFSET,
        (uint32_t)((g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_RPTR_OFFSET) >> 32));

    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_BASE - BC250_SDMA0_REG_BASE_OFFSET,
        (uint32_t)(g_Bc250SdmaPresent.RingGpuAddress >> 8));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_BASE_HI - BC250_SDMA0_REG_BASE_OFFSET,
        (uint32_t)(g_Bc250SdmaPresent.RingGpuAddress >> 40));

    /* 4. Desabilita doorbell para polling direto */
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_DOORBELL - BC250_SDMA0_REG_BASE_OFFSET);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_DOORBELL - BC250_SDMA0_REG_BASE_OFFSET,
        value & ~F_DOORBELL_ENABLE);

    /* 5. Des-halta o microengine F32 */
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_F32_CNTL - BC250_SDMA0_REG_BASE_OFFSET);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_F32_CNTL - BC250_SDMA0_REG_BASE_OFFSET,
        value & ~F_F32_HALT);

    /* 6. Habilita o anel e Indirect Buffers */
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_REG_BASE_OFFSET,
        rbCntl | F_RB_ENABLE);
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_IB_CNTL - BC250_SDMA0_REG_BASE_OFFSET);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_IB_CNTL - BC250_SDMA0_REG_BASE_OFFSET,
        value | F_IB_ENABLE);

    g_Bc250SdmaPresent.CurrentCursor = 0u;
    g_Bc250SdmaPresent.HardwareEnabled = TRUE;
    g_Bc250SdmaPresent.Initialized = TRUE;

    Bc250DisplayLog("[SdmaPresent] SDMA0 Engine Initialized: RingGpu=0x%llX, WorkGpu=0x%llX, ScanoutGpu=0x%llX\n",
        g_Bc250SdmaPresent.RingGpuAddress,
        g_Bc250SdmaPresent.WorkGpuAddress,
        g_Bc250SdmaPresent.ScanoutGpuAddress);

    return STATUS_SUCCESS;
#else
    UNREFERENCED_PARAMETER(MmioBar5Physical);
    UNREFERENCED_PARAMETER(ScanoutGpuMcAddress);
    g_Bc250SdmaPresent.Initialized = TRUE;
    g_Bc250SdmaPresent.HardwareEnabled = TRUE;
    return STATUS_SUCCESS;
#endif
}

BOOLEAN Bc250SdmaPresentBlt(
    _In_ uint64_t SourceGpuAddress,
    _In_ uint64_t DestGpuAddress,
    _In_ ULONG ByteCount)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    uint32_t cursor;
    uint32_t remainingBytes;
    uint64_t srcOffset, dstOffset;
    LONG expectedFence;
    ULONG poll;

    if (!g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled ||
        ByteCount == 0 || ByteCount > (16u * 1024u * 1024u)) {
        InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
        return FALSE;
    }

    /* Aloca a próxima sequência de fence */
    expectedFence = InterlockedIncrement(&g_Bc250SdmaPresent.NextFenceSequence);

    /* Poison check na fence física antes da GPU escrever */
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / sizeof(uint32_t)] =
        BC250_SDMA_PRESENT_FENCE_POISON;
    KeMemoryBarrier();

    cursor = g_Bc250SdmaPresent.CurrentCursor;
    remainingBytes = ByteCount;
    srcOffset = 0;
    dstOffset = 0;

    /* Emite pacotes de cópia linear (máximo 4MB por pacote) */
    while (remainingBytes > 0) {
        uint32_t chunkSize = (remainingBytes > BC250_SDMA_COPY_MAX_BYTES)
            ? BC250_SDMA_COPY_MAX_BYTES
            : remainingBytes;

        if (Bc250SdmaEmitCopyLinear(
                (uint32_t *)g_Bc250SdmaPresent.Ring,
                BC250_SDMA_PRESENT_RING_DWORDS,
                &cursor,
                SourceGpuAddress + srcOffset,
                DestGpuAddress + dstOffset,
                chunkSize) != BC250_SDMA_OK) {
            InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
            return FALSE;
        }

        srcOffset += chunkSize;
        dstOffset += chunkSize;
        remainingBytes -= chunkSize;
    }

    /* Flush de cache da GPU */
    if (Bc250SdmaEmitGcr(
            (uint32_t *)g_Bc250SdmaPresent.Ring,
            BC250_SDMA_PRESENT_RING_DWORDS,
            &cursor) != BC250_SDMA_OK) {
        InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
        return FALSE;
    }

    /* Fence física gravada pela GPU */
    if (Bc250SdmaEmitFence(
            (uint32_t *)g_Bc250SdmaPresent.Ring,
            BC250_SDMA_PRESENT_RING_DWORDS,
            &cursor,
            g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_FENCE_OFFSET,
            (uint32_t)expectedFence) != BC250_SDMA_OK) {
        InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
        return FALSE;
    }

    KeMemoryBarrier();

    /* Submete para o hardware atualizando o WPTR */
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_REG_BASE_OFFSET,
        cursor * (uint32_t)sizeof(uint32_t));
    g_Bc250SdmaPresent.CurrentCursor = cursor;
    InterlockedIncrement(&g_Bc250SdmaPresent.BltSubmittedCount);

    /* 
     * Aguarda a GPU aposentar a fence física em memória (tripé no silício).
     * Timeout curto de 10 ms para nunca travar a pipeline do Windows.
     */
    for (poll = 0; poll < 10000u; ++poll) {
        if (g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / sizeof(uint32_t)] ==
            (uint32_t)expectedFence) {
            InterlockedIncrement(&g_Bc250SdmaPresent.BltCompletedCount);
            return TRUE; /* SUCESSO ABSOLUTO: GPU executou o blt e aposentou a fence física */
        }
        KeStallExecutionProcessor(1u);
    }

    /* Timeout do hardware: registra e sinaliza fallback fail-closed */
    InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
    return FALSE;
#else
    UNREFERENCED_PARAMETER(SourceGpuAddress);
    UNREFERENCED_PARAMETER(DestGpuAddress);
    UNREFERENCED_PARAMETER(ByteCount);
    return TRUE;
#endif
}

void Bc250SdmaPresentTeardown(void)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    if (!g_Bc250SdmaPresent.Initialized) {
        return;
    }

    g_Bc250SdmaPresent.HardwareEnabled = FALSE;

    if (g_Bc250SdmaPresent.MmioVirtual != NULL) {
        uint32_t rbCntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_REG_BASE_OFFSET);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_REG_BASE_OFFSET,
            rbCntl & ~F_RB_ENABLE);
        MmUnmapIoSpace((PVOID)g_Bc250SdmaPresent.MmioVirtual, g_Bc250SdmaPresent.MmioLength);
        g_Bc250SdmaPresent.MmioVirtual = NULL;
    }

    if (g_Bc250SdmaPresent.BufferVirtual != NULL) {
        MmFreeContiguousMemory(g_Bc250SdmaPresent.BufferVirtual);
        g_Bc250SdmaPresent.BufferVirtual = NULL;
    }

    g_Bc250SdmaPresent.Initialized = FALSE;
    Bc250DisplayLog("[SdmaPresent] Teardown Complete. Submitted=%ld, Completed=%ld, Fallbacks=%ld\n",
        g_Bc250SdmaPresent.BltSubmittedCount,
        g_Bc250SdmaPresent.BltCompletedCount,
        g_Bc250SdmaPresent.FallbackCpuCount);
#else
    g_Bc250SdmaPresent.Initialized = FALSE;
    g_Bc250SdmaPresent.HardwareEnabled = FALSE;
#endif
}
