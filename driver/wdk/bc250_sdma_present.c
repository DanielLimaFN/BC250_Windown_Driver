#include "bc250_sdma_present.h"
#include "bc250_probe_telemetry.h"
#include "bc250_display_ddi.h"
#include "../core/bc250_sdma_queue_state_contract.h"
#include "../core/bc250_segment_address_contract.h"
#include "../core/bc250_tdr_recovery_contract.h"
#include "../core/bc250_submit_command_contract.h"

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

/*
 * O RPTR do hardware NAO volta a zero na fronteira do anel -- ele continua
 * contando. O cursor que o driver mantem, sim, e modular.
 *
 * MEDIDO na corrida de 05/09 14:19, na 12a submissao do PLANO A DIRECT FILL:
 *
 *   TargetWPTR = 96   WPTR_Reg = 96   RPTR_Reg = 4192   RPTR_Wb = 4176
 *
 * O anel tem 1024 dwords = 4096 bytes, e 4192 mod 4096 = 96 -- exatamente o
 * alvo. Os ponteiros estavam IGUAIS. A comparacao crua declarou divergencia, o
 * fail-closed disparou, e o driver devolveu erro ao VidMm -- que produz
 * BugCheck 0x119 e derruba a maquina.
 *
 * As 11 submissoes anteriores passaram so porque ainda nao tinham cruzado a
 * volta do anel. As pernas 2 (fence) e 3 (dados) deram MATCH em 17 de 17: o
 * preenchimento executou de verdade no silicio, e o que reprovou foi
 * aritmetica faltando aqui.
 */
static BOOLEAN Bc250SdmaRptrMatches(
    _In_ uint32_t RptrReg,
    _In_ uint32_t RptrWb,
    _In_ uint32_t TargetWptr)
{
    uint32_t target = TargetWptr % BC250_SDMA_PRESENT_RING_BYTES;

    return ((RptrReg % BC250_SDMA_PRESENT_RING_BYTES) == target ||
            (RptrWb % BC250_SDMA_PRESENT_RING_BYTES) == target) ? TRUE : FALSE;
}

/* Teto e amostragem do tripe -- ver o laco de espera em ExecuteIndirect. */
#define BC250_SDMA_TRIPOD_MAX_POLLS      200000u
#define BC250_SDMA_TRIPOD_SAMPLES        8u
#define BC250_SDMA_TRIPOD_SAMPLE_STRIDE  25000u

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

VOID Bc250SdmaPresentSetSegmentGeometry(
    _In_ uint64_t LocalSegmentBaseOffset,
    _In_ uint64_t LocalSegmentBytes)
{
    /*
     * Caps ainda nao resolvidas chegam zeradas; nesse caso NAO publicamos, para
     * nao apagar uma geometria valida com "nao sei".
     */
    if (LocalSegmentBytes == 0ull) {
        return;
    }
    if (g_Bc250SdmaPresent.LocalSegmentBaseOffset == LocalSegmentBaseOffset &&
        g_Bc250SdmaPresent.LocalSegmentBytes == LocalSegmentBytes &&
        InterlockedCompareExchange(&g_Bc250SdmaPresent.SegmentGeometryReady, 1, 1) == 1) {
        return;
    }
    g_Bc250SdmaPresent.LocalSegmentBaseOffset = LocalSegmentBaseOffset;
    g_Bc250SdmaPresent.LocalSegmentBytes = LocalSegmentBytes;
    InterlockedExchange(&g_Bc250SdmaPresent.SegmentGeometryReady, 1);
    Bc250DisplayLog("[SdmaPresent] SEGMENT_GEOMETRY publicada: SegBase=0x%llX SegBytes=0x%llX\n",
        (unsigned long long)LocalSegmentBaseOffset,
        (unsigned long long)LocalSegmentBytes);
}

