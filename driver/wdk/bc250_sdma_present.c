#include "bc250_sdma_present.h"
#include "bc250_probe_telemetry.h"
#include "bc250_display_ddi.h"

BC250_SDMA_PRESENT_CONTEXT g_Bc250SdmaPresent = { 0 };

#define F_RB_ENABLE         0x00000001u
#define M_RB_SIZE           0x0000003Eu
#define F_RPTR_WB           0x00001000u
/*
 * Mascaras alinhadas ao modulo provado em silicio
 * (driver/wdk/bc250_sdma_ring_work.c:62-69) e ao catalogo.
 *
 * F_POLL_ENABLE era 0x1 aqui e 0x4 no modulo provado.  O catalogo fixa o
 * valor completo do SDMA como 0x00403004 (docs/03_CATALOGO_DE_REGISTRADORES.md:212),
 * isto e' 0x00403000 | 0x4 -- o bit certo e' 0x4.
 *
 * F_DOORBELL_ENABLE era 0x1 aqui e 0x10000000 no modulo provado
 * (SDMA0_GFX_DOORBELL__ENABLE, bit 28).  Com 0x1 o clear de doorbell mexia
 * no bit errado.
 */
#define F_POLL_ENABLE       0x00000004u
#define F_IB_ENABLE         0x00000001u
#define F_F32_HALT          0x00000001u
#define F_DOORBELL_ENABLE   0x10000000u
/* SDMA0_CNTL: UTC_L1_ENABLE = bit 1, MIDCMD_PREEMPT_ENABLE = bit 5. */
#define F_CNTL_UTC_L1       0x00000002u
#define F_CNTL_MIDCMD       0x00000020u

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

/*
 * Sequencia de coerencia HDP identica a Bc250SdmaHdp de
 * driver/wdk/bc250_sdma_ring_work.c:109-123, que e' o caminho provado em
 * silicio (Item 63) e o mesmo padrao do driver de laboratorio (dbg/hw.c:5661).
 *
 * O que este modulo fazia antes: uma unica escrita `HDP_FLUSH(0x04A80) = 1`,
 * sem leitura de volta e sem tocar em HDP_INVALIDATE nem em HDP_MISC.  Ou
 * seja: valor invertido no registrador errado, e o invalidate de verdade
 * nunca acontecia -- consistente com o sintoma da corrida 09:04 (engine
 * ociosa, RPTR=0, fence com poison intacto, 4/4 bytes divergentes).
 *
 * As leituras de volta nao sao decorativas: sao o que garante que a escrita
 * postada chegou ao dispositivo antes do passo seguinte.
 */
