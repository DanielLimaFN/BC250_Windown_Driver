#ifndef BC250_SDMA_PRESENT_H
#define BC250_SDMA_PRESENT_H

#include <ntddk.h>
#include <dispmprt.h>
#include "../core/bc250_sdma_packet_builder.h"
#include "../core/bc250_blt_proof_contract.h"
#include "../core/bc250_tdr_recovery_contract.h"
#include "../core/bc250_hdp_contract.h"

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
 * A janela mapeada precisa cobrir TRES blocos, nao apenas o do SDMA0:
 *
 *   0x0378C  RCC_CONFIG_MEMSIZE            (NBIO, leitura de confirmacao)
 *   0x0385C  BIF HDP_MEM_COHERENCY_FLUSH_CNTL (NBIO, flush de ESCRITA)
 *   0x03FC4  HDP_READ_CACHE_INVALIDATE     (bloco HDP)
 *   0x03FCC  HDP_MISC_CNTL                 (bloco HDP)
 *   0x049EC.. registradores do SDMA0
 *   0x08020  GRBM_SOFT_RESET
 *
 * HDP.1 (28/09): 0x03FC4 era rotulado "HDP_MEM_COHERENCY_FLUSH_CNTL" aqui --
 * falso; e' a invalidacao de leitura. O flush de escrita real e' 0x0385C,
 * com proveniencia em ../core/bc250_hdp_contract.h.
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
/*
 * HDP.1: 0x04A80 NAO e' flush de HDP -- e' SDMA0_UTCL1_INV0, somente leitura
 * para este driver. O nome verdadeiro (BC250_SDMA0_REG_UTCL1_INV0) e o flush
 * real (BC250_HDP_REG_MEM_COHERENCY_FLUSH_CNTL) vem de
 * ../core/bc250_hdp_contract.h. O nome antigo BC250_SDMA0_REG_HDP_FLUSH deixou
 * de existir de proposito: qualquer uso esquecido vira erro de build.
 */
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
/*
 * P2 (doc 598): WPTR do SDMA0 por doorbell, como o amdgpu. Proveniencia (P1):
 * - SDMA0_GFX_DOORBELL_OFFSET: mmSDMA0_GFX_DOORBELL_OFFSET 0x00AB idx0
 *   (gc_10_1_0_offset.h), base GC idx0 0x1260 -> (0x1260+0xAB)*4 = 0x4C2C;
 *   campo OFFSET mascara 0x0FFFFFFC, shift 2 (gc_10_1_0_sh_mask.h).
 * - BIF_SDMA0_DOORBELL_RANGE: mmBIF_SDMA0_DOORBELL_RANGE 0x01D0 idx2
 *   (nbio_2_3_offset.h), NBIO_BASE seg2 0x0D20 (navi10_ip_offset.h) ->
 *   (0xD20+0x1D0)*4 = 0x3BC0; OFFSET 0x00000FFC shift 2, SIZE 0x001F0000
 *   shift 16 (nbio_2_3_sh_mask.h). A mesma base idx2 da' 0x378C e 0x385C,
 *   que ja' usamos e provamos no silicio.
 * - Indice: AMDGPU_NAVI10_DOORBELL_sDMA_ENGINE0 = 0x100 (amdgpu_doorbell.h),
 *   ring->doorbell_index = 0x100 << 1 = 0x200 (sdma_v5_0_sw_init);
 *   sdma_doorbell_range = 20 (nv.c). WDOORBELL64(0x200) escreve no byte 0x800.
 */
#define BC250_SDMA0_REG_DOORBELL_OFFSET   0x04C2Cu
/*
 * P2c (doc 600): abertura de doorbell do BIF. nv_common_hw_init liga
 * RCC_DEV0_EPF0_RCC_DOORBELL_APER_EN.BIF_DOORBELL_APER_EN antes de qualquer
 * motor (nbio_v2_3_enable_doorbell_aperture) e nv_common_hw_fini desliga.
 * mmRCC_DEV0_EPF0_RCC_DOORBELL_APER_EN 0x00C0 idx2 (nbio_2_3_offset.h) ->
 * (0xD20+0xC0)*4 = 0x3780; campo bit 0, mascara 0x1 (nbio_2_3_sh_mask.h).
 * Corridas 221/222: sem ela nenhuma doorbell chegou (nem a do MEC: HitObserved=0).
 */
