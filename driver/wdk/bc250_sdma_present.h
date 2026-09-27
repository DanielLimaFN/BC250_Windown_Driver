#ifndef BC250_SDMA_PRESENT_H
#define BC250_SDMA_PRESENT_H

#include <ntddk.h>
#include <dispmprt.h>
#include "../core/bc250_sdma_packet_builder.h"
#include "../core/bc250_blt_proof_contract.h"
#include "../core/bc250_tdr_recovery_contract.h"

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
#define BC250_SDMA_PRESENT_ASYNC_FENCE_OFFSET 0x880u
#define BC250_SDMA_PRESENT_FENCE_POISON   0x43DAFFFEu
#define BC250_SDMA_PRESENT_FENCE_INIT     0x1000u

static inline uint32_t Bc250SdmaAsyncSlotFenceOffset(uint32_t SlotIndex)
{
    return BC250_SDMA_PRESENT_ASYNC_FENCE_OFFSET + (SlotIndex * (uint32_t)sizeof(uint32_t));
}

/*
 * Offsets de registradores relativos à base de BAR5.
 *
 * A janela mapeada precisa cobrir DOIS blocos, nao apenas o do SDMA0:
 *
 *   0x03FC4  HDP_MEM_COHERENCY_FLUSH_CNTL  (bloco HDP)
 *   0x03FCC  HDP_MISC_CNTL                 (bloco HDP)
 *   0x049EC.. registradores do SDMA0
 *   0x08020  GRBM_SOFT_RESET
 *
 * A janela anterior comecava em 0x04000 com 4 KB, o que deixava os dois
 * registradores de HDP FORA do mapeamento -- por construcao, a sequencia de
 * coerencia provada em silicio (Bc250SdmaHdp, bc250_sdma_ring_work.c) nunca
 * podia ser executada por este modulo.  A janela agora comeca em 0x03000 e
 * cobre 24 KB (0x03000..0x08FFF). O limite superior nao e' opcional: sem
 * ele, o helper de MMIO rejeita 0x08020 e o pulso de reset vira no-op.
 *
 * Proveniencia dos offsets de HDP: dbg/hw.c:5661 (provado em silicio),
 * driver/wdk/bc250_sdma_ring_work.c:55-57 e
 * docs/03_CATALOGO_DE_REGISTRADORES.md.
 */
#define BC250_SDMA0_MMIO_WINDOW_START     0x03000u
#define BC250_SDMA0_MMIO_WINDOW_BYTES     0x06000u  /* 24 KB (0x03000..0x08FFF): cobre HDP, SDMA0 e GRBM_SOFT_RESET (0x08020) */
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
/*
 * Catalogo docs/03_CATALOGO_DE_REGISTRADORES.md, tabela de leitura P1.
 * Somente leitura. SDMA0_STATUS2_REG NAO entra: o catalogo tem a mascara do
 * campo CMD_OP (linha 542) mas nao o offset do registrador, e a Lei 7 proibe
 * inferir endereco.
 */
#define BC250_SDMA0_REG_FREEZE            0x04A2Cu
#define BC250_SDMA0_REG_STATUS3           0x04AB0u
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
#define BC250_SDMA0_UTCL1_CNTL_FORCE_INVALIDATION 0x00004000u
#define BC250_SDMA0_REG_UTCL1_RD_STATUS   0x04A78u
#define BC250_SDMA0_REG_UTCL1_WR_STATUS   0x04A7Cu
#define BC250_SDMA0_REG_UTCL1_RD_XNACK0   0x04A8Cu
#define BC250_SDMA0_REG_UTCL1_RD_XNACK1   0x04A90u
#define BC250_SDMA0_REG_UTCL1_WR_XNACK0   0x04A94u
#define BC250_SDMA0_REG_UTCL1_WR_XNACK1   0x04A98u
#define BC250_SDMA0_REG_UTCL1_TIMEOUT     0x04A9Cu
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
/*
 * GRBM_SOFT_RESET (0x08020) no BAR5.
 * Proveniência: docs/03_CATALOGO_DE_REGISTRADORES.md linha 543 e dbg/hw.c:799.
 * Bit 23 (0x00800000) e SOFT_RESET_SDMA0 (validado em silicio em 2026-08-23).
 */
#ifndef BC250_GRBM_REG_SOFT_RESET
#define BC250_GRBM_REG_SOFT_RESET         0x08020u
#endif
#define BC250_GRBM_SOFT_RESET_SDMA0       0x00800000u

