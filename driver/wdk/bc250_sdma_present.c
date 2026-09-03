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

    /*
     * PROBE READ-ONLY de SDMA0:
     * Lei 6: SNAPSHOT antes de tocar no hardware.
     * Mapeia 4KB de registradores do SDMA0 e realiza leitura passiva (proveniente de BAR5).
     */
    clk = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_REG_BASE_OFFSET);
    rbCntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_REG_BASE_OFFSET);
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_F32_CNTL - BC250_SDMA0_REG_BASE_OFFSET);
    {
        uint32_t regCntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CNTL - BC250_SDMA0_REG_BASE_OFFSET);
        uint32_t regStatus = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_STATUS - BC250_SDMA0_REG_BASE_OFFSET);
        uint32_t regRbBase = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_BASE - BC250_SDMA0_REG_BASE_OFFSET);
        uint32_t regRbRptr = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_REG_BASE_OFFSET);
        uint32_t regRbWptr = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_REG_BASE_OFFSET);
        uint32_t regDoorbell = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_DOORBELL - BC250_SDMA0_REG_BASE_OFFSET);

        Bc250DisplayLog("[SdmaPresent] READ-ONLY SNAPSHOT SDMA0: CLK=%08X CNTL=%08X STATUS=%08X F32=%08X RBCNTL=%08X RBBASE=%08X RPTR=%08X WPTR=%08X DOORBELL=%08X\n",
            clk, regCntl, regStatus, value, rbCntl, regRbBase, regRbRptr, regRbWptr, regDoorbell);
    }

    /* 
     * Mantém HardwareEnabled = FALSE para garantir Código 0 e tela acesa via Present fail-closed.
     * O backend de Blt executa fallback seguro para CPU até que o microcódigo esteja autenticado.
     */
    g_Bc250SdmaPresent.HardwareEnabled = FALSE;
    g_Bc250SdmaPresent.Initialized = TRUE;

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