#define BC250_NBIO_REG_RCC_DOORBELL_APER_EN 0x03780u
#define BC250_NBIO_DOORBELL_APER_EN_MASK  0x00000001u
#define BC250_NBIO_REG_BIF_SDMA0_DOORBELL_RANGE 0x03BC0u
#define BC250_SDMA0_DOORBELL_OFFSET_MASK  0x0FFFFFFCu
#define BC250_BIF_DOORBELL_RANGE_OFFSET_MASK 0x00000FFCu
#define BC250_BIF_DOORBELL_RANGE_SIZE_MASK   0x001F0000u
#define BC250_P2_SDMA0_DOORBELL_INDEX     0x200u
#define BC250_P2_SDMA0_DOORBELL_RANGE_SIZE 20u
#define BC250_P2_SDMA0_DOORBELL_BYTE_OFFSET (BC250_P2_SDMA0_DOORBELL_INDEX * 4u)
#define BC250_P2_DOORBELL_MAP_BYTES       0x1000u
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
    /* 4c.2: VMIDs invalidados pelo SDMA no anel ANTES deste INDIRECT (0 = nenhum). */
    uint32_t TlbFlushVmidMask;
    uint32_t TlbFlushDwords;
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
    /*
     * CG.1 (doc 557): SDMA0_CLK_CTRL como o POST deixou, lido UMA vez antes do
     * primeiro "clk |= 0xFF000000". Depois do anel provado, os SOFT_OVERRIDE
     * voltam a este valor -- o amdgpu nunca escreve este registrador na SDMA
     * 5.0.1 (Cyan Skillfish) e o Linux roda com o MGCG do POST.
     */
    BOOLEAN ClockCtrlEntryCaptured;
    uint32_t ClockCtrlAtEntry;

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

    /*
     * D2 (doc 583): anel e Work do SDMA0 em memoria de sistema, ligados a GPU
     * pela abertura AGP. O init continua subindo na VRAM (a MMU global ainda
     * esta no estado do POST); depois da MMU, Bc250SdmaPresentRelocateRing-
     * ToSystemMemory troca Ring/Work e os enderecos de GPU e reconstroi o anel
     * pelo mesmo caminho provado do restart do TDR. Os ponteiros da VRAM
     * ficam guardados para o rollback e para o teardown.
     */
    BOOLEAN RingInSystemMemory;
    PVOID SystemRingWork;
    PHYSICAL_ADDRESS SystemRingWorkPhysical;
    volatile uint32_t *VramRing;
    uint64_t VramRingGpuAddress;
    volatile uint32_t *VramWork;
    uint64_t VramWorkGpuAddress;
    /* D2 por GART: quem desliga a GART no rollback e no teardown. */
    VOID (*SystemRingHook)(PVOID Context, uint32_t Reason);
    PVOID SystemRingHookContext;
    /*
     * P2 (doc 598): WPTR por doorbell. DoorbellActive so' vai a TRUE depois de
     * um NOP com fence sobre poison entregue SO' pela doorbell (poll da shadow
     * desligado na prova). O restart do TDR derruba a doorbell e a rearma.
     */
    volatile UCHAR *DoorbellVirtual;
    BOOLEAN DoorbellRequested;
    BOOLEAN DoorbellActive;
    BOOLEAN DoorbellBeforeCaptured;
    uint32_t DoorbellRegBefore;
    uint32_t DoorbellOffsetRegBefore;
    uint32_t BifRangeBefore;
    uint32_t DoorbellAperBefore;
    volatile LONG64 DoorbellKicks;
    /*
     * P3.1 (doc 609): anel 0 do IH na GART da VMID 0 e TRAP do SDMA0, como o
     * amdgpu numa APU (navi10_ih_sw_init: AMD_IS_APU -> use_bus_addr=false,
     * MC_SPACE=4). A conclusao continua por polling nesta etapa; o IH so'
     * prova que a interrupcao nasce no hardware e chega ao ISR.
     */
    volatile uint32_t *IhRing;
    uint64_t IhRingGpuVa;
    ULONG IhRingBytes;
    volatile uint32_t *IhWptrCpu;
    uint64_t IhWptrGpuVa;
    uint32_t IhRptr;
    BOOLEAN IhArmed;
    BOOLEAN IhMsi;
    BOOLEAN TrapArmed;
    BOOLEAN IhBeforeCaptured;
    uint32_t IhRbCntlBefore;
    uint32_t IhIntCntlBefore;
    uint32_t IhIntCntl2Before;
    /* P3.3 (doc 635): RPTR do IH pela doorbell, so' depois da prova. */
    BOOLEAN IhRptrDoorbellActive;
    BOOLEAN IhRptrDoorbellCaptured;
    uint32_t IhDoorbellRptrBefore;
    uint32_t IhBifRangeBefore;
    volatile LONG64 IhRptrDoorbellWrites;
    volatile LONG64 IhIsrCalls;
    volatile LONG64 IhIsrClaimed;
    volatile LONG64 IhEntries;
    volatile LONG64 IhSdmaTraps;
    volatile LONG64 IhCpEops;
    volatile LONG64 IhOther;
    volatile LONG64 IhDcn;      /* DCN.2 (doc 713): entradas do cliente DCE consumidas */
    volatile LONG64 IhOverflows;
    volatile LONG IhLastClientSrc;
    volatile LONG64 TrapsEmitted;
    /* P3.2 (doc 616): DPCs de aposentadoria enfileirados pelo ISR. */
    volatile LONG64 IhDpcKicks;
    /*
     * EOP do CP (doc 618): vetores CP_EOP do anel grafico (me 0, pipe 0),
     * DPCs de aposentadoria do GFX enfileirados pelo ISR, e RELEASE_MEM do
     * anel grafico emitidos com INT_SEL=2.
     */
    volatile LONG64 IhGfxEops;
    /*
     * C06 (doc 764): erros do CP no anel grafico, como gfx_v10_0_priv_reg_irq,
     * gfx_v10_0_priv_inst_irq e o bad opcode (CP_INT_CNTL_RING0 bits 22-24, o
     * valor 0x05C00000 do rastro do amdgpu nesta placa). So' contagem e o
     * ultimo ring_id/dw; o TDR continua sendo do dxgkrnl.
     */
    volatile LONG64 IhCpPrivReg;
    volatile LONG64 IhCpPrivInstr;
    volatile LONG64 IhCpBadOpcode;
    volatile LONG64 IhCpRsvdBit;
    volatile LONG IhCpErrLastDw0;
    volatile LONG IhCpErrLastDw1;
    volatile LONG64 IhGfxDpcKicks;
    volatile LONG64 CpEopIntsEmitted;
    /*
     * P4.2 (doc 622): SDMA1 anexado ao envio do Node 0. Protegido pelo
     * RingLock. Go/Done ficam na pagina Work do SDMA1 (VMID 0).
     */
    BOOLEAN Sdma1Attached;
    volatile uint32_t *Sdma1Ring;
    volatile uint32_t *Sdma1Work;
    uint64_t Sdma1RingGpu;
    uint64_t Sdma1WorkGpu;
    uint32_t Sdma1CursorDwords;
    uint32_t Sdma1NextToken;
    volatile LONG64 Sdma1RangesRouted;
    volatile LONG64 Sdma1RangesInline;
    volatile LONG64 Sdma1RangesNoSpace;
} BC250_SDMA_PRESENT_CONTEXT;