BOOLEAN Bc250SdmaPresentSegmentOffsetToVram(
    _In_ UINT32 SegmentId,
    _In_ UINT64 SegmentOffset,
    _In_ UINT64 SizeBytes,
    _Out_ UINT64 *VramOffset)
{
    BC250_SEGMENT_ADDRESS_STATUS status;
    uint64_t resolved = 0ull;

    if (VramOffset == NULL) {
        return FALSE;
    }
    *VramOffset = 0ull;

    if (InterlockedCompareExchange(&g_Bc250SdmaPresent.SegmentGeometryReady, 1, 1) != 1) {
        /*
         * Geometria desconhecida. Supor offset cru aqui e' o defeito que
         * produziu o BugCheck 0x119 da corrida 13:11 -- falha fechada.
         */
        return FALSE;
    }

    status = Bc250SegmentOffsetToVramOffset(
        g_Bc250SdmaPresent.LocalSegmentBaseOffset,
        g_Bc250SdmaPresent.LocalSegmentBytes,
        SegmentId, SegmentOffset, SizeBytes, &resolved);
    if (status != BC250_SEGMENT_ADDRESS_OK) {
        return FALSE;
    }

    *VramOffset = resolved;
    return TRUE;
}

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
    KeInitializeSpinLock(&g_Bc250SdmaPresent.RingLock);

    if (MmioBar5Physical.QuadPart == 0 || VramBar0Physical.QuadPart == 0) {
        Bc250DisplayLog("[SdmaPresent] Error: BAR5 or BAR0 base address is zero\n");
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Mapeia a janela 0x03000..0x08FFF do BAR5 (24 KB). Precisa comecar em
     * 0x03000 pelos registradores HDP e chegar a 0x08020 para que o pulso em
     * GRBM_SOFT_RESET.SDMA0 seja uma escrita real, nao rejeitada pelo limite
     * de Bc250SdmaRegWrite. Ver proveniencia em bc250_sdma_present.h.
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

    /* Des-gating de clock ANTES de qualquer configuracao (bc250_sdma_ring_work.c:323-330) */
    clk = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_MMIO_WINDOW_START, clk | 0xFF000000u);
    KeStallExecutionProcessor(50u);

    /* Desativa polling e anel antes de mexer em qualquer ponteiro */
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_WPTR_POLL_CNTL - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_SEM_TIMER - BC250_SDMA0_MMIO_WINDOW_START, 0u);

    /*
     * Soft Reset do bloco SDMA0 via GRBM_SOFT_RESET (Lei 6, provado no lab em 2026-08-23).
     * Se o sistema sofreu um BugCheck ou reboot a quente, a engine SDMA0 pode estar
     * com ERR_LOG latchado (0x0000000F) ou decodificador de comandos travado. O pulso
     * de soft-reset no bit 23 do GRBM_SOFT_RESET restaura a engine ao estado inicial limpo.
     */
    {
        uint32_t f32 = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_F32_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t grbmBefore = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_GRBM_REG_SOFT_RESET - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t errBefore = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_ERROR_LOG - BC250_SDMA0_MMIO_WINDOW_START);

        /* 1. Halt no F32 e desliga UTC_L1 como no stop_queue do upstream */
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_F32_CNTL - BC250_SDMA0_MMIO_WINDOW_START, f32 | F_F32_HALT);
        value = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CNTL - BC250_SDMA0_MMIO_WINDOW_START, value & ~F_CNTL_UTC_L1);

        /* 2. Pulso de soft-reset de 50 µs em GRBM_SOFT_RESET.SOFT_RESET_SDMA0 */
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_GRBM_REG_SOFT_RESET - BC250_SDMA0_MMIO_WINDOW_START,
            grbmBefore | BC250_GRBM_SOFT_RESET_SDMA0);
        KeStallExecutionProcessor(50u);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_GRBM_REG_SOFT_RESET - BC250_SDMA0_MMIO_WINDOW_START,
            grbmBefore & ~BC250_GRBM_SOFT_RESET_SDMA0);
        KeStallExecutionProcessor(1000u); /* 1 ms para o bloco assentar */

        /* 3. Un-halt F32 */
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_F32_CNTL - BC250_SDMA0_MMIO_WINDOW_START, f32 & ~F_F32_HALT);
        KeStallExecutionProcessor(50u);

        uint32_t grbmAfter = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_GRBM_REG_SOFT_RESET - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t errAfter = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_ERROR_LOG - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t stAfter = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
        Bc250DisplayLog("[SdmaPresent] SDMA0 Reset: GRBM 0x%08X -> 0x%08X, ERR_LOG 0x%08X -> 0x%08X, STATUS=0x%08X\n",
            grbmBefore, grbmAfter, errBefore, errAfter, stAfter);
    }

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
        value | F_CNTL_UTC_L1);

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

    /*
     * 7. Veredito das TRES pernas da Lei 2, pelo contrato de
     *    bc250_blt_proof_contract.h.
     *
     * A13 (07/09): antes desta correcao o gate abaixo era
     *
     *     if (TripodFenceObserved == FENCE_INIT + 1 && TripodMismatches == 0)
     *
     * isto e', a perna 1 (RPTR == WPTR) NAO entrava na decisao. Foi assim que
     * a corrida 16:19 imprimiu "TRIPÉ NO SILÍCIO COMPROVADO ... WPTR=68
     * RPTR=52": o poll aceitava writeback OU registrador, o writeback estava
     * atrasado, e o log publicava o writeback ao lado da palavra COMPROVADO.
     * Agora a igualdade e' exigida, e o log diz QUAL das duas leituras casou.
     */
    g_Bc250SdmaPresent.TripodValidated = FALSE;
    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    {
        uint32_t targetWptr = (uint32_t)(cursor * sizeof(uint32_t));
        uint32_t rptrWriteback =
            g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
        uint32_t rptrRegister = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t fenceObserved =
            g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
        BC250_TRIPOD_RPTR_SOURCE rptrSource = BC250_TRIPOD_RPTR_SOURCE_NONE;
        BC250_TRIPOD_VERDICT tripodVerdict = Bc250ClassifySdmaTripod(
            targetWptr,
            rptrWriteback,
            rptrRegister,
            BC250_SDMA_PRESENT_FENCE_INIT + 1u,
            fenceObserved,
            g_Bc250SdmaPresent.TripodMismatches,
            &rptrSource);

        g_Bc250SdmaPresent.TripodVerdict = (uint32_t)tripodVerdict;
        g_Bc250SdmaPresent.TripodRptrSource = (uint32_t)rptrSource;
        g_Bc250SdmaPresent.TripodFenceObserved = fenceObserved;

        Bc250DisplayLog("[SdmaPresent] TRIPE_VEREDITO: %s (TargetWPTR=%u RPTR_Wb=%u RPTR_Reg=%u Casou=%s FenceExp=%08X FenceObs=%08X Poison=%08X Mismatches=%u Polls=%u)\n",
            Bc250TripodVerdictName(tripodVerdict),
            targetWptr,
            rptrWriteback,
            rptrRegister,
            Bc250TripodRptrSourceName(rptrSource),
            (uint32_t)(BC250_SDMA_PRESENT_FENCE_INIT + 1u),
            fenceObserved,
            (uint32_t)BC250_SDMA_PRESENT_FENCE_POISON,
            g_Bc250SdmaPresent.TripodMismatches,
            g_Bc250SdmaPresent.TripodPolls);

        if (Bc250TripodAllowsClaim(tripodVerdict) != 0u) {
            g_Bc250SdmaPresent.TripodValidated = TRUE;
            g_Bc250SdmaPresent.HardwareEnabled = TRUE;
            g_Bc250SdmaPresent.CurrentCursor = cursor;
            g_Bc250SdmaPresent.NextFenceSequence = BC250_SDMA_PRESENT_FENCE_INIT + 1u;
            Bc250DisplayLog("[SdmaPresent] TRIPÉ NO SILÍCIO COMPROVADO (Item 63)! WPTR=%u RPTR=%u (fonte %s) Fence=%08X Dados=4/4 OK em %u polls!\n",
                targetWptr,
                (rptrSource == BC250_TRIPOD_RPTR_SOURCE_REGISTER) ?
                    rptrRegister : rptrWriteback,
                Bc250TripodRptrSourceName(rptrSource),
                fenceObserved,
                g_Bc250SdmaPresent.TripodPolls);
        }
    }

    if (!g_Bc250SdmaPresent.TripodValidated) {
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

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
VOID Bc250SdmaPresentFlushHdp(void)
{
    if (g_Bc250SdmaPresent.Initialized && g_Bc250SdmaPresent.HardwareEnabled) {
        Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    }
}
#endif

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
/*
 * Bc250SdmaPresentAllocateRingSlot:
 * Garante fluxo continuo de leitura no anel de hardware do SDMA (Leis 2 e 4).
 * Se o pacote de tamanho RequiredDwords nao cabe antes do fim do anel (1024 DW),
 * o espaco restante ATE O FIM do anel e' obrigatoriamente preenchido com NOPs (0u),
 * e o CurrentCursor (contador cumulativo de 32 bits) e' avancado para alinhar no inicio
 * da proxima volta. Desta forma, o WPTR gravado na engine de hardware e' sempre
 * estritamente monotonico (nunca regride contra o RPTR do hardware) e o decodificador
 * consome NOPs limpos antes de fazer o wrap para o offset zero.
 */
static uint32_t Bc250SdmaPresentAllocateRingSlot(
    _In_ uint32_t RequiredDwords)
{
    uint32_t ringIndex = g_Bc250SdmaPresent.CurrentCursor % BC250_SDMA_PRESENT_RING_DWORDS;
    if (ringIndex + RequiredDwords > BC250_SDMA_PRESENT_RING_DWORDS) {
        uint32_t pad = BC250_SDMA_PRESENT_RING_DWORDS - ringIndex;
        uint32_t i;
        for (i = 0u; i < pad; ++i) {
            g_Bc250SdmaPresent.Ring[ringIndex + i] = BC250_SDMA_OP_NOP;
        }
        g_Bc250SdmaPresent.CurrentCursor += pad;
        ringIndex = 0u;
    }
    return ringIndex;
}
#endif

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
    KIRQL oldIrql;

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

    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);

    /* Cada submissao precisa substituir poison; fence anterior nao e' prova. */
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u] =
        BC250_SDMA_PRESENT_FENCE_POISON;
    KeMemoryBarrier();

    startCursor = Bc250SdmaPresentAllocateRingSlot(cursor);

    for (i = 0u; i < cursor; ++i) {
        g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
    }
    KeMemoryBarrier();

    g_Bc250SdmaPresent.CurrentCursor += cursor;
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
        if (fenceObserved != nextFence) {
            uint32_t rptrRegCheck = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
            uint32_t rptrWbCheck = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
            uint32_t targetWptrCheck = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
            if (Bc250SdmaRptrMatches(rptrRegCheck, rptrWbCheck, targetWptrCheck) || ((poll & 0x7Fu) == 0u)) {
                Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
                fenceObserved = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
            }
        }
        if (fenceObserved == nextFence) {
            uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
            uint32_t rptrWb = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
            uint32_t targetWptr = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
            /* A15: o helper cobre a volta do anel; o teste cru era redundante
             * e servia de molde para copiar o defeito adiante. */
            BOOLEAN rptrOk = Bc250SdmaRptrMatches(rptrReg, rptrWb, targetWptr);
            if (rptrOk) {
                LONG completed;
                g_Bc250SdmaPresent.TripodValidated = TRUE;
                completed = InterlockedIncrement(&g_Bc250SdmaPresent.BltCompletedCount);
                KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
                Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
                if (completed == 1 || (completed % 60) == 0) {
                    /*
                     * A10: a linha so' se chama TRIPE quando as TRES pernas da
                     * Lei 2 estao provadas. Aqui provamos duas -- RPTR == WPTR e
                     * fence da GPU sobre poison. A terceira (dados conferidos)
                     * vem de Bc250DisplayCompareSurfaceBytes e chega por
                     * BltDataProofState. Antes disso, dizer "tripe" seria
                     * afirmar tres pernas tendo duas.
                     */
                    LONG dataProof = InterlockedCompareExchange(
                        &g_Bc250SdmaPresent.BltDataProofState, 0, 0);
                    if (dataProof == BC250_BLT_DATA_PROOF_OK) {
                        Bc250DisplayLog("[SdmaPresent] BLT_TRIPOD PASS #%ld (3 pernas): WPTR=%u RPTR=%u Fence=%08X Poison=%08X Bytes=%u\n",
                            completed, targetWptr, rptrReg, fenceObserved,
                            BC250_SDMA_PRESENT_FENCE_POISON, ByteCount);
                    } else {
                        Bc250DisplayLog("[SdmaPresent] BLT_ENGINE_2LEGS #%ld (RPTR==WPTR + fence sobre poison; PERNA DE DADOS %s): WPTR=%u RPTR=%u Fence=%08X Poison=%08X Bytes=%u\n",
                            completed,
                            (dataProof == BC250_BLT_DATA_PROOF_BLANK) ?
                                "INCONCLUSIVA (frame em branco)" : "NAO CONFERIDA",
                            targetWptr, rptrReg, fenceObserved,
                            BC250_SDMA_PRESENT_FENCE_POISON, ByteCount);
                    }
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
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
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

BOOLEAN Bc250SdmaPresentBltRect(
    _In_ uint64_t SourceGpuAddress,
    _In_ ULONG SourcePitchBytes,
    _In_ uint64_t DestGpuAddress,
    _In_ ULONG DestPitchBytes,
    _In_ ULONG DestX,
    _In_ ULONG DestY,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    uint32_t packets[128] = { 0u };
    uint32_t cursor = 0u;
    uint32_t nextFence;
    uint32_t startCursor;
    uint32_t i;
    ULONG poll;
    uint64_t byteCount64 = (uint64_t)Width * Height * 4ull;
    BC250_SDMA_STATUS status;
    KIRQL oldIrql;

    if (!g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled ||
        SourceGpuAddress == 0ull || DestGpuAddress == 0ull ||
        byteCount64 == 0ull || byteCount64 > (64ull * 1024ull * 1024ull)) {
        InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
        return FALSE;
    }
    status = Bc250SdmaEmitCopyLinearSubWindow32(
        packets, 128u, &cursor, SourceGpuAddress, SourcePitchBytes,
        DestGpuAddress, DestPitchBytes, DestX, DestY, Width, Height);
    if (status != BC250_SDMA_OK ||
        Bc250SdmaEmitGcr(packets, 128u, &cursor) != BC250_SDMA_OK) {
        InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
        return FALSE;
    }
    nextFence = (uint32_t)InterlockedIncrement(
        &g_Bc250SdmaPresent.NextFenceSequence);
    if (Bc250SdmaEmitFence(packets, 128u, &cursor,
            g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_FENCE_OFFSET,
            nextFence) != BC250_SDMA_OK) {
        InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
        return FALSE;
    }

    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u] =
        BC250_SDMA_PRESENT_FENCE_POISON;
    KeMemoryBarrier();
    startCursor = Bc250SdmaPresentAllocateRingSlot(cursor);
    for (i = 0u; i < cursor; ++i) {
        g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
    }
    KeMemoryBarrier();
    g_Bc250SdmaPresent.CurrentCursor += cursor;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
    KeMemoryBarrier();

    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 1u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START,
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    InterlockedIncrement(&g_Bc250SdmaPresent.BltSubmittedCount);

    for (poll = 1u; poll <= 10000u; ++poll) {
        uint32_t fenceObserved =
            g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
        if (fenceObserved != nextFence) {
            uint32_t rptrRegCheck = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
                BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
            uint32_t rptrWbCheck =
                g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
            uint32_t targetWptrCheck =
                g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
            if (Bc250SdmaRptrMatches(rptrRegCheck, rptrWbCheck,
                    targetWptrCheck) || ((poll & 0x7Fu) == 0u)) {
                Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
                fenceObserved = g_Bc250SdmaPresent.Work[
                    BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
            }
        }
        if (fenceObserved == nextFence) {
            uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
                BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
            uint32_t rptrWb =
                g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
            uint32_t targetWptr =
                g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
            if (Bc250SdmaRptrMatches(rptrReg, rptrWb, targetWptr)) {
                LONG completed;
                g_Bc250SdmaPresent.TripodValidated = TRUE;
                completed = InterlockedIncrement(
                    &g_Bc250SdmaPresent.BltCompletedCount);
                KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
                Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
                if (completed == 1 || (completed % 60) == 0) {
                    Bc250DisplayLog("[SdmaPresent] BLT_RECT_ENGINE_2LEGS #%ld: %ux%u srcPitch=%u dstPitch=%u DstXY=%u,%u WPTR=%u RPTR=%u Fence=%08X Bytes=%llu SEMANTICA_H\n",
                        completed, Width, Height, SourcePitchBytes,
                        DestPitchBytes, DestX, DestY, targetWptr, rptrReg,
                        fenceObserved, (unsigned long long)byteCount64);
                }
                return TRUE;
            }
        }
        KeStallExecutionProcessor(5u);
    }

    {
        uint32_t wptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t statusReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t fenceObs = g_Bc250SdmaPresent.Work[
            BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
        LONG fallbacks = InterlockedIncrement(
            &g_Bc250SdmaPresent.FallbackCpuCount);
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        Bc250DisplayLog("[SdmaPresent] BLT_RECT TIMEOUT #%ld: %ux%u srcPitch=%u dstPitch=%u DstXY=%u,%u TargetWPTR=%u WPTR_Reg=%u RPTR_Reg=%u STATUS=%08X FenceExp=%08X FenceObs=%08X\n",
            fallbacks, Width, Height, SourcePitchBytes, DestPitchBytes,
            DestX, DestY,
            g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t), wptrReg,
            rptrReg, statusReg, nextFence, fenceObs);
    }
    return FALSE;
#else
    UNREFERENCED_PARAMETER(SourceGpuAddress);
    UNREFERENCED_PARAMETER(SourcePitchBytes);
    UNREFERENCED_PARAMETER(DestGpuAddress);
    UNREFERENCED_PARAMETER(DestPitchBytes);
    UNREFERENCED_PARAMETER(DestX);
    UNREFERENCED_PARAMETER(DestY);
    UNREFERENCED_PARAMETER(Width);
    UNREFERENCED_PARAMETER(Height);
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
    KIRQL oldIrql;
    uint64_t vramOffset = 0ull;
    BOOLEAN canCheckVram = FALSE;
    ULONG canaryOff[5];
    uint32_t canaryVal[5];
    UINT32 cIdx;

    if (!g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled) {
        Bc250DisplayLog("[SdmaPresent] PAGING_FILL RECUSADO: Initialized=%u, HardwareEnabled=%u, TripodValidated=%u\n",
            g_Bc250SdmaPresent.Initialized ? 1u : 0u,
            g_Bc250SdmaPresent.HardwareEnabled ? 1u : 0u,
            g_Bc250SdmaPresent.TripodValidated ? 1u : 0u);
        return FALSE;
    }
    if (DestGpuAddress == 0 || ByteCount == 0 ||
        ByteCount > (64u * 1024u * 1024u)) {
        Bc250DisplayLog("[SdmaPresent] PAGING_FILL RECUSADO: Dest=0x%llX, Bytes=%u (limite 64MB)\n",
            (unsigned long long)DestGpuAddress, ByteCount);
        return FALSE;
    }

    /*
     * Tripé no Silício: Prova Física de Escrita na VRAM (Anti-Falso-Positivo).
     * Se o buffer de destino estiver na abertura de framebuffer mapeada pela CPU,
     * plantamos 5 canaries distintos ao longo de todo o buffer ANTES do disparo da SDMA.
     * Após o retiro da fence, conferimos se a GPU física sobrescreveu todos eles com FillPattern.
     *
     * A14 (07/09): o layout dos canaries saiu daqui e passou a vir de
     * Bc250PlanDataProofCanaries, em fonte unica com o caminho INDIRECT, que
     * nao tinha perna 3 nenhuma. A formula dos cinco pontos e dos valores e' a
     * MESMA que rodava aqui; o plano so' acrescenta duas recusas que faltavam:
     * destino menor que 4 bytes (os canaries de 4 bytes escreveriam fora da
     * regiao) e canary igual ao padrao esperado (a prova ficaria vazia).
     */
    {
        BC250_DATA_PROOF_PLAN plan;
        if (Bc250PlanDataProofCanaries(DestGpuAddress, ByteCount, FillPattern,
                g_Bc250SdmaPresent.VramMcStart,
                (uint64_t)g_Bc250DisplayState.FrameBufferSize,
                (g_Bc250DisplayState.FrameBufferVirtual != NULL) ? 1u : 0u,
                &plan) != 0u) {
            canCheckVram = TRUE;
            vramOffset = plan.VramOffset;
            for (cIdx = 0u; cIdx < 5u; ++cIdx) {
                volatile uint32_t *pCanary;
                canaryOff[cIdx] = plan.Offsets[cIdx];
                canaryVal[cIdx] = plan.Values[cIdx];
                pCanary = (volatile uint32_t *)((char *)g_Bc250DisplayState.FrameBufferVirtual + vramOffset + canaryOff[cIdx]);
                *pCanary = canaryVal[cIdx];
            }
            KeMemoryBarrier();
            Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
        } else {
            for (cIdx = 0u; cIdx < 5u; ++cIdx) {
                canaryOff[cIdx] = 0u;
                canaryVal[cIdx] = 0u;
            }
        }
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

    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);

    startCursor = Bc250SdmaPresentAllocateRingSlot(cursor);

    for (i = 0u; i < cursor; ++i) {
        g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
    }
    KeMemoryBarrier();

    g_Bc250SdmaPresent.CurrentCursor += cursor;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u] =
        BC250_SDMA_PRESENT_FENCE_POISON;
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
            uint32_t wptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START);
            uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
            uint32_t rptrWb = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
            uint32_t statusReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
            uint32_t post[5];
            BOOLEAN canaryOk = TRUE;
            ULONG nonZeroCount = 0u;
            ULONG sampleHash = 0u;
            uint32_t targetWptr = g_Bc250SdmaPresent.CurrentCursor * (uint32_t)sizeof(uint32_t);
            BOOLEAN rptrOk = Bc250SdmaRptrMatches(rptrReg, rptrWb, targetWptr);

            /* Aguarda até 500 µs se o RPTR de writeback/reg ainda estiver em trânsito */
            if (!rptrOk) {
                UINT32 rPoll;
                for (rPoll = 0u; rPoll < 100u && !rptrOk; ++rPoll) {
                    KeStallExecutionProcessor(5u);
                    rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
                    rptrWb = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
                    rptrOk = Bc250SdmaRptrMatches(rptrReg, rptrWb, targetWptr);
                }
            }

            RtlZeroMemory(post, sizeof(post));
            if (canCheckVram) {
                Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
                for (cIdx = 0u; cIdx < 5u; ++cIdx) {
                    volatile uint32_t *pCanary = (volatile uint32_t *)((char *)g_Bc250DisplayState.FrameBufferVirtual + vramOffset + canaryOff[cIdx]);
                    post[cIdx] = *pCanary;
                    if (post[cIdx] != FillPattern) {
                        canaryOk = FALSE;
                    }
                }
                (void)Bc250DisplaySampleSurface(vramOffset, ByteCount, 16u, &nonZeroCount, &sampleHash);
            }

            KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);

            Bc250DisplayLog("[SdmaPresent] PAGING_FILL TRIPÉ %s: Dest=0x%llX (Off=0x%llX), Size=%u, Pattern=0x%08X in %u polls\n",
                (rptrOk && canaryOk) ? "PROVADO NO SILÍCIO" : "FALHA DE VALIDAÇÃO",
                (unsigned long long)DestGpuAddress, (unsigned long long)vramOffset, ByteCount, FillPattern, poll);
            Bc250DisplayLog("[SdmaPresent]   [TRIPÉ 1/3 - RPTR] TargetWPTR=%u, WPTR_Reg=%u, RPTR_Reg=%u, RPTR_Wb=%u, STATUS=0x%08X -> %s\n",
                targetWptr, wptrReg, rptrReg, rptrWb, statusReg, rptrOk ? "MATCH" : "DIVERGE");
            Bc250DisplayLog("[SdmaPresent]   [TRIPÉ 2/3 - FENCE] Exp=0x%08X, Obs=0x%08X (Poison 0x43DAFFFE substituido pela GPU) -> MATCH\n",
                nextFence, fenceObserved);
            if (canCheckVram) {
                Bc250DisplayLog("[SdmaPresent]   [TRIPÉ 3/3 - DADOS] 5 Canaries -> PostGPU=[0x%08X, 0x%08X, 0x%08X, 0x%08X, 0x%08X] -> %s (16 pts: NonZero=%u, Hash=0x%08X)\n",
                    post[0], post[1], post[2], post[3], post[4],
                    canaryOk ? "MATCH TOTAL (GPU SOBRESCREVEU)" : "FALHA: DADOS INTACTOS",
                    nonZeroCount, sampleHash);
            } else {
                Bc250DisplayLog("[SdmaPresent]   [TRIPÉ 3/3 - DADOS] Destino fora do FB mapeado (Off=0x%llX)\n",
                    (unsigned long long)vramOffset);
            }

            if (!rptrOk) {
                Bc250DisplayLog("[SdmaPresent] RECUSA TRIPÉ: RPTR divergiu de WPTR! Fail-closed acionado!\n");
                return FALSE;
            }

            if (canCheckVram && !canaryOk) {
                Bc250DisplayLog("[SdmaPresent] RECUSA TRIPÉ: Fence retirou mas dados na VRAM nao mudaram! Fail-closed acionado!\n");
                return FALSE;
            }

            if (canCheckVram && FillPattern == 0u && nonZeroCount != 0u) {
                Bc250DisplayLog("[SdmaPresent] RECUSA TRIPÉ: FillPattern=0 mas 16 amostras encontraram NonZero=%u! Fail-closed acionado!\n", nonZeroCount);
                return FALSE;
            }

            return TRUE;
        }
        KeStallExecutionProcessor(5u);
    }
    {
        uint32_t wptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t statusReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t fenceObserved = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        Bc250DisplayLog("[SdmaPresent] PAGING_FILL: Dest=0x%llX, Size=%u, Pattern=0x%08X -> TIMEOUT (FenceExp=0x%08X, FenceObs=0x%08X, WPTR=%u, RPTR=%u, STATUS=0x%08X)\n",
            (unsigned long long)DestGpuAddress, ByteCount, FillPattern, nextFence,
            fenceObserved, wptrReg, rptrReg, statusReg);
    }
    return FALSE;
