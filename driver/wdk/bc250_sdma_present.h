#ifndef BC250_SDMA_PRESENT_H
#define BC250_SDMA_PRESENT_H

#include <ntddk.h>
#include <dispmprt.h>
#include "../core/bc250_sdma_packet_builder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_SDMA_LEGACY_RING_OFFSET     0x0FF00000ull
#define BC250_SDMA_VRAM_MC_BASE           0x000000F400000000ull

#define BC250_SDMA_PRESENT_RING_DWORDS    1024u
#define BC250_SDMA_PRESENT_RING_BYTES     (BC250_SDMA_PRESENT_RING_DWORDS * sizeof(uint32_t))
#define BC250_SDMA_PRESENT_WORK_BYTES     4096u
#define BC250_SDMA_PRESENT_BUFFER_TOTAL   (BC250_SDMA_PRESENT_RING_BYTES + BC250_SDMA_PRESENT_WORK_BYTES)

#define BC250_SDMA_PRESENT_RPTR_OFFSET    0x000u
#define BC250_SDMA_PRESENT_DEST_OFFSET    0x100u
#define BC250_SDMA_PRESENT_WPTR_OFFSET    0x400u
#define BC250_SDMA_PRESENT_FENCE_OFFSET   0x800u
#define BC250_SDMA_PRESENT_FENCE_POISON   0x43DAFFFEu
#define BC250_SDMA_PRESENT_FENCE_INIT     0x1000u

/*
 * Offsets de registradores relativos à base de BAR5.
 *
 * A janela mapeada precisa cobrir DOIS blocos, nao apenas o do SDMA0:
 *
 *   0x03FC4  HDP_MEM_COHERENCY_FLUSH_CNTL  (bloco HDP)
 *   0x03FCC  HDP_MISC_CNTL                 (bloco HDP)
 *   0x049EC.. registradores do SDMA0
 *
 * A janela anterior comecava em 0x04000 com 4 KB, o que deixava os dois
 * registradores de HDP FORA do mapeamento -- por construcao, a sequencia de
 * coerencia provada em silicio (Bc250SdmaHdp, bc250_sdma_ring_work.c) nunca
 * podia ser executada por este modulo.  A janela agora comeca em 0x03000 e
 * cobre 8 KB (0x03000..0x04FFF).
 *
 * Proveniencia dos offsets de HDP: dbg/hw.c:5661 (provado em silicio),
 * driver/wdk/bc250_sdma_ring_work.c:55-57 e
 * docs/03_CATALOGO_DE_REGISTRADORES.md.
 */
#define BC250_SDMA0_MMIO_WINDOW_START     0x03000u
#define BC250_SDMA0_MMIO_WINDOW_BYTES     0x02000u
#define BC250_SDMA0_REG_BASE_OFFSET       0x04000u
#define BC250_SDMA0_REG_HDP_INVALIDATE    0x03FC4u
#define BC250_SDMA0_REG_HDP_MISC          0x03FCCu
#define BC250_SDMA0_REG_CLK_CTRL          0x049ECu
#define BC250_SDMA0_REG_CNTL              0x049F0u
#define BC250_SDMA0_REG_CHICKEN           0x049F4u
#define BC250_SDMA0_REG_GB_ADDR           0x049F8u
#define BC250_SDMA0_REG_GB_ADDR_READ      0x049FCu
#define BC250_SDMA0_REG_SEM_TIMER         0x04A04u
#define BC250_SDMA0_REG_STATUS            0x04A14u
/*
 * 0x04A18 e' SDMA0_STATUS1_REG, nao um log de erro: o criterio do upstream e'
 * `(valor & 0x3FF) == 0x3FF` => engine OCIOSA.  Na corrida 09:04 este
 * registrador foi lido e reportado como "ERR_LOG=000003FF", o que significa o
 * oposto do que o rotulo sugeria.  O log de erro real e' 0x04AC0.
 * Fonte: docs/03_CATALOGO_DE_REGISTRADORES.md:466,493,541.
 */