extern BC250_SDMA_PRESENT_CONTEXT g_Bc250SdmaPresent;

/*
 * P3.1 (doc 609) -- registradores do IH e do NBIO, P1:
 *  OSSSYS_BASE seg0 = 0x10A0 (cyan_skillfish_ip_offset.h), osssys_5_0_0_offset.h:
 *    IH_RB_CNTL 0x80, IH_RB_BASE 0x81, IH_RB_BASE_HI 0x82, IH_RB_RPTR 0x83,
 *    IH_RB_WPTR 0x84, IH_RB_WPTR_ADDR_HI 0x85, IH_RB_WPTR_ADDR_LO 0x86,
 *    IH_DOORBELL_RPTR 0x87 (todos BASE_IDX 0) -> bytes = (0x10A0 + reg) * 4.
 *  NBIO_BASE seg2 = 0xD20, nbio_2_3_offset.h: INTERRUPT_CNTL 0xF1,
 *    INTERRUPT_CNTL2 0xF2 (BASE_IDX 2) -> bytes = (0xD20 + reg) * 4.
 * Campos (osssys_5_0_0_sh_mask.h, nbio_2_3_sh_mask.h, gc_10_1_0_sh_mask.h).
 */
#define BC250_IH_REG_RB_CNTL            0x04480u
#define BC250_IH_REG_RB_BASE            0x04484u
#define BC250_IH_REG_RB_BASE_HI         0x04488u
#define BC250_IH_REG_RB_RPTR            0x0448Cu
#define BC250_IH_REG_RB_WPTR            0x04490u
#define BC250_IH_REG_RB_WPTR_ADDR_HI    0x04494u
#define BC250_IH_REG_RB_WPTR_ADDR_LO    0x04498u
#define BC250_IH_REG_DOORBELL_RPTR      0x0449Cu
#define BC250_NBIO_REG_INTERRUPT_CNTL   0x03844u
#define BC250_NBIO_REG_INTERRUPT_CNTL2  0x03848u
/*
 * P3.3 (doc 635): RPTR do IH por doorbell, como navi10_ih (use_doorbell=true).
 *  mmBIF_IH_DOORBELL_RANGE 0x01D2 idx2 (nbio_2_3_offset.h) -> (0xD20+0x1D2)*4;
 *  OFFSET [11:2], SIZE [20:16] (nbio_2_3_sh_mask.h); nbio_v2_3_ih_doorbell_range
 *  usa SIZE=2. IH_DOORBELL_RPTR: OFFSET [25:0], ENABLE bit 28
 *  (osssys_5_0_0_sh_mask.h). Indice: AMDGPU_NAVI10_DOORBELL_IH 0x178 << 1 =
 *  0x2F0 dwords (amdgpu_doorbell.h, navi10_ih.c:573) -> byte 0xBC0 da BAR de
 *  doorbell; WDOORBELL32 (navi10_ih_set_rptr). A do SDMA0 fica em 0x800.
 */