#else
    UNREFERENCED_PARAMETER(DestGpuAddress);
    UNREFERENCED_PARAMETER(ByteCount);
    UNREFERENCED_PARAMETER(FillPattern);
    return TRUE;
#endif
}

BOOLEAN Bc250SdmaPresentSyncNop(void)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    uint32_t packets[64] = { 0 };
    uint32_t cursor = 0u;
    uint32_t nextFence;
    uint32_t startCursor;
    uint32_t i;
    ULONG poll;
    KIRQL oldIrql;

    if (!g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled ||
        g_Bc250SdmaPresent.Ring == NULL) {
        return FALSE;
    }

    nextFence = (uint32_t)InterlockedIncrement(&g_Bc250SdmaPresent.NextFenceSequence);
    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);

    startCursor = Bc250SdmaPresentAllocateRingSlot(16u);

    /* 1. Emite NOP, GCR Flush e Fence física na VRAM */
    packets[cursor++] = BC250_SDMA_OP_NOP;
    Bc250SdmaEmitGcr(packets, 64u, &cursor);
    Bc250SdmaEmitFence(packets, 64u, &cursor,
        g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_FENCE_OFFSET,
        nextFence);

    /* 2. Envenena a fence antes de disparar */
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u] =
        BC250_SDMA_PRESENT_FENCE_POISON;
    KeMemoryBarrier();

    /* 3. Copia para o anel */
    for (i = 0u; i < cursor; ++i) {
        g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
    }
    KeMemoryBarrier();

    g_Bc250SdmaPresent.CurrentCursor += cursor;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
    KeMemoryBarrier();

    /* 4. Atualiza WPTR na engine SDMA0 */
    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 1u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START,
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);

    InterlockedIncrement(&g_Bc250SdmaPresent.SubmissionsSubmittedCount);

    /* 5. Tripé no silício: espera RPTR == WPTR e Fence == nextFence */
    for (poll = 1u; poll <= BC250_SDMA_TRIPOD_MAX_POLLS; ++poll) {
        uint32_t fenceObserved = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
        if (fenceObserved == nextFence) {
            uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
                BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
            uint32_t rptrWb = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
            uint32_t targetWptr = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
            if (Bc250SdmaRptrMatches(rptrReg, rptrWb, targetWptr)) {
                InterlockedIncrement(&g_Bc250SdmaPresent.SubmissionsCompletedCount);
                KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
                Bc250DisplayLog("[SdmaPresent] SYNC_NOP TRIPOD PASS: WPTR=%u RPTR=%u Fence=%08X in %u polls\n",
                    targetWptr, rptrReg, fenceObserved, poll);
                return TRUE;
            }
        }
        KeStallExecutionProcessor(5u);
    }

    {
        uint32_t wptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t statusReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t fenceObserved = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        Bc250DisplayLog("[SdmaPresent] SYNC_NOP TIMEOUT: WPTR=%u RPTR=%u FenceExp=0x%08X FenceObs=0x%08X STATUS=0x%08X\n",
            wptrReg, rptrReg, nextFence, fenceObserved, statusReg);
    }
    return FALSE;
#else
    return TRUE;
#endif
}

/*
 * Fotografia dos registradores de fault da UTCL1/SDMA0.
 *
 * A corrida de 04/09 22:27 mostrou por que ela precisa ser tirada ANTES do
 * disparo: o pos-fault trouxe RD_XNACK=00000200/00000180 e
 * WR_XNACK=0045FE00/00000100, e sem valor de referencia nao ha como dizer se
 * aqueles bits foram levantados pela IB ou se ja estavam ali desde o boot da
 * engine. Um delta e interpretavel; um valor absoluto nao e.
 */
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
typedef struct _BC250_SDMA_UTCL1_SNAPSHOT {
    uint32_t RdStatus;
    uint32_t WrStatus;
    uint32_t RdXnack0;
    uint32_t RdXnack1;
    uint32_t WrXnack0;
    uint32_t WrXnack1;
    uint32_t Timeout;
    uint32_t ErrorLog;
    uint32_t Status1;
    uint32_t Freeze;
    uint32_t Status3;
} BC250_SDMA_UTCL1_SNAPSHOT;

static void Bc250SdmaCaptureUtcl1Snapshot(
    _Out_ BC250_SDMA_UTCL1_SNAPSHOT *Snapshot)
{
    Snapshot->RdStatus = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_UTCL1_RD_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
    Snapshot->WrStatus = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_UTCL1_WR_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
    Snapshot->RdXnack0 = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_UTCL1_RD_XNACK0 - BC250_SDMA0_MMIO_WINDOW_START);
    Snapshot->RdXnack1 = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_UTCL1_RD_XNACK1 - BC250_SDMA0_MMIO_WINDOW_START);
    Snapshot->WrXnack0 = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_UTCL1_WR_XNACK0 - BC250_SDMA0_MMIO_WINDOW_START);
    Snapshot->WrXnack1 = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_UTCL1_WR_XNACK1 - BC250_SDMA0_MMIO_WINDOW_START);
    Snapshot->Timeout = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_UTCL1_TIMEOUT - BC250_SDMA0_MMIO_WINDOW_START);
    Snapshot->ErrorLog = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_ERROR_LOG - BC250_SDMA0_MMIO_WINDOW_START);
    Snapshot->Status1 = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_STATUS1 - BC250_SDMA0_MMIO_WINDOW_START);
    Snapshot->Freeze = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_FREEZE - BC250_SDMA0_MMIO_WINDOW_START);
    Snapshot->Status3 = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_STATUS3 - BC250_SDMA0_MMIO_WINDOW_START);
}
#endif /* BC250_ENABLE_DXGK_REGISTRATION */