static void Bc250SdmaPresentHdp(
    _In_ BC250_SDMA_PRESENT_CONTEXT *Context)
{
    uint32_t misc;

    misc = Bc250SdmaRegRead(Context,
        BC250_SDMA0_REG_HDP_MISC - BC250_SDMA0_MMIO_WINDOW_START);

    Bc250SdmaRegWrite(Context,
        BC250_SDMA0_REG_HDP_FLUSH - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    (void)Bc250SdmaRegRead(Context,
        BC250_SDMA0_REG_HDP_FLUSH - BC250_SDMA0_MMIO_WINDOW_START);

    Bc250SdmaRegWrite(Context,
        BC250_SDMA0_REG_HDP_INVALIDATE - BC250_SDMA0_MMIO_WINDOW_START, 1u);
    (void)Bc250SdmaRegRead(Context,
        BC250_SDMA0_REG_HDP_INVALIDATE - BC250_SDMA0_MMIO_WINDOW_START);

    Bc250SdmaRegWrite(Context,
        BC250_SDMA0_REG_HDP_MISC - BC250_SDMA0_MMIO_WINDOW_START, misc | 1u);
    (void)Bc250SdmaRegRead(Context,
        BC250_SDMA0_REG_HDP_MISC - BC250_SDMA0_MMIO_WINDOW_START);

    Bc250SdmaRegWrite(Context,
        BC250_SDMA0_REG_HDP_MISC - BC250_SDMA0_MMIO_WINDOW_START, misc & ~1u);
    (void)Bc250SdmaRegRead(Context,
        BC250_SDMA0_REG_HDP_MISC - BC250_SDMA0_MMIO_WINDOW_START);
}

#endif /* BC250_ENABLE_DXGK_REGISTRATION */

NTSTATUS Bc250SdmaPresentInit(
    _In_ PHYSICAL_ADDRESS MmioBar5Physical,
    _In_ PHYSICAL_ADDRESS VramBar0Physical,
    _In_ uint64_t VramMcStart,
    _In_ uint64_t ScanoutGpuMcAddress)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    PHYSICAL_ADDRESS sdmaRegPhysical;
    PHYSICAL_ADDRESS sdmaVramPhysical;
    uint32_t clk, rbCntl, value;
    uint32_t i, cursor = 0u;
    uint32_t packets[32] = { 0 };
    static const uint32_t testData[4] = {
        0x11223344u, 0x55667788u, 0xA5A5A5A5u, 0xBC250125u
    };
    ULONG poll;

    if (g_Bc250SdmaPresent.Initialized) {
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&g_Bc250SdmaPresent, sizeof(g_Bc250SdmaPresent));
    g_Bc250SdmaPresent.MmioBase = MmioBar5Physical;
    g_Bc250SdmaPresent.VramBase = VramBar0Physical;
    if (VramMcStart == 0) {
        VramMcStart = BC250_SDMA_VRAM_MC_BASE;
    }
    g_Bc250SdmaPresent.VramMcStart = VramMcStart;
    g_Bc250SdmaPresent.ScanoutGpuAddress = ScanoutGpuMcAddress;
    g_Bc250SdmaPresent.NextFenceSequence = BC250_SDMA_PRESENT_FENCE_INIT;

    if (MmioBar5Physical.QuadPart == 0 || VramBar0Physical.QuadPart == 0) {
        Bc250DisplayLog("[SdmaPresent] Error: BAR5 or BAR0 base address is zero\n");
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Mapeia a janela 0x03000..0x04FFF do BAR5 (8 KB).  Precisa comecar em
     * 0x03000 e nao em 0x04000 porque HDP_MEM_COHERENCY_FLUSH_CNTL (0x03FC4) e
     * HDP_MISC_CNTL (0x03FCC) ficam abaixo do bloco do SDMA0 -- ver comentario
     * de proveniencia em bc250_sdma_present.h.
     */
    sdmaRegPhysical.QuadPart = MmioBar5Physical.QuadPart + BC250_SDMA0_MMIO_WINDOW_START;
    g_Bc250SdmaPresent.MmioLength = BC250_SDMA0_MMIO_WINDOW_BYTES;
    g_Bc250SdmaPresent.MmioVirtual = (volatile UCHAR *)MmMapIoSpace(
        sdmaRegPhysical,
        g_Bc250SdmaPresent.MmioLength,
        MmNonCached);
    if (g_Bc250SdmaPresent.MmioVirtual == NULL) {
        Bc250DisplayLog("[SdmaPresent] Error: Failed to map SDMA0 MMIO at 0x%llX\n",
            sdmaRegPhysical.QuadPart);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Mapeia a janela de Ring (4KB) + Work (4KB) em VRAM no BAR0 (offset BC250_SDMA_LEGACY_RING_OFFSET) */
    sdmaVramPhysical.QuadPart = VramBar0Physical.QuadPart + BC250_SDMA_LEGACY_RING_OFFSET;
    g_Bc250SdmaPresent.VramLength = BC250_SDMA_PRESENT_BUFFER_TOTAL;
    g_Bc250SdmaPresent.VramVirtual = (volatile UCHAR *)MmMapIoSpace(
        sdmaVramPhysical,
        g_Bc250SdmaPresent.VramLength,
        MmNonCached);
    if (g_Bc250SdmaPresent.VramVirtual == NULL) {
        Bc250DisplayLog("[SdmaPresent] Error: Failed to map SDMA0 VRAM aperture at 0x%llX\n",
            sdmaVramPhysical.QuadPart);
        MmUnmapIoSpace((PVOID)g_Bc250SdmaPresent.MmioVirtual, g_Bc250SdmaPresent.MmioLength);
        g_Bc250SdmaPresent.MmioVirtual = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Divide a janela de VRAM em Ring e Work com endereços MC autoritativos da GPU */
    g_Bc250SdmaPresent.Ring = (volatile uint32_t *)g_Bc250SdmaPresent.VramVirtual;
    g_Bc250SdmaPresent.RingGpuAddress = g_Bc250SdmaPresent.VramMcStart + BC250_SDMA_LEGACY_RING_OFFSET;

    g_Bc250SdmaPresent.Work = (volatile uint32_t *)(g_Bc250SdmaPresent.VramVirtual + BC250_SDMA_PRESENT_RING_BYTES);
    g_Bc250SdmaPresent.WorkGpuAddress = g_Bc250SdmaPresent.RingGpuAddress + BC250_SDMA_PRESENT_RING_BYTES;

    /* Snapshot inicial de registradores */
    clk = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_MMIO_WINDOW_START);
    rbCntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250DisplayLog("[SdmaPresent] Pre-Arm SDMA0: CLK=%08X STATUS=%08X RBCNTL=%08X RingGpu=0x%llX WorkGpu=0x%llX\n",
        clk, value, rbCntl, g_Bc250SdmaPresent.RingGpuAddress, g_Bc250SdmaPresent.WorkGpuAddress);

    /*
     * 1. Preparação da memória em VRAM:
     * Inicializa Work buffer com Poison na Fence e padrões conhecidos no Destino.
     */
    for (i = 0u; i < BC250_SDMA_PRESENT_RING_DWORDS; ++i) {
        g_Bc250SdmaPresent.Ring[i] = 0u;
    }
    for (i = 0u; i < 4u; ++i) {
        g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_DEST_OFFSET / 4u + i] = 0xDEADC000u + i;
    }
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u] = 0u;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u + 1u] = 0u;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] = 0u;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u + 1u] = 0u;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u] = BC250_SDMA_PRESENT_FENCE_POISON;
    KeMemoryBarrier();

    /*
     * 2. Monta pacotes de submissão do Tripé do Silício:
     * Pacote A: Write Linear de 4 dwords em Destino VRAM.
     * Pacote B: GCR Cache Flush da GPU.
     * Pacote C: Fence física gravada pela GPU na VRAM.
     */
    cursor = 0u;
    Bc250SdmaEmitWriteLinear(packets, 32u, &cursor,
        g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_DEST_OFFSET,
        testData, 4u);
    Bc250SdmaEmitGcr(packets, 32u, &cursor);
    Bc250SdmaEmitFence(packets, 32u, &cursor,
        g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_FENCE_OFFSET,
        BC250_SDMA_PRESENT_FENCE_INIT + 1u);

    for (i = 0u; i < cursor; ++i) {
        g_Bc250SdmaPresent.Ring[i] = packets[i];
    }
    KeMemoryBarrier();

    /*
     * 3. Configuração dos registradores SDMA0 (proveniência: bc250_sdma_ring_work.c):
     */
    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_SEM_TIMER - BC250_SDMA0_MMIO_WINDOW_START, 0u);

    rbCntl = (rbCntl & ~(M_RB_SIZE | F_RB_ENABLE | F_RPTR_WB)) | (10u << 1) | F_RPTR_WB;
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START, rbCntl & ~F_RB_ENABLE);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);

    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_WPTR_POLL_ADDR_LO - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)(g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_WPTR_OFFSET));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_WPTR_POLL_ADDR_HI - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)((g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_WPTR_OFFSET) >> 32));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_WPTR_POLL_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        0x00403000u | F_POLL_ENABLE);

    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RPTR_ADDR_HI - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)((g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_RPTR_OFFSET) >> 32));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RPTR_ADDR_LO - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)(g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_RPTR_OFFSET) & 0xFFFFFFFCu);

    /* Golden values para Cyan Skillfish (Lei 7: RMW seguro) */
    {
        uint32_t cur = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CHICKEN - BC250_SDMA0_MMIO_WINDOW_START);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CHICKEN - BC250_SDMA0_MMIO_WINDOW_START,
            (cur & ~0xFFBF1F0Fu) | (0x03AB0107u & 0xFFBF1F0Fu));

        cur = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_GB_ADDR - BC250_SDMA0_MMIO_WINDOW_START);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_GB_ADDR - BC250_SDMA0_MMIO_WINDOW_START,
            (cur & ~0x001877FFu) | (0x00000044u & 0x001877FFu));

        cur = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_GB_ADDR_READ - BC250_SDMA0_MMIO_WINDOW_START);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_GB_ADDR_READ - BC250_SDMA0_MMIO_WINDOW_START,
            (cur & ~0x001877FFu) | (0x00000044u & 0x001877FFu));
    }

    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_BASE - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)(g_Bc250SdmaPresent.RingGpuAddress >> 8));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_BASE_HI - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)(g_Bc250SdmaPresent.RingGpuAddress >> 40));

    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 1u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);

    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_DOORBELL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_DOORBELL - BC250_SDMA0_MMIO_WINDOW_START,
        value & ~F_DOORBELL_ENABLE);

    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_MMIO_WINDOW_START,
        value | 0xFF000000u);

    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        value | F_CNTL_UTC_L1 | F_CNTL_MIDCMD);
    /*
     * O valor anterior era 0x42 = bit1 | bit6.  O comentario dizia
     * "UTC_L1_ENABLE | MIDCMD_PREEMPT_ENABLE", mas MIDCMD_PREEMPT_ENABLE e' o
     * bit 5 (0x20) -- ver bc250_sdma_ring_work.c:63.  Ou seja: ligava um bit
     * que nao e' o pretendido e deixava o pretendido desligado.
     */

    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_UTCL1_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_UTCL1_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        (value & ~(0x00000E00u | 0x0000003Eu)) | (3u << 9) | (9u << 1));

    {
        uint32_t cur = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_UTCL1_PAGE - BC250_SDMA0_MMIO_WINDOW_START);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_UTCL1_PAGE - BC250_SDMA0_MMIO_WINDOW_START,
            (cur & ~0x00FFF000u) | (0x004C5C00u & 0x00FFF000u));
    }

    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_F32_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_F32_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        value & ~F_F32_HALT);

    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        rbCntl | F_RB_ENABLE);

    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_IB_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_IB_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        value | F_IB_ENABLE);

    /*
     * 4. Dispara a submissão do Tripé:
     */
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] = cursor * sizeof(uint32_t);
    KeMemoryBarrier();
    KeStallExecutionProcessor(50u);

    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 1u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START,
        cursor * sizeof(uint32_t));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);

    {
        uint32_t wptrAfter = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t rptrAfter = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t statusAfter = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
        Bc250DisplayLog("[SdmaPresent] Post-Kick SDMA0: WPTR_Reg=%u, RPTR_Reg=%u, STATUS=%08X (TargetWPTR=%u)\n",
            wptrAfter, rptrAfter, statusAfter, cursor * sizeof(uint32_t));
    }

    /*
     * 5. Poll com timeout curto (10 ms) aguardando aposentadoria de RPTR e Fence gravada pela GPU:
     */
    for (poll = 1u; poll <= 2000u; ++poll) {
        uint32_t rptrWb;
        uint32_t fenceObserved;
        uint32_t rptrReg;

        Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
        rptrWb = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
        fenceObserved = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
        rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);

        if ((rptrWb == cursor * sizeof(uint32_t) || rptrReg == cursor * sizeof(uint32_t)) &&
            fenceObserved == (BC250_SDMA_PRESENT_FENCE_INIT + 1u)) {
            g_Bc250SdmaPresent.TripodPolls = poll;
            g_Bc250SdmaPresent.TripodFenceObserved = fenceObserved;
            break;
        }
        KeStallExecutionProcessor(5u);
    }

    /*
     * 6. Validação byte a byte dos dados transferidos pela GPU:
     */
    g_Bc250SdmaPresent.TripodMismatches = 0u;
    for (i = 0u; i < 4u; ++i) {
        if (g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_DEST_OFFSET / 4u + i] != testData[i]) {
            g_Bc250SdmaPresent.TripodMismatches++;
        }
    }

    if (g_Bc250SdmaPresent.TripodFenceObserved == (BC250_SDMA_PRESENT_FENCE_INIT + 1u) &&
        g_Bc250SdmaPresent.TripodMismatches == 0u) {
        g_Bc250SdmaPresent.TripodValidated = TRUE;
        g_Bc250SdmaPresent.HardwareEnabled = TRUE;
        g_Bc250SdmaPresent.CurrentCursor = cursor;
        Bc250DisplayLog("[SdmaPresent] TRIPÉ NO SILÍCIO COMPROVADO (Item 63)! WPTR=%u RPTR=%u Fence=%08X Dados=4/4 OK em %u polls!\n",
            cursor * sizeof(uint32_t),
            g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u],
            g_Bc250SdmaPresent.TripodFenceObserved,
            g_Bc250SdmaPresent.TripodPolls);
    } else {
        uint32_t wptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t statusReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
        /*
         * 0x04A18 e' STATUS1_REG (0x3FF = OCIOSA pelo criterio do upstream),
         * nao um log de erro.  A corrida 09:04 reportou "ERR_LOG=000003FF", o
         * que na verdade dizia "engine parada e ociosa".  O log de erro real
         * mora em 0x04AC0 e agora e' lido separadamente.
         */
        uint32_t status1Reg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_STATUS1 - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t errorLog = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_ERROR_LOG - BC250_SDMA0_MMIO_WINDOW_START);

        g_Bc250SdmaPresent.TripodValidated = FALSE;
        g_Bc250SdmaPresent.HardwareEnabled = FALSE;
        Bc250DisplayLog("[SdmaPresent] AVISO TRIPÉ: Timeout/Mismatch (Polls=%u, Fence=%08X [Poison=%08X], Mismatches=%u, WPTR_Reg=%u, RPTR_Reg=%u, STATUS=%08X, STATUS1=%08X [0x3FF=idle], ERR_LOG=%08X). Mantendo fallback fail-closed.\n",
            poll,
            g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u],
            BC250_SDMA_PRESENT_FENCE_POISON,
            g_Bc250SdmaPresent.TripodMismatches,
            wptrReg, rptrReg, statusReg, status1Reg, errorLog);
    }

    g_Bc250SdmaPresent.Initialized = TRUE;
    return STATUS_SUCCESS;