#define BC250_NBIO_REG_BIF_IH_DOORBELL_RANGE 0x03BC8u
#define BC250_P33_IH_DOORBELL_INDEX          0x2F0u
#define BC250_P33_IH_DOORBELL_BYTE_OFFSET    (BC250_P33_IH_DOORBELL_INDEX * 4u)
#define BC250_P33_IH_DOORBELL_RANGE_SIZE     2u
#define BC250_IH_DOORBELL_RPTR_OFFSET_MASK   0x03FFFFFFu
#define BC250_IH_DOORBELL_RPTR_ENABLE        0x10000000u

#define BC250_IH_RB_CNTL_RB_ENABLE              0x00000001u
#define BC250_IH_RB_CNTL_RB_SIZE_SHIFT          1u
#define BC250_IH_RB_CNTL_RB_SIZE_MASK           0x0000003Eu
#define BC250_IH_RB_CNTL_RB_GPU_TS_ENABLE       0x00000080u
#define BC250_IH_RB_CNTL_WPTR_WRITEBACK_ENABLE  0x00000100u
#define BC250_IH_RB_CNTL_WPTR_OVERFLOW_ENABLE   0x00010000u
#define BC250_IH_RB_CNTL_ENABLE_INTR            0x00020000u
#define BC250_IH_RB_CNTL_MC_SNOOP               0x00100000u
#define BC250_IH_RB_CNTL_RPTR_REARM             0x00200000u
#define BC250_IH_RB_CNTL_MC_RO                  0x00400000u
#define BC250_IH_RB_CNTL_MC_VMID_MASK           0x0F000000u
#define BC250_IH_RB_CNTL_MC_SPACE_SHIFT         28u
#define BC250_IH_RB_CNTL_MC_SPACE_MASK          0x70000000u
#define BC250_IH_RB_CNTL_WPTR_OVERFLOW_CLEAR    0x80000000u
#define BC250_IH_RB_WPTR_RB_OVERFLOW            0x00000001u
#define BC250_INTERRUPT_CNTL_IH_DUMMY_RD_OVERRIDE 0x00000001u
#define BC250_INTERRUPT_CNTL_IH_REQ_NONSNOOP_EN   0x00000008u
#define BC250_SDMA0_CNTL_TRAP_ENABLE            0x00000001u