BOOLEAN Bc250SdmaPresentExecuteIndirect(
    _In_ uint64_t DmaBufferGpuVa,
    _In_ ULONG DmaBufferSize,
    _In_ uint32_t IbSizeDwords,
    _In_ uint32_t Vmid,
    _In_ uint64_t ExpectedEffectDestMc,
    _In_ ULONG ExpectedEffectBytes,
    _In_ uint32_t ExpectedEffectPattern)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    BC250_DATA_PROOF_PLAN dataPlan;
    uint32_t packets[64] = { 0 };
    uint32_t cursor = 0u;
    uint32_t nextFence;
    uint32_t startCursor;
    uint32_t i;
    ULONG poll;
    uint32_t ibSizeDwords;
    uint32_t padDwords;
    uint32_t targetWptrBytes;
    uint32_t indirectEndDwords;
    BC250_SDMA_UTCL1_SNAPSHOT baseline;
    uint32_t rptrSamples[BC250_SDMA_TRIPOD_SAMPLES] = { 0 };
    uint32_t rptrSampleCount = 0u;
    BC250_SDMA_STATUS status;
    KIRQL oldIrql;

    if (!g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled) {
        Bc250DisplayLog("[SdmaPresent] ExecuteIndirect recusado: Init=%d HwEnabled=%d TripodValidated=%d\n",
            g_Bc250SdmaPresent.Initialized ? 1 : 0,
            g_Bc250SdmaPresent.HardwareEnabled ? 1 : 0,
            g_Bc250SdmaPresent.TripodValidated ? 1 : 0);
        return FALSE;
    }
    if (DmaBufferGpuVa == 0 || DmaBufferSize == 0 ||
        (DmaBufferGpuVa & (uint64_t)(BC250_SDMA_IB_ALIGNMENT_BYTES - 1u)) != 0 ||
        (DmaBufferSize & 3u) != 0 || Vmid > 15u) {
        Bc250DisplayLog("[SdmaPresent] ExecuteIndirect invalid params: VA=0x%llX, Size=%u, Vmid=%u\n",
            (unsigned long long)DmaBufferGpuVa, DmaBufferSize, Vmid);
        return FALSE;
    }

    /*
     * IB_SIZE = dwords que a engine vai buscar. O default continua sendo o
     * tamanho publicado ao VidMm; quando o encoder informa quantos dwords sao
     * uteis, os NOPs de cauda ficam de fora do fetch. Ver bc250_sdma_present.h.
     */
    ibSizeDwords = DmaBufferSize / sizeof(uint32_t);
    if (IbSizeDwords != 0u && IbSizeDwords <= ibSizeDwords) {
        ibSizeDwords = IbSizeDwords;
    }

    /*
     * A14: perna 3 do tripe para o IB REAL. O que o IB deve produzir vem
     * declarado por quem submete (destino, tamanho e padrao esperados). Se o
     * destino cair na abertura mapeada pela CPU, plantamos canaries nao-nulos
     * ANTES do disparo -- e' o que torna conclusivo um fill de padrao ZERO:
     * zero sobre poison prova escrita, zero sobre zero nao prova nada.
     *
     * Plano nao utilizavel nao e' falha: a perna 3 fica NAO CONFERIDA e o log
     * perde o direito a palavra TRIPE.
     */
    (void)Bc250PlanDataProofCanaries(ExpectedEffectDestMc, ExpectedEffectBytes,
        ExpectedEffectPattern, g_Bc250SdmaPresent.VramMcStart,
        (uint64_t)g_Bc250DisplayState.FrameBufferSize,
        (g_Bc250DisplayState.FrameBufferVirtual != NULL) ? 1u : 0u,
        &dataPlan);

    nextFence = (uint32_t)InterlockedIncrement(&g_Bc250SdmaPresent.NextFenceSequence);
    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);

    if (dataPlan.Usable != 0u) {
        uint32_t planIndex;
        for (planIndex = 0u; planIndex < dataPlan.CanaryCount; ++planIndex) {
            volatile uint32_t *pCanary = (volatile uint32_t *)
                ((char *)g_Bc250DisplayState.FrameBufferVirtual +
                 dataPlan.VramOffset + dataPlan.Offsets[planIndex]);
            *pCanary = dataPlan.Values[planIndex];
        }
        KeMemoryBarrier();
        Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    }

    Bc250SdmaCaptureUtcl1Snapshot(&baseline);

    /*
     * O SDMA 5.0 de Cyan Skillfish exige que o pacote INDIRECT de 6 DW
     * TERMINE em fronteira de 8 DW. Na corrida de 22:00 o CurrentCursor era
     * 17 DW e emitimos o INDIRECT diretamente; ele terminou em 23, violando
     * a regra do upstream. Calculamos o padding somente depois de serializar
     * o anel, pois ele depende do WPTR real.
     */
    startCursor = Bc250SdmaPresentAllocateRingSlot(22u);
    padDwords = Bc250SdmaIndirectPrePadDwords(startCursor);
    cursor = 0u;
    for (i = 0u; i < padDwords; ++i) {
        packets[cursor++] = BC250_SDMA_OP_NOP;
    }

    /* 1. Emite INDIRECT BUFFER no anel com VMID especificado. */
    status = Bc250SdmaEmitIndirect(packets, 64u, &cursor,
        DmaBufferGpuVa, ibSizeDwords, 0ull /* CsaAddress */, Vmid);
    if (status != BC250_SDMA_OK) {
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        Bc250DisplayLog("[SdmaPresent] EmitIndirect failed: status=%u\n", (uint32_t)status);
        return FALSE;
    }

    /* 2. Emite GCR Cache Flush da GPU. */
    Bc250SdmaEmitGcr(packets, 64u, &cursor);

    /* 3. Emite Fence fisica na VRAM para afericao do tripe. */
    Bc250SdmaEmitFence(packets, 64u, &cursor,
        g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_FENCE_OFFSET,
        nextFence);

    /* 4. Envenena a fence em VRAM antes do disparo (Lei 2: substituição de poison) */
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u] =
        BC250_SDMA_PRESENT_FENCE_POISON;
    KeMemoryBarrier();

    /* 5. Grava os pacotes no anel */
    for (i = 0u; i < cursor; ++i) {
        g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
    }
    KeMemoryBarrier();

    g_Bc250SdmaPresent.CurrentCursor += cursor;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
    KeMemoryBarrier();

    /* 6. Atualiza WPTR no registrador da engine SDMA0 */
    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 1u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START,
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);

    InterlockedIncrement(&g_Bc250SdmaPresent.SubmissionsSubmittedCount);

    /*
     * NAO logar aqui. Bc250DisplayLog so escreve em disco em PASSIVE_LEVEL
     * (bc250_display_ddi.c) e estamos segurando RingLock, ou seja, em
     * DISPATCH_LEVEL. Na corrida de 04/09 22:27 o IB_ALIGN foi emitido neste
     * ponto e sumiu do arquivo, deixando a corrida cega justamente no dado que
     * descreve o layout do anel. Guardamos em locais e emitimos depois do
     * KeReleaseSpinLock, nos dois caminhos de saida.
     */
    indirectEndDwords = startCursor + padDwords + BC250_SDMA_DWORDS_INDIRECT;
    targetWptrBytes = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));

    /*
     * 7. Tripé no silício: espera RPTR == WPTR e Fence == nextFence.
     *
     * Teto alongado de 10.000 para 200.000 polls (50 ms -> ~1 s) e amostragem
     * periodica do RPTR. Na corrida de 05/09 01:12 o RPTR parou em 96 com a
     * traducao do IB ja resolvida, e 50 ms nao permitiam distinguir "a engine
     * travou" de "11 MiB de CONST_FILL nao cabem no meu teto". Com as amostras
     * a distincao fica no log: RPTR que avanca e progresso; RPTR cravado e
     * travamento.
     */
    for (poll = 1u; poll <= BC250_SDMA_TRIPOD_MAX_POLLS; ++poll) {
        uint32_t fenceObserved = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
        if ((poll % BC250_SDMA_TRIPOD_SAMPLE_STRIDE) == 0u &&
            rptrSampleCount < BC250_SDMA_TRIPOD_SAMPLES) {
            rptrSamples[rptrSampleCount++] = Bc250SdmaRegRead(
                &g_Bc250SdmaPresent,
                BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
        }
        if (fenceObserved != nextFence) {
            uint32_t rptrCheck = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
                BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
            uint32_t targetWptrCheck = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
            uint32_t rptrWbCheck =
                g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
            if (Bc250SdmaRptrMatches(rptrCheck, rptrWbCheck, targetWptrCheck) || ((poll & 0x7Fu) == 0u)) {
                Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
                fenceObserved = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
            }
        }
        if (fenceObserved == nextFence) {
            uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
                BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
            uint32_t targetWptr = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
            uint32_t rptrWbRaw =
                g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
            /*
             * A15 (07/09) -- VOLTA DO ANEL. Esta comparacao era CRUA:
             * `rptrReg == targetWptr`. Na corrida 17:43, submissao FenceId=24,
             * saiu
             *
             *   IB_TRIPOD TIMEOUT: TargetWPTR=516, WPTR=516, RPTR=4612,
             *                      FenceExp=00001030 FenceObs=00001030
             *
             * e 4612 = 4096 + 516 num anel de 4096 bytes. A GPU TERMINOU (a
             * fence bateu), mas o RPTR do registrador conta alem de uma volta e
             * a igualdade crua nunca fecharia. O timeout devolveu
             * STATUS_IO_DEVICE_ERROR ao VidMm -> BugCheck 0x119 (Arg1=2,
             * Arg2=0xC0000185).
             *
             * A aritmetica JA existia neste arquivo (Bc250SdmaRptrMatches, com
             * comentario descrevendo a mesma falha em 17/17 fills). Este
             * caminho simplesmente nao a usava.
             */
            if (Bc250SdmaRptrMatches(rptrReg, rptrWbRaw, targetWptr)) {
                LONG completed = InterlockedIncrement(&g_Bc250SdmaPresent.SubmissionsCompletedCount);
                uint32_t post[BC250_DATA_PROOF_CANARY_COUNT];
                uint32_t dataMismatches = BC250_DATA_PROOF_CANARY_COUNT;
                BC250_TRIPOD_VERDICT ibVerdict;
                BC250_TRIPOD_RPTR_SOURCE ibRptrSource = BC250_TRIPOD_RPTR_SOURCE_NONE;
                /* Valores JA reduzidos a uma volta do anel: e' o que a perna 1
                 * compara, e e' o que o log tem de mostrar. */
                uint32_t targetWrapped = Bc250TripodWrapRingPointer(
                    targetWptr, (uint32_t)BC250_SDMA_PRESENT_RING_BYTES);
                uint32_t rptrRegWrapped = Bc250TripodWrapRingPointer(
                    rptrReg, (uint32_t)BC250_SDMA_PRESENT_RING_BYTES);
                uint32_t rptrWb = Bc250TripodWrapRingPointer(
                    rptrWbRaw, (uint32_t)BC250_SDMA_PRESENT_RING_BYTES);

                RtlZeroMemory(post, sizeof(post));
                if (dataPlan.Usable != 0u) {
                    uint32_t planIndex;
                    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
                    for (planIndex = 0u; planIndex < dataPlan.CanaryCount; ++planIndex) {
                        volatile uint32_t *pCanary = (volatile uint32_t *)
                            ((char *)g_Bc250DisplayState.FrameBufferVirtual +
                             dataPlan.VramOffset + dataPlan.Offsets[planIndex]);
                        post[planIndex] = *pCanary;
                    }
                    dataMismatches = Bc250CountDataProofMismatches(&dataPlan, post,
                        ExpectedEffectPattern);
                }

                ibVerdict = Bc250ClassifySdmaTripod(targetWrapped, rptrWb,
                    rptrRegWrapped, nextFence, fenceObserved, dataMismatches,
                    &ibRptrSource);

                KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
                Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_ALIGN: StartDW=%u PadDW=%u IndirectEndDW=%u EndModulo8=%u TargetWPTR=%u IbSizeDW=%u PublishedDW=%u\n",
                    startCursor, padDwords, indirectEndDwords,
                    indirectEndDwords & 7u, targetWptrBytes, ibSizeDwords,
                    (uint32_t)(DmaBufferSize / sizeof(uint32_t)));
                /*
                 * A14 -- CORRECAO DE FALSO SUCESSO. Esta linha dizia
                 * um rotulo de tripe, mas o gate acima so' avalia DUAS pernas:
                 * RPTR==WPTR e fence sobre poison. Na corrida 17:13 ela saiu
                 * dez vezes sem que um unico byte do destino fosse conferido.
                 * A palavra TRIPE agora depende da perna 3 existir.
                 */
                if (dataPlan.Usable == 0u) {
                    Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_ENGINE_2LEGS #%ld (RPTR==WPTR + fence sobre poison; PERNA DE DADOS NAO CONFERIDA): WPTR=%u RPTR=%u Fence=%08X Poison=%08X Size=%u Vmid=%u EfeitoDeclarado=0x%llX/%u/0x%08X in %u polls\n",
                        completed, targetWrapped, rptrRegWrapped, fenceObserved,
                        BC250_SDMA_PRESENT_FENCE_POISON, DmaBufferSize, Vmid,
                        (unsigned long long)ExpectedEffectDestMc,
                        ExpectedEffectBytes, ExpectedEffectPattern, poll);
                } else {
                    Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_TRIPE_VEREDITO #%ld: %s (WPTR=%u RPTR_Reg=%u RPTR_Wb=%u Casou=%s Fence=%08X Poison=%08X Canaries=%u Divergentes=%u PostGPU=[%08X %08X %08X %08X %08X] Esperado=%08X Vmid=%u in %u polls)\n",
                        completed, Bc250TripodVerdictName(ibVerdict),
                        targetWrapped, rptrRegWrapped, rptrWb,
                        Bc250TripodRptrSourceName(ibRptrSource),
                        fenceObserved, BC250_SDMA_PRESENT_FENCE_POISON,
                        dataPlan.CanaryCount, dataMismatches,
                        post[0], post[1], post[2], post[3], post[4],
                        ExpectedEffectPattern, Vmid, poll);
                    if (Bc250TripodAllowsClaim(ibVerdict) != 0u) {
                        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_TRIPE PROVADO NO SILICIO #%ld: a GPU buscou o DMA buffer sob VMID%u e o efeito declarado esta nos bytes do destino\n",
                            completed, Vmid);
                    } else {
                        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB SEM TRIPE #%ld: fence e anel avancaram, mas %u de %u canaries ficaram INTACTOS -- o IB nao produziu o efeito declarado\n",
                            completed, dataMismatches, dataPlan.CanaryCount);
                    }
                }
                return TRUE;
            }
        }
        KeStallExecutionProcessor(5u);
    }

    /* Timeout / falha do tripé */
    {
        uint32_t wptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t statusReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
        BC250_SDMA_UTCL1_SNAPSHOT after;
        uint32_t fenceObs;
        uint32_t fenceAfterHdp;
        uint32_t rptrWriteback;
        BC250_SDMA_QUEUE_STATE queueState;
        BC250_SDMA_QUEUE_VERDICT queueVerdict;
        uint32_t stalledInsideIb;
        uint32_t ringDrained;
        uint32_t ibCntlReg;
        uint32_t ibRptrReg;
        uint32_t ibOffsetReg;
        uint32_t ibSizeReg;
        uint32_t skipCntlReg;
        uint32_t contextStatusReg;
        uint32_t ibBaseLoReg;
        uint32_t ibBaseHiReg;
        uint32_t ibSubRemainReg;

        Bc250SdmaCaptureUtcl1Snapshot(&after);

        /*
         * Bloco SDMA0_GFX_IB_* -- SOMENTE LEITURA, e a leitura acontece aqui
         * dentro do lock porque o log so pode sair depois do release.
         *
         * Estes nove registradores nao sao escritos por software: o microcodigo
         * do F32 e que os carrega ao consumir o INDIRECT. docs/168 arquivou as
         * leituras de 05/09 mas terminou dizendo que a semantica de
         * CONTEXT_STATUS e SKIP_CNTL nao havia sido interpretada. Ela esta em
         * inc/linux_ref/gc_10_1_0_sh_mask.h, e agora sai decodificada.
         */
        ibCntlReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_GFX_IB_CNTL_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        ibRptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_GFX_IB_RPTR_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        ibOffsetReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_GFX_IB_OFFSET_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        ibBaseLoReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_GFX_IB_BASE_LO_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        ibBaseHiReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_GFX_IB_BASE_HI_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        ibSizeReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_GFX_IB_SIZE_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        skipCntlReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_GFX_SKIP_CNTL_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        contextStatusReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_GFX_CONTEXT_STATUS_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        ibSubRemainReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_GFX_IB_SUB_REMAIN_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        fenceObs = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];

        /*
         * O laco de poll do boot tripod invalida o HDP a cada iteracao e aceita
         * o RPTR de writeback como alternativa ao registrador; este caminho nao
         * fazia nem um nem outro. Se a fence aparecer SO depois do invalidate,
         * o problema e visibilidade para a CPU, nao execucao na engine.
         */
        Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
        fenceAfterHdp = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
        rptrWriteback = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];

        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_ALIGN: StartDW=%u PadDW=%u IndirectEndDW=%u EndModulo8=%u TargetWPTR=%u IbSizeDW=%u PublishedDW=%u\n",
            startCursor, padDwords, indirectEndDwords,
            indirectEndDwords & 7u, targetWptrBytes, ibSizeDwords,
            (uint32_t)(DmaBufferSize / sizeof(uint32_t)));
        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_TRIPOD TIMEOUT: TargetWPTR=%u, WPTR=%u, RPTR=%u, STATUS=%08X, ERR_LOG=%08X, FenceExp=%08X, FenceObs=%08X\n",
            targetWptrBytes,
            wptrReg, rptrReg, statusReg, after.ErrorLog, nextFence, fenceObs);
        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] UTCL1_BASELINE: RD_STAT=%08X WR_STAT=%08X RD_XNACK=%08X/%08X WR_XNACK=%08X/%08X TIMEOUT=%08X ERR_LOG=%08X\n",
            baseline.RdStatus, baseline.WrStatus, baseline.RdXnack0,
            baseline.RdXnack1, baseline.WrXnack0, baseline.WrXnack1,
            baseline.Timeout, baseline.ErrorLog);
        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] UTCL1_FAULT_SNAPSHOT: RD_STAT=%08X WR_STAT=%08X RD_XNACK=%08X/%08X WR_XNACK=%08X/%08X TIMEOUT=%08X\n",
            after.RdStatus, after.WrStatus, after.RdXnack0, after.RdXnack1,
            after.WrXnack0, after.WrXnack1, after.Timeout);
        /*
         * O delta e a unica leitura interpretavel: bit ligado aqui foi ligado
         * por ESTA IB. Zero em todos os campos significa que a engine parou
         * sem levantar fault de traducao.
         */
        /*
         * RPTR_SAMPLES separa travamento de lentidao sem custar outra corrida:
         * valores distintos ao longo da espera significam que a engine estava
         * progredindo e o teto e que era curto; valores todos iguais provam
         * que ela parou naquele ponto.
         */
        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] ENGINE_STATE: STATUS1=%08X (idle se &0x3FF==0x3FF) FREEZE=%08X STATUS3=%08X\n",
            after.Status1, after.Freeze, after.Status3);
        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] FENCE_VISIBILIDADE: antes_hdp=%08X depois_hdp=%08X esperado=%08X | RPTR_writeback=%u RPTR_reg=%u\n",
            fenceObs, fenceAfterHdp, nextFence, rptrWriteback, rptrReg);
        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] RPTR_SAMPLES (n=%u, stride=%u polls): %u %u %u %u %u %u %u %u\n",
            rptrSampleCount, BC250_SDMA_TRIPOD_SAMPLE_STRIDE,
            rptrSamples[0], rptrSamples[1], rptrSamples[2], rptrSamples[3],
            rptrSamples[4], rptrSamples[5], rptrSamples[6], rptrSamples[7]);
        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] UTCL1_DELTA: RD_STAT=%08X WR_STAT=%08X RD_XNACK=%08X/%08X WR_XNACK=%08X/%08X TIMEOUT=%08X ERR_LOG=%08X\n",
            after.RdStatus ^ baseline.RdStatus,
            after.WrStatus ^ baseline.WrStatus,
            after.RdXnack0 ^ baseline.RdXnack0,
            after.RdXnack1 ^ baseline.RdXnack1,
            after.WrXnack0 ^ baseline.WrXnack0,
            after.WrXnack1 ^ baseline.WrXnack1,
            after.Timeout ^ baseline.Timeout,
            after.ErrorLog ^ baseline.ErrorLog);

        /*
         * Telemetria de fila DECODIFICADA. Em 05/09 estes valores sairam crus e
         * ficaram meses sem interpretacao; agora o log diz o que eles querem
         * dizer, e o cru fica ao lado para conferencia.
         */
        (void)Bc250SdmaDecodeQueueState(ibCntlReg, ibRptrReg, ibOffsetReg,
            ibSizeReg, skipCntlReg, contextStatusReg,
            ((uint64_t)ibBaseHiReg << 32) | (uint64_t)ibBaseLoReg,
            ibSubRemainReg,
            &queueState);
        queueVerdict = Bc250SdmaClassifyQueueState(&queueState);
        /*
         * A15: este discriminante usava comparacao crua e por isso o log da
         * corrida 17:43 disse anel_drenado=0 com RPTR=4612 e alvo 516 -- o anel
         * ESTAVA drenado, 4612 = 4096 + 516. Diagnostico errado por aritmetica.
         */
        ringDrained = (Bc250TripodWrapRingPointer(rptrReg,
                          (uint32_t)BC250_SDMA_PRESENT_RING_BYTES) ==
                       Bc250TripodWrapRingPointer(targetWptrBytes,
                          (uint32_t)BC250_SDMA_PRESENT_RING_BYTES)) ? 1u : 0u;
        stalledInsideIb = Bc250SdmaQueueLooksStalledInsideIb(&queueState,
            ringDrained);

        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_QUEUE_RAW: IB_CNTL=%08X IB_RPTR=%08X IB_OFFSET=%08X IB_BASE=%016llX IB_SIZE=%08X SKIP_CNTL=%08X CONTEXT_STATUS=%08X IB_SUB_REMAIN=%08X\n",
            ibCntlReg, ibRptrReg, ibOffsetReg,
            (unsigned long long)queueState.RawIbBase,
            ibSizeReg, skipCntlReg, contextStatusReg, ibSubRemainReg);
        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_QUEUE_DECODED: veredito=%s IB_ENABLE=%u SWITCH_INSIDE_IB=%u CMD_VMID=%u IB_RPTR=%u dw IB_OFFSET=%u dw IB_SIZE=%u dw SKIP_COUNT=%u IB_SUB_REMAIN=%u dw\n",
            Bc250SdmaQueueVerdictName(queueVerdict),
            queueState.IbEnable, queueState.SwitchInsideIb, queueState.CmdVmid,
            queueState.IbRptrDwords, queueState.IbOffsetDwords,
            queueState.IbSizeDwords, queueState.SkipCount, queueState.IbSubRemainDwords);
        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_QUEUE_STATUS: SELECTED=%u IDLE=%u EXPIRED=%u EXCEPTION=%u CTXSW_ABLE=%u CTXSW_READY=%u PREEMPTED=%u PREEMPT_DISABLE=%u BITS_SEM_NOME=%08X\n",
            queueState.Selected, queueState.Idle, queueState.Expired,
            queueState.Exception, queueState.CtxSwAble, queueState.CtxSwReady,
            queueState.Preempted, queueState.PreemptDisable,
            queueState.UnknownBits);
        /*
         * O discriminante de 05/09, agora avaliado em tempo de corrida em vez
         * de a mao depois: anel drenado + fila NAO ociosa + base de IB
         * carregada. NAO e' veredito de defeito, e' rotulo.
         */
        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_QUEUE_DISCRIMINANTE: anel_drenado=%u parado_dentro_do_IB=%u (gabarito 05/09: EXEC/VMID8 CONTEXT_STATUS=09 sem IDLE, PHYS/VMID0=0D com IDLE)\n",
            ringDrained, stalledInsideIb);
    }
    return FALSE;