#else
    UNREFERENCED_PARAMETER(MmioBar5Physical);
    UNREFERENCED_PARAMETER(VramBar0Physical);
    UNREFERENCED_PARAMETER(VramMcStart);
    UNREFERENCED_PARAMETER(ScanoutGpuMcAddress);
    g_Bc250SdmaPresent.Initialized = TRUE;
    g_Bc250SdmaPresent.HardwareEnabled = TRUE;
    g_Bc250SdmaPresent.TripodValidated = TRUE;
    return STATUS_SUCCESS;
#endif
}

BOOLEAN Bc250SdmaPresentBlt(
    _In_ uint64_t SourceGpuAddress,
    _In_ uint64_t DestGpuAddress,
    _In_ ULONG ByteCount)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    uint32_t packets[128] = { 0 };
    uint32_t cursor = 0u;
    uint32_t nextFence;
    uint32_t startCursor;
    uint32_t i;
    ULONG poll;
    ULONG remainingBytes;
    uint64_t curSrc;
    uint64_t curDst;
    BC250_SDMA_STATUS status;

    if (!g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled) {
        InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
        return FALSE;
    }
    if (SourceGpuAddress == 0 || DestGpuAddress == 0 || ByteCount == 0 ||
        ByteCount > (64u * 1024u * 1024u)) {
        InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
        return FALSE;
    }

    remainingBytes = ByteCount;
    curSrc = SourceGpuAddress;
    curDst = DestGpuAddress;
    cursor = 0u;

    while (remainingBytes > 0) {
        ULONG chunkSize = (remainingBytes > BC250_SDMA_COPY_MAX_BYTES) ?
            BC250_SDMA_COPY_MAX_BYTES : remainingBytes;

        status = Bc250SdmaEmitCopyLinear(packets, 128u, &cursor,
            curSrc, curDst, chunkSize);
        if (status != BC250_SDMA_OK) {
            InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
            return FALSE;
        }
        remainingBytes -= chunkSize;
        curSrc += chunkSize;
        curDst += chunkSize;
    }

    Bc250SdmaEmitGcr(packets, 128u, &cursor);
    nextFence = (uint32_t)InterlockedIncrement(&g_Bc250SdmaPresent.NextFenceSequence);
    Bc250SdmaEmitFence(packets, 128u, &cursor,
        g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_FENCE_OFFSET,
        nextFence);

    /* Cada submissao precisa substituir poison; fence anterior nao e' prova. */
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u] =
        BC250_SDMA_PRESENT_FENCE_POISON;
    KeMemoryBarrier();

    startCursor = g_Bc250SdmaPresent.CurrentCursor;
    if (startCursor + cursor >= BC250_SDMA_PRESENT_RING_DWORDS) {
        startCursor = 0u;
    }

    for (i = 0u; i < cursor; ++i) {
        g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
    }
    KeMemoryBarrier();

    g_Bc250SdmaPresent.CurrentCursor = startCursor + cursor;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
    KeMemoryBarrier();

    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 1u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START,
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);

    InterlockedIncrement(&g_Bc250SdmaPresent.BltSubmittedCount);

    for (poll = 1u; poll <= 10000u; ++poll) {
        uint32_t fenceObserved = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
        if (fenceObserved == nextFence) {
            uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
            uint32_t targetWptr = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
            if (rptrReg == targetWptr) {
                LONG completed;
                g_Bc250SdmaPresent.TripodValidated = TRUE;
                completed = InterlockedIncrement(&g_Bc250SdmaPresent.BltCompletedCount);
                if (completed == 1 || (completed % 60) == 0) {
                    Bc250DisplayLog("[SdmaPresent] BLT_TRIPOD PASS #%ld: WPTR=%u RPTR=%u Fence=%08X Poison=%08X Bytes=%u\n",
                        completed, targetWptr, rptrReg, fenceObserved,
                        BC250_SDMA_PRESENT_FENCE_POISON, ByteCount);
                }
                return TRUE;
            }
        }
        KeStallExecutionProcessor(5u);
    }

    {
        uint32_t wptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t statusReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t fenceObs = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
        LONG fallbacks = InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
        if (fallbacks == 1 || (fallbacks % 60) == 0) {
            Bc250DisplayLog("[SdmaPresent] Blt Timeout #%ld: TargetWPTR=%u, WPTR_Reg=%u, RPTR_Reg=%u, STATUS=%08X, FenceExp=%08X, FenceObs=%08X\n",
                fallbacks, (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t)),
                wptrReg, rptrReg, statusReg, nextFence, fenceObs);
        }
    }
    return FALSE;