/* soc15_ih_clientid.h e irqsrcs (P1). */
#define BC250_IH_CLIENT_SDMA0       0x08u
#define BC250_IH_CLIENT_GRBM_CP     0x14u
#define BC250_IH_SRC_SDMA_TRAP      0xE0u
#define BC250_IH_SRC_CP_EOP         0xB5u
/* irqsrcs_gfx_10_1.h (P1): 180 PM4 rsvd bit, 183 bad opcode, 184 priv reg, 185 priv instr. */
#define BC250_IH_SRC_CP_RSVD_BIT    0xB4u
#define BC250_IH_SRC_CP_BAD_OPCODE  0xB7u
#define BC250_IH_SRC_CP_PRIV_REG    0xB8u
#define BC250_IH_SRC_CP_PRIV_INSTR  0xB9u
#define BC250_IH_ENTRY_BYTES        32u

/*
 * Arma o anel 0 do IH (sequencia de navi10_ih_irq_init + nbio_v2_3_ih_control)
 * e o TRAP do SDMA0, e prova: um NOP com TRAP tem de fazer o IH escrever um
 * vetor (SDMA0, 0xE0) no anel pela GART. Falha fechada: sem vetor, desarma tudo.
 * O anel e a pagina de writeback ja estao mapeados na GART pelo chamador.
 */
BOOLEAN Bc250SdmaPresentIhArm(
    _In_ volatile uint32_t *RingCpu,
    _In_ uint64_t RingGpuVa,
    _In_ ULONG RingBytes,
    _In_ volatile uint32_t *WptrCpu,
    _In_ uint64_t WptrGpuVa,
    _In_ uint64_t DummyPagePa,
    _In_ BOOLEAN Msi,
    _In_ BOOLEAN RptrByDoorbell);

/* Desliga o IH e o TRAP e devolve os registradores ao snapshot. */
VOID Bc250SdmaPresentIhDisarm(_In_z_ const char *Why);

/*
 * ISR (DIRQL): consome o anel do IH, conta por fonte, atualiza o RPTR.
 * P3.2: SdmaTrapsThisCall recebe quantos TRAPs do SDMA0 esta chamada consumiu
 * (0 se nenhum), para o chamador enfileirar o DPC de aposentadoria.
 * EOP do CP (doc 618): GfxEopsThisCall recebe quantos CP_EOP do anel grafico
 * (ring_id me 0, pipe 0, como gfx_v10_0_eop_irq) a chamada consumiu.
 */
BOOLEAN Bc250SdmaPresentIhIsr(_Out_opt_ ULONG *SdmaTrapsThisCall,
    _Out_opt_ ULONG *GfxEopsThisCall);