#else
    UNREFERENCED_PARAMETER(DmaBufferGpuVa);
    UNREFERENCED_PARAMETER(DmaBufferSize);
    UNREFERENCED_PARAMETER(IbSizeDwords);
    UNREFERENCED_PARAMETER(Vmid);
    UNREFERENCED_PARAMETER(ExpectedEffectDestMc);
    UNREFERENCED_PARAMETER(ExpectedEffectBytes);
    UNREFERENCED_PARAMETER(ExpectedEffectPattern);
    return TRUE;
#endif
}

BOOLEAN Bc250SdmaPresentSubmitIndirectAsync(
    _In_ uint64_t DmaBufferGpuVa,
    _In_ ULONG DmaBufferSize,
    _In_ uint32_t IbSizeDwords,
    _In_ uint32_t Vmid,
    _In_ uint64_t ExpectedEffectDestMc,
    _In_ ULONG ExpectedEffectBytes,
    _In_ uint32_t ExpectedEffectPattern,
    _In_ uint64_t SubmissionFenceId,
    _In_ uint32_t ExecPath,
    _Out_ uint32_t *OutAssignedHardwareFence)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    BC250_DATA_PROOF_PLAN dataPlan;
    uint32_t packets[64] = { 0 };
    uint32_t cursor = 0u;
    uint32_t nextFence;
    uint32_t startCursor;
    uint32_t i;
    uint32_t ibSizeDwords;
    uint32_t padDwords;
    uint32_t targetWptrBytes;
    uint32_t indirectEndDwords;
    BC250_SDMA_STATUS status;
    KIRQL oldIrql;

    if (OutAssignedHardwareFence != NULL) {
        *OutAssignedHardwareFence = 0u;
    }

    if (!g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled) {
        Bc250DisplayLog("[SdmaPresent] SubmitIndirectAsync recusado: Init=%d HwEnabled=%d TripodValidated=%d\n",
            g_Bc250SdmaPresent.Initialized ? 1 : 0,
            g_Bc250SdmaPresent.HardwareEnabled ? 1 : 0,
            g_Bc250SdmaPresent.TripodValidated ? 1 : 0);
        return FALSE;
    }
    if (DmaBufferGpuVa == 0 || DmaBufferSize == 0 ||
        (DmaBufferGpuVa & (uint64_t)(BC250_SDMA_IB_ALIGNMENT_BYTES - 1u)) != 0 ||
        (DmaBufferSize & 3u) != 0 || Vmid > 15u) {
        Bc250DisplayLog("[SdmaPresent] SubmitIndirectAsync invalid params: VA=0x%llX, Size=%u, Vmid=%u\n",
            (unsigned long long)DmaBufferGpuVa, DmaBufferSize, Vmid);
        return FALSE;
    }

    ibSizeDwords = DmaBufferSize / sizeof(uint32_t);
    if (IbSizeDwords != 0u && IbSizeDwords <= ibSizeDwords) {
        ibSizeDwords = IbSizeDwords;
    }

    (void)Bc250PlanDataProofCanaries(ExpectedEffectDestMc, ExpectedEffectBytes,
        ExpectedEffectPattern, g_Bc250SdmaPresent.VramMcStart,
        (uint64_t)g_Bc250DisplayState.FrameBufferSize,
        (g_Bc250DisplayState.FrameBufferVirtual != NULL) ? 1u : 0u,
        &dataPlan);

    nextFence = (uint32_t)InterlockedIncrement(&g_Bc250SdmaPresent.NextFenceSequence);
    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);

    if (g_Bc250SdmaPresent.InFlightCount >= BC250_SDMA_IN_FLIGHT_CAPACITY) {
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        return FALSE;
    }

    if (dataPlan.Usable != 0u) {
        uint32_t planIndex;
        for (planIndex = 0u; planIndex < dataPlan.CanaryCount; ++planIndex) {
            volatile uint32_t *pCanary = (volatile uint32_t *)
                ((char *)g_Bc250DisplayState.FrameBufferVirtual +
                 dataPlan.VramOffset + dataPlan.Offsets[planIndex]);
            *pCanary = dataPlan.Values[planIndex];
        }
        KeMemoryBarrier();
        Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    }

    startCursor = Bc250SdmaPresentAllocateRingSlot(22u);
    padDwords = Bc250SdmaIndirectPrePadDwords(startCursor);
    cursor = 0u;
    for (i = 0u; i < padDwords; ++i) {
        packets[cursor++] = BC250_SDMA_OP_NOP;
    }

    status = Bc250SdmaEmitIndirect(packets, 64u, &cursor,
        DmaBufferGpuVa, ibSizeDwords, 0ull /* CsaAddress */, Vmid);
    if (status != BC250_SDMA_OK) {
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        Bc250DisplayLog("[SdmaPresent] SubmitIndirectAsync EmitIndirect failed: status=%u\n", (uint32_t)status);
        return FALSE;
    }

    {
        uint32_t slotIndex = g_Bc250SdmaPresent.InFlightTail;
        uint32_t slotFenceOffset = Bc250SdmaAsyncSlotFenceOffset(slotIndex);

        Bc250SdmaEmitGcr(packets, 64u, &cursor);
        Bc250SdmaEmitFence(packets, 64u, &cursor,
            g_Bc250SdmaPresent.WorkGpuAddress + slotFenceOffset,
            nextFence);

        g_Bc250SdmaPresent.Work[slotFenceOffset / 4u] =
            BC250_SDMA_PRESENT_FENCE_POISON;
        KeMemoryBarrier();

        for (i = 0u; i < cursor; ++i) {
            g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
        }
        KeMemoryBarrier();

        g_Bc250SdmaPresent.CurrentCursor += cursor;
        g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
            g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
        KeMemoryBarrier();

        Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 1u);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START,
            g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);

        InterlockedIncrement(&g_Bc250SdmaPresent.SubmissionsSubmittedCount);

        indirectEndDwords = startCursor + padDwords + BC250_SDMA_DWORDS_INDIRECT;
        targetWptrBytes = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));

        {
            BC250_SDMA_IN_FLIGHT_SUBMISSION *slot =
                &g_Bc250SdmaPresent.InFlightQueue[slotIndex];
            RtlZeroMemory(slot, sizeof(*slot));
            slot->Active = TRUE;
            slot->SubmissionFenceId = SubmissionFenceId;
            slot->HardwareFence = nextFence;
            slot->TargetWptrBytes = targetWptrBytes;
            slot->StartCursor = startCursor;
            slot->PadDwords = padDwords;
            slot->IndirectEndDwords = indirectEndDwords;
            slot->IbSizeDwords = ibSizeDwords;
            slot->DmaBufferSize = DmaBufferSize;
            slot->DmaBufferVirtualAddress = DmaBufferGpuVa;
            slot->Vmid = Vmid;
            slot->ExecPath = ExecPath;
            slot->ExpectedEffectDestMc = ExpectedEffectDestMc;
            slot->ExpectedEffectBytes = ExpectedEffectBytes;
            slot->ExpectedEffectPattern = ExpectedEffectPattern;
            slot->DataPlan = dataPlan;
            slot->SubmitTime = KeQueryPerformanceCounter(NULL);

            g_Bc250SdmaPresent.InFlightTail = (slotIndex + 1u) % BC250_SDMA_IN_FLIGHT_CAPACITY;
            g_Bc250SdmaPresent.InFlightCount++;
        }
    }

    if (OutAssignedHardwareFence != NULL) {
        *OutAssignedHardwareFence = nextFence;
    }

    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
    return TRUE;
#else
    UNREFERENCED_PARAMETER(DmaBufferGpuVa);
    UNREFERENCED_PARAMETER(DmaBufferSize);
    UNREFERENCED_PARAMETER(IbSizeDwords);
    UNREFERENCED_PARAMETER(Vmid);
    UNREFERENCED_PARAMETER(ExpectedEffectDestMc);
    UNREFERENCED_PARAMETER(ExpectedEffectBytes);
    UNREFERENCED_PARAMETER(ExpectedEffectPattern);
    UNREFERENCED_PARAMETER(SubmissionFenceId);
    UNREFERENCED_PARAMETER(ExecPath);
    UNREFERENCED_PARAMETER(OutAssignedHardwareFence);
    return FALSE;
#endif
}