/*
 * A10 (06/09) -- estado da TERCEIRA perna do tripe da Lei 2.
 *
 * O blt de apresentacao verifica duas pernas dentro de Bc250SdmaPresentBlt:
 * RPTR == WPTR e a fence escrita pela GPU sobre poison. A terceira perna --
 * conferencia dos dados -- e' feita FORA, em Bc250DisplayCompareSurfaceBytes,
 * e somente em alguns Present. Enquanto ela nao passar com conteudo real, a
 * linha de log NAO pode se chamar "TRIPE": seria afirmar tres pernas tendo
 * duas. Este campo carrega o resultado da perna de dados para o log dizer a
 * verdade.
 */
typedef enum BC250_BLT_DATA_PROOF_STATE {
    /* Nunca conferido: o blt tem duas pernas provadas, nao tres. */
    BC250_BLT_DATA_PROOF_NONE = 0,
    /* Conferido byte a byte E com conteudo real (amostra nao-zerada). */
    BC250_BLT_DATA_PROOF_OK = 1,
    /* Divergencia de bytes: falha fechada. */
    BC250_BLT_DATA_PROOF_MISMATCH = -1,
    /*
     * Copia fiel de um frame EM BRANCO. Zero contra zero nao prova caminho de
     * pixel; nao e' PASS e nao e' FAIL -- e' inconclusivo.
     */
    BC250_BLT_DATA_PROOF_BLANK = 2
} BC250_BLT_DATA_PROOF_STATE;

typedef struct BC250_SDMA_IN_FLIGHT_SUBMISSION {
    BOOLEAN Active;
    BOOLEAN TimeoutLogged;
    uint64_t SubmissionFenceId;
    uint32_t HardwareFence;
    uint32_t TargetWptrBytes;
    uint32_t StartCursor;
    uint32_t PadDwords;
    uint32_t IndirectEndDwords;
    uint32_t IbSizeDwords;
    ULONG DmaBufferSize;
    uint64_t DmaBufferVirtualAddress;
    uint32_t Vmid;
    uint32_t ExecPath;
    uint64_t ExpectedEffectDestMc;
    ULONG ExpectedEffectBytes;
    uint32_t ExpectedEffectPattern;
    BC250_DATA_PROOF_PLAN DataPlan;
    LARGE_INTEGER SubmitTime;
} BC250_SDMA_IN_FLIGHT_SUBMISSION;

typedef struct BC250_SDMA_RETIREMENT_RESULT {
    uint64_t SubmissionFenceId;
    BOOLEAN Completed;
    BOOLEAN TripodPassed;
    BOOLEAN Timeout;
    uint32_t HardwareFence;
    uint32_t RptrRegWrapped;
    uint32_t RptrWbWrapped;
    uint32_t TargetWptrWrapped;
    BC250_TRIPOD_VERDICT Verdict;
    BC250_TRIPOD_RPTR_SOURCE RptrSource;
    uint32_t DataMismatches;
    uint32_t CanaryCount;
    uint32_t PostGPU[BC250_DATA_PROOF_CANARY_COUNT];
    uint32_t ExecPath;
    ULONG DmaBufferSize;
    uint64_t DmaBufferVirtualAddress;
} BC250_SDMA_RETIREMENT_RESULT;