/*
 * P4.2 (doc 622): anexa o SDMA1 ja provado (Bc250Sdma1BringUp) ao envio do
 * Node 0: as faixas de PTE do paging buffer passam a rodar nele. Detach
 * devolve o controle (as faixas voltam a rodar no SDMA0). Halt=TRUE tambem
 * para o F32 e o anel do SDMA1 (TDR do SDMA0: uma espera do SDMA1 pode ter
 * ficado sem quem a libere).
 */
VOID Bc250SdmaPresentSdma1Attach(
    _In_ volatile uint32_t *RingCpu, _In_ uint64_t RingGpu,
    _In_ volatile uint32_t *WorkCpu, _In_ uint64_t WorkGpu,
    _In_ uint32_t CursorDwords);
VOID Bc250SdmaPresentSdma1Detach(_In_z_ const char *Why, _In_ BOOLEAN Halt);

/* Relatorio periodico (DISPATCH): contadores e falha fechada se o ISR nao roda. */
VOID Bc250SdmaPresentIhReport(_In_z_ const char *Why);

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

/*
 * FB.1 (doc 607): Corrida 227 caiu em 0x119/0x9000 no flip MPO3 depois de um
 * Bc250SdmaPresentBlt FALSE, e a linha do motivo nao chegou ao disco. Este
 * registro e' preenchido em TODA saida FALSE do blt, antes do retorno, e fica
 * na memoria do kernel: o MEMORY.DMP o traz (dt bc250mcdm!g_Bc250SdmaLastBltFailure).
 */
#define BC250_SDMA_BLT_FAIL_NOT_READY  1u
#define BC250_SDMA_BLT_FAIL_BAD_ARGS   2u
#define BC250_SDMA_BLT_FAIL_EMIT       3u
#define BC250_SDMA_BLT_FAIL_TIMEOUT    4u

typedef struct _BC250_SDMA_BLT_FAILURE {
    ULONG Magic;            /* 0x464C5442 'BTLF' quando preenchido */
    volatile LONG Count;    /* falhas desde o StartDevice */
    ULONG Reason;           /* BC250_SDMA_BLT_FAIL_* */
    ULONG Initialized;
    ULONG HardwareEnabled;
    ULONG ByteCount;
    uint64_t Source;
    uint64_t Dest;
    ULONG Polls;
    uint32_t TargetWptr;
    uint32_t WptrReg;
    uint32_t RptrReg;
    uint32_t RptrWb;
    uint32_t Status;
    uint32_t Status1;
    uint32_t ErrLog;
    uint32_t IbCntl;
    uint32_t IbRptr;
    uint32_t CtxStatus;
    uint32_t FenceExp;
    uint32_t FenceObs;
    uint32_t InFlight;
    ULONG DoorbellActive;
    ULONG VramViaDram;
    LONG BltSubmitted;
    LONG BltCompleted;
    LONGLONG Qpc;
} BC250_SDMA_BLT_FAILURE;

extern BC250_SDMA_BLT_FAILURE g_Bc250SdmaLastBltFailure;

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

/* COMP.VA (doc 743): copia pela janela da GART, com invalidacao da VMID 0 antes. */
BOOLEAN Bc250SdmaPresentBltViaGartWindow(
    _In_ uint64_t SourceGpuAddress,
    _In_ ULONG SourcePitchBytes,
    _In_ uint64_t DestGpuAddress,
    _In_ ULONG DestPitchBytes,
    _In_ ULONG DestX,
    _In_ ULONG DestY,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG LinearBytes,
    _In_ BOOLEAN FlushGart); /* COMP.VA.3: FALSE quando a janela nao mudou */

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

/*
 * 4c etapa 1 (doc 567) -- ensaio de pacotes de memoria virtual no boot.
 *
 * O SDMA0 executa, em ordem: WRITE_LINEAR de 16 dwords "tipo PTE" numa area
 * de rascunho do Work, a invalidacao de TLB de UMA VMID pelo SDMA
 * (SRBM_WRITE do REQ + POLL_REGMEM de um ciclo + POLL_REGMEM do ACK, engine
 * do SDMA) e a fence. UseIndirect=1 poe o corpo DENTRO de um IB (VMID 0, IB
 * no Work), que e' onde os pacotes moram em runtime -- o Linux so' emite a
 * invalidacao no anel, entao SRBM_WRITE dentro de IB e' o que este ensaio
 * mede. Nao muda NENHUM caminho de runtime. Timeout -> quiesce + restart do
 * TDR (Lei 5) e Recovered diz se o anel voltou.
 */