BOOLEAN Bc250SdmaPresentSubmitFillAsync(
    _In_ uint64_t DestGpuAddress,
    _In_ ULONG ByteCount,
    _In_ uint32_t FillPattern,
    _In_ uint64_t SubmissionFenceId,
    _In_ uint64_t DmaBufferGpuVa,
    _Out_ uint32_t *OutAssignedHardwareFence)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    BC250_DATA_PROOF_PLAN dataPlan;
    uint32_t packets[128] = { 0 };
    uint32_t cursor = 0u;
    uint32_t nextFence;
    uint32_t startCursor;
    uint32_t i;
    ULONG remainingBytes;
    uint64_t curDst;
    BC250_SDMA_STATUS status;
    KIRQL oldIrql;

    if (OutAssignedHardwareFence != NULL) {
        *OutAssignedHardwareFence = 0u;
    }

    if (!g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled) {
        return FALSE;
    }
    if (DestGpuAddress == 0 || ByteCount == 0 ||
        ByteCount > (64u * 1024u * 1024u)) {
        return FALSE;
    }

    (void)Bc250PlanDataProofCanaries(DestGpuAddress, ByteCount, FillPattern,
        g_Bc250SdmaPresent.VramMcStart,
        (uint64_t)g_Bc250DisplayState.FrameBufferSize,
        (g_Bc250DisplayState.FrameBufferVirtual != NULL) ? 1u : 0u,
        &dataPlan);

    nextFence = (uint32_t)InterlockedIncrement(&g_Bc250SdmaPresent.NextFenceSequence);
    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);

    if (g_Bc250SdmaPresent.InFlightCount >= BC250_SDMA_IN_FLIGHT_CAPACITY) {
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        return FALSE;
    }

    if (dataPlan.Usable != 0u) {
        uint32_t planIndex;
        for (planIndex = 0u; planIndex < dataPlan.CanaryCount; ++planIndex) {
            volatile uint32_t *pCanary = (volatile uint32_t *)
                ((char *)g_Bc250DisplayState.FrameBufferVirtual +
                 dataPlan.VramOffset + dataPlan.Offsets[planIndex]);
            *pCanary = dataPlan.Values[planIndex];
        }
        KeMemoryBarrier();
        Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
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
            KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
            return FALSE;
        }
        remainingBytes -= chunkSize;
        curDst += chunkSize;
    }

    {
        uint32_t slotIndex = g_Bc250SdmaPresent.InFlightTail;
        uint32_t slotFenceOffset = Bc250SdmaAsyncSlotFenceOffset(slotIndex);

        Bc250SdmaEmitGcr(packets, 128u, &cursor);
        Bc250SdmaEmitFence(packets, 128u, &cursor,
            g_Bc250SdmaPresent.WorkGpuAddress + slotFenceOffset,
            nextFence);

        startCursor = Bc250SdmaPresentAllocateRingSlot(cursor);

        for (i = 0u; i < cursor; ++i) {
            g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
        }
        KeMemoryBarrier();

        g_Bc250SdmaPresent.CurrentCursor += cursor;
        g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
            g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
        g_Bc250SdmaPresent.Work[slotFenceOffset / 4u] =
            BC250_SDMA_PRESENT_FENCE_POISON;
        KeMemoryBarrier();

        Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 1u);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START,
            g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);

        InterlockedIncrement(&g_Bc250SdmaPresent.SubmissionsSubmittedCount);

        {
            BC250_SDMA_IN_FLIGHT_SUBMISSION *slot =
                &g_Bc250SdmaPresent.InFlightQueue[slotIndex];
            RtlZeroMemory(slot, sizeof(*slot));
            slot->Active = TRUE;
            slot->SubmissionFenceId = SubmissionFenceId;
            slot->HardwareFence = nextFence;
            slot->TargetWptrBytes = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
            slot->StartCursor = startCursor;
            slot->PadDwords = 0u;
            slot->IndirectEndDwords = 0u;
            slot->IbSizeDwords = 0u;
            slot->DmaBufferSize = ByteCount;
            slot->DmaBufferVirtualAddress = DmaBufferGpuVa;
            slot->Vmid = 0u;
            slot->ExecPath = BC250_SUBMIT_PATH_RING_DIRECT_FILL;
            slot->ExpectedEffectDestMc = DestGpuAddress;
            slot->ExpectedEffectBytes = ByteCount;
            slot->ExpectedEffectPattern = FillPattern;
            slot->DataPlan = dataPlan;
            slot->SubmitTime = KeQueryPerformanceCounter(NULL);

            g_Bc250SdmaPresent.InFlightTail = (slotIndex + 1u) % BC250_SDMA_IN_FLIGHT_CAPACITY;
            g_Bc250SdmaPresent.InFlightCount++;
        }
    }

    if (OutAssignedHardwareFence != NULL) {
        *OutAssignedHardwareFence = nextFence;
    }

    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
    return TRUE;
#else
    UNREFERENCED_PARAMETER(DestGpuAddress);
    UNREFERENCED_PARAMETER(ByteCount);
    UNREFERENCED_PARAMETER(FillPattern);
    UNREFERENCED_PARAMETER(SubmissionFenceId);
    UNREFERENCED_PARAMETER(DmaBufferGpuVa);
    UNREFERENCED_PARAMETER(OutAssignedHardwareFence);
    return FALSE;
#endif
}

BOOLEAN Bc250SdmaPresentSubmitSyncNopAsync(
    _In_ uint64_t SubmissionFenceId,
    _In_ uint64_t DmaBufferGpuVa,
    _Out_ uint32_t *OutAssignedHardwareFence)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    uint32_t packets[32] = { 0 };
    uint32_t cursor = 0u;
    uint32_t nextFence;
    uint32_t startCursor;
    uint32_t i;
    KIRQL oldIrql;

    if (OutAssignedHardwareFence != NULL) {
        *OutAssignedHardwareFence = 0u;
    }

    if (!g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled) {
        return FALSE;
    }

    packets[cursor++] = BC250_SDMA_OP_NOP;
    Bc250SdmaEmitGcr(packets, 32u, &cursor);
    nextFence = (uint32_t)InterlockedIncrement(&g_Bc250SdmaPresent.NextFenceSequence);
    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);

    if (g_Bc250SdmaPresent.InFlightCount >= BC250_SDMA_IN_FLIGHT_CAPACITY) {
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        return FALSE;
    }

    {
        uint32_t slotIndex = g_Bc250SdmaPresent.InFlightTail;
        uint32_t slotFenceOffset = Bc250SdmaAsyncSlotFenceOffset(slotIndex);

        Bc250SdmaEmitFence(packets, 32u, &cursor,
            g_Bc250SdmaPresent.WorkGpuAddress + slotFenceOffset,
            nextFence);

        startCursor = Bc250SdmaPresentAllocateRingSlot(cursor);

        for (i = 0u; i < cursor; ++i) {
            g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
        }
        KeMemoryBarrier();

        g_Bc250SdmaPresent.CurrentCursor += cursor;
        g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
            g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
        g_Bc250SdmaPresent.Work[slotFenceOffset / 4u] =
            BC250_SDMA_PRESENT_FENCE_POISON;
        KeMemoryBarrier();

        Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 1u);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START,
            g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);

        InterlockedIncrement(&g_Bc250SdmaPresent.SubmissionsSubmittedCount);

        {
            BC250_SDMA_IN_FLIGHT_SUBMISSION *slot =
                &g_Bc250SdmaPresent.InFlightQueue[slotIndex];
            RtlZeroMemory(slot, sizeof(*slot));
            slot->Active = TRUE;
            slot->SubmissionFenceId = SubmissionFenceId;
            slot->HardwareFence = nextFence;
            slot->TargetWptrBytes = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
            slot->StartCursor = startCursor;
            slot->DmaBufferSize = 4u;
            slot->DmaBufferVirtualAddress = DmaBufferGpuVa;
            slot->Vmid = 0u;
            slot->ExecPath = BC250_SUBMIT_PATH_RING_SYNC_NOP;
            slot->SubmitTime = KeQueryPerformanceCounter(NULL);

            g_Bc250SdmaPresent.InFlightTail = (slotIndex + 1u) % BC250_SDMA_IN_FLIGHT_CAPACITY;
            g_Bc250SdmaPresent.InFlightCount++;
        }
    }

    if (OutAssignedHardwareFence != NULL) {
        *OutAssignedHardwareFence = nextFence;
    }

    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
    return TRUE;
#else
    UNREFERENCED_PARAMETER(SubmissionFenceId);
    UNREFERENCED_PARAMETER(DmaBufferGpuVa);
    UNREFERENCED_PARAMETER(OutAssignedHardwareFence);
    return FALSE;
#endif
}

BOOLEAN Bc250SdmaPresentPollRetirement(
    _Out_ BC250_SDMA_RETIREMENT_RESULT *Result)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KIRQL oldIrql;
    uint32_t fenceObserved;
    uint32_t nextFence;
    uint32_t targetWptrBytes;
    LARGE_INTEGER now;
    LARGE_INTEGER freq;
    LONGLONG elapsedMs;
    BC250_SDMA_IN_FLIGHT_SUBMISSION *head;
    uint32_t headSlot;
    uint32_t slotFenceOffset;

    if (Result == NULL) {
        return FALSE;
    }
    RtlZeroMemory(Result, sizeof(*Result));

    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);

    if (g_Bc250SdmaPresent.InFlightCount == 0u) {
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        return FALSE;
    }

    headSlot = g_Bc250SdmaPresent.InFlightHead;
    head = &g_Bc250SdmaPresent.InFlightQueue[headSlot];
    slotFenceOffset = Bc250SdmaAsyncSlotFenceOffset(headSlot);

    nextFence = head->HardwareFence;
    targetWptrBytes = head->TargetWptrBytes;
    Result->SubmissionFenceId = head->SubmissionFenceId;
    Result->HardwareFence = nextFence;
    Result->ExecPath = head->ExecPath;
    Result->DmaBufferSize = head->DmaBufferSize;
    Result->DmaBufferVirtualAddress = head->DmaBufferVirtualAddress;

    /*
     * HDP flush incondicional caso a fence ainda nao esteja visivel na memoria.
     * Na fila assincrona multi-slot, o anel SDMA e' estritamente in-order;
     * o pacote SDMA_OP_FENCE e' emitido no final do segmento de cada submissao
     * apos GCR cache flush. fenceObserved == nextFence sobre o poison
     * (0x43DAFFFE) e' a prova fisica canonica de que a GPU concluiu a submissao.
     */
    fenceObserved = g_Bc250SdmaPresent.Work[slotFenceOffset / 4u];
    if (fenceObserved != nextFence) {
        Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
        fenceObserved = g_Bc250SdmaPresent.Work[slotFenceOffset / 4u];
    }

    if (fenceObserved == nextFence) {
        LONG completed = InterlockedIncrement(&g_Bc250SdmaPresent.SubmissionsCompletedCount);
        uint32_t post[BC250_DATA_PROOF_CANARY_COUNT];
        uint32_t dataMismatches = BC250_DATA_PROOF_CANARY_COUNT;
        BC250_TRIPOD_VERDICT ibVerdict;
        BC250_TRIPOD_RPTR_SOURCE ibRptrSource = BC250_TRIPOD_RPTR_SOURCE_NONE;
        uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t rptrWbRaw = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
        uint32_t targetWrapped = Bc250TripodWrapRingPointer(
            targetWptrBytes, (uint32_t)BC250_SDMA_PRESENT_RING_BYTES);
        uint32_t rptrRegWrapped = Bc250TripodWrapRingPointer(
            rptrReg, (uint32_t)BC250_SDMA_PRESENT_RING_BYTES);
        uint32_t rptrWb = Bc250TripodWrapRingPointer(
            rptrWbRaw, (uint32_t)BC250_SDMA_PRESENT_RING_BYTES);
            BC250_DATA_PROOF_PLAN dataPlan = head->DataPlan;
            uint32_t expectedPattern = head->ExpectedEffectPattern;
            uint64_t expectedDestMc = head->ExpectedEffectDestMc;
            ULONG expectedBytes = head->ExpectedEffectBytes;
            uint32_t vmid = head->Vmid;
            uint32_t startCursor = head->StartCursor;
            uint32_t padDwords = head->PadDwords;
            uint32_t indirectEndDwords = head->IndirectEndDwords;
            uint32_t ibSizeDwords = head->IbSizeDwords;
            ULONG dmaBufferSize = head->DmaBufferSize;
            BC250_SUBMIT_EXEC_PATH execPath = (BC250_SUBMIT_EXEC_PATH)head->ExecPath;

            RtlZeroMemory(post, sizeof(post));
            if (dataPlan.Usable != 0u) {
                uint32_t planIndex;
                Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
                for (planIndex = 0u; planIndex < dataPlan.CanaryCount; ++planIndex) {
                    volatile uint32_t *pCanary = (volatile uint32_t *)
                        ((char *)g_Bc250DisplayState.FrameBufferVirtual +
                         dataPlan.VramOffset + dataPlan.Offsets[planIndex]);
                    post[planIndex] = *pCanary;
                }
                dataMismatches = Bc250CountDataProofMismatches(&dataPlan, post,
                    expectedPattern);
            }

            ibVerdict = Bc250ClassifySdmaTripod(targetWrapped, rptrWb,
                rptrRegWrapped, nextFence, fenceObserved, dataMismatches,
                &ibRptrSource);

            head->Active = FALSE;
            g_Bc250SdmaPresent.InFlightHead = (headSlot + 1u) % BC250_SDMA_IN_FLIGHT_CAPACITY;
            g_Bc250SdmaPresent.InFlightCount--;
            KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);

            Result->Completed = TRUE;
            Result->TripodPassed = (dataPlan.Usable == 0u || Bc250TripodAllowsClaim(ibVerdict) != 0u);
            Result->Timeout = FALSE;
            Result->RptrRegWrapped = rptrRegWrapped;
            Result->RptrWbWrapped = rptrWb;
            Result->TargetWptrWrapped = targetWrapped;
            Result->Verdict = ibVerdict;
            Result->RptrSource = ibRptrSource;
            Result->DataMismatches = dataMismatches;
            Result->CanaryCount = dataPlan.CanaryCount;
            RtlCopyMemory(Result->PostGPU, post, sizeof(post));

            if (execPath == BC250_SUBMIT_PATH_INDIRECT) {
                Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_ALIGN: StartDW=%u PadDW=%u IndirectEndDW=%u EndModulo8=%u TargetWPTR=%u IbSizeDW=%u PublishedDW=%u\n",
                    startCursor, padDwords, indirectEndDwords,
                    indirectEndDwords & 7u, targetWptrBytes, ibSizeDwords,
                    (uint32_t)(dmaBufferSize / sizeof(uint32_t)));

                if (dataPlan.Usable == 0u) {
                    Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_ENGINE_2LEGS #%ld (RPTR==WPTR + fence sobre poison; PERNA DE DADOS NAO CONFERIDA): WPTR=%u RPTR=%u Fence=%08X Poison=%08X Size=%u Vmid=%u EfeitoDeclarado=0x%llX/%u/0x%08X (async)\n",
                        completed, targetWrapped, rptrRegWrapped, fenceObserved,
                        BC250_SDMA_PRESENT_FENCE_POISON, dmaBufferSize, vmid,
                        (unsigned long long)expectedDestMc,
                        expectedBytes, expectedPattern);
                } else {
                    Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_TRIPE_VEREDITO #%ld: %s (WPTR=%u RPTR_Reg=%u RPTR_Wb=%u Casou=%s Fence=%08X Poison=%08X Canaries=%u Divergentes=%u PostGPU=[%08X %08X %08X %08X %08X] Esperado=%08X Vmid=%u (async))\n",
                        completed, Bc250TripodVerdictName(ibVerdict),
                        targetWrapped, rptrRegWrapped, rptrWb,
                        Bc250TripodRptrSourceName(ibRptrSource),
                        fenceObserved, BC250_SDMA_PRESENT_FENCE_POISON,
                        dataPlan.CanaryCount, dataMismatches,
                        post[0], post[1], post[2], post[3], post[4],
                        expectedPattern, vmid);
                    if (Bc250TripodAllowsClaim(ibVerdict) != 0u) {
                        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_TRIPE PROVADO NO SILICIO #%ld: a GPU buscou o DMA buffer sob VMID%u e o efeito declarado esta nos bytes do destino (async)\n",
                            completed, vmid);
                    } else {
                        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB SEM TRIPE #%ld: fence e anel avancaram, mas %u de %u canaries ficaram INTACTOS -- o IB nao produziu o efeito declarado (async)\n",
                            completed, dataMismatches, dataPlan.CanaryCount);
                    }
                }
            } else if (execPath == BC250_SUBMIT_PATH_RING_DIRECT_FILL) {
                Bc250DisplayLog("[SubmitVirtual Node0 SDMA] PLANO A DIRECT FILL COMPLETED #%ld: WPTR=%u RPTR=%u Fence=%08X Canaries=%u Divergentes=%u (async)\n",
                    completed, targetWrapped, rptrRegWrapped, fenceObserved,
                    dataPlan.CanaryCount, dataMismatches);
            } else if (execPath == BC250_SUBMIT_PATH_RING_SYNC_NOP) {
                Bc250DisplayLog("[SubmitVirtual Node0 SDMA] PLANO A SYNC NOP COMPLETED #%ld: WPTR=%u RPTR=%u Fence=%08X (async)\n",
                    completed, targetWrapped, rptrRegWrapped, fenceObserved);
            }
            return TRUE;
        }

    now = KeQueryPerformanceCounter(&freq);
    elapsedMs = 0;
    if (freq.QuadPart > 0) {
        elapsedMs = ((now.QuadPart - head->SubmitTime.QuadPart) * 1000) / freq.QuadPart;
    }

    if (elapsedMs >= 2000) {
        if (!head->TimeoutLogged) {
            uint32_t wptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START);
            uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
            uint32_t statusReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
            BC250_SDMA_UTCL1_SNAPSHOT after;
            Bc250SdmaCaptureUtcl1Snapshot(&after);

            Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_TRIPOD TIMEOUT (ASYNC %lld ms): TargetWPTR=%u, WPTR=%u, RPTR=%u, STATUS=%08X, ERR_LOG=%08X, FenceExp=%08X, FenceObs=%08X (preso em voo ate TDR)\n",
                elapsedMs, targetWptrBytes, wptrReg, rptrReg, statusReg, after.ErrorLog, nextFence, fenceObserved);
            head->TimeoutLogged = TRUE;
        }

        /*
         * INVARIANTE FAIL-CLOSED (Leis 2 e 4):
         * A submissao NAO concluiu e o anel NAO esta drenado. NUNCA desmarque
         * head->Active e NUNCA decremente InFlightCount sem fence fisica ou TDR.
         * Fazer o pop falso permite que a proxima submissao troque a raiz do VMID8
         * com o motor ainda preso dentro do IB. A recuperacao correta e' delegada
         * ao TDR (ResetFromTimeout/RestartFromTimeout do dxgkrnl).
         */
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);

        Result->Completed = FALSE;
        Result->Timeout = TRUE;
        return FALSE;
    }

    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
    Result->Completed = FALSE;
    Result->Timeout = FALSE;
    return TRUE;