typedef struct BC250_SDMA_PRESENT_CONTEXT {
    BOOLEAN Initialized;
    BOOLEAN HardwareEnabled;
    BOOLEAN TripodValidated;

    /* A10: ver BC250_BLT_DATA_PROOF_STATE. */
    volatile LONG BltDataProofState;
    /* Quantos Present seguidos vieram em branco -- diagnostico de tela preta. */
    volatile LONG BltBlankStreak;

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
    KSPIN_LOCK RingLock;
    uint32_t CurrentCursor;
    volatile LONG NextFenceSequence;
    volatile LONG BltSubmittedCount;
    volatile LONG BltCompletedCount;
    volatile LONG SubmissionsSubmittedCount;
    volatile LONG SubmissionsCompletedCount;
    volatile LONG FallbackCpuCount;
    /*
     * C3 (12/09): FallbackCpuCount conta SOMENTE recusa/timeout de
     * Bc250SdmaPresentBlt -- ou seja, "o caminho de GPU nao foi tomado". Ele e'
     * CEGO para a copia por CPU que o caminho de escape (PRESENT_BLT) realmente
     * executa, e foi por isso que a corrida 11/09 22:46 arquivou "Fallbacks=0"
     * enquanto os 11.059.200 bytes tinham sido copiados por RtlCopyMemory. Este
     * contador registra a copia EFETIVAMENTE FEITA pela CPU. Qualquer quadro
     * contado aqui NAO e' atribuivel a GPU (Lei 4).
     */
    volatile LONG HostCpuCopyCount;

    /*
     * C3 (12/09) -- observabilidade do caminho de escape.
     *
     * Nas cinco corridas de Marco 3 que falharam em 12/09 o KMD nao tem como
     * dizer qual foi o ULTIMO present que ele viu: o log do escape e' amostrado
     * (1a linha e a cada 60), entao ate' 59 chamadas ficam invisiveis antes da
     * ruptura. EscapeEnteredCount fecha essa lacuna sem inflar o log.
     *
     * As duracoes existem porque a medicao de 10:42 deu 26 FPS (~38 ms por
     * quadro) para copiar 11.059.200 bytes -- ~290 MB/s, ordens de grandeza
     * abaixo do que GDDR6 entrega. Ou o blt sincrono de Bc250SdmaPresentBlt e'
     * o gargalo, ou nao e'; sem medir, e' chute. Microssegundos, do laco de
     * poll inteiro, nao do pacote.
     */
    volatile LONG EscapeEnteredCount;
    volatile LONG EscapeNonZeroBltCount;
    volatile LONG EscapeBltMaxMicroseconds;
    volatile LONG64 EscapeBltTotalMicroseconds;

    /*
     * C3.6 (12/09) -- geometria do segmento 1, espelhada aqui para que os DOIS
     * caminhos de present possam converter offset de segmento sem inventar
     * aritmetica propria.
     *
     * ESCRITOR UNICO: Bc250SdmaPresentSetSegmentGeometry, chamada pelo miniport
     * quando BC250_MCDM_RUNTIME_CAPS fica pronta. Ninguem mais escreve. A
     * conversao continua saindo de Bc250SegmentOffsetTo{VramOffset,Mc} -- estes
     * campos sao só o transporte do dado ate' o caminho de display, que nao tem
     * o contexto do adaptador em maos.
     *
     * SegmentGeometryReady=0 significa "ainda nao sei", e converter nesse estado
     * e' PROIBIDO: foi exatamente supor offset cru que produziu o BugCheck 0x119
     * da corrida 13:11.
     */
    uint64_t LocalSegmentBaseOffset;
    uint64_t LocalSegmentBytes;
    volatile LONG SegmentGeometryReady;

    /* Métricas de prova do tripé */
    ULONG TripodPolls;
    uint32_t TripodFenceObserved;
    uint32_t TripodMismatches;
    /*
     * A13 (07/09): veredito das tres pernas e qual leitura de RPTR casou com o
     * WPTR alvo. Guardados porque a corrida 16:19 declarou tripe com
     * WPTR=68/RPTR=52 -- sem estes campos nao ha como auditar a decisao depois.
     * Valores de BC250_TRIPOD_VERDICT / BC250_TRIPOD_RPTR_SOURCE.
     */
    uint32_t TripodVerdict;
    uint32_t TripodRptrSource;

    /* Endereços de VRAM e GOP Scanout MC */
    uint64_t VramMcStart;
    uint64_t ScanoutGpuAddress;

    /*
     * DX.32 -- ULTIMA ESCRITA NO SCANOUT, fonte unica: os wrappers publicos de
     * Bc250SdmaPresentBlt/BltRect. Corrida 77: cada PRIMARY_COPY do MPO3 copiava
     * a MESMA origem que o DxgkDdiPresent anterior acabara de copiar (22 de 22
     * com amostras do destino ja iguais). Com isto a DDI de flip sabe se o
     * scanout ja recebeu, por INTEIRO e sem escrita posterior, esta origem.
     * Paginacao do dxgkrnl (fill/INDIRECT) nao passa por aqui e nao mira o
     * scanout reservado do GOP -- limite declarado.
     */
    uint64_t ScanoutLastFullSourceMc;
    ULONG ScanoutLastFullBytes;
    volatile LONG ScanoutWriteSequence;
    BOOLEAN ScanoutLastWriteFull;

    /*
     * Último offset de segmento apresentado via Escape (PRESENT_BLT / Vulkan scanout).
     * Permite a Bc250DxgkDdiPresent apresentar a imagem real destilada quando a swapchain
     * do dxgkrnl vier vazia, eliminando a alternância com tela preta.
     */
    volatile UINT64 LastEscapeSegmentOffset;

#define BC250_SDMA_IN_FLIGHT_CAPACITY 32u

    /* Submissão assíncrona do Nó 0 (elimina 0x133 e 0x119 por backpressure) */
    BC250_SDMA_IN_FLIGHT_SUBMISSION InFlightQueue[BC250_SDMA_IN_FLIGHT_CAPACITY];
    uint32_t InFlightHead;
    uint32_t InFlightTail;
    uint32_t InFlightCount;
} BC250_SDMA_PRESENT_CONTEXT;