#define BC250_SDMA_PROBE_SCRATCH_OFFSET 0xA00u
#define BC250_SDMA_PROBE_IB_OFFSET      0xC00u
#define BC250_SDMA_PROBE_DATA_DWORDS    16u
#define BC250_SDMA_PROBE_POISON         0x7A11DEADu

typedef struct _BC250_SDMA_VMFLUSH_PROBE_RESULT {
    uint32_t UseIndirect;
    uint32_t Engine;
    uint32_t Vmid;
    uint32_t BodyDwords;
    uint32_t EncodeStatus;
    uint32_t RbCntl;
    uint32_t IbCntl;
    uint32_t TargetWptr;
    uint32_t RptrRegister;
    uint32_t RptrWriteback;
    uint32_t RptrMatched;
    uint32_t FenceExpected;
    uint32_t FenceObserved;
    uint32_t DataMismatches;
    uint32_t Polls;
    uint32_t TimedOut;
    uint32_t StatusReg;
    uint32_t Status1Reg;
    uint32_t ErrorLog;
    uint32_t Recovered;
} BC250_SDMA_VMFLUSH_PROBE_RESULT;

BOOLEAN Bc250SdmaPresentRunVmFlushProbe(
    _In_ uint32_t UseIndirect,
    _In_ uint32_t Engine,
    _In_ uint32_t Vmid,
    _Out_ BC250_SDMA_VMFLUSH_PROBE_RESULT *Result);

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
VOID Bc250SdmaPresentFlushHdp(void);
#else
static __inline VOID Bc250SdmaPresentFlushHdp(void) { }
#endif

/*
 * C1 (doc 672): a abertura de doorbell mapeada pela P2 serve tambem ao anel
 * GFX (CP_RB0), como no amdgpu, onde todos os aneis usam a mesma BAR. Ready
 * exige o mapeamento e o RCC_DOORBELL_APER_EN do BIF ligado (P2c). Write64
 * recusa offset desalinhado ou fora do mapa (0x1000 bytes).
 */
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
BOOLEAN Bc250SdmaPresentDoorbellApertureReady(void);
BOOLEAN Bc250SdmaPresentDoorbellWrite64(_In_ uint32_t ByteOffset, _In_ uint64_t Value);
#else
static __inline BOOLEAN Bc250SdmaPresentDoorbellApertureReady(void) { return FALSE; }
static __inline BOOLEAN Bc250SdmaPresentDoorbellWrite64(uint32_t ByteOffset, uint64_t Value)
{
    (void)ByteOffset;
    (void)Value;
    return FALSE;
}
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
    _In_ uint32_t TlbFlushVmidMask,
    _In_reads_opt_(SplitCount) const BC250_SDMA_IB_SPLIT *Splits,
    _In_ uint32_t SplitCount,
    _Out_ uint32_t *OutAssignedHardwareFence);

/*
 * 4c.4 (doc 575): teto de dwords de UMA entrada de IB dividido no anel. A
 * entrada pode atravessar o fim do anel (como amdgpu_ring_write), entao o
 * limite e' o anel menos a folga, nao a metade dele.
 */
#define BC250_SDMA_SPLIT_ENTRY_MAX_DWORDS 960u

/*
 * 4c.2 (doc 569): teto de dwords da invalidacao no anel = 15 VMIDs (1..15)
 * x 15 dwords. Espaco do anel e' conferido contra o RPTR do HARDWARE antes de
 * escrever, porque estes pacotes sao dez vezes maiores que uma entrada comum
 * e a fila de 32 slots nao garante mais, sozinha, que o anel nao transborda.
 */