#else
    UNREFERENCED_PARAMETER(Result);
    return FALSE;
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
    Bc250DisplayLog("[SdmaPresent] Teardown Complete. TripodValidated=%d, Submitted=%ld, Completed=%ld, Fallbacks=%ld, HostCpuCopies=%ld\n",
        g_Bc250SdmaPresent.TripodValidated ? 1 : 0,
        g_Bc250SdmaPresent.BltSubmittedCount,
        g_Bc250SdmaPresent.BltCompletedCount,
        g_Bc250SdmaPresent.FallbackCpuCount,
        g_Bc250SdmaPresent.HostCpuCopyCount);
    /*
     * C3: contagem NAO amostrada dos escapes que o KMD recebeu, mais o custo do
     * blt sincrono. EscapeEntered e' o ultimo present que chegou ate' aqui --
     * comparar com o numero do quadro em que o harness falhou diz se a ruptura
     * foi antes ou depois do KMD.
     */
    Bc250DisplayLog("[SdmaPresent] Escape Archive. EscapeEntered=%ld, BltMax=%ld us, BltTotal=%lld us\n",
        g_Bc250SdmaPresent.EscapeEnteredCount,
        g_Bc250SdmaPresent.EscapeBltMaxMicroseconds,
        (long long)g_Bc250SdmaPresent.EscapeBltTotalMicroseconds);
#else
    g_Bc250SdmaPresent.Initialized = FALSE;
    g_Bc250SdmaPresent.HardwareEnabled = FALSE;
#endif
}

/*
 * =====================================================================
 * B6 / item 25 -- quiesce e reinicio do SDMA0 para o TDR.
 * Ver o bloco de motivo em bc250_sdma_present.h.
 * =====================================================================
 */
BOOLEAN Bc250SdmaPresentQuiesceForTdr(_Out_ BC250_SDMA_TDR_SNAPSHOT *Snapshot)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KIRQL oldIrql;
    uint32_t rbCntl;
    uint32_t f32Cntl;

    if (Snapshot == NULL) {
        return FALSE;
    }
    RtlZeroMemory(Snapshot, sizeof(*Snapshot));

    if (!g_Bc250SdmaPresent.Initialized ||
        g_Bc250SdmaPresent.MmioVirtual == NULL) {
        Bc250DisplayLog("[SdmaPresent] TDR_QUIESCE RECUSADO: Initialized=%u MmioVirtual=%p -- sem transporte, nao ha quiesce\n",
            g_Bc250SdmaPresent.Initialized ? 1u : 0u,
            (void *)g_Bc250SdmaPresent.MmioVirtual);
        return FALSE;
    }

    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);

    Snapshot->HardwareWasEnabled = g_Bc250SdmaPresent.HardwareEnabled ? 1u : 0u;
    Snapshot->RptrBefore = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
    Snapshot->WptrBefore = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START);
    Snapshot->StatusBefore = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
    Snapshot->Status1Before = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_STATUS1 - BC250_SDMA0_MMIO_WINDOW_START);
    Snapshot->ErrorLogBefore = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_ERROR_LOG - BC250_SDMA0_MMIO_WINDOW_START);

    /*
     * 1. Fecha a porta para submissao nova ANTES de mexer no silicio: quem
     *    checa HardwareEnabled passa a falhar fechado em vez de escrever num
     *    anel que esta sendo desligado.
     */
    g_Bc250SdmaPresent.HardwareEnabled = FALSE;

    /* 2. Desliga o anel. */
    rbCntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Snapshot->RbCntlBefore = rbCntl;
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        rbCntl & ~F_RB_ENABLE);

    /* 3. Trava o microcontrolador F32 para parar o fetch em andamento. */
    f32Cntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_F32_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Snapshot->F32CntlBefore = f32Cntl;
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_F32_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        f32Cntl | F_F32_HALT);

    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);

    Bc250DisplayLog("[SdmaPresent] TDR_QUIESCE: RB_ENABLE desligado e F32 travado (HwEstavaLigado=%u RPTR=%u WPTR=%u STATUS=%08X STATUS1=%08X [0x3FF=idle] ERR_LOG=%08X RB_CNTL=%08X->%08X F32_CNTL=%08X->%08X)\n",
        Snapshot->HardwareWasEnabled,
        Snapshot->RptrBefore, Snapshot->WptrBefore,
        Snapshot->StatusBefore, Snapshot->Status1Before,
        Snapshot->ErrorLogBefore,
        Snapshot->RbCntlBefore, Snapshot->RbCntlBefore & ~F_RB_ENABLE,
        Snapshot->F32CntlBefore, Snapshot->F32CntlBefore | F_F32_HALT);
    return TRUE;
#else
    if (Snapshot != NULL) {
        RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    }
    return FALSE;
#endif
}