#define BC250_SDMA0_REG_STATUS1           0x04A18u
#define BC250_SDMA0_REG_F32_CNTL          0x04A28u
#define BC250_SDMA0_REG_HDP_FLUSH         0x04A80u
/*
 * UTCL1_CNTL/UTCL1_PAGE: os valores antigos (0x04AE8/0x04AEC) nao constam do
 * catalogo nem do modulo provado.  Offsets corretos pela composicao SOC15
 * (dw 0x003c e 0x0048 do segmento 0): 0x04A70 e 0x04AA0.
 * Fonte: docs/03_CATALOGO_DE_REGISTRADORES.md:426,453-454 e
 * driver/wdk/bc250_sdma_ring_work.c:38-39.
 */
#define BC250_SDMA0_REG_UTCL1_CNTL        0x04A70u
#define BC250_SDMA0_REG_UTCL1_PAGE        0x04AA0u
#define BC250_SDMA0_REG_ERROR_LOG         0x04AC0u
#define BC250_SDMA0_REG_RB_CNTL           0x04B80u
#define BC250_SDMA0_REG_RB_BASE           0x04B84u
#define BC250_SDMA0_REG_RB_BASE_HI        0x04B88u
#define BC250_SDMA0_REG_RB_RPTR           0x04B8Cu
#define BC250_SDMA0_REG_RB_RPTR_HI        0x04B90u
#define BC250_SDMA0_REG_RB_WPTR           0x04B94u
#define BC250_SDMA0_REG_RB_WPTR_HI        0x04B98u
#define BC250_SDMA0_REG_WPTR_POLL_CNTL    0x04B9Cu
#define BC250_SDMA0_REG_RPTR_ADDR_HI      0x04BA0u
#define BC250_SDMA0_REG_RPTR_ADDR_LO      0x04BA4u
#define BC250_SDMA0_REG_IB_CNTL           0x04BA8u
#define BC250_SDMA0_REG_DOORBELL          0x04BC8u
#define BC250_SDMA0_REG_WPTR_POLL_ADDR_HI 0x04C48u
#define BC250_SDMA0_REG_WPTR_POLL_ADDR_LO 0x04C4Cu
#define BC250_SDMA0_REG_MINOR_PTR         0x04C54u

typedef struct BC250_SDMA_PRESENT_CONTEXT {
    BOOLEAN Initialized;
    BOOLEAN HardwareEnabled;
    BOOLEAN TripodValidated;

    PHYSICAL_ADDRESS MmioBase;
    volatile UCHAR *MmioVirtual;
    ULONG MmioLength;

    PHYSICAL_ADDRESS VramBase;
    volatile UCHAR *VramVirtual;
    ULONG VramLength;

    volatile uint32_t *Ring;
    uint64_t RingGpuAddress;

    volatile uint32_t *Work;
    uint64_t WorkGpuAddress;

    /* Estado de sincronização e fence */
    uint32_t CurrentCursor;
    volatile LONG NextFenceSequence;
    volatile LONG BltSubmittedCount;
    volatile LONG BltCompletedCount;
    volatile LONG FallbackCpuCount;

    /* Métricas de prova do tripé */
    ULONG TripodPolls;
    uint32_t TripodFenceObserved;
    uint32_t TripodMismatches;

    /* Endereços de VRAM e GOP Scanout MC */
    uint64_t VramMcStart;
    uint64_t ScanoutGpuAddress;
} BC250_SDMA_PRESENT_CONTEXT;

extern BC250_SDMA_PRESENT_CONTEXT g_Bc250SdmaPresent;

NTSTATUS Bc250SdmaPresentInit(
    _In_ PHYSICAL_ADDRESS MmioBar5Physical,
    _In_ PHYSICAL_ADDRESS VramBar0Physical,
    _In_ uint64_t VramMcStart,
    _In_ uint64_t ScanoutGpuMcAddress);

BOOLEAN Bc250SdmaPresentBlt(
    _In_ uint64_t SourceGpuAddress,
    _In_ uint64_t DestGpuAddress,
    _In_ ULONG ByteCount);

BOOLEAN Bc250SdmaPresentFill(
    _In_ uint64_t DestGpuAddress,
    _In_ ULONG ByteCount,
    _In_ uint32_t FillPattern);

void Bc250SdmaPresentTeardown(void);

#ifdef __cplusplus
}
#endif

#endif /* BC250_SDMA_PRESENT_H */