extern BC250_SDMA_PRESENT_CONTEXT g_Bc250SdmaPresent;

/*
 * C3.6: publica a geometria do segmento 1. Escritor unico do par
 * LocalSegmentBaseOffset/LocalSegmentBytes. Chamar com valores zerados e' no-op,
 * para que caps ainda nao resolvidas nao apaguem uma geometria ja valida.
 */
VOID Bc250SdmaPresentSetSegmentGeometry(
    _In_ uint64_t LocalSegmentBaseOffset,
    _In_ uint64_t LocalSegmentBytes);

/*
 * C3.6: converte offset de segmento em offset CRU de VRAM, pronto para indexar
 * g_Bc250DisplayState.FrameBufferVirtual. Devolve FALSE -- e o caminho deve
 * falhar fechado -- quando a geometria nao esta publicada ou a faixa nao cabe.
 */
BOOLEAN Bc250SdmaPresentSegmentOffsetToVram(
    _In_ UINT32 SegmentId,
    _In_ UINT64 SegmentOffset,
    _In_ UINT64 SizeBytes,
    _Out_ UINT64 *VramOffset);

NTSTATUS Bc250SdmaPresentInit(
    _In_ PHYSICAL_ADDRESS MmioBar5Physical,
    _In_ PHYSICAL_ADDRESS VramBar0Physical,
    _In_ uint64_t VramMcStart,
    _In_ uint64_t ScanoutGpuMcAddress);

BOOLEAN Bc250SdmaPresentBlt(
    _In_ uint64_t SourceGpuAddress,
    _In_ uint64_t DestGpuAddress,
    _In_ ULONG ByteCount);

/* COPY_LINEAR_SUB_WIND: layout P1, semantica de contagens H, nao validada. */
BOOLEAN Bc250SdmaPresentBltRect(
    _In_ uint64_t SourceGpuAddress,
    _In_ ULONG SourcePitchBytes,
    _In_ uint64_t DestGpuAddress,
    _In_ ULONG DestPitchBytes,
    _In_ ULONG DestX,
    _In_ ULONG DestY,
    _In_ ULONG Width,
    _In_ ULONG Height);

/* DX.32: o scanout recebeu, por inteiro e sem escrita posterior, esta origem? */
BOOLEAN Bc250SdmaPresentScanoutHoldsFullCopyOf(
    _In_ uint64_t SourceGpuAddress,
    _In_ ULONG ByteCount,
    _Out_ LONG *WriteSequence);

BOOLEAN Bc250SdmaPresentFill(
    _In_ uint64_t DestGpuAddress,
    _In_ ULONG ByteCount,
    _In_ uint32_t FillPattern);

BOOLEAN Bc250SdmaPresentSyncNop(void);

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
VOID Bc250SdmaPresentFlushHdp(void);
#else
static __inline VOID Bc250SdmaPresentFlushHdp(void) { }
#endif

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
    _Out_ uint32_t *OutAssignedHardwareFence);

BOOLEAN Bc250SdmaPresentSubmitFillAsync(
    _In_ uint64_t DestGpuAddress,
    _In_ ULONG ByteCount,
    _In_ uint32_t FillPattern,
    _In_ uint64_t SubmissionFenceId,
    _In_ uint64_t DmaBufferGpuVa,
    _Out_ uint32_t *OutAssignedHardwareFence);

BOOLEAN Bc250SdmaPresentSubmitSyncNopAsync(
    _In_ uint64_t SubmissionFenceId,
    _In_ uint64_t DmaBufferGpuVa,
    _Out_ uint32_t *OutAssignedHardwareFence);

BOOLEAN Bc250SdmaPresentPollRetirement(
    _Out_ BC250_SDMA_RETIREMENT_RESULT *Result);

/*
 * IbSizeDwords e o valor publicado no campo IB_SIZE do pacote INDIRECT, ou
 * seja, quantos dwords a engine vai efetivamente buscar a partir de
 * DmaBufferGpuVa. Ele pode ser MENOR que DmaBufferSize/4: o encoder de
 * paginacao acrescenta NOPs de cauda com o unico proposito de alinhar a GPUVA
 * do PROXIMO buffer publicado ao VidMm, e esses NOPs nao precisam ser
 * buscados. Passar 0 faz o executor derivar DmaBufferSize/4.
 */