#else
    UNREFERENCED_PARAMETER(SourceGpuAddress);
    UNREFERENCED_PARAMETER(DestGpuAddress);
    UNREFERENCED_PARAMETER(ByteCount);
    return TRUE;
#endif
}

BOOLEAN Bc250SdmaPresentFill(
    _In_ uint64_t DestGpuAddress,
    _In_ ULONG ByteCount,
    _In_ uint32_t FillPattern)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    uint32_t packets[128] = { 0 };
    uint32_t cursor = 0u;
    uint32_t nextFence;
    uint32_t startCursor;
    uint32_t i;
    ULONG poll;
    ULONG remainingBytes;
    uint64_t curDst;
    BC250_SDMA_STATUS status;

    if (!g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled) {
        return FALSE;
    }
    if (DestGpuAddress == 0 || ByteCount == 0 ||
        ByteCount > (64u * 1024u * 1024u)) {
        return FALSE;
    }

    remainingBytes = ByteCount;
    curDst = DestGpuAddress;
    cursor = 0u;

    while (remainingBytes > 0) {
        ULONG chunkSize = (remainingBytes > BC250_SDMA_FILL_MAX_BYTES) ?
            BC250_SDMA_FILL_MAX_BYTES : remainingBytes;

        status = Bc250SdmaEmitConstFill(packets, 128u, &cursor,
            curDst, FillPattern, chunkSize);
        if (status != BC250_SDMA_OK) {
            return FALSE;
        }
        remainingBytes -= chunkSize;
        curDst += chunkSize;
    }

    Bc250SdmaEmitGcr(packets, 128u, &cursor);
    nextFence = (uint32_t)InterlockedIncrement(&g_Bc250SdmaPresent.NextFenceSequence);
    Bc250SdmaEmitFence(packets, 128u, &cursor,
        g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_FENCE_OFFSET,
        nextFence);

    startCursor = g_Bc250SdmaPresent.CurrentCursor;
    if (startCursor + cursor >= BC250_SDMA_PRESENT_RING_DWORDS) {
        startCursor = 0u;
    }

    for (i = 0u; i < cursor; ++i) {
        g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
    }
    KeMemoryBarrier();

    g_Bc250SdmaPresent.CurrentCursor = startCursor + cursor;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
    KeMemoryBarrier();

    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 1u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START,
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);

    for (poll = 1u; poll <= 10000u; ++poll) {
        uint32_t fenceObserved = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
        if (fenceObserved == nextFence) {
            return TRUE;
        }
        KeStallExecutionProcessor(5u);
    }
    return FALSE;