#define BC250_SDMA_RING_TLB_FLUSH_MAX_DWORDS (15u * BC250_SDMA_DWORDS_VM_INVALIDATE)
#define BC250_SDMA_RING_SPACE_MAX_WAIT_POLLS 100000u /* x 10 us = 1 s */

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
    /* P4.2 (doc 622): SDMA1 desanexado e parado no quiesce, e a troca. */
    uint32_t Sdma1Detached;
    uint32_t Sdma1Go;
    uint32_t Sdma1Done;
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
 * Corrida 215: restart do TDR que, com o anel na RAM pela GART, volta para a
 * VRAM se a prova falhar (gancho desliga a GART antes) e prova de novo, em vez
 * de devolver falha e deixar o dxgkrnl derrubar a maquina. A RAM fica retida.
 */
BOOLEAN Bc250SdmaPresentRestartAfterTdrOrRevert(void);

/*
 * P2 (doc 598): mapeia uma pagina da BAR de doorbell, arma a doorbell do SDMA0
 * (SDMA0_GFX_DOORBELL, DOORBELL_OFFSET e BIF_SDMA0_DOORBELL_RANGE) e prova com
 * NOP + fence. PASSIVE_LEVEL, anel ocioso. Falha = desarma, reconstroi o anel
 * pelo restart e segue com o WPTR por MMIO (declarado no log).
 */
NTSTATUS Bc250SdmaPresentEnableDoorbell(
    _In_ PHYSICAL_ADDRESS DoorbellBarPhysical,
    _In_ uint64_t DoorbellBarLength);

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

/*
 * D2 por GART (doc 587). O chamador aloca a RAM (AllocateSystemRingWork),
 * mapeia as duas paginas na GART da VMID 0 e passa o MC da GART. A relocacao
 * prova pelo restart do TDR (readback + NOP com fence sobre poison na RAM).
 * Se falhar: chama Hook(ROLLBACK) para o chamador desligar a GART, volta o
 * anel para a VRAM e prova de novo. No teardown, Hook(TEARDOWN) roda depois
 * de a engine soltar a RAM e antes de libera-la.
 * Posse da memoria: NOT_READY e UNSUCCESSFUL liberam; SUCCESS e
 * DEVICE_HARDWARE_ERROR ficam com ela (a engine pode apontar para la).
 *   STATUS_SUCCESS             -> anel na RAM, provado
 *   STATUS_UNSUCCESSFUL        -> D2 nao ligou, anel de volta na VRAM e provado
 *   STATUS_DEVICE_NOT_READY    -> preflight recusou, nada mudou
 *   STATUS_DEVICE_HARDWARE_ERROR -> rollback falhou, SDMA0 indisponivel
 */
#define BC250_SDMA_RING_HOOK_ROLLBACK 1u
#define BC250_SDMA_RING_HOOK_TEARDOWN 2u
/* Corrida 215: so' fotografa (GART e tabela), sem mudar estado; vale em DISPATCH. */
#define BC250_SDMA_RING_HOOK_SNAPSHOT 3u
typedef VOID (*BC250_SDMA_RING_HOOK)(PVOID Context, uint32_t Reason);

NTSTATUS Bc250SdmaPresentAllocateSystemRingWork(
    _Out_ PVOID *Memory,
    _Out_ PHYSICAL_ADDRESS *Physical);

/* Libera RAM de AllocateSystemRingWork que nunca chegou a engine. */
VOID Bc250SdmaPresentFreeSystemRingWork(_In_opt_ PVOID Memory);

NTSTATUS Bc250SdmaPresentRelocateRingToSystemMemory(
    _In_ PVOID Memory,
    _In_ PHYSICAL_ADDRESS Physical,
    _In_ uint64_t RingMc,
    _In_opt_ BC250_SDMA_RING_HOOK Hook,
    _In_opt_ PVOID HookContext);

void Bc250SdmaPresentTeardown(void);

#ifdef __cplusplus
}
#endif

#endif /* BC250_SDMA_PRESENT_H */