/*
 * A14 (07/09): ExpectedEffect* declara o que o IB deve produzir, para que a
 * perna 3 do tripe exista neste caminho. Na corrida 17:13 dez IBs sairam com o
 * rotulo "IB_TRIPOD PASS" tendo apenas DUAS pernas conferidas.
 *
 * ExpectedEffectDestMc  -- endereco MC do destino que o IB deve escrever.
 * ExpectedEffectBytes   -- quantos bytes o IB deve cobrir.
 * ExpectedEffectPattern -- valor que o destino deve conter depois.
 *
 * Passar 0 em Bytes (ou destino fora da abertura mapeada) e' legitimo: o
 * executor declara PERNA DE DADOS NAO CONFERIDA e nao usa a palavra TRIPE.
 */
BOOLEAN Bc250SdmaPresentExecuteIndirect(
    _In_ uint64_t DmaBufferGpuVa,
    _In_ ULONG DmaBufferSize,
    _In_ uint32_t IbSizeDwords,
    _In_ uint32_t Vmid,
    _In_ uint64_t ExpectedEffectDestMc,
    _In_ ULONG ExpectedEffectBytes,
    _In_ uint32_t ExpectedEffectPattern);

/*
 * =========================================================================
 * B6 / item 25 (07/09) -- quiesce e reinicio do SDMA0 para o TDR.
 *
 * MOTIVO: Bc250McdmResetFromTimeout e Bc250McdmRestartFromTimeout
 * retornavam STATUS_SUCCESS sem tocar no silicio -- so' escreviam uma linha
 * de log. Isso e' violacao direta da Lei 4 ("nunca retorne STATUS_SUCCESS em
 * DDI sem backend funcional") num par de DDIs em que o dxgkrnl CONFIA para
 * decidir se o adaptador voltou. E ficou urgente por causa do B3: com o
 * default em somente INDIRECT, submissao que nao resolve devolve
 * STATUS_IO_DEVICE_ERROR, e o caminho conhecido dai e' o BugCheck 0x119.
 *
 * Quiesce NAO desmapeia o BAR (Bc250SdmaPresentTeardown faz isso e serve para
 * StopDevice): ele desliga o anel e trava o F32, preservando os mapeamentos
 * para que o reinicio seja possivel dentro da mesma sessao de TDR.
 * =========================================================================
 */

typedef struct BC250_SDMA_TDR_SNAPSHOT {
    uint32_t RptrBefore;
    uint32_t WptrBefore;
    uint32_t StatusBefore;
    uint32_t Status1Before;
    uint32_t ErrorLogBefore;
    uint32_t RbCntlBefore;
    uint32_t F32CntlBefore;
    uint32_t HardwareWasEnabled;
} BC250_SDMA_TDR_SNAPSHOT;

/*
 * Desliga RB_ENABLE, trava o F32 e marca HardwareEnabled=FALSE, mantendo os
 * mapeamentos. Devolve FALSE quando nao ha transporte para tocar -- e nesse
 * caso NAO ha quiesce, e quem chama nao pode declarar sucesso.
 */
BOOLEAN Bc250SdmaPresentQuiesceForTdr(_Out_ BC250_SDMA_TDR_SNAPSHOT *Snapshot);

/*
 * Pulsa GRBM_SOFT_RESET.SDMA0, reconstrói integralmente a configuracao do
 * anel, exige readback de reset desassertado/ponteiros zero/bases/controles e
 * so' entao PROVA que a engine voltou submetendo um NOP com fence fisica sobre
 * poison. HardwareEnabled so' volta a TRUE se readback e prova fecharem.
 */
BOOLEAN Bc250SdmaPresentRestartAfterTdr(void);

/*
 * item 24 (07/09): deteccao propria de travamento por DUAS passagens de
 * RPTR/WPTR, com os ponteiros reduzidos a uma volta do anel. Serve ao
 * DxgkDdiQueryEngineStatus, que antes respondia Responsive=1 sem medir nada.
 * Devolve FALSE quando NAO foi possivel medir -- e nesse caso quem chama nao
 * pode afirmar nem que esta viva nem que travou.
 */
BOOLEAN Bc250SdmaPresentProbeResponsive(_Out_ uint32_t *IsResponsive);

/*
 * item 25 (07/09): hang INDUZIDO -- POLL_REGMEM esperando um valor que nunca
 * aparece. A engine para de verdade. Quem chama tem de devolver sucesso ao
 * dxgkrnl SEM notificar completion, porque e' a fence pendente que faz o
 * timeout de 2 s disparar o TDR. Armar e' decisao do dono do projeto.
 */
BOOLEAN Bc250SdmaPresentInduceHangForTdr(void);

void Bc250SdmaPresentTeardown(void);

#ifdef __cplusplus
}
#endif

#endif /* BC250_SDMA_PRESENT_H */