#else
    UNREFERENCED_PARAMETER(DestGpuAddress);
    UNREFERENCED_PARAMETER(ByteCount);
    UNREFERENCED_PARAMETER(FillPattern);
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
            BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
            rbCntl & ~F_RB_ENABLE);
        MmUnmapIoSpace((PVOID)g_Bc250SdmaPresent.MmioVirtual, g_Bc250SdmaPresent.MmioLength);
        g_Bc250SdmaPresent.MmioVirtual = NULL;
    }

    if (g_Bc250SdmaPresent.VramVirtual != NULL) {
        MmUnmapIoSpace((PVOID)g_Bc250SdmaPresent.VramVirtual, g_Bc250SdmaPresent.VramLength);
        g_Bc250SdmaPresent.VramVirtual = NULL;
    }

    g_Bc250SdmaPresent.Initialized = FALSE;
    Bc250DisplayLog("[SdmaPresent] Teardown Complete. TripodValidated=%d, Submitted=%ld, Completed=%ld, Fallbacks=%ld\n",
        g_Bc250SdmaPresent.TripodValidated ? 1 : 0,
        g_Bc250SdmaPresent.BltSubmittedCount,
        g_Bc250SdmaPresent.BltCompletedCount,
        g_Bc250SdmaPresent.FallbackCpuCount);
#else
    g_Bc250SdmaPresent.Initialized = FALSE;
    g_Bc250SdmaPresent.HardwareEnabled = FALSE;
#endif
}