BOOLEAN Bc250SdmaPresentRestartAfterTdr(void)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KIRQL oldIrql;
    uint32_t value;
    uint32_t grbmBefore;
    uint32_t grbmAsserted;
    uint32_t grbmAfter;
    uint32_t rbCntl;
    uint32_t rptr;
    uint32_t rptrHi;
    uint32_t wptr;
    uint32_t wptrHi;
    uint32_t rbBase;
    uint32_t rbBaseHi;
    uint32_t pollCntl;
    uint32_t pollAddrLo;
    uint32_t pollAddrHi;
    uint32_t rptrAddrLo;
    uint32_t rptrAddrHi;
    uint32_t ibCntl;
    uint32_t f32Cntl;
    uint32_t cntl;
    uint32_t chicken;
    uint32_t gbAddr;
    uint32_t gbAddrRead;
    BC250_TDR_STATUS readbackStatus;
    BOOLEAN proven;

    if (!g_Bc250SdmaPresent.Initialized ||
        g_Bc250SdmaPresent.MmioVirtual == NULL ||
        g_Bc250SdmaPresent.Ring == NULL) {
        Bc250DisplayLog("[SdmaPresent] TDR_RESTART RECUSADO: Initialized=%u MmioVirtual=%p Ring=%p\n",
            g_Bc250SdmaPresent.Initialized ? 1u : 0u,
            (void *)g_Bc250SdmaPresent.MmioVirtual,
            (void *)g_Bc250SdmaPresent.Ring);
        return FALSE;
    }

    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);

    /*
     * 1. RESET COMPLETO DO BLOCO.
     *
     * A corrida 07/09 20:41 provou que HALT + recriar ponteiros nao aborta o
     * POLL_REGMEM: o restart publicou WPTR=40, mas RPTR ficou em 68 e a fence
     * reteve o poison. A evidencia de 23/08 ja dizia que somente o pulso em
     * GRBM_SOFT_RESET.SDMA0 recupera a engine dessa classe de comando preso.
     *
     * Primeiro desliga qualquer fonte de trabalho, mantem F32 haltado e
     * desliga UTC_L1; depois pulsa o reset com readback nos dois lados.
     */
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_WPTR_POLL_CNTL - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_SEM_TIMER - BC250_SDMA0_MMIO_WINDOW_START, 0u);

    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_MMIO_WINDOW_START,
        value | 0xFF000000u);

    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_F32_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_F32_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        value | F_F32_HALT);
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        value & ~F_CNTL_UTC_L1);

    grbmBefore = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_GRBM_REG_SOFT_RESET - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_GRBM_REG_SOFT_RESET - BC250_SDMA0_MMIO_WINDOW_START,
        grbmBefore | BC250_GRBM_SOFT_RESET_SDMA0);
    grbmAsserted = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_GRBM_REG_SOFT_RESET - BC250_SDMA0_MMIO_WINDOW_START);
    KeStallExecutionProcessor(50u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_GRBM_REG_SOFT_RESET - BC250_SDMA0_MMIO_WINDOW_START,
        grbmBefore & ~BC250_GRBM_SOFT_RESET_SDMA0);
    grbmAfter = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_GRBM_REG_SOFT_RESET - BC250_SDMA0_MMIO_WINDOW_START);
    KeStallExecutionProcessor(1000u);

    if ((grbmAsserted & BC250_GRBM_SOFT_RESET_SDMA0) == 0u ||
        (grbmAfter & BC250_GRBM_SOFT_RESET_SDMA0) != 0u) {
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        Bc250DisplayLog("[SdmaPresent] TDR_FULL_RESET FALHOU: GRBM_SOFT_RESET nao confirmou o pulso SDMA0 (Before=%08X Asserted=%08X After=%08X)\n",
            grbmBefore, grbmAsserted, grbmAfter);
        return FALSE;
    }

    /*
     * 2. RECONSTRUCAO INTEGRAL DO ANEL. O soft reset apaga configuracao; nao
     * basta religar RB_ENABLE. Esta e' a mesma familia de escritas do init
     * que ja sobe o SDMA0 no silicio: ponteiros, bases, writeback/polling,
     * golden settings, UTCL1, doorbell e IB.
     */
    g_Bc250SdmaPresent.CurrentCursor = 0u;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] = 0u;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u] = 0u;
    KeMemoryBarrier();
    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);

    rbCntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    rbCntl = (rbCntl & ~(M_RB_SIZE | F_RB_ENABLE | F_RPTR_WB)) |
        (10u << 1) | F_RPTR_WB;
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        rbCntl & ~F_RB_ENABLE);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_RPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);

    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_WPTR_POLL_ADDR_LO - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)(g_Bc250SdmaPresent.WorkGpuAddress +
            BC250_SDMA_PRESENT_WPTR_OFFSET));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_WPTR_POLL_ADDR_HI - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)((g_Bc250SdmaPresent.WorkGpuAddress +
            BC250_SDMA_PRESENT_WPTR_OFFSET) >> 32));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_WPTR_POLL_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        0x00403000u | F_POLL_ENABLE);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RPTR_ADDR_HI - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)((g_Bc250SdmaPresent.WorkGpuAddress +
            BC250_SDMA_PRESENT_RPTR_OFFSET) >> 32));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RPTR_ADDR_LO - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)(g_Bc250SdmaPresent.WorkGpuAddress +
            BC250_SDMA_PRESENT_RPTR_OFFSET) & 0xFFFFFFFCu);

    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CHICKEN - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CHICKEN - BC250_SDMA0_MMIO_WINDOW_START,
        (value & ~0xFFBF1F0Fu) | (0x03AB0107u & 0xFFBF1F0Fu));
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_GB_ADDR - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_GB_ADDR - BC250_SDMA0_MMIO_WINDOW_START,
        (value & ~0x001877FFu) | (0x00000044u & 0x001877FFu));
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_GB_ADDR_READ - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_GB_ADDR_READ - BC250_SDMA0_MMIO_WINDOW_START,
        (value & ~0x001877FFu) | (0x00000044u & 0x001877FFu));

    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_BASE - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)(g_Bc250SdmaPresent.RingGpuAddress >> 8));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_BASE_HI - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)(g_Bc250SdmaPresent.RingGpuAddress >> 40));

    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 1u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);

    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_DOORBELL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_DOORBELL - BC250_SDMA0_MMIO_WINDOW_START,
        value & ~F_DOORBELL_ENABLE);
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_MMIO_WINDOW_START,
        value | 0xFF000000u);
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        value | F_CNTL_UTC_L1);
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_UTCL1_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_UTCL1_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        (value & ~(0x00000E00u | 0x0000003Eu)) | (3u << 9) | (9u << 1));
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_UTCL1_PAGE - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_UTCL1_PAGE - BC250_SDMA0_MMIO_WINDOW_START,
        (value & ~0x00FFF000u) | (0x004C5C00u & 0x00FFF000u));

    /* 3. Destrava o F32 e religa anel/IB. */
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_F32_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_F32_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        value & ~F_F32_HALT);

    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        value | F_RB_ENABLE);
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_IB_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_IB_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        value | F_IB_ENABLE);

    /*
     * 4. READBACK ANTES DA PROVA. A fence nao pode mascarar configuracao que
     *    nem chegou ao bloco. O contrato puro rejeita o RPTR=68 medido na
     *    corrida 20:41, reset ainda assertado, F32 haltado ou anel desligado.
     */
    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    rptr = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
    rptrHi = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_RPTR_HI - BC250_SDMA0_MMIO_WINDOW_START);
    wptr = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START);
    wptrHi = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START);
    rbCntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    rbBase = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_BASE - BC250_SDMA0_MMIO_WINDOW_START);
    rbBaseHi = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_BASE_HI - BC250_SDMA0_MMIO_WINDOW_START);
    pollCntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_WPTR_POLL_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    pollAddrLo = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_WPTR_POLL_ADDR_LO - BC250_SDMA0_MMIO_WINDOW_START);
    pollAddrHi = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_WPTR_POLL_ADDR_HI - BC250_SDMA0_MMIO_WINDOW_START);
    rptrAddrLo = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RPTR_ADDR_LO - BC250_SDMA0_MMIO_WINDOW_START);
    rptrAddrHi = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RPTR_ADDR_HI - BC250_SDMA0_MMIO_WINDOW_START);
    ibCntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_IB_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    f32Cntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_F32_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    cntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    chicken = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CHICKEN - BC250_SDMA0_MMIO_WINDOW_START);
    gbAddr = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_GB_ADDR - BC250_SDMA0_MMIO_WINDOW_START);
    gbAddrRead = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_GB_ADDR_READ - BC250_SDMA0_MMIO_WINDOW_START);

    readbackStatus = Bc250TdrValidateSdmaRestartReadback(
        rptr, rptrHi, wptr, wptrHi, rbCntl, F_RB_ENABLE,
        f32Cntl, F_F32_HALT, grbmAfter, BC250_GRBM_SOFT_RESET_SDMA0);
    if (readbackStatus != BC250_TDR_OK ||
        (rbCntl & (F_RB_ENABLE | M_RB_SIZE | F_RPTR_WB)) !=
            ((1u | (10u << 1) | F_RPTR_WB) &
                (F_RB_ENABLE | M_RB_SIZE | F_RPTR_WB)) ||
        rbBase != (uint32_t)(g_Bc250SdmaPresent.RingGpuAddress >> 8) ||
        rbBaseHi != (uint32_t)(g_Bc250SdmaPresent.RingGpuAddress >> 40) ||
        pollCntl != (0x00403000u | F_POLL_ENABLE) ||
        pollAddrLo != (uint32_t)(g_Bc250SdmaPresent.WorkGpuAddress +
            BC250_SDMA_PRESENT_WPTR_OFFSET) ||
        pollAddrHi != (uint32_t)((g_Bc250SdmaPresent.WorkGpuAddress +
            BC250_SDMA_PRESENT_WPTR_OFFSET) >> 32) ||
        rptrAddrLo != ((uint32_t)(g_Bc250SdmaPresent.WorkGpuAddress +
            BC250_SDMA_PRESENT_RPTR_OFFSET) & 0xFFFFFFFCu) ||
        rptrAddrHi != (uint32_t)((g_Bc250SdmaPresent.WorkGpuAddress +
            BC250_SDMA_PRESENT_RPTR_OFFSET) >> 32) ||
        (ibCntl & F_IB_ENABLE) == 0u || (cntl & F_CNTL_UTC_L1) == 0u ||
        (chicken & 0xFFBF1F0Fu) != (0x03AB0107u & 0xFFBF1F0Fu) ||
        (gbAddr & 0x001877FFu) != (0x00000044u & 0x001877FFu) ||
        (gbAddrRead & 0x001877FFu) != (0x00000044u & 0x001877FFu)) {
        g_Bc250SdmaPresent.HardwareEnabled = FALSE;
        g_Bc250SdmaPresent.TripodValidated = FALSE;
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        Bc250DisplayLog("[SdmaPresent] TDR_REPROGRAM_READBACK FALHOU: Contract=%s GRBM=%08X/%08X/%08X RPTR=%u:%u WPTR=%u:%u RB_CNTL=%08X BASE=%08X:%08X POLL=%08X IB=%08X F32=%08X CNTL=%08X\n",
            Bc250TdrStatusName(readbackStatus),
            grbmBefore, grbmAsserted, grbmAfter,
            rptrHi, rptr, wptrHi, wptr, rbCntl,
            rbBaseHi, rbBase, pollCntl, ibCntl, f32Cntl, cntl);
        return FALSE;
    }

    /*
     * 5. Habilita PROVISORIAMENTE para que a prova abaixo possa submeter. Se a
     *    prova falhar, isso volta a FALSE antes de qualquer submissao real --
     *    e' o unico jeito de usar o proprio caminho de submissao como prova.
     */
    g_Bc250SdmaPresent.HardwareEnabled = TRUE;
    g_Bc250SdmaPresent.InFlightHead = 0u;
    g_Bc250SdmaPresent.InFlightTail = 0u;
    g_Bc250SdmaPresent.InFlightCount = 0u;
    RtlZeroMemory(g_Bc250SdmaPresent.InFlightQueue, sizeof(g_Bc250SdmaPresent.InFlightQueue));

    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);

    /*
     * 6. PROVA FISICA: um NOP com GCR flush e fence sobre poison. Se a engine
     *    nao retirar a fence, ela nao voltou -- e nao ha direito de dizer ao
     *    dxgkrnl que o adaptador esta pronto.
     */
    proven = Bc250SdmaPresentSyncNop();
    if (!proven) {
        g_Bc250SdmaPresent.HardwareEnabled = FALSE;
        g_Bc250SdmaPresent.TripodValidated = FALSE;
        Bc250DisplayLog("[SdmaPresent] TDR_RESTART FALHOU: anel rearmado, mas o NOP de prova nao retirou fence. HardwareEnabled volta a FALSE; adaptador continua recusando submissao\n");
        return FALSE;
    }

    /*
     * Bc250DisplayLog persiste em arquivo somente em PASSIVE_LEVEL. O readback
     * ocorreu sob RingLock/DISPATCH_LEVEL e a corrida 21:49 provou que uma
     * linha emitida ali some do log integral. Publica os valores somente
     * agora, depois de a fence fisica fechar e sem spinlock retido.
     */
    Bc250DisplayLog("[SdmaPresent] TDR_FULL_RESET E REPROGRAMACAO: READBACK PASS (GRBM=%08X/%08X/%08X RPTR=%u WPTR=%u RB_CNTL=%08X BASE=%08X:%08X POLL=%08X IB=%08X F32=%08X CNTL=%08X)\n",
        grbmBefore, grbmAsserted, grbmAfter, rptr, wptr, rbCntl,
        rbBaseHi, rbBase, pollCntl, ibCntl, f32Cntl, cntl);
    Bc250DisplayLog("[SdmaPresent] TDR_RESTART PROVADO: anel rearmado e NOP fisico com fence sobre poison concluido; HardwareEnabled=1\n");
    return TRUE;
#else
    return FALSE;
#endif
}

/*
 * =====================================================================
 * item 24 (07/09) -- deteccao propria de travamento, para responder ao
 * DxgkDdiQueryEngineStatus com MEDICAO em vez de otimismo.
 *
 * MOTIVO: Bc250McdmQueryEngineStatus punha EngineStatus.Responsive = 1 sem
 * olhar um unico registrador. O dxgkrnl chama essa DDI durante o TDR para
 * decidir se a engine esta viva; responder "viva" sempre e' a mesma classe de
 * falso sucesso do par Reset/Restart.
 *
 * ARMADILHA JA MEDIDA: os ponteiros tem de ser reduzidos a UMA volta do anel
 * antes da comparacao. Na corrida 17:43 o registrador leu RPTR=4612 com alvo
 * 516 num anel de 4096 bytes; comparar cru diria "trabalho pendente" com o
 * anel drenado, e o resultado seria "travado" para uma engine ociosa.
 * =====================================================================
 */
BOOLEAN Bc250SdmaPresentProbeResponsive(_Out_ uint32_t *IsResponsive)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    uint32_t rptr1, wptr1, rptr2, wptr2;
    uint32_t isHung = 0u;
    BC250_TDR_STATUS tdrStatus;

    if (IsResponsive == NULL) {
        return FALSE;
    }
    *IsResponsive = 0u;

    if (!g_Bc250SdmaPresent.Initialized ||
        g_Bc250SdmaPresent.MmioVirtual == NULL) {
        Bc250DisplayLog("[SdmaPresent] ENGINE_PROBE RECUSADO: Initialized=%u MmioVirtual=%p -- sem transporte nao ha medicao\n",
            g_Bc250SdmaPresent.Initialized ? 1u : 0u,
            (void *)g_Bc250SdmaPresent.MmioVirtual);
        return FALSE;
    }

    rptr1 = Bc250TripodWrapRingPointer(
        Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START),
        (uint32_t)BC250_SDMA_PRESENT_RING_BYTES);
    wptr1 = Bc250TripodWrapRingPointer(
        Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START),
        (uint32_t)BC250_SDMA_PRESENT_RING_BYTES);

    /* Duas passagens com folga suficiente para a engine progredir. */
    KeStallExecutionProcessor(50u);

    rptr2 = Bc250TripodWrapRingPointer(
        Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START),
        (uint32_t)BC250_SDMA_PRESENT_RING_BYTES);
    wptr2 = Bc250TripodWrapRingPointer(
        Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START),
        (uint32_t)BC250_SDMA_PRESENT_RING_BYTES);

    tdrStatus = Bc250TdrDetectHang(0u, rptr1, wptr1, rptr2, wptr2, &isHung);
    if (tdrStatus != BC250_TDR_OK) {
        Bc250DisplayLog("[SdmaPresent] ENGINE_PROBE INDETERMINADO: Bc250TdrDetectHang=%s\n",
            Bc250TdrStatusName(tdrStatus));
        return FALSE;
    }

    *IsResponsive = (isHung == 0u) ? 1u : 0u;
    Bc250DisplayLog("[SdmaPresent] ENGINE_PROBE: Responsive=%u (voltas reduzidas: RPTR %u->%u WPTR %u->%u; anel=%u bytes)\n",
        *IsResponsive, rptr1, rptr2, wptr1, wptr2,
        (uint32_t)BC250_SDMA_PRESENT_RING_BYTES);
    return TRUE;
#else
    if (IsResponsive != NULL) *IsResponsive = 0u;
    return FALSE;
#endif
}

/*
 * =====================================================================
 * item 25 (07/09) -- HANG INDUZIDO DE PROPOSITO.
 *
 * Faltava no arsenal: nada provocava o timeout, e sem provocar nao ha como
 * medir a cadeia deteccao -> reset -> readmissao -> display vivo, que e' o
 * gate de saida da P6.
 *
 * COMO: um POLL_REGMEM esperando um valor que NUNCA aparece no slot de fence,
 * com retry e intervalo no maximo. A engine para de verdade -- nao e'
 * simulacao, nao e' delay de CPU, nao e' fence falsa.
 *
 * QUEM CHAMA tem de devolver sucesso ao dxgkrnl SEM notificar completion:
 * a fence pendente e' o que faz o timeout de 2 s do sistema disparar e o TDR
 * comecar. Notificar completion aqui destruiria o proprio ensaio.
 *
 * Armar e' decisao do dono do projeto; o default e' desarmado.
 * =====================================================================
 */
BOOLEAN Bc250SdmaPresentInduceHangForTdr(void)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    uint32_t packets[16] = { 0 };
    uint32_t cursor = 0u;
    uint32_t startCursor;
    uint32_t i;
    KIRQL oldIrql;
    BC250_SDMA_STATUS status;

    if (!g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled ||
        g_Bc250SdmaPresent.Ring == NULL) {
        Bc250DisplayLog("[SdmaPresent] TDR_HANG_INDUZIDO RECUSADO: Init=%u HwEnabled=%u Ring=%p\n",
            g_Bc250SdmaPresent.Initialized ? 1u : 0u,
            g_Bc250SdmaPresent.HardwareEnabled ? 1u : 0u,
            (void *)g_Bc250SdmaPresent.Ring);
        return FALSE;
    }

    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);

    startCursor = Bc250SdmaPresentAllocateRingSlot(16u);

    /*
     * Espera FUNC=3 (igual) pelo valor 0xDEADDEAD no slot de fence, que o
     * driver nunca escreve. Intervalo e retry no teto: a engine fica presa
     * neste pacote, e nenhum pacote posterior do anel avanca.
     */
    status = Bc250SdmaEmitPollRegMem(packets, 16u, &cursor,
        g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_FENCE_OFFSET,
        0xDEADDEADu, 0xFFFFFFFFu, 3u, 1u, 0xFFFFu, 0xFFFu);
    if (status != BC250_SDMA_OK) {
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        Bc250DisplayLog("[SdmaPresent] TDR_HANG_INDUZIDO RECUSADO: EmitPollRegMem status=%u\n",
            (uint32_t)status);
        return FALSE;
    }

    for (i = 0u; i < cursor; ++i) {
        g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
    }
    KeMemoryBarrier();

    g_Bc250SdmaPresent.CurrentCursor += cursor;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
    KeMemoryBarrier();

    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 1u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START,
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);

    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);

    Bc250DisplayLog("[SdmaPresent] TDR_HANG_INDUZIDO ARMADO NO SILICIO: POLL_REGMEM esperando 0xDEADDEAD em 0x%llX, WPTR=%u. A engine NAO vai progredir; o completion NAO deve ser notificado para que o timeout do dxgkrnl dispare o TDR\n",
        (unsigned long long)(g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_FENCE_OFFSET),
        (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t)));
    return TRUE;
#else
    return FALSE;
#endif
}
