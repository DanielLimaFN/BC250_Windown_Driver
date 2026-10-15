#include "bc250_sdma_present.h"
#include "bc250_mmio_breadcrumb.h"
#include "bc250_probe_telemetry.h"
#include "bc250_display_ddi.h"
#include "../core/bc250_sdma_queue_state_contract.h"
#include "../core/bc250_segment_address_contract.h"
#include "../core/bc250_tdr_recovery_contract.h"
#include "../core/bc250_submit_command_contract.h"
#include "bc250_vram_cpu_path.h"
#include "../core/bc250_sdma1_bringup.h"
#include "bc250_mcdm_callbacks.h"

BC250_SDMA_PRESENT_CONTEXT g_Bc250SdmaPresent = { 0 };
BC250_SDMA_BLT_FAILURE g_Bc250SdmaLastBltFailure = { 0 };

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
/*
 * FB.2 (doc 607): a fence dos caminhos SINCRONOS (blt do flip, blt/rect da
 * composicao) mora num unico endereco (BC250_SDMA_PRESENT_FENCE_OFFSET). Corrida
 * 227, extraido do dump: FenceExp=0x2CE2 FenceObs=0x2CE3 com RPTR==WPTR e o SDMA0
 * ocioso -- a copia do flip TERMINOU e a composicao seguinte sobrescreveu o
 * endereco com a fence dela; a igualdade exata nunca casou, deu timeout e o
 * fail-closed do MPO3 virou 0x119. Como o anel executa em ordem e a fence e'
 * numerada dentro do RingLock (ordem de fence == ordem no anel), fence posterior
 * prova que a nossa passou. Poison e' "ainda nao". Diferenca com sinal cobre a
 * volta do contador de 32 bits.
 */
static __inline BOOLEAN Bc250SdmaSyncFenceReached(uint32_t Observed, uint32_t Expected)
{
    if (Observed == BC250_SDMA_PRESENT_FENCE_POISON) {
        return FALSE;
    }
    return ((LONG)(Observed - Expected) >= 0) ? TRUE : FALSE;
}

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
    uint32_t value;
    ULONG crumb;
    if (Context->MmioVirtual == NULL || RegOffset >= Context->MmioLength) {
        return 0u;
    }
    crumb = Bc250CrumbBefore(BC250_CRUMB_SITE_SDMA_READ, RegOffset);
    value = *(volatile uint32_t *)(Context->MmioVirtual + RegOffset);
    Bc250CrumbAfter(crumb);
    return value;
}

static inline void Bc250SdmaRegWrite(
    _In_ BC250_SDMA_PRESENT_CONTEXT *Context,
    _In_ ULONG RegOffset,
    _In_ uint32_t Value)
{
    if (Context->MmioVirtual != NULL && RegOffset < Context->MmioLength) {
        ULONG crumb = Bc250CrumbBefore(BC250_CRUMB_SITE_SDMA_WRITE, RegOffset);
        *(volatile uint32_t *)(Context->MmioVirtual + RegOffset) = Value;
        Bc250CrumbAfter(crumb);
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
/*
 * DX.96-HDP (28/09, auditoria doc 509): a sequencia antiga tambem alternava
 * HDP_MISC_CNTL.FLUSH_INVALIDATE_CACHE (1 e depois 0) a CADA chamada -- em
 * todo submit, fill, blit e em cada verificacao de conclusao sem fence visivel
 * (centenas por segundo depois da FPS.3b) -- e terminava com o bit DESLIGADO.
 * Upstream (hdp_v5_0.c, P1): HDP_MISC_CNTL e' escrito UMA vez, na init
 * (hdp_v5_0_init_registers liga FLUSH_INVALIDATE_CACHE, bit 0, mascara 0x1 em
 * hdp_5_0_0_sh_mask.h); em tempo de execucao so' flush + invalidacao com
 * leitura de volta. bc250_submission_engine.c ja' evitava tocar HDP_MISC em
 * submissao ativa por um bloqueio medido do MEC2 (CP_CPC_STALLED_STAT1 =
 * 0x00040000). Agora esta rotina NAO toca HDP_MISC; a init faz a escrita
 * unica em Bc250SdmaPresentHdpInitOnce. NAO VALIDADO EM HARDWARE.
 */
/*
 * HDP.1 / T1 (28/09, doc 519): O FLUSH ERA NO REGISTRADOR ERRADO.
 *
 * A primeira escrita desta rotina ia para 0x4A80 com valor 0 -- que NAO e'
 * flush de HDP, e' SDMA0_UTCL1_INV0 (P1, ../core/bc250_hdp_contract.h). Ou
 * seja: em todo laco de espera, em varias CPUs, zeravamos o controle de
 * invalidacao da UTCL1 do SDMA, e o flush de escrita do HDP nunca acontecia.
 *
 * Agora, como amdgpu_hdp_generic_flush + hdp_v5_0_invalidate_hdp (P1):
 *   1. barreira de CPU (mb() do upstream): as escritas da CPU na VRAM pela
 *      BAR saem dos buffers de write-combining antes do flush;
 *   2. BIF HDP_MEM_COHERENCY_FLUSH_CNTL = 0 (0x385C) -- flush de ESCRITA;
 *   3. leitura de RCC_CONFIG_MEMSIZE (0x378C) -- confirma a chegada, como o
 *      get_memsize do upstream. O valor tem de ser o mesmo sempre: se mudar,
 *      a leitura nao foi ao NBIO e a divergencia e' contada e declarada;
 *   4. HDP_READ_CACHE_INVALIDATE = 1 (0x3FC4) + leitura, como antes.
 * 0x4A80 nao e' mais escrito por ninguem; e' so' LIDO, de forma amostrada,
 * para medir o valor real de UTCL1_INV0 no silicio. NAO VALIDADO EM HARDWARE.
 */
static volatile LONG64 g_Bc250HdpFlushCount = 0;
static volatile LONG g_Bc250HdpMemsizeRef = 0;
static volatile LONG g_Bc250HdpMemsizeMismatch = 0;

static void Bc250SdmaPresentHdp(
    _In_ BC250_SDMA_PRESENT_CONTEXT *Context)
{
    uint32_t memsize;
    LONG64 seq;

    if (!Bc250VramCpuHdpNeeded()) {
        /* P1b: CPU na VRAM pela DRAM; amdgpu_device_flush_hdp e
         * amdgpu_device_invalidate_hdp retornam cedo em APU. */
        _mm_mfence();
        return;
    }
    _mm_mfence(); /* mb() do upstream: drena write-combining antes do flush */
    Bc250SdmaRegWrite(Context,
        BC250_HDP_REG_MEM_COHERENCY_FLUSH_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        BC250_HDP_MEM_FLUSH_VALUE);
    memsize = Bc250SdmaRegRead(Context,
        BC250_HDP_REG_FLUSH_POSTING_READ - BC250_SDMA0_MMIO_WINDOW_START);

    Bc250SdmaRegWrite(Context,
        BC250_SDMA0_REG_HDP_INVALIDATE - BC250_SDMA0_MMIO_WINDOW_START, 1u);
    (void)Bc250SdmaRegRead(Context,
        BC250_SDMA0_REG_HDP_INVALIDATE - BC250_SDMA0_MMIO_WINDOW_START);

    seq = InterlockedIncrement64(&g_Bc250HdpFlushCount);
    if (InterlockedCompareExchange(&g_Bc250HdpMemsizeRef, (LONG)memsize, 0) != 0 &&
        (uint32_t)g_Bc250HdpMemsizeRef != memsize) {
        LONG mismatches = InterlockedIncrement(&g_Bc250HdpMemsizeMismatch);
        if (mismatches <= 8 || (mismatches % 4096) == 0) {
            Bc250DisplayLog("[HDP1_FLUSH] DIVERGENCIA #%ld: leitura de confirmacao RCC_CONFIG_MEMSIZE=0x%08X, referencia=0x%08X -- a leitura nao chegou ao NBIO como esperado\n",
                mismatches, memsize, (uint32_t)g_Bc250HdpMemsizeRef);
        }
    }
    if (seq <= 4 || (seq % 65536) == 0) {
        uint32_t inv0 = Bc250SdmaRegRead(Context,
            BC250_SDMA0_REG_UTCL1_INV0 - BC250_SDMA0_MMIO_WINDOW_START);
        Bc250DisplayLog("[HDP1_FLUSH] #%lld flush real 0x385C=0 + leitura 0x378C=0x%08X (ref=0x%08X divergencias=%ld) | UTCL1_INV0=0x%08X (somente leitura)\n",
            (long long)seq, memsize, (uint32_t)g_Bc250HdpMemsizeRef,
            (LONG)g_Bc250HdpMemsizeMismatch, inv0);
    }
}

/*
 * P2 (doc 598): publicacao do WPTR do SDMA0, fonte unica. Com a doorbell
 * provada faz o que sdma_v5_0_ring_set_wptr faz -- shadow do WPTR na memoria
 * e WDOORBELL64 com o WPTR em bytes, sem registrador. Sem ela, o caminho
 * antigo por MMIO (MINOR_PTR, RB_WPTR, RB_WPTR_HI, MINOR_PTR).
 */
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
static void Bc250SdmaPresentP2Disarm(_In_z_ const char *Why);
#endif

static void Bc250SdmaPresentPublishWptr(_In_ uint32_t WptrBytes)
{
    if (g_Bc250SdmaPresent.DoorbellActive &&
        g_Bc250SdmaPresent.DoorbellVirtual != NULL) {
        g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] = WptrBytes;
        KeMemoryBarrier();
        *(volatile uint64_t *)(g_Bc250SdmaPresent.DoorbellVirtual +
            BC250_P2_SDMA0_DOORBELL_BYTE_OFFSET) = (uint64_t)WptrBytes;
        KeMemoryBarrier();
        InterlockedIncrement64(&g_Bc250SdmaPresent.DoorbellKicks);
        return;
    }
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 1u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START, WptrBytes);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_MINOR_PTR - BC250_SDMA0_MMIO_WINDOW_START, 0u);
}

/*
 * X1 (29/09, doc 522): sondagens decididas so' pelo registrador RPTR (sem HDP
 * e sem ler VRAM) e leituras de fence feitas depois que o RPTR passou.
 */
static volatile LONG64 g_Bc250X1GatedPolls = 0;
static volatile LONG64 g_Bc250X1FenceReads = 0;
/* X3 (doc 524): leituras de canario feitas (anel ocioso) e adiadas (SDMA ocupado). */
static volatile LONG64 g_Bc250X3CanaryRead = 0;
static volatile LONG64 g_Bc250X3CanarySkipped = 0;

/*
 * X4 (29/09, doc 525): SEM CANARIOS NO CAMINHO DE EXECUCAO.
 *
 * Corridas 170 e 171 (P0): a maquina parou com a CPU00 DENTRO da leitura dos
 * canarios (migalha VRAM_CANARY_READ fase ANTES, VRAM 0xF280000 e 0xF3CC000),
 * lendo pela janela WC, logo depois do flush/invalidacao do HDP, o destino que
 * um FILL de paginacao acabara de escrever -- na 171 com o SDMA ja' OCIOSO
 * (o portao do X3 nunca agiu). Cada alocacao nova gera um FILL com canarios,
 * e a partida e a abertura do menu sao as fases de mais alocacoes.
 *
 * Agora os IBs submetidos em execucao nao plantam nem leem canarios: a prova
 * fica com DUAS pernas (RPTR e fence gravada pela GPU sobre poison) e a de
 * dados e' declarada NAO CONFERIDA -- nunca simulada (Leis 2 e 4). O tripe
 * completo continua no ensaio de boot. NAO VALIDADO EM HARDWARE.
 */
static volatile LONG64 g_Bc250X4CanaryDisabled = 0;

static VOID Bc250X4DisableRuntimeCanaries(
    _Inout_ BC250_DATA_PROOF_PLAN *Plan,
    _In_z_ const char *Path)
{
    LONG64 count;
    if (Plan == NULL || Plan->Usable == 0u) {
        return;
    }
    Plan->Usable = 0u;
    count = InterlockedIncrement64(&g_Bc250X4CanaryDisabled);
    if (count <= 4 || (count % 4096) == 0) {
        Bc250DisplayLog("[X4_CANARY] #%lld %s: canarios NAO plantados nem lidos em execucao -- perna de dados NAO CONFERIDA (prova de duas pernas: RPTR + fence da GPU)\n",
            (long long)count, Path);
    }
}

/*
 * X1: portao dos lacos SINCRONOS (blts do Present). Com o RingLock seguro
 * nada e' acrescentado ao anel, entao o ultimo WPTR publicado e' o proprio
 * alvo. Enquanto devolver FALSE, o laco NAO toca HDP nem le VRAM.
 */
static BOOLEAN Bc250SdmaSyncEntryConsumed(
    _In_ uint32_t StartBytes,
    _In_ uint32_t TargetBytes)
{
    uint32_t rptr = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
    uint32_t tailBytes = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
    if (Bc250TripodRingEntryConsumed(rptr, StartBytes, TargetBytes,
            tailBytes, (uint32_t)BC250_SDMA_PRESENT_RING_BYTES) == 0u) {
        InterlockedIncrement64(&g_Bc250X1GatedPolls);
        return FALSE;
    }
    return TRUE;
}

/* X1: leitura da fence na VRAM, so' depois do portao, com HDP e migalha. */
static uint32_t Bc250SdmaReadFenceAfterConsumed(_In_ ULONG FenceOffset)
{
    uint32_t value;
    ULONG crumb;
    InterlockedIncrement64(&g_Bc250X1FenceReads);
    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    crumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_FENCE_READ, FenceOffset);
    value = g_Bc250SdmaPresent.Work[FenceOffset / 4u];
    Bc250CrumbAfter(crumb);
    return value;
}

/*
 * DX.96-HDP: escrita UNICA de HDP_MISC_CNTL, na init, como o upstream.
 * Formula SOC15 (Lei 6): (lido & ~mask) | (or & mask), mask = bit 0.
 * Registra o valor encontrado (o que o POST ou o bring-up deixou) e o lido de
 * volta: a proxima corrida diz se o bit estava desligado pelo toggle antigo.
 */
#define BC250_HDP_MISC_FLUSH_INVALIDATE_CACHE_MASK 0x00000001u
static void Bc250SdmaPresentHdpInitOnce(
    _In_ BC250_SDMA_PRESENT_CONTEXT *Context)
{
    uint32_t found;
    uint32_t planned;
    uint32_t readback;
    uint32_t inv0;
    uint32_t memsize;

    /*
     * HDP.1: sonda SOMENTE LEITURA, antes de qualquer flush. UTCL1_INV0 nunca
     * foi medido neste projeto e, ate a 3.310, era zerado em todo laco de
     * espera; neste build ninguem o escreve, entao o valor lido aqui e' o que
     * o POST/firmware deixou. RCC_CONFIG_MEMSIZE e' a referencia da leitura de
     * confirmacao do flush (esperado 0x200 = 512 MiB, medido pelo leitor de
     * topologia).
     */
    inv0 = Bc250SdmaRegRead(Context,
        BC250_SDMA0_REG_UTCL1_INV0 - BC250_SDMA0_MMIO_WINDOW_START);
    memsize = Bc250SdmaRegRead(Context,
        BC250_HDP_REG_FLUSH_POSTING_READ - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250DisplayLog("[HDP1_INIT] UTCL1_INV0(SDMA0 dw 0x40)=0x%08X lido antes de qualquer flush (neste build ninguem o escreve) | RCC_CONFIG_MEMSIZE(0x378C)=0x%08X\n",
        inv0, memsize);

    found = Bc250SdmaRegRead(Context,
        BC250_SDMA0_REG_HDP_MISC - BC250_SDMA0_MMIO_WINDOW_START);
    planned = (found & ~BC250_HDP_MISC_FLUSH_INVALIDATE_CACHE_MASK) |
        (BC250_HDP_MISC_FLUSH_INVALIDATE_CACHE_MASK &
         BC250_HDP_MISC_FLUSH_INVALIDATE_CACHE_MASK);
    if (planned != found) {
        Bc250SdmaRegWrite(Context,
            BC250_SDMA0_REG_HDP_MISC - BC250_SDMA0_MMIO_WINDOW_START, planned);
    }
    readback = Bc250SdmaRegRead(Context,
        BC250_SDMA0_REG_HDP_MISC - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250DisplayLog("[DX96_HDP] HDP_MISC_CNTL init unica: encontrado=0x%08X planejado=0x%08X lido=0x%08X escrito=%u -- em tempo de execucao HDP_MISC nao e' mais tocado\n",
        found, planned, readback, (planned != found) ? 1u : 0u);
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
    /*
     * T3.1: o VidMm mapeia o segmento WC; a faixa privada UC comeca em
     * BC250_VRAM_WC_WINDOW_BYTES. Segmento que passe desse limite poria
     * alocacoes WC do VidMm em cima das estruturas UC do driver.
     */
    if (LocalSegmentBaseOffset + LocalSegmentBytes > BC250_VRAM_WC_WINDOW_BYTES) {
        Bc250DisplayLog("[T31_CACHE_MAPA] DIVERGENCIA: segmento [0x%llX,0x%llX) passa do inicio da faixa privada UC 0x%llX -- aliasing WC/UC possivel\n",
            (unsigned long long)LocalSegmentBaseOffset,
            (unsigned long long)(LocalSegmentBaseOffset + LocalSegmentBytes),
            (unsigned long long)BC250_VRAM_WC_WINDOW_BYTES);
    } else {
        Bc250DisplayLog("[T31_CACHE_MAPA] segmento [0x%llX,0x%llX) inteiro dentro da janela WC [0x0,0x%llX)\n",
            (unsigned long long)LocalSegmentBaseOffset,
            (unsigned long long)(LocalSegmentBaseOffset + LocalSegmentBytes),
            (unsigned long long)BC250_VRAM_WC_WINDOW_BYTES);
    }
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

/*
 * CG.1 (doc 557): o clock do SDMA0 e' forcado ligado (SOFT_OVERRIDE0..7 = 1)
 * para programar o anel (29/08: EX_IDLE_POLL_TIMER_EXPIRE persistente com o
 * motor possivelmente sem clock). Esse force NUNCA era desfeito, e a Corrida
 * 200 mediu SDMA0_CLK_CTRL=0xFF0C0100 o tempo todo, contra 0x000C0100 no
 * SDMA1 intocado (padrao do POST). O amdgpu nao escreve este registrador na
 * SDMA 5.0.1 (sdma_v5_0_set_clockgating_state so' trata 5.0.0/5.0.2/5.0.5;
 * nv.c: cg_flags = pg_flags = 0 para GC 10.1.3), e o Linux roda com o MGCG do
 * POST sem travar (doc 552). Aqui, depois do anel PROVADO, so' os bits de
 * override voltam ao valor de entrada (Lei 6), com releitura.
 */
#ifndef BC250_SDMA0_RESTORE_POST_CLOCK_GATING
#define BC250_SDMA0_RESTORE_POST_CLOCK_GATING 1
#endif
#define BC250_SDMA0_CLK_SOFT_OVERRIDE_MASK 0xFF000000u

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
static VOID Bc250SdmaPresentRestorePostClockGating(_In_z_ const char *Where)
{
#if BC250_SDMA0_RESTORE_POST_CLOCK_GATING
    uint32_t before;
    uint32_t planned;
    uint32_t after;
    if (!g_Bc250SdmaPresent.ClockCtrlEntryCaptured) {
        Bc250DisplayLog("[SDMA_CG] %s: valor de entrada do SDMA0_CLK_CTRL nao capturado; overrides MANTIDOS\n", Where);
        return;
    }
    before = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_MMIO_WINDOW_START);
    planned = (before & ~BC250_SDMA0_CLK_SOFT_OVERRIDE_MASK) |
        (g_Bc250SdmaPresent.ClockCtrlAtEntry & BC250_SDMA0_CLK_SOFT_OVERRIDE_MASK);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_MMIO_WINDOW_START, planned);
    after = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250DisplayLog("[SDMA_CG] %s: SDMA0_CLK_CTRL entrada(POST)=0x%08X antes=0x%08X planejado=0x%08X lido=0x%08X -> %s\n",
        Where, g_Bc250SdmaPresent.ClockCtrlAtEntry, before, planned, after,
        ((after & BC250_SDMA0_CLK_SOFT_OVERRIDE_MASK) ==
            (planned & BC250_SDMA0_CLK_SOFT_OVERRIDE_MASK)) ?
            "OVERRIDES DO POST RESTAURADOS (MGCG como no Linux)" :
            "RELEITURA DIVERGE");
#else
    Bc250DisplayLog("[SDMA_CG] %s: restauracao desligada (BC250_SDMA0_RESTORE_POST_CLOCK_GATING=0)\n", Where);
#endif
}
#endif

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
    Bc250LogUcVramMapping("SDMA_PRESENT_RING_WORK",
        BC250_SDMA_LEGACY_RING_OFFSET, g_Bc250SdmaPresent.VramLength);

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
    Bc250SdmaPresentHdpInitOnce(&g_Bc250SdmaPresent);
    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);

    /* Des-gating de clock ANTES de qualquer configuracao (bc250_sdma_ring_work.c:323-330) */
    clk = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_MMIO_WINDOW_START);
    /* CG.1: o primeiro valor lido e' o do POST; inits seguintes nao sobrescrevem. */
    if (!g_Bc250SdmaPresent.ClockCtrlEntryCaptured) {
        g_Bc250SdmaPresent.ClockCtrlAtEntry = clk;
        g_Bc250SdmaPresent.ClockCtrlEntryCaptured = TRUE;
    }
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
    /* P3.1: o soft-reset devolve o CNTL ao padrao; o TRAP volta com o IH armado. */
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        value | F_CNTL_UTC_L1 | (g_Bc250SdmaPresent.TrapArmed ? BC250_SDMA0_CNTL_TRAP_ENABLE : 0u));

    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_UTCL1_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_UTCL1_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        (value & ~(0x00000E00u | 0x0000003Eu | BC250_SDMA0_UTCL1_CNTL_FORCE_INVALIDATION)) | (3u << 9) | (9u << 1));

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
            /* CG.1: so' depois do anel provado com o clock forcado. */
            Bc250SdmaPresentRestorePostClockGating("INIT");
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

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
/*
 * FB.1 (doc 607): registra a saida FALSE do blt na memoria do kernel antes do
 * retorno. Sem I/O e sem log: o MEMORY.DMP de um 0x119 subsequente o carrega.
 */
static VOID Bc250SdmaRecordBltFailure(
    _In_ ULONG Reason,
    _In_ uint64_t Source,
    _In_ uint64_t Dest,
    _In_ ULONG ByteCount,
    _In_ ULONG Polls,
    _In_ uint32_t TargetWptr,
    _In_ uint32_t FenceExp)
{
    BC250_SDMA_BLT_FAILURE *f = &g_Bc250SdmaLastBltFailure;
    f->Magic = 0u;
    KeMemoryBarrier();
    f->Reason = Reason;
    f->Initialized = g_Bc250SdmaPresent.Initialized ? 1u : 0u;
    f->HardwareEnabled = g_Bc250SdmaPresent.HardwareEnabled ? 1u : 0u;
    f->ByteCount = ByteCount;
    f->Source = Source;
    f->Dest = Dest;
    f->Polls = Polls;
    f->TargetWptr = TargetWptr;
    f->FenceExp = FenceExp;
    f->InFlight = g_Bc250SdmaPresent.InFlightCount;
    f->DoorbellActive = g_Bc250SdmaPresent.DoorbellActive ? 1u : 0u;
    f->VramViaDram = (g_Bc250VramCpuViaDram != 0) ? 1u : 0u;
    f->BltSubmitted = g_Bc250SdmaPresent.BltSubmittedCount;
    f->BltCompleted = g_Bc250SdmaPresent.BltCompletedCount;
    f->Qpc = KeQueryPerformanceCounter(NULL).QuadPart;
    if (Reason == BC250_SDMA_BLT_FAIL_TIMEOUT && g_Bc250SdmaPresent.Initialized &&
        g_Bc250SdmaPresent.Work != NULL) {
        f->WptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START);
        f->RptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
        f->RptrWb = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
        f->Status = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
        f->Status1 = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_STATUS1 - BC250_SDMA0_MMIO_WINDOW_START);
        f->ErrLog = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_ERROR_LOG - BC250_SDMA0_MMIO_WINDOW_START);
        f->IbCntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_GFX_IB_CNTL_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        f->IbRptr = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_GFX_IB_RPTR_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        f->CtxStatus = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_GFX_CONTEXT_STATUS_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        f->FenceObs = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
    } else {
        f->WptrReg = f->RptrReg = f->RptrWb = f->Status = f->Status1 = 0u;
        f->ErrLog = f->IbCntl = f->IbRptr = f->CtxStatus = f->FenceObs = 0u;
    }
    InterlockedIncrement(&f->Count);
    KeMemoryBarrier();
    f->Magic = 0x464C5442u;
}
#endif

static BOOLEAN Bc250SdmaPresentBltCore(
    _In_ uint64_t SourceGpuAddress,
    _In_ uint64_t DestGpuAddress,
    _In_ ULONG ByteCount,
    _In_ BOOLEAN FlushGartFirst)
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
    uint32_t x1StartBytes;
    uint32_t x1TargetBytes;
    uint32_t targetWptr = 0u;

    if (!g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled) {
        InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
        Bc250SdmaRecordBltFailure(BC250_SDMA_BLT_FAIL_NOT_READY, SourceGpuAddress,
            DestGpuAddress, ByteCount, 0u, 0u, 0u);
        Bc250DisplayLog("[SdmaPresent] Blt RECUSADO: NOT_READY Initialized=%u HardwareEnabled=%u Bytes=%u\n",
            g_Bc250SdmaPresent.Initialized ? 1u : 0u, g_Bc250SdmaPresent.HardwareEnabled ? 1u : 0u, ByteCount);
        return FALSE;
    }
    if (SourceGpuAddress == 0 || DestGpuAddress == 0 || ByteCount == 0 ||
        ByteCount > (64u * 1024u * 1024u)) {
        InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
        Bc250SdmaRecordBltFailure(BC250_SDMA_BLT_FAIL_BAD_ARGS, SourceGpuAddress,
            DestGpuAddress, ByteCount, 0u, 0u, 0u);
        Bc250DisplayLog("[SdmaPresent] Blt RECUSADO: BAD_ARGS Src=0x%llX Dst=0x%llX Bytes=%u\n",
            SourceGpuAddress, DestGpuAddress, ByteCount);
        return FALSE;
    }

    remainingBytes = ByteCount;
    curSrc = SourceGpuAddress;
    curDst = DestGpuAddress;
    cursor = 0u;
    /* COMP.VA (doc 743): janela da GART regravada pela CPU; invalida a VMID 0
     * no anel, em ordem, antes da copia que a usa. */
    if (FlushGartFirst &&
        Bc250SdmaEmitGartInvalidate(packets, 128u, &cursor,
            BC250_SDMA_INVALIDATE_ENGINE) != BC250_SDMA_OK) {
        InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
        Bc250SdmaRecordBltFailure(BC250_SDMA_BLT_FAIL_EMIT, SourceGpuAddress,
            DestGpuAddress, ByteCount, 0u, 0u, 0u);
        return FALSE;
    }

    while (remainingBytes > 0) {
        ULONG chunkSize = (remainingBytes > BC250_SDMA_COPY_MAX_BYTES) ?
            BC250_SDMA_COPY_MAX_BYTES : remainingBytes;

        status = Bc250SdmaEmitCopyLinear(packets, 128u, &cursor,
            curSrc, curDst, chunkSize);
        if (status != BC250_SDMA_OK) {
            InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
            Bc250SdmaRecordBltFailure(BC250_SDMA_BLT_FAIL_EMIT, SourceGpuAddress,
                DestGpuAddress, ByteCount, 0u, 0u, 0u);
            Bc250DisplayLog("[SdmaPresent] Blt RECUSADO: EMIT status=%d Bytes=%u\n", (int)status, ByteCount);
            return FALSE;
        }
        remainingBytes -= chunkSize;
        curSrc += chunkSize;
        curDst += chunkSize;
    }

    Bc250SdmaEmitGcr(packets, 128u, &cursor);

    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);

    /* FB.2: fence numerada DENTRO do RingLock -- ordem de fence == ordem no anel. */
    nextFence = (uint32_t)InterlockedIncrement(&g_Bc250SdmaPresent.NextFenceSequence);
    Bc250SdmaEmitFence(packets, 128u, &cursor,
        g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_FENCE_OFFSET,
        nextFence);

    /* Cada submissao precisa substituir poison; fence anterior nao e' prova. */
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u] =
        BC250_SDMA_PRESENT_FENCE_POISON;
    KeMemoryBarrier();

    startCursor = Bc250SdmaPresentAllocateRingSlot(cursor);

    {
        /* X2a (doc 523): pacotes no anel, escritas UC na VRAM. */
        ULONG ringWriteCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_RING_WRITE, startCursor);
        for (i = 0u; i < cursor; ++i) {
            g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
        }
        Bc250CrumbAfter(ringWriteCrumb);
    }
    KeMemoryBarrier();

    g_Bc250SdmaPresent.CurrentCursor += cursor;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
    KeMemoryBarrier();

    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    Bc250SdmaPresentPublishWptr((uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t)));

    InterlockedIncrement(&g_Bc250SdmaPresent.BltSubmittedCount);

    /* X1 (doc 522): o laco nao le VRAM nem mexe no HDP ate o RPTR passar. */
    x1StartBytes = startCursor * (uint32_t)sizeof(uint32_t);
    x1TargetBytes = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
    targetWptr = x1TargetBytes;
    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);

    for (poll = 1u; poll <= 10000u; ++poll) {
        uint32_t fenceObserved;
        if (!Bc250SdmaSyncEntryConsumed(x1StartBytes, x1TargetBytes)) {
            KeStallExecutionProcessor(5u);
            continue;
        }
        fenceObserved = Bc250SdmaReadFenceAfterConsumed(BC250_SDMA_PRESENT_FENCE_OFFSET);
        if (Bc250SdmaSyncFenceReached(fenceObserved, nextFence)) {
            uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
            uint32_t rptrWb = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
            /* A15: o helper cobre a volta do anel; o teste cru era redundante
             * e servia de molde para copiar o defeito adiante. */
            BOOLEAN rptrOk = Bc250SdmaRptrMatches(rptrReg, rptrWb, targetWptr);
            if (!rptrOk) {
                uint32_t currentTail = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
                if (Bc250TripodRingEntryConsumed(rptrReg, x1StartBytes, x1TargetBytes,
                        currentTail, (uint32_t)BC250_SDMA_PRESENT_RING_BYTES) != 0u ||
                    Bc250TripodRingEntryConsumed(rptrWb, x1StartBytes, x1TargetBytes,
                        currentTail, (uint32_t)BC250_SDMA_PRESENT_RING_BYTES) != 0u ||
                    Bc250SdmaSyncFenceReached(fenceObserved, nextFence)) {
                    rptrOk = TRUE;
                }
            }
            if (rptrOk) {
                LONG completed;
                g_Bc250SdmaPresent.TripodValidated = TRUE;
                completed = InterlockedIncrement(&g_Bc250SdmaPresent.BltCompletedCount);
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

    Bc250SdmaRecordBltFailure(BC250_SDMA_BLT_FAIL_TIMEOUT, SourceGpuAddress,
        DestGpuAddress, ByteCount, poll, targetWptr, nextFence);
    {
        uint32_t wptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t statusReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
        /*
         * SDMA.4M (Corrida 151): o timeout so' dizia WPTR/RPTR/STATUS, e o RPTR
         * parado ANTES do blt apontava para um IB de paginacao anterior (fill de
         * 4 MiB) sem dizer onde dentro dele. Leituras somente, na janela SDMA0
         * ja' mapeada, com o RingLock ainda seguro (nenhum escritor concorrente).
         */
        uint32_t status1Reg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_STATUS1 - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t errLogReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_ERROR_LOG - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t ibCntlReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_GFX_IB_CNTL_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t ibRptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_GFX_IB_RPTR_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t ibBaseLoReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_GFX_IB_BASE_LO_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t ibSizeReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_GFX_IB_SIZE_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t ctxStatusReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_GFX_CONTEXT_STATUS_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t fenceObs = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
        LONG fallbacks = InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
        if (fallbacks == 1 || (fallbacks % 60) == 0) {
            Bc250DisplayLog("[SdmaPresent] Blt Timeout #%ld: TargetWPTR=%u, WPTR_Reg=%u, RPTR_Reg=%u, STATUS=%08X, FenceExp=%08X, FenceObs=%08X\n",
                fallbacks, targetWptr,
                wptrReg, rptrReg, statusReg, nextFence, fenceObs);
            Bc250DisplayLog("[SdmaPresent] Blt Timeout IB_ESTADO #%ld: STATUS1=%08X ERR_LOG=%08X IB_CNTL=%08X IB_RPTR=%u IB_BASE_LO=%08X IB_SIZE=%u CTX_STATUS=%08X InFlight=%u\n",
                fallbacks, status1Reg, errLogReg, ibCntlReg, ibRptrReg, ibBaseLoReg,
                ibSizeReg, ctxStatusReg, g_Bc250SdmaPresent.InFlightCount);
        }
    }
    return FALSE;
#else
    UNREFERENCED_PARAMETER(FlushGartFirst);
    UNREFERENCED_PARAMETER(SourceGpuAddress);
    UNREFERENCED_PARAMETER(DestGpuAddress);
    UNREFERENCED_PARAMETER(ByteCount);
    return TRUE;
#endif
}

static BOOLEAN Bc250SdmaPresentBltRectCore(
    _In_ uint64_t SourceGpuAddress,
    _In_ ULONG SourcePitchBytes,
    _In_ uint64_t DestGpuAddress,
    _In_ ULONG DestPitchBytes,
    _In_ ULONG DestX,
    _In_ ULONG DestY,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ BOOLEAN FlushGartFirst)
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
    uint32_t x1StartBytes;
    uint32_t x1TargetBytes;
    uint32_t targetWptr = 0u;

    if (!g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled ||
        SourceGpuAddress == 0ull || DestGpuAddress == 0ull ||
        byteCount64 == 0ull || byteCount64 > (64ull * 1024ull * 1024ull)) {
        InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
        return FALSE;
    }
    /* COMP.VA (doc 743): mesma regra do blt linear. */
    if (FlushGartFirst &&
        Bc250SdmaEmitGartInvalidate(packets, 128u, &cursor,
            BC250_SDMA_INVALIDATE_ENGINE) != BC250_SDMA_OK) {
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
    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);
    /* FB.2: fence numerada DENTRO do RingLock -- ordem de fence == ordem no anel. */
    nextFence = (uint32_t)InterlockedIncrement(
        &g_Bc250SdmaPresent.NextFenceSequence);
    if (Bc250SdmaEmitFence(packets, 128u, &cursor,
            g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_FENCE_OFFSET,
            nextFence) != BC250_SDMA_OK) {
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        InterlockedIncrement(&g_Bc250SdmaPresent.FallbackCpuCount);
        return FALSE;
    }
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u] =
        BC250_SDMA_PRESENT_FENCE_POISON;
    KeMemoryBarrier();
    startCursor = Bc250SdmaPresentAllocateRingSlot(cursor);
    {
        /* X2a (doc 523): pacotes no anel, escritas UC na VRAM. */
        ULONG ringWriteCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_RING_WRITE, startCursor);
        for (i = 0u; i < cursor; ++i) {
            g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
        }
        Bc250CrumbAfter(ringWriteCrumb);
    }
    KeMemoryBarrier();
    g_Bc250SdmaPresent.CurrentCursor += cursor;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
    KeMemoryBarrier();

    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    Bc250SdmaPresentPublishWptr((uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t)));
    InterlockedIncrement(&g_Bc250SdmaPresent.BltSubmittedCount);

    /* X1 (doc 522): o laco nao le VRAM nem mexe no HDP ate o RPTR passar. */
    x1StartBytes = startCursor * (uint32_t)sizeof(uint32_t);
    x1TargetBytes = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
    targetWptr = x1TargetBytes;
    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);

    for (poll = 1u; poll <= 10000u; ++poll) {
        uint32_t fenceObserved;
        if (!Bc250SdmaSyncEntryConsumed(x1StartBytes, x1TargetBytes)) {
            KeStallExecutionProcessor(5u);
            continue;
        }
        fenceObserved = Bc250SdmaReadFenceAfterConsumed(BC250_SDMA_PRESENT_FENCE_OFFSET);
        if (Bc250SdmaSyncFenceReached(fenceObserved, nextFence)) {
            uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
                BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
            uint32_t rptrWb =
                g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
            BOOLEAN rptrOk = Bc250SdmaRptrMatches(rptrReg, rptrWb, targetWptr);
            if (!rptrOk) {
                uint32_t currentTail = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
                if (Bc250TripodRingEntryConsumed(rptrReg, x1StartBytes, x1TargetBytes,
                        currentTail, (uint32_t)BC250_SDMA_PRESENT_RING_BYTES) != 0u ||
                    Bc250TripodRingEntryConsumed(rptrWb, x1StartBytes, x1TargetBytes,
                        currentTail, (uint32_t)BC250_SDMA_PRESENT_RING_BYTES) != 0u ||
                    Bc250SdmaSyncFenceReached(fenceObserved, nextFence)) {
                    rptrOk = TRUE;
                }
            }
            if (rptrOk) {
                LONG completed;
                g_Bc250SdmaPresent.TripodValidated = TRUE;
                completed = InterlockedIncrement(
                    &g_Bc250SdmaPresent.BltCompletedCount);
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
        Bc250DisplayLog("[SdmaPresent] BLT_RECT TIMEOUT #%ld: %ux%u srcPitch=%u dstPitch=%u DstXY=%u,%u TargetWPTR=%u WPTR_Reg=%u RPTR_Reg=%u STATUS=%08X FenceExp=%08X FenceObs=%08X\n",
            fallbacks, Width, Height, SourcePitchBytes, DestPitchBytes,
            DestX, DestY,
            targetWptr, wptrReg,
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
    UNREFERENCED_PARAMETER(FlushGartFirst);
    UNREFERENCED_PARAMETER(Width);
    UNREFERENCED_PARAMETER(Height);
    return TRUE;
#endif
}

/*
 * DX.32 -- wrappers publicos: toda escrita de apresentacao no scanout passa por
 * aqui, entao a marca "o scanout contem, por inteiro, a origem X" tem uma fonte
 * so'. Escrita integral bem-sucedida do scanout inteiro a partir do seu inicio
 * grava a marca; qualquer outra escrita que toque o scanout (retangulo, parcial,
 * falha) a apaga. Falha tambem apaga: depois de timeout o estado e' desconhecido.
 */
static BOOLEAN Bc250SdmaPresentTouchesScanout(uint64_t DestGpuAddress)
{
    const uint64_t scanout = g_Bc250SdmaPresent.ScanoutGpuAddress;
    return (scanout != 0ull && DestGpuAddress >= scanout &&
            DestGpuAddress < scanout + (64ull * 1024ull * 1024ull)) ? TRUE : FALSE;
}

BOOLEAN Bc250SdmaPresentBlt(
    _In_ uint64_t SourceGpuAddress,
    _In_ uint64_t DestGpuAddress,
    _In_ ULONG ByteCount)
{
    const BOOLEAN ok = Bc250SdmaPresentBltCore(SourceGpuAddress, DestGpuAddress, ByteCount, FALSE);
    if (Bc250SdmaPresentTouchesScanout(DestGpuAddress)) {
        const BOOLEAN full = (ok && DestGpuAddress == g_Bc250SdmaPresent.ScanoutGpuAddress) ? TRUE : FALSE;
        g_Bc250SdmaPresent.ScanoutLastWriteFull = FALSE;
        KeMemoryBarrier();
        g_Bc250SdmaPresent.ScanoutLastFullSourceMc = full ? SourceGpuAddress : 0ull;
        g_Bc250SdmaPresent.ScanoutLastFullBytes = full ? ByteCount : 0u;
        InterlockedIncrement(&g_Bc250SdmaPresent.ScanoutWriteSequence);
        KeMemoryBarrier();
        g_Bc250SdmaPresent.ScanoutLastWriteFull = full;
    }
    return ok;
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
    const BOOLEAN ok = Bc250SdmaPresentBltRectCore(SourceGpuAddress, SourcePitchBytes,
        DestGpuAddress, DestPitchBytes, DestX, DestY, Width, Height, FALSE);
    if (Bc250SdmaPresentTouchesScanout(DestGpuAddress)) {
        /* Retangulo nunca prova "scanout inteiro = esta origem". */
        g_Bc250SdmaPresent.ScanoutLastWriteFull = FALSE;
        g_Bc250SdmaPresent.ScanoutLastFullSourceMc = 0ull;
        g_Bc250SdmaPresent.ScanoutLastFullBytes = 0u;
        InterlockedIncrement(&g_Bc250SdmaPresent.ScanoutWriteSequence);
    }
    return ok;
}

/*
 * COMP.VA (doc 743): copia por uma janela da GART que a CPU acabou de
 * regravar. Invalida a VMID 0 no anel antes da copia. Linear quando
 * LinearBytes != 0 (pitch igual, linhas inteiras), senao retangulo. Escrita no
 * scanout por aqui nunca prova "scanout inteiro = esta origem".
 */
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
    _In_ BOOLEAN FlushGart)
{
    const BOOLEAN ok = (LinearBytes != 0u) ?
        Bc250SdmaPresentBltCore(SourceGpuAddress, DestGpuAddress, LinearBytes, FlushGart) :
        Bc250SdmaPresentBltRectCore(SourceGpuAddress, SourcePitchBytes,
            DestGpuAddress, DestPitchBytes, DestX, DestY, Width, Height, FlushGart);
    if (Bc250SdmaPresentTouchesScanout(DestGpuAddress)) {
        g_Bc250SdmaPresent.ScanoutLastWriteFull = FALSE;
        g_Bc250SdmaPresent.ScanoutLastFullSourceMc = 0ull;
        g_Bc250SdmaPresent.ScanoutLastFullBytes = 0u;
        InterlockedIncrement(&g_Bc250SdmaPresent.ScanoutWriteSequence);
    }
    return ok;
}

BOOLEAN Bc250SdmaPresentScanoutHoldsFullCopyOf(
    _In_ uint64_t SourceGpuAddress,
    _In_ ULONG ByteCount,
    _Out_ LONG *WriteSequence)
{
    *WriteSequence = g_Bc250SdmaPresent.ScanoutWriteSequence;
    return (g_Bc250SdmaPresent.ScanoutLastWriteFull &&
            g_Bc250SdmaPresent.ScanoutLastFullSourceMc == SourceGpuAddress &&
            SourceGpuAddress != 0ull &&
            g_Bc250SdmaPresent.ScanoutLastFullBytes >= ByteCount) ? TRUE : FALSE;
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
    uint32_t targetWptr = 0u;

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

    {
        /* X2a (doc 523): pacotes no anel, escritas UC na VRAM. */
        ULONG ringWriteCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_RING_WRITE, startCursor);
        for (i = 0u; i < cursor; ++i) {
            g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
        }
        Bc250CrumbAfter(ringWriteCrumb);
    }
    KeMemoryBarrier();

    g_Bc250SdmaPresent.CurrentCursor += cursor;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u] =
        BC250_SDMA_PRESENT_FENCE_POISON;
    KeMemoryBarrier();

    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    Bc250SdmaPresentPublishWptr((uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t)));

    targetWptr = g_Bc250SdmaPresent.CurrentCursor * (uint32_t)sizeof(uint32_t);
    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);

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
            BOOLEAN rptrOk = Bc250SdmaRptrMatches(rptrReg, rptrWb, targetWptr);

            /* Aguarda até 500 µs se o RPTR de writeback/reg ainda estiver em trânsito */
            if (!rptrOk && fenceObserved != nextFence) {
                UINT32 rPoll;
                for (rPoll = 0u; rPoll < 100u && !rptrOk; ++rPoll) {
                    KeStallExecutionProcessor(5u);
                    rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
                    rptrWb = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
                    rptrOk = Bc250SdmaRptrMatches(rptrReg, rptrWb, targetWptr);
                }
            }
            if (!rptrOk && fenceObserved == nextFence) {
                rptrOk = TRUE;
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

/*
 * P3.1 (doc 609): como sdma_v5_0_ring_emit_fence com AMDGPU_FENCE_FLAG_INT --
 * TRAP depois da fence, INT_CONTEXT 0 (fila sem MES). So' com o IH armado.
 */
static VOID Bc250SdmaEmitTrapIfArmed(uint32_t *Packets, uint32_t PacketDwords, uint32_t *Cursor)
{
    if (g_Bc250SdmaPresent.TrapArmed &&
        Bc250SdmaEmitTrap(Packets, PacketDwords, Cursor, 0u) == BC250_SDMA_OK) {
        InterlockedIncrement64(&g_Bc250SdmaPresent.TrapsEmitted);
    }
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
    uint32_t targetWptr = 0u;

    if (!g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled ||
        g_Bc250SdmaPresent.Ring == NULL) {
        return FALSE;
    }

    nextFence = (uint32_t)InterlockedIncrement(&g_Bc250SdmaPresent.NextFenceSequence);
    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);

    /* 1. Emite NOP, GCR Flush e Fence física na VRAM (+ TRAP com a P3.1) */
    packets[cursor++] = BC250_SDMA_OP_NOP;
    Bc250SdmaEmitGcr(packets, 64u, &cursor);
    Bc250SdmaEmitFence(packets, 64u, &cursor,
        g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_FENCE_OFFSET,
        nextFence);
    Bc250SdmaEmitTrapIfArmed(packets, 64u, &cursor);

    /* P3.1: o slot tem o tamanho real (antes era 16 fixo, e o TRAP somaria 2). */
    startCursor = Bc250SdmaPresentAllocateRingSlot(cursor);

    /* 2. Envenena a fence antes de disparar */
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u] =
        BC250_SDMA_PRESENT_FENCE_POISON;
    KeMemoryBarrier();

    /* 3. Copia para o anel */
    {
        /* X2a (doc 523): pacotes no anel, escritas UC na VRAM. */
        ULONG ringWriteCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_RING_WRITE, startCursor);
        for (i = 0u; i < cursor; ++i) {
            g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
        }
        Bc250CrumbAfter(ringWriteCrumb);
    }
    KeMemoryBarrier();

    g_Bc250SdmaPresent.CurrentCursor += cursor;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
    KeMemoryBarrier();

    /* 4. Atualiza WPTR na engine SDMA0 */
    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    Bc250SdmaPresentPublishWptr((uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t)));

    InterlockedIncrement(&g_Bc250SdmaPresent.SubmissionsSubmittedCount);
    targetWptr = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);

    /* 5. Tripé no silício: espera RPTR == WPTR e Fence == nextFence */
    for (poll = 1u; poll <= BC250_SDMA_TRIPOD_MAX_POLLS; ++poll) {
        uint32_t fenceObserved = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
        if (fenceObserved == nextFence) {
            uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
                BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
            uint32_t rptrWb = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
            BOOLEAN rptrOk = Bc250SdmaRptrMatches(rptrReg, rptrWb, targetWptr);
            if (!rptrOk && fenceObserved == nextFence) {
                rptrOk = TRUE;
            }
            if (rptrOk) {
                InterlockedIncrement(&g_Bc250SdmaPresent.SubmissionsCompletedCount);
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
        Bc250DisplayLog("[SdmaPresent] SYNC_NOP TIMEOUT: WPTR=%u RPTR=%u FenceExp=0x%08X FenceObs=0x%08X STATUS=0x%08X\n",
            wptrReg, rptrReg, nextFence, fenceObserved, statusReg);
    }
    return FALSE;
#else
    return TRUE;
#endif
}

BOOLEAN Bc250SdmaPresentRunVmFlushProbe(
    _In_ uint32_t UseIndirect,
    _In_ uint32_t Engine,
    _In_ uint32_t Vmid,
    _Out_ BC250_SDMA_VMFLUSH_PROBE_RESULT *Result)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    uint32_t body[64] = { 0 };
    uint32_t packets[64] = { 0 };
    uint32_t data[BC250_SDMA_PROBE_DATA_DWORDS];
    uint32_t bodyCursor = 0u;
    uint32_t cursor = 0u;
    uint32_t nextFence;
    uint32_t startCursor;
    uint32_t targetWptr;
    uint32_t i;
    ULONG poll;
    KIRQL oldIrql;
    BC250_SDMA_STATUS status;
    const uint32_t scratchIndex = BC250_SDMA_PROBE_SCRATCH_OFFSET / 4u;
    const uint64_t scratchGpu =
        g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PROBE_SCRATCH_OFFSET;
    const uint64_t ibGpu =
        g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PROBE_IB_OFFSET;

    if (Result == NULL) return FALSE;
    RtlZeroMemory(Result, sizeof(*Result));
    Result->UseIndirect = UseIndirect != 0u ? 1u : 0u;
    Result->Engine = Engine;
    Result->Vmid = Vmid;
    if (!g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled ||
        g_Bc250SdmaPresent.Ring == NULL || g_Bc250SdmaPresent.Work == NULL) {
        Bc250DisplayLog("[VMFLUSH_PROBE] RECUSADO: SDMA0 sem anel provado (Initialized=%u HardwareEnabled=%u)\n",
            g_Bc250SdmaPresent.Initialized ? 1u : 0u,
            g_Bc250SdmaPresent.HardwareEnabled ? 1u : 0u);
        return FALSE;
    }

    /* Dados "tipo PTE": distintos por modo e VMID, nunca iguais ao veneno. */
    for (i = 0u; i < BC250_SDMA_PROBE_DATA_DWORDS; ++i) {
        data[i] = 0xC4C00000u | (Result->UseIndirect << 12) |
            ((Vmid & 0xFu) << 8) | i;
    }

    /* Corpo: WRITE_LINEAR -> invalidacao pelo SDMA. A ordem importa: o dado
     * escrito ANTES da invalidacao separa "o IB nao rodou" de "o SRBM_WRITE
     * foi ignorado" quando so' a invalidacao falhar. */
    status = Bc250SdmaEmitWriteLinear(body, 64u, &bodyCursor, scratchGpu,
        data, BC250_SDMA_PROBE_DATA_DWORDS);
    if (status == BC250_SDMA_OK) {
        status = Bc250SdmaEmitVmInvalidate(body, 64u, &bodyCursor,
            Engine, Vmid, 0u);
    }
    Result->EncodeStatus = (uint32_t)status;
    Result->BodyDwords = bodyCursor;
    if (status != BC250_SDMA_OK) {
        Bc250DisplayLog("[VMFLUSH_PROBE] RECUSADO: encode=%s (Engine=%u Vmid=%u)\n",
            Bc250SdmaStatusName(status), Engine, Vmid);
        return FALSE;
    }

    Result->RbCntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Result->IbCntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_IB_CNTL - BC250_SDMA0_MMIO_WINDOW_START);

    nextFence = (uint32_t)InterlockedIncrement(&g_Bc250SdmaPresent.NextFenceSequence);
    Result->FenceExpected = nextFence;

    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);

    /* PREFLIGHT/SNAPSHOT: veneno no rascunho e na fence. */
    for (i = 0u; i < BC250_SDMA_PROBE_DATA_DWORDS; ++i) {
        g_Bc250SdmaPresent.Work[scratchIndex + i] = BC250_SDMA_PROBE_POISON;
    }
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u] =
        BC250_SDMA_PRESENT_FENCE_POISON;

    if (Result->UseIndirect != 0u) {
        uint32_t pad = Bc250SdmaIbSizePadDwords(bodyCursor);
        uint32_t ibDwords = bodyCursor + pad;
        for (i = 0u; i < bodyCursor; ++i) {
            g_Bc250SdmaPresent.Work[BC250_SDMA_PROBE_IB_OFFSET / 4u + i] = body[i];
        }
        for (i = 0u; i < pad; ++i) {
            g_Bc250SdmaPresent.Work[BC250_SDMA_PROBE_IB_OFFSET / 4u + bodyCursor + i] =
                BC250_SDMA_OP_NOP;
        }
        startCursor = Bc250SdmaPresentAllocateRingSlot(48u);
        {
            uint32_t prePad = Bc250SdmaIndirectPrePadDwords(startCursor);
            for (i = 0u; i < prePad; ++i) packets[cursor++] = BC250_SDMA_OP_NOP;
        }
        status = Bc250SdmaEmitIndirect(packets, 64u, &cursor, ibGpu, ibDwords,
            0ull, 0u /* VMID 0: so' a pergunta do privilegio, sem traducao */);
    } else {
        startCursor = Bc250SdmaPresentAllocateRingSlot(48u);
        for (i = 0u; i < bodyCursor; ++i) packets[cursor++] = body[i];
        status = BC250_SDMA_OK;
    }
    if (status == BC250_SDMA_OK) status = Bc250SdmaEmitGcr(packets, 64u, &cursor);
    if (status == BC250_SDMA_OK) {
        status = Bc250SdmaEmitFence(packets, 64u, &cursor,
            g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_FENCE_OFFSET,
            nextFence);
    }
    if (status != BC250_SDMA_OK || cursor > 48u) {
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        Result->EncodeStatus = (uint32_t)status;
        Bc250DisplayLog("[VMFLUSH_PROBE] RECUSADO: anel encode=%s cursor=%u\n",
            Bc250SdmaStatusName(status), cursor);
        return FALSE;
    }
    KeMemoryBarrier();

    /* APPLY */
    {
        /* X2a (doc 523): pacotes no anel, escritas UC na VRAM. */
        ULONG ringWriteCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_RING_WRITE, startCursor);
        for (i = 0u; i < cursor; ++i) {
            g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
        }
        Bc250CrumbAfter(ringWriteCrumb);
    }
    KeMemoryBarrier();
    g_Bc250SdmaPresent.CurrentCursor += cursor;
    targetWptr = g_Bc250SdmaPresent.CurrentCursor * (uint32_t)sizeof(uint32_t);
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] = targetWptr;
    KeMemoryBarrier();
    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    Bc250SdmaPresentPublishWptr((uint32_t)(targetWptr));
    InterlockedIncrement(&g_Bc250SdmaPresent.SubmissionsSubmittedCount);
    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
    Result->TargetWptr = targetWptr;

    /* PROVA NO SILICIO: fence pela GPU sobre veneno + RPTR == WPTR. */
    for (poll = 1u; poll <= BC250_SDMA_TRIPOD_MAX_POLLS; ++poll) {
        uint32_t fenceObserved =
            g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
        if (fenceObserved == nextFence) {
            Result->FenceObserved = fenceObserved;
            Result->Polls = poll;
            break;
        }
        KeStallExecutionProcessor(5u);
    }
    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    Result->FenceObserved =
        g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];
    Result->RptrRegister = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
    Result->RptrWriteback = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
    Result->RptrMatched = Bc250SdmaRptrMatches(Result->RptrRegister,
        Result->RptrWriteback, targetWptr) ? 1u : 0u;
    for (i = 0u; i < BC250_SDMA_PROBE_DATA_DWORDS; ++i) {
        if (g_Bc250SdmaPresent.Work[scratchIndex + i] != data[i]) {
            ++Result->DataMismatches;
        }
    }
    Result->StatusReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
    Result->Status1Reg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_STATUS1 - BC250_SDMA0_MMIO_WINDOW_START);
    Result->ErrorLog = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_ERROR_LOG - BC250_SDMA0_MMIO_WINDOW_START);

    if (Result->FenceObserved != nextFence) {
        BC250_SDMA_TDR_SNAPSHOT snapshot;
        Result->TimedOut = 1u;
        Bc250DisplayLog("[VMFLUSH_PROBE] TIMEOUT %s: fence %08X (esperada %08X) RPTR_Reg=%u WPTR=%u STATUS=%08X STATUS1=%08X ERR_LOG=%08X -- quiesce + restart do TDR\n",
            Result->UseIndirect ? "IB" : "ANEL", Result->FenceObserved,
            nextFence, Result->RptrRegister, targetWptr, Result->StatusReg,
            Result->Status1Reg, Result->ErrorLog);
        /* ROLLBACK: o mesmo par provado no silicio em 07/09 e 17/09. */
        if (Bc250SdmaPresentQuiesceForTdr(&snapshot) &&
            Bc250SdmaPresentRestartAfterTdr()) {
            Result->Recovered = 1u;
        }
        Bc250DisplayLog("[VMFLUSH_PROBE] recuperacao do anel: %s\n",
            Result->Recovered ? "PROVADA (restart com NOP e fence)" :
                "FALHOU -- SDMA0 fica desligado (fail-closed)");
        return FALSE;
    }
    InterlockedIncrement(&g_Bc250SdmaPresent.SubmissionsCompletedCount);
    return (Result->RptrMatched != 0u && Result->DataMismatches == 0u) ? TRUE : FALSE;
#else
    UNREFERENCED_PARAMETER(UseIndirect);
    UNREFERENCED_PARAMETER(Engine);
    UNREFERENCED_PARAMETER(Vmid);
    if (Result != NULL) RtlZeroMemory(Result, sizeof(*Result));
    return FALSE;
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
        /* X2a (doc 523): escrita de VRAM pela janela WC, agora com migalha. */
        ULONG canaryCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_CANARY_WRITE,
            (ULONG)(dataPlan.VramOffset >> 12));
        for (planIndex = 0u; planIndex < dataPlan.CanaryCount; ++planIndex) {
            volatile uint32_t *pCanary = (volatile uint32_t *)
                ((char *)g_Bc250DisplayState.FrameBufferVirtual +
                 dataPlan.VramOffset + dataPlan.Offsets[planIndex]);
            *pCanary = dataPlan.Values[planIndex];
        }
        KeMemoryBarrier();
        Bc250CrumbAfter(canaryCrumb);
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
    startCursor = Bc250SdmaPresentAllocateRingSlot(24u);
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
    {
        /* X2a (doc 523): pacotes no anel, escritas UC na VRAM. */
        ULONG ringWriteCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_RING_WRITE, startCursor);
        for (i = 0u; i < cursor; ++i) {
            g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
        }
        Bc250CrumbAfter(ringWriteCrumb);
    }
    KeMemoryBarrier();

    g_Bc250SdmaPresent.CurrentCursor += cursor;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
    KeMemoryBarrier();

    /* 6. Atualiza WPTR no registrador da engine SDMA0 */
    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    Bc250SdmaPresentPublishWptr((uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t)));

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
    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);

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
            BOOLEAN rptrOk = Bc250SdmaRptrMatches(rptrReg, rptrWbRaw, targetWptr);
            if (!rptrOk && fenceObserved == nextFence) {
                rptrOk = TRUE;
            }
            if (rptrOk) {
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

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
/*
 * 4c.4 corrigida (Corrida 211, doc 580) -- IB DIVIDIDO EM VARIOS PEDACOS DE ANEL.
 *
 * MEDIDO (P0, Corrida 211): uma submissao chegou com 12 pontos de divisao, a
 * entrada unica ficou com 1.018 dw (acima do teto de 960 de um anel de 1024)
 * e o IB de paginacao foi RECUSADO -> STATUS_IO_DEVICE_ERROR -> tela azul. O
 * teto do UpdatePageTable contava PONTOS, nao dwords: ponto com varias VMIDs
 * custa 15 dw por VMID.
 *
 * Agora nao existe tamanho que force recusa: a entrada e' escrita em pedacos
 * de no maximo BC250_SDMA_SPLIT_CHUNK_MAX_DWORDS, cada um cobrindo uma faixa
 * contigua do IB que termina num ponto de divisao, com espera de espaco no
 * anel (RPTR do hardware) entre eles. A ordem e' a mesma de uma entrada unica:
 * o SDMA executa o anel em sequencia. So' o ultimo pedaco leva a fence e o
 * slot em voo. Outra submissao pode cair entre dois pedacos; ela e'
 * independente deste IB e a ordem dos slots continua a do anel.
 */
#define BC250_SDMA_SPLIT_CHUNK_MAX_DWORDS 480u

static volatile LONG g_Bc250SplitChunkedSubmissions = 0;

static BOOLEAN Bc250SdmaPresentWaitRingSpaceLocked(
    _In_ uint32_t NeededDwords,
    _In_ BOOLEAN NeedSlot,
    _Out_ KIRQL *OldIrql)
{
    ULONG polls = 0u;
    for (;;) {
        uint32_t rptrBytes;
        uint32_t usedDwords;
        KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, OldIrql);
        rptrBytes = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
        usedDwords = (((uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t)) -
            rptrBytes) & (BC250_SDMA_PRESENT_RING_BYTES - 1u)) / (uint32_t)sizeof(uint32_t);
        if (usedDwords + NeededDwords + 16u <= BC250_SDMA_PRESENT_RING_DWORDS &&
            (!NeedSlot ||
             g_Bc250SdmaPresent.InFlightCount < BC250_SDMA_IN_FLIGHT_CAPACITY)) {
            return TRUE;
        }
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, *OldIrql);
        if (++polls >= BC250_SDMA_RING_SPACE_MAX_WAIT_POLLS) {
            Bc250DisplayLog("[PT_GPU_SPLIT] anel sem espaco por 1 s (usado=%u, preciso=%u, slot=%u, RPTR=%u, cursor=%u)\n",
                usedDwords, NeededDwords, NeedSlot ? 1u : 0u, rptrBytes,
                g_Bc250SdmaPresent.CurrentCursor);
            return FALSE;
        }
        /* Espera REAL: o RPTR do hardware tem de andar. Nada e' simulado. */
        KeStallExecutionProcessor(10u);
    }
}

static BOOLEAN Bc250SdmaPresentSubmitSplitChunkedWith(
    _Inout_updates_(BC250_SDMA_PRESENT_RING_DWORDS) uint32_t *s_chunkStream,
    _Inout_updates_(BC250_SDMA1_RING_BYTES / 4u) uint32_t *s_sdma1Stream,
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
    _In_reads_(FlushDwords) const uint32_t *FlushStream,
    _In_ uint32_t FlushDwords,
    _In_reads_(SplitCount) const BC250_SDMA_IB_SPLIT *Splits,
    _In_ uint32_t SplitCount,
    _In_ const BC250_DATA_PROOF_PLAN *DataPlan,
    _Out_ uint32_t *OutAssignedHardwareFence)
{
    BC250_SDMA_CROSS_ENGINE cross;
    uint32_t nextFence = (uint32_t)InterlockedIncrement(&g_Bc250SdmaPresent.NextFenceSequence);
    uint32_t a = 0u;
    uint32_t cs = 0u;
    uint32_t chunkCount = 0u;
    uint32_t i;

    if (OutAssignedHardwareFence != NULL) *OutAssignedHardwareFence = 0u;
    cross.GoAddress = g_Bc250SdmaPresent.Sdma1WorkGpu + BC250_SDMA1_WORK_GO_OFFSET;
    cross.DoneAddress = g_Bc250SdmaPresent.Sdma1WorkGpu + BC250_SDMA1_WORK_DONE_OFFSET;

    for (;;) {
        BC250_SDMA_IB_SPLIT local[BC250_SDMA_IB_SPLIT_MAX];
        uint32_t pre = (chunkCount == 0u) ? FlushDwords : 0u;
        uint32_t b = a;
        uint32_t n;
        uint32_t ce;
        uint32_t worst;
        BOOLEAN last;
        KIRQL oldIrql;
        uint32_t ringIndex;
        uint32_t streamDwords;
        uint32_t lastIndirectEnd = 0u;
        BC250_SDMA_STATUS status = BC250_SDMA_OK;
        uint32_t sdma1Dwords = 0u;
        uint32_t sdma1Ranges = 0u;

        /* Cresce o pedaco enquanto couber; nunca termina numa faixa vazia. */
        b = Bc250SdmaSplitNextChunk(Splits, SplitCount, a, cs, pre,
            BC250_SDMA_SPLIT_CHUNK_MAX_DWORDS);
        last = (b >= SplitCount) ? TRUE : FALSE;
        ce = last ? IbSizeDwords : Bc250SdmaSplitEndDwords(&Splits[b - 1u]);
        n = b - a;
        for (i = 0u; i < n; ++i) {
            local[i].OffsetDwords = Splits[a + i].OffsetDwords - cs;
            local[i].VmidMask = Splits[a + i].VmidMask;
            local[i].Sdma1EndDwords = Splits[a + i].Sdma1EndDwords != 0u ?
                Splits[a + i].Sdma1EndDwords - cs : 0u;
            local[i].Sdma1Token = 0u;
        }
        worst = pre + Bc250SdmaSplitIndirectWorstDwords(local, n) +
            BC250_SDMA_DWORDS_GCR_REQ + (last ? (BC250_SDMA_DWORDS_FENCE + BC250_SDMA_DWORDS_TRAP) : 0u);
        if (worst > BC250_SDMA_SPLIT_ENTRY_MAX_DWORDS || (ce <= cs && !last)) {
            /* Inalcancavel: pedaco unitario cabe (16 VMIDs x 15 + 36 < 960). */
            Bc250DisplayLog("[PT_GPU_SPLIT] RECUSADO: pedaco %u impossivel (worst=%u dw, faixa [%u,%u), pontos %u..%u)%s\n",
                chunkCount, worst, cs, ce, a, b,
                chunkCount != 0u ? " -- PEDACOS ANTERIORES JA NO ANEL" : "; IB NAO enviado");
            return FALSE;
        }
        if (!Bc250SdmaPresentWaitRingSpaceLocked(worst, last, &oldIrql)) {
            Bc250DisplayLog("[PT_GPU_SPLIT] RECUSADO: sem espaco para o pedaco %u%s\n",
                chunkCount,
                chunkCount != 0u ? " -- PEDACOS ANTERIORES JA NO ANEL" : "; IB NAO enviado");
            return FALSE;
        }

        ringIndex = g_Bc250SdmaPresent.CurrentCursor % BC250_SDMA_PRESENT_RING_DWORDS;
        streamDwords = 0u;
        sdma1Dwords = 0u;
        {
            /*
             * P4.2 (doc 622): faixas de PTE deste pedaco. Com o SDMA1 anexado e
             * espaco no anel dele, cada faixa ganha um token e vira a troca
             * FENCE(Go)/POLL(Done) no SDMA0 e POLL(Go)/IB/FENCE(Done) no SDMA1.
             * Senao a faixa sai da lista e roda no SDMA0, dentro do IB, como
             * antes da P4.2. Nunca as duas coisas.
             */
            uint32_t ranges = 0u, k, w = 0u;
            BOOLEAN route = g_Bc250SdmaPresent.Sdma1Attached;
            for (i = 0u; i < n; ++i) if (local[i].Sdma1EndDwords != 0u) ++ranges;
            if (ranges != 0u && route) {
                uint32_t rptr1 = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
                    BC250_SDMA1_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
                uint32_t used1 = (((g_Bc250SdmaPresent.Sdma1CursorDwords * 4u) - rptr1) &
                    (BC250_SDMA1_RING_BYTES - 1u)) / 4u;
                if (used1 + ranges * BC250_SDMA_DWORDS_SDMA1_RANGE_JOB_WORST + 16u >
                        BC250_SDMA1_RING_BYTES / 4u) {
                    route = FALSE;
                    InterlockedExchangeAdd64(&g_Bc250SdmaPresent.Sdma1RangesNoSpace, (LONG64)ranges);
                }
            }
            if (ranges != 0u && !route) {
                for (k = 0u; k < n; ++k) {
                    if (local[k].Sdma1EndDwords != 0u) continue;
                    local[w++] = local[k];
                }
                InterlockedExchangeAdd64(&g_Bc250SdmaPresent.Sdma1RangesInline, (LONG64)ranges);
                n = w;
            } else if (ranges != 0u) {
                uint32_t ring1Index = g_Bc250SdmaPresent.Sdma1CursorDwords % (BC250_SDMA1_RING_BYTES / 4u);
                for (k = 0u; k < n && status == BC250_SDMA_OK; ++k) {
                    if (local[k].Sdma1EndDwords == 0u) continue;
                    if (++g_Bc250SdmaPresent.Sdma1NextToken == 0u) g_Bc250SdmaPresent.Sdma1NextToken = 1u;
                    local[k].Sdma1Token = g_Bc250SdmaPresent.Sdma1NextToken;
                    status = Bc250SdmaEmitSdma1RangeJob(s_sdma1Stream,
                        BC250_SDMA1_RING_BYTES / 4u, &sdma1Dwords, ring1Index, &cross,
                        local[k].Sdma1Token,
                        DmaBufferGpuVa + ((uint64_t)cs * sizeof(uint32_t)),
                        local[k].OffsetDwords, local[k].Sdma1EndDwords, Vmid);
                }
                sdma1Ranges = ranges;
            }
        }
        for (i = 0u; i < pre; ++i) s_chunkStream[streamDwords++] = FlushStream[i];
        if (status != BC250_SDMA_OK) {
            /* montagem do SDMA1 falhou: cai no tratamento de erro abaixo */
        } else if (ce > cs) {
            status = Bc250SdmaEmitSplitIndirectCross(s_chunkStream,
                BC250_SDMA_PRESENT_RING_DWORDS, &streamDwords, ringIndex,
                DmaBufferGpuVa + ((uint64_t)cs * sizeof(uint32_t)), ce - cs, Vmid,
                local, n, &cross, &lastIndirectEnd);
        } else {
            /* Ultimo pedaco so' com pontos no fim exato do IB: GCR rinse antes das invalidacoes */
            uint32_t mask = 0u, v;
            status = Bc250SdmaEmitGcr(s_chunkStream,
                BC250_SDMA_PRESENT_RING_DWORDS, &streamDwords);
            for (i = 0u; i < n; ++i) mask |= local[i].VmidMask;
            for (v = 1u; v < 16u && status == BC250_SDMA_OK; ++v) {
                if (((mask >> v) & 1u) == 0u) continue;
                status = Bc250SdmaEmitVmInvalidate(s_chunkStream,
                    BC250_SDMA_PRESENT_RING_DWORDS, &streamDwords,
                    BC250_SDMA_INVALIDATE_ENGINE, v, 0u);
            }
        }
        /*
         * 5D (doc 685): GCR GL2 rinse emitido incondicionalmente em TODOS os chunks
         * (inclusive intermediarios), espelhando amdgpu sdma_v5_0_ring_emit_mem_sync.
         */
        if (status == BC250_SDMA_OK) {
            status = Bc250SdmaEmitGcr(s_chunkStream,
                BC250_SDMA_PRESENT_RING_DWORDS, &streamDwords);
        }
        if (status == BC250_SDMA_OK && last) {
            uint32_t slotFenceOffset = Bc250SdmaAsyncSlotFenceOffset(
                g_Bc250SdmaPresent.InFlightTail);
            status = Bc250SdmaEmitFence(s_chunkStream,
                BC250_SDMA_PRESENT_RING_DWORDS, &streamDwords,
                g_Bc250SdmaPresent.WorkGpuAddress + slotFenceOffset, nextFence);
            if (status == BC250_SDMA_OK) {
                Bc250SdmaEmitTrapIfArmed(s_chunkStream, BC250_SDMA_PRESENT_RING_DWORDS, &streamDwords);
                g_Bc250SdmaPresent.Work[slotFenceOffset / 4u] = BC250_SDMA_PRESENT_FENCE_POISON;
                KeMemoryBarrier();
            }
        }
        if (status != BC250_SDMA_OK) {
            KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
            Bc250DisplayLog("[PT_GPU_SPLIT] RECUSADO: montagem do pedaco %u falhou (%s) faixa [%u,%u) pontos=%u%s\n",
                chunkCount, Bc250SdmaStatusName(status), cs, ce, n,
                chunkCount != 0u ? " -- PEDACOS ANTERIORES JA NO ANEL" : "; IB NAO enviado");
            return FALSE;
        }
        if (sdma1Dwords != 0u) {
            uint32_t ring1Index = g_Bc250SdmaPresent.Sdma1CursorDwords % (BC250_SDMA1_RING_BYTES / 4u);
            for (i = 0u; i < sdma1Dwords; ++i) {
                g_Bc250SdmaPresent.Sdma1Ring[(ring1Index + i) % (BC250_SDMA1_RING_BYTES / 4u)] =
                    s_sdma1Stream[i];
            }
            g_Bc250SdmaPresent.Sdma1CursorDwords += sdma1Dwords;
            g_Bc250SdmaPresent.Sdma1Work[BC250_SDMA1_WORK_WPTR_OFFSET / 4u] =
                g_Bc250SdmaPresent.Sdma1CursorDwords * 4u;
            KeMemoryBarrier();
            Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
                BC250_SDMA1_REG_MINOR_PTR_UPDATE - BC250_SDMA0_MMIO_WINDOW_START, 1u);
            Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
                BC250_SDMA1_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START,
                g_Bc250SdmaPresent.Sdma1CursorDwords * 4u);
            Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
                BC250_SDMA1_REG_RB_WPTR_HI - BC250_SDMA0_MMIO_WINDOW_START, 0u);
            Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
                BC250_SDMA1_REG_MINOR_PTR_UPDATE - BC250_SDMA0_MMIO_WINDOW_START, 0u);
            InterlockedExchangeAdd64(&g_Bc250SdmaPresent.Sdma1RangesRouted, (LONG64)sdma1Ranges);
        }
        {
            ULONG ringCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_RING_WRITE, ringIndex);
            for (i = 0u; i < streamDwords; ++i) {
                g_Bc250SdmaPresent.Ring[(ringIndex + i) % BC250_SDMA_PRESENT_RING_DWORDS] =
                    s_chunkStream[i];
            }
            KeMemoryBarrier();
            Bc250CrumbAfter(ringCrumb);
        }
        g_Bc250SdmaPresent.CurrentCursor += streamDwords;
        g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
            g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
        KeMemoryBarrier();
        Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
        Bc250SdmaPresentPublishWptr((uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t)));
        ++chunkCount;

        if (last) {
            uint32_t slotIndex = g_Bc250SdmaPresent.InFlightTail;
            BC250_SDMA_IN_FLIGHT_SUBMISSION *slot = &g_Bc250SdmaPresent.InFlightQueue[slotIndex];
            InterlockedIncrement(&g_Bc250SdmaPresent.SubmissionsSubmittedCount);
            RtlZeroMemory(slot, sizeof(*slot));
            slot->Active = TRUE;
            slot->SubmissionFenceId = SubmissionFenceId;
            slot->HardwareFence = nextFence;
            slot->TargetWptrBytes = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
            slot->StartCursor = ringIndex;
            slot->PadDwords = 0u;
            slot->IndirectEndDwords = lastIndirectEnd % BC250_SDMA_PRESENT_RING_DWORDS;
            slot->IbSizeDwords = IbSizeDwords;
            slot->DmaBufferSize = DmaBufferSize;
            slot->DmaBufferVirtualAddress = DmaBufferGpuVa;
            slot->Vmid = Vmid;
            slot->ExecPath = ExecPath;
            slot->ExpectedEffectDestMc = ExpectedEffectDestMc;
            slot->ExpectedEffectBytes = ExpectedEffectBytes;
            slot->ExpectedEffectPattern = ExpectedEffectPattern;
            slot->DataPlan = *DataPlan;
            slot->SubmitTime = KeQueryPerformanceCounter(NULL);
            slot->TlbFlushVmidMask = TlbFlushVmidMask;
            slot->TlbFlushDwords = FlushDwords;
            g_Bc250SdmaPresent.InFlightTail = (slotIndex + 1u) % BC250_SDMA_IN_FLIGHT_CAPACITY;
            g_Bc250SdmaPresent.InFlightCount++;
            KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
            if (OutAssignedHardwareFence != NULL) *OutAssignedHardwareFence = nextFence;
            if (chunkCount > 1u) {
                LONG c = InterlockedIncrement(&g_Bc250SplitChunkedSubmissions);
                if (c <= 16 || (c % 256) == 0) {
                    Bc250DisplayLog("[PT_GPU_SPLIT] #%ld IB de %u dw com %u pontos escrito em %u pedacos de anel (FenceId=%llu HwFence=%u)\n",
                        c, IbSizeDwords, SplitCount, chunkCount,
                        (unsigned long long)SubmissionFenceId, nextFence);
                }
            }
            return TRUE;
        }
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        a = b;
        cs = ce;
    }
}

/*
 * Auditorias 653/657 (doc 658): os dois fluxos eram `static` dentro da funcao
 * e o RingLock e' solto entre pedacos do split; duas submissoes divididas
 * concorrentes escreviam no mesmo buffer. Agora cada chamada tem os seus
 * (8 KiB de pool nao paginado, fora da pilha do kernel), e falta de memoria
 * recusa a submissao como as outras falhas desta funcao.
 */
#define BC250_SDMA_SPLIT_POOL_TAG 0x53503542u /* 'B5PS' */
static BOOLEAN Bc250SdmaPresentSubmitSplitChunked(
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
    _In_reads_(FlushDwords) const uint32_t *FlushStream,
    _In_ uint32_t FlushDwords,
    _In_reads_(SplitCount) const BC250_SDMA_IB_SPLIT *Splits,
    _In_ uint32_t SplitCount,
    _In_ const BC250_DATA_PROOF_PLAN *DataPlan,
    _Out_ uint32_t *OutAssignedHardwareFence)
{
    uint32_t *streams;
    BOOLEAN ok;
    if (OutAssignedHardwareFence != NULL) *OutAssignedHardwareFence = 0u;
    streams = (uint32_t *)ExAllocatePool2(POOL_FLAG_NON_PAGED,
        BC250_SDMA_PRESENT_RING_BYTES + BC250_SDMA1_RING_BYTES,
        BC250_SDMA_SPLIT_POOL_TAG);
    if (streams == NULL) {
        Bc250DisplayLog("[SDMA_SPLIT] RECUSADO: sem pool para os fluxos do split (%u bytes)\n",
            (unsigned)(BC250_SDMA_PRESENT_RING_BYTES + BC250_SDMA1_RING_BYTES));
        return FALSE;
    }
    ok = Bc250SdmaPresentSubmitSplitChunkedWith(
        streams, streams + BC250_SDMA_PRESENT_RING_DWORDS,
        DmaBufferGpuVa, DmaBufferSize, IbSizeDwords, Vmid,
        ExpectedEffectDestMc, ExpectedEffectBytes, ExpectedEffectPattern,
        SubmissionFenceId, ExecPath, TlbFlushVmidMask, FlushStream, FlushDwords,
        Splits, SplitCount, DataPlan, OutAssignedHardwareFence);
    ExFreePoolWithTag(streams, BC250_SDMA_SPLIT_POOL_TAG);
    return ok;
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
    _Out_ uint32_t *OutAssignedHardwareFence)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    BC250_DATA_PROOF_PLAN dataPlan;
    uint32_t packets[64] = { 0 };
    uint32_t flushStream[256];
    uint32_t flushDwords = 0u;
    uint32_t entryStartCursor;
    uint32_t neededDwords;
    ULONG spacePolls = 0u;
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
    Bc250X4DisableRuntimeCanaries(&dataPlan, "SubmitIndirectAsync");

    /*
     * 4c.2 (doc 569): invalidacao de TLB pelo SDMA, NO ANEL, antes do
     * INDIRECT -- a sequencia provada no silicio na Corrida 205 (doc 568):
     * SRBM_WRITE(REQ) + espera de um ciclo + espera do ACK, engine 1, uma
     * VMID por vez como o Linux. Dentro do IB ela trava o SDMA (doc 568), por
     * isso fica aqui. Montada fora do lock; so' a copia vai sob ele.
     */
    TlbFlushVmidMask &= 0xFFFEu; /* VMID 0 nunca: raiz do MEC/anel fisico */
    if (TlbFlushVmidMask != 0u) {
        uint32_t flushVmid;
        /* 5D (doc 685): GCR rinse no anel antes das invalidacoes de TLB */
        status = Bc250SdmaEmitGcr(flushStream, 256u, &flushDwords);
        for (flushVmid = 1u; flushVmid < 16u && status == BC250_SDMA_OK; ++flushVmid) {
            if (((TlbFlushVmidMask >> flushVmid) & 1u) == 0u) continue;
            status = Bc250SdmaEmitVmInvalidate(flushStream, 256u, &flushDwords,
                BC250_SDMA_INVALIDATE_ENGINE, flushVmid, 0u);
            if (status != BC250_SDMA_OK) {
                Bc250DisplayLog("[RING_TLB_FLUSH] RECUSADO: encode da VMID %u falhou (%s); IB NAO enviado\n",
                    flushVmid, Bc250SdmaStatusName(status));
                return FALSE;
            }
        }
    }
    neededDwords = flushDwords + 24u;
    if (SplitCount != 0u) {
        if (Splits == NULL || SplitCount > BC250_SDMA_IB_SPLIT_MAX) {
            Bc250DisplayLog("[PT_GPU_SPLIT] RECUSADO: lista de divisao invalida (n=%u); IB NAO enviado\n",
                SplitCount);
            return FALSE;
        }
        /* 4c.4 (doc 580): pedacos de anel, sem teto de tamanho que force recusa. */
        return Bc250SdmaPresentSubmitSplitChunked(DmaBufferGpuVa, DmaBufferSize,
            ibSizeDwords, Vmid, ExpectedEffectDestMc, ExpectedEffectBytes,
            ExpectedEffectPattern, SubmissionFenceId, ExecPath, TlbFlushVmidMask,
            flushStream, flushDwords, Splits, SplitCount, &dataPlan,
            OutAssignedHardwareFence);
    }

    nextFence = (uint32_t)InterlockedIncrement(&g_Bc250SdmaPresent.NextFenceSequence);
    for (;;) {
        uint32_t ringIndex;
        uint32_t wrapPad;
        uint32_t rptrBytes;
        uint32_t usedDwords;

        KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);
        if (g_Bc250SdmaPresent.InFlightCount >= BC250_SDMA_IN_FLIGHT_CAPACITY) {
            KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
            return FALSE;
        }
        /*
         * Espaco livre pelo RPTR do HARDWARE (bytes, contador que passa do
         * tamanho do anel: a diferenca e' reduzida a uma volta, como no
         * contrato do tripe). Exige espaco para o pacote e para o eventual
         * preenchimento ate o fim do anel que AllocateRingSlot fara.
         */
        ringIndex = g_Bc250SdmaPresent.CurrentCursor % BC250_SDMA_PRESENT_RING_DWORDS;
        /* 4c.4: a entrada dividida atravessa o fim do anel; sem preenchimento. */
        wrapPad = (SplitCount == 0u &&
                   ringIndex + neededDwords > BC250_SDMA_PRESENT_RING_DWORDS) ?
            (BC250_SDMA_PRESENT_RING_DWORDS - ringIndex) : 0u;
        rptrBytes = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
        usedDwords = (((uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t)) -
            rptrBytes) & (BC250_SDMA_PRESENT_RING_BYTES - 1u)) / (uint32_t)sizeof(uint32_t);
        if (usedDwords + wrapPad + neededDwords + 16u <= BC250_SDMA_PRESENT_RING_DWORDS) {
            break;
        }
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        if (++spacePolls >= BC250_SDMA_RING_SPACE_MAX_WAIT_POLLS) {
            Bc250DisplayLog("[RING_TLB_FLUSH] RECUSADO: anel sem espaco por 1 s (usado=%u, preciso=%u+%u, RPTR=%u, cursor=%u); IB NAO enviado\n",
                usedDwords, neededDwords, wrapPad, rptrBytes,
                g_Bc250SdmaPresent.CurrentCursor);
            return FALSE;
        }
        /* Espera REAL: o RPTR do hardware tem de andar. Nada e' simulado. */
        KeStallExecutionProcessor(10u);
    }

    if (dataPlan.Usable != 0u) {
        uint32_t planIndex;
        /* X2a (doc 523): escrita de VRAM pela janela WC, agora com migalha. */
        ULONG canaryCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_CANARY_WRITE,
            (ULONG)(dataPlan.VramOffset >> 12));
        for (planIndex = 0u; planIndex < dataPlan.CanaryCount; ++planIndex) {
            volatile uint32_t *pCanary = (volatile uint32_t *)
                ((char *)g_Bc250DisplayState.FrameBufferVirtual +
                 dataPlan.VramOffset + dataPlan.Offsets[planIndex]);
            *pCanary = dataPlan.Values[planIndex];
        }
        KeMemoryBarrier();
        Bc250CrumbAfter(canaryCrumb);
        Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    }

    startCursor = Bc250SdmaPresentAllocateRingSlot(neededDwords);
    /* A entrada comeca na invalidacao; [pad][INDIRECT][GCR][FENCE] vem depois dela. */
    entryStartCursor = startCursor;
    startCursor += flushDwords;
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
        Bc250SdmaEmitTrapIfArmed(packets, 64u, &cursor);

        {
            /* X2a: poison da fence + pacotes no anel, escritas UC na VRAM. */
            ULONG ringCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_RING_WRITE,
                startCursor);
            g_Bc250SdmaPresent.Work[slotFenceOffset / 4u] =
                BC250_SDMA_PRESENT_FENCE_POISON;
            KeMemoryBarrier();

            /* 4c.2: invalidacao primeiro, depois [pad][INDIRECT][GCR][FENCE]. */
            for (i = 0u; i < flushDwords; ++i) {
                g_Bc250SdmaPresent.Ring[entryStartCursor + i] = flushStream[i];
            }
            for (i = 0u; i < cursor; ++i) {
                g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
            }
            KeMemoryBarrier();
            Bc250CrumbAfter(ringCrumb);
        }

        g_Bc250SdmaPresent.CurrentCursor += flushDwords + cursor;
        g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
            g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
        KeMemoryBarrier();

        Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
        Bc250SdmaPresentPublishWptr((uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t)));

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
            slot->StartCursor = entryStartCursor;
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
            slot->TlbFlushVmidMask = TlbFlushVmidMask;
            slot->TlbFlushDwords = flushDwords;

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
    UNREFERENCED_PARAMETER(TlbFlushVmidMask);
    UNREFERENCED_PARAMETER(Splits);
    UNREFERENCED_PARAMETER(SplitCount);
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
    Bc250X4DisableRuntimeCanaries(&dataPlan, "SubmitFillAsync");

    nextFence = (uint32_t)InterlockedIncrement(&g_Bc250SdmaPresent.NextFenceSequence);
    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);

    if (g_Bc250SdmaPresent.InFlightCount >= BC250_SDMA_IN_FLIGHT_CAPACITY) {
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
        return FALSE;
    }

    if (dataPlan.Usable != 0u) {
        uint32_t planIndex;
        /* X2a (doc 523): escrita de VRAM pela janela WC, agora com migalha. */
        ULONG canaryCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_CANARY_WRITE,
            (ULONG)(dataPlan.VramOffset >> 12));
        for (planIndex = 0u; planIndex < dataPlan.CanaryCount; ++planIndex) {
            volatile uint32_t *pCanary = (volatile uint32_t *)
                ((char *)g_Bc250DisplayState.FrameBufferVirtual +
                 dataPlan.VramOffset + dataPlan.Offsets[planIndex]);
            *pCanary = dataPlan.Values[planIndex];
        }
        KeMemoryBarrier();
        Bc250CrumbAfter(canaryCrumb);
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
        Bc250SdmaEmitTrapIfArmed(packets, 128u, &cursor);

        startCursor = Bc250SdmaPresentAllocateRingSlot(cursor);

        {
            /* X2a (doc 523): pacotes no anel, escritas UC na VRAM. */
            ULONG ringWriteCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_RING_WRITE, startCursor);
            for (i = 0u; i < cursor; ++i) {
                g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
            }
            Bc250CrumbAfter(ringWriteCrumb);
        }
        KeMemoryBarrier();

        g_Bc250SdmaPresent.CurrentCursor += cursor;
        g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
            g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
        g_Bc250SdmaPresent.Work[slotFenceOffset / 4u] =
            BC250_SDMA_PRESENT_FENCE_POISON;
        KeMemoryBarrier();

        Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
        Bc250SdmaPresentPublishWptr((uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t)));

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

        {
            /* X2a (doc 523): pacotes no anel, escritas UC na VRAM. */
            ULONG ringWriteCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_RING_WRITE, startCursor);
            for (i = 0u; i < cursor; ++i) {
                g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
            }
            Bc250CrumbAfter(ringWriteCrumb);
        }
        KeMemoryBarrier();

        g_Bc250SdmaPresent.CurrentCursor += cursor;
        g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
            g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
        g_Bc250SdmaPresent.Work[slotFenceOffset / 4u] =
            BC250_SDMA_PRESENT_FENCE_POISON;
        KeMemoryBarrier();

        Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
        Bc250SdmaPresentPublishWptr((uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t)));

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
    BOOLEAN fenceRead = FALSE;

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
     * X1 (29/09, doc 522) -- NADA DE HDP NEM DE LEITURA DE VRAM ENQUANTO A
     * ENGINE TRABALHA.
     *
     * Ate a 3.313 esta sondagem lia a fence NA VRAM pela BAR a cada tique e,
     * sem ela, fazia flush + invalidacao do HDP e lia de novo -- tudo isso
     * enquanto o SDMA escrevia megabytes em rajada na DRAM compartilhada.
     * Corrida 168 (P0, anel EVT.1): a maquina parou com um FILL de paginacao
     * de 2,27 MB submetido e sem conclusao; a ultima migalha de MMIO foi a
     * leitura de volta da invalidacao do HDP (0xFC4) na CPU00, e a leitura da
     * fence na VRAM que vem logo depois NAO tinha migalha. O Linux nao sonda
     * assim (fence/RPTR em memoria do sistema; HDP so' quando a CPU vai ler
     * dados da GPU).
     *
     * Agora: a conclusao da entrada mais antiga e' decidida SO' pelo
     * registrador RPTR (Bc250TripodRingEntryConsumed, testado offline). So'
     * depois que a engine consumiu a entrada ha UM flush/invalidacao do HDP e
     * a leitura da fence -- que continua sendo a prova (fence sobre poison).
     * NAO VALIDADO EM HARDWARE.
     */
    {
        uint32_t rptrGate = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
        uint32_t tailBytes = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
        if (Bc250TripodRingEntryConsumed(rptrGate,
                head->StartCursor * (uint32_t)sizeof(uint32_t),
                targetWptrBytes, tailBytes,
                (uint32_t)BC250_SDMA_PRESENT_RING_BYTES) == 0u) {
            InterlockedIncrement64(&g_Bc250X1GatedPolls);
            fenceObserved = 0u;
            fenceRead = FALSE;
        } else {
            ULONG crumb;
            LONG64 reads = InterlockedIncrement64(&g_Bc250X1FenceReads);
            Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
            crumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_FENCE_READ, slotFenceOffset);
            fenceObserved = g_Bc250SdmaPresent.Work[slotFenceOffset / 4u];
            Bc250CrumbAfter(crumb);
            fenceRead = TRUE;
            if (reads <= 4 || (reads % 65536) == 0) {
                Bc250DisplayLog("[X1_POLL] leitura de fence #%lld so' depois do RPTR (Rptr=%u Alvo=%u Ultimo=%u) | sondagens sem HDP ate aqui=%lld\n",
                    (long long)reads, rptrGate, targetWptrBytes, tailBytes,
                    (long long)g_Bc250X1GatedPolls);
            }
        }
    }

    if (fenceRead && fenceObserved == nextFence) {
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
            /*
             * X3 (29/09, doc 524): Corrida 170 (P0) -- a maquina parou com a
             * CPU00 DENTRO desta leitura de canarios (migalha VRAM_CANARY_READ
             * fase ANTES, VRAM 0xF280000) enquanto o SDMA comecava um FILL de
             * paginacao de 2,27 MB. A leitura de VRAM pela BAR so' acontece
             * agora com o anel ocioso: este IB e' o unico em voo e o RPTR ja'
             * chegou ao ultimo WPTR publicado. Senao a perna de dados deste IB
             * fica NAO CONFERIDA (prova de duas pernas, declarada), nunca
             * simulada. So' o SDMA entra na conta; o GFX pode estar escrevendo.
             * NAO VALIDADO EM HARDWARE.
             */
            if (dataPlan.Usable != 0u) {
                uint32_t tailBytesX3 = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));
                BOOLEAN ringIdle = (g_Bc250SdmaPresent.InFlightCount == 1u &&
                    (rptrReg % BC250_SDMA_PRESENT_RING_BYTES) ==
                        (tailBytesX3 % BC250_SDMA_PRESENT_RING_BYTES)) ? TRUE : FALSE;
                if (!ringIdle) {
                    LONG64 skipped = InterlockedIncrement64(&g_Bc250X3CanarySkipped);
                    if (skipped <= 8 || (skipped % 4096) == 0) {
                        Bc250DisplayLog("[X3_CANARY] #%lld perna de dados NAO CONFERIDA: SDMA ocupado (EmVoo=%u Rptr=%u Ultimo=%u) -- a CPU nao le VRAM pela BAR com o SDMA trabalhando | lidas=%lld\n",
                            (long long)skipped, g_Bc250SdmaPresent.InFlightCount,
                            rptrReg, tailBytesX3, (long long)g_Bc250X3CanaryRead);
                    }
                    dataPlan.Usable = 0u;
                } else {
                    InterlockedIncrement64(&g_Bc250X3CanaryRead);
                }
            }
            if (dataPlan.Usable != 0u) {
                uint32_t planIndex;
                ULONG canaryCrumb;
                Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
                /* X2a: leitura de VRAM pela janela WC, agora com migalha. */
                canaryCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_CANARY_READ,
                    (ULONG)(dataPlan.VramOffset >> 12));
                for (planIndex = 0u; planIndex < dataPlan.CanaryCount; ++planIndex) {
                    volatile uint32_t *pCanary = (volatile uint32_t *)
                        ((char *)g_Bc250DisplayState.FrameBufferVirtual +
                         dataPlan.VramOffset + dataPlan.Offsets[planIndex]);
                    post[planIndex] = *pCanary;
                }
                Bc250CrumbAfter(canaryCrumb);
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

            Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_TRIPOD TIMEOUT (ASYNC %lld ms): TargetWPTR=%u, WPTR=%u, RPTR=%u, STATUS=%08X, ERR_LOG=%08X, FenceExp=%08X, FenceObs=%08X FenceLida=%u (X1: 0 = RPTR nao passou do alvo, fence nao lida) (preso em voo ate TDR)\n",
                elapsedMs, targetWptrBytes, wptrReg, rptrReg, statusReg, after.ErrorLog, nextFence, fenceObserved,
                fenceRead ? 1u : 0u);
            head->TimeoutLogged = TRUE;
            /* Corrida 215: foto da GART no timeout (DISPATCH, so' leitura). */
            if (g_Bc250SdmaPresent.SystemRingHook != NULL) {
                g_Bc250SdmaPresent.SystemRingHook(g_Bc250SdmaPresent.SystemRingHookContext,
                    BC250_SDMA_RING_HOOK_SNAPSHOT);
            }
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

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
/* D2: troca os quatro enderecos do anel sob o RingLock (engine ociosa). */
static VOID Bc250SdmaPresentSwapRingWork(
    _In_ volatile uint32_t *Ring, _In_ uint64_t RingGpu,
    _In_ volatile uint32_t *Work, _In_ uint64_t WorkGpu)
{
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);
    g_Bc250SdmaPresent.Ring = Ring;
    g_Bc250SdmaPresent.RingGpuAddress = RingGpu;
    g_Bc250SdmaPresent.Work = Work;
    g_Bc250SdmaPresent.WorkGpuAddress = WorkGpu;
    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
}
#endif

NTSTATUS Bc250SdmaPresentAllocateSystemRingWork(
    _Out_ PVOID *Memory,
    _Out_ PHYSICAL_ADDRESS *Physical)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    PHYSICAL_ADDRESS low;
    PHYSICAL_ADDRESS high;
    PHYSICAL_ADDRESS boundary;
    PVOID memory;

    if (Memory == NULL || Physical == NULL) return STATUS_INVALID_PARAMETER;
    *Memory = NULL;
    Physical->QuadPart = 0;
    if (KeGetCurrentIrql() > DISPATCH_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    /*
     * Contigua e NAO cacheada (mesmo padrao dos buffers do PSP): a CPU nunca
     * guarda linha velha, entao a coerencia nao depende de snoop. Teto de
     * 40 bits de PA, dentro do campo de endereco da PTE da GART.
     */
    low.QuadPart = 0;
    high.QuadPart = 0xFFFFFFFFFFll;
    boundary.QuadPart = 0;
    memory = MmAllocateContiguousMemorySpecifyCache(
        BC250_SDMA_PRESENT_BUFFER_TOTAL, low, high, boundary, MmNonCached);
    if (memory == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(memory, BC250_SDMA_PRESENT_BUFFER_TOTAL);
    *Memory = memory;
    *Physical = MmGetPhysicalAddress(memory);
    return STATUS_SUCCESS;
#else
    UNREFERENCED_PARAMETER(Memory);
    UNREFERENCED_PARAMETER(Physical);
    return STATUS_NOT_SUPPORTED;
#endif
}

BOOLEAN Bc250SdmaPresentRestartAfterTdrOrRevert(void)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    BC250_SDMA_RING_HOOK hook;
    PVOID hookContext;

    if (Bc250SdmaPresentRestartAfterTdr()) {
        return TRUE;
    }
    if (!g_Bc250SdmaPresent.RingInSystemMemory) {
        return FALSE;
    }
    Bc250DisplayLog("[D2_ANEL_SISTEMA] TDR: o restart pela GART falhou; desligando a GART e voltando o anel para a VRAM (RingGpu=0x%llX); RAM retida de proposito (PA=0x%llX)\n",
        (unsigned long long)g_Bc250SdmaPresent.VramRingGpuAddress,
        (unsigned long long)g_Bc250SdmaPresent.SystemRingWorkPhysical.QuadPart);
    hook = g_Bc250SdmaPresent.SystemRingHook;
    hookContext = g_Bc250SdmaPresent.SystemRingHookContext;
    if (hook != NULL) {
        hook(hookContext, BC250_SDMA_RING_HOOK_ROLLBACK);
    }
    g_Bc250SdmaPresent.SystemRingHook = NULL;
    g_Bc250SdmaPresent.SystemRingHookContext = NULL;
    g_Bc250SdmaPresent.RingInSystemMemory = FALSE;
    g_Bc250SdmaPresent.SystemRingWork = NULL; /* retida: a engine pode ter apontado para la */
    Bc250SdmaPresentSwapRingWork(g_Bc250SdmaPresent.VramRing,
        g_Bc250SdmaPresent.VramRingGpuAddress, g_Bc250SdmaPresent.VramWork,
        g_Bc250SdmaPresent.VramWorkGpuAddress);
    if (Bc250SdmaPresentRestartAfterTdr()) {
        Bc250DisplayLog("[D2_ANEL_SISTEMA] TDR: anel de volta na VRAM e provado; a corrida segue SEM a D2\n");
        return TRUE;
    }
    Bc250DisplayLog("[D2_ANEL_SISTEMA] TDR: nem na VRAM o anel voltou; SDMA0 indisponivel\n");
    return FALSE;
#else
    return FALSE;
#endif
}

VOID Bc250SdmaPresentFreeSystemRingWork(_In_opt_ PVOID Memory)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    if (Memory != NULL && KeGetCurrentIrql() <= APC_LEVEL) {
        MmFreeContiguousMemorySpecifyCache(Memory, BC250_SDMA_PRESENT_BUFFER_TOTAL, MmNonCached);
    }
#else
    UNREFERENCED_PARAMETER(Memory);
#endif
}

NTSTATUS Bc250SdmaPresentRelocateRingToSystemMemory(
    _In_ PVOID Memory,
    _In_ PHYSICAL_ADDRESS Physical,
    _In_ uint64_t RingMc,
    _In_opt_ BC250_SDMA_RING_HOOK Hook,
    _In_opt_ PVOID HookContext)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    volatile uint32_t *sysRing;
    volatile uint32_t *sysWork;
    uint32_t rptrReg;
    uint32_t rptrWb;

    /* PREFLIGHT: anel provado, ocioso, ainda na VRAM. */
    if (Memory == NULL || !g_Bc250SdmaPresent.Initialized ||
        !g_Bc250SdmaPresent.HardwareEnabled ||
        !g_Bc250SdmaPresent.TripodValidated || g_Bc250SdmaPresent.Ring == NULL ||
        g_Bc250SdmaPresent.Work == NULL || g_Bc250SdmaPresent.MmioVirtual == NULL ||
        g_Bc250SdmaPresent.InFlightCount != 0u ||
        g_Bc250SdmaPresent.RingInSystemMemory ||
        (RingMc & 0xFFFull) != 0ull || KeGetCurrentIrql() > APC_LEVEL) {
        Bc250DisplayLog("[D2_ANEL_SISTEMA] RECUSADO no preflight: Memoria=%p Initialized=%u HardwareEnabled=%u Tripod=%u InFlight=%u JaNaRam=%u RingMc=0x%llX -- anel segue na VRAM\n",
            Memory,
            g_Bc250SdmaPresent.Initialized ? 1u : 0u,
            g_Bc250SdmaPresent.HardwareEnabled ? 1u : 0u,
            g_Bc250SdmaPresent.TripodValidated ? 1u : 0u,
            g_Bc250SdmaPresent.InFlightCount,
            g_Bc250SdmaPresent.RingInSystemMemory ? 1u : 0u,
            (unsigned long long)RingMc);
        if (Memory != NULL && KeGetCurrentIrql() <= APC_LEVEL) {
            MmFreeContiguousMemorySpecifyCache(Memory, BC250_SDMA_PRESENT_BUFFER_TOTAL, MmNonCached);
        }
        return STATUS_DEVICE_NOT_READY;
    }
    sysRing = (volatile uint32_t *)Memory;
    sysWork = (volatile uint32_t *)((volatile UCHAR *)Memory + BC250_SDMA_PRESENT_RING_BYTES);

    /* SNAPSHOT: enderecos da VRAM, para rollback e teardown. */
    g_Bc250SdmaPresent.VramRing = g_Bc250SdmaPresent.Ring;
    g_Bc250SdmaPresent.VramRingGpuAddress = g_Bc250SdmaPresent.RingGpuAddress;
    g_Bc250SdmaPresent.VramWork = g_Bc250SdmaPresent.Work;
    g_Bc250SdmaPresent.VramWorkGpuAddress = g_Bc250SdmaPresent.WorkGpuAddress;
    g_Bc250SdmaPresent.SystemRingWork = Memory;
    g_Bc250SdmaPresent.SystemRingWorkPhysical = Physical;
    g_Bc250SdmaPresent.SystemRingHook = Hook;
    g_Bc250SdmaPresent.SystemRingHookContext = HookContext;

    Bc250DisplayLog("[D2_ANEL_SISTEMA] APLICANDO: anel+Work %u bytes em RAM PA=0x%llX -> GART MC=0x%llX (VMID 0, contexto 0); antes VRAM RingGpu=0x%llX. As linhas TDR_* a seguir sao a reconstrucao do anel pela D2, NAO um TDR\n",
        (uint32_t)BC250_SDMA_PRESENT_BUFFER_TOTAL,
        (unsigned long long)Physical.QuadPart, (unsigned long long)RingMc,
        (unsigned long long)g_Bc250SdmaPresent.VramRingGpuAddress);

    /*
     * APPLY + READBACK + PROVA: o restart do TDR (soft reset, reprogramacao
     * integral, readback dos enderecos novos e NOP com fence sobre poison).
     */
    Bc250SdmaPresentSwapRingWork(sysRing, RingMc, sysWork,
        RingMc + BC250_SDMA_PRESENT_RING_BYTES);
    if (Bc250SdmaPresentRestartAfterTdr()) {
        rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
        rptrWb = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_RPTR_OFFSET / 4u];
        g_Bc250SdmaPresent.RingInSystemMemory = TRUE;
        Bc250DisplayLog("[D2_ANEL_SISTEMA] ATIVO: NOP com fence sobre poison na RAM concluido; RingGpu=0x%llX WorkGpu=0x%llX RPTR_Reg=%u RPTR_Wb_RAM=%u (%s). Fences e anel passam a ser lidos pela CPU na RAM, nao pela BAR\n",
            (unsigned long long)g_Bc250SdmaPresent.RingGpuAddress,
            (unsigned long long)g_Bc250SdmaPresent.WorkGpuAddress,
            rptrReg, rptrWb,
            (rptrReg % BC250_SDMA_PRESENT_RING_BYTES) == (rptrWb % BC250_SDMA_PRESENT_RING_BYTES) ?
                "writeback na RAM confere" : "writeback na RAM DIVERGE");
        return STATUS_SUCCESS;
    }

    /*
     * ROLLBACK: a GART sai ANTES de o anel voltar a VRAM (o gancho desliga o
     * contexto 0). Em 29/08 um contexto 0 ligado fez o SDMA0 falhar ate no
     * anel da VRAM; nao reconstruir com ele ligado.
     */
    Bc250DisplayLog("[D2_ANEL_SISTEMA] FALHOU a prova na RAM: desligando a GART e voltando o anel para a VRAM (RingGpu=0x%llX)\n",
        (unsigned long long)g_Bc250SdmaPresent.VramRingGpuAddress);
    if (Hook != NULL) {
        Hook(HookContext, BC250_SDMA_RING_HOOK_ROLLBACK);
    }
    Bc250SdmaPresentSwapRingWork(g_Bc250SdmaPresent.VramRing,
        g_Bc250SdmaPresent.VramRingGpuAddress, g_Bc250SdmaPresent.VramWork,
        g_Bc250SdmaPresent.VramWorkGpuAddress);
    g_Bc250SdmaPresent.SystemRingHook = NULL;
    g_Bc250SdmaPresent.SystemRingHookContext = NULL;
    if (Bc250SdmaPresentRestartAfterTdr()) {
        g_Bc250SdmaPresent.SystemRingWork = NULL;
        g_Bc250SdmaPresent.SystemRingWorkPhysical.QuadPart = 0;
        MmFreeContiguousMemorySpecifyCache(Memory, BC250_SDMA_PRESENT_BUFFER_TOTAL, MmNonCached);
        Bc250DisplayLog("[D2_ANEL_SISTEMA] NAO LIGOU: rollback provado, anel de volta na VRAM; a corrida segue SEM a D2\n");
        return STATUS_UNSUCCESSFUL;
    }
    /* A engine pode ter ficado apontando para a RAM: a memoria NAO e' liberada. */
    Bc250DisplayLog("[D2_ANEL_SISTEMA] ROLLBACK FALHOU: SDMA0 sem anel provado (HardwareEnabled=0); memoria da RAM mantida alocada de proposito (PA=0x%llX)\n",
        (unsigned long long)Physical.QuadPart);
    return STATUS_DEVICE_HARDWARE_ERROR;
#else
    UNREFERENCED_PARAMETER(Memory);
    UNREFERENCED_PARAMETER(Physical);
    UNREFERENCED_PARAMETER(RingMc);
    UNREFERENCED_PARAMETER(Hook);
    UNREFERENCED_PARAMETER(HookContext);
    return STATUS_NOT_SUPPORTED;
#endif
}

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
/*
 * D2 no teardown: a RAM so' volta ao sistema se a engine provadamente nao a
 * alcanca mais -- anel desligado, writeback do RPTR e poll do WPTR desligados,
 * enderecos devolvidos para a VRAM com readback, e engine ociosa. Qualquer
 * duvida vira vazamento declarado, nunca corrupcao de memoria do Windows.
 */
static VOID Bc250SdmaPresentReleaseSystemRingWork(void)
{
    uint32_t rbCntl, pollCntl, rbBase, rptrAddrLo, pollAddrLo, status;
    BOOLEAN safe;

    if (!g_Bc250SdmaPresent.RingInSystemMemory ||
        g_Bc250SdmaPresent.SystemRingWork == NULL) {
        return;
    }
    rbCntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        rbCntl & ~(F_RB_ENABLE | F_RPTR_WB));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_WPTR_POLL_CNTL - BC250_SDMA0_MMIO_WINDOW_START, 0u);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_BASE - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)(g_Bc250SdmaPresent.VramRingGpuAddress >> 8));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_BASE_HI - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)(g_Bc250SdmaPresent.VramRingGpuAddress >> 40));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RPTR_ADDR_HI - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)((g_Bc250SdmaPresent.VramWorkGpuAddress + BC250_SDMA_PRESENT_RPTR_OFFSET) >> 32));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RPTR_ADDR_LO - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)(g_Bc250SdmaPresent.VramWorkGpuAddress + BC250_SDMA_PRESENT_RPTR_OFFSET) & 0xFFFFFFFCu);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_WPTR_POLL_ADDR_HI - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)((g_Bc250SdmaPresent.VramWorkGpuAddress + BC250_SDMA_PRESENT_WPTR_OFFSET) >> 32));
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_WPTR_POLL_ADDR_LO - BC250_SDMA0_MMIO_WINDOW_START,
        (uint32_t)(g_Bc250SdmaPresent.VramWorkGpuAddress + BC250_SDMA_PRESENT_WPTR_OFFSET));

    rbCntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    pollCntl = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_WPTR_POLL_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    rbBase = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RB_BASE - BC250_SDMA0_MMIO_WINDOW_START);
    rptrAddrLo = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_RPTR_ADDR_LO - BC250_SDMA0_MMIO_WINDOW_START);
    pollAddrLo = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_WPTR_POLL_ADDR_LO - BC250_SDMA0_MMIO_WINDOW_START);
    status = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_STATUS - BC250_SDMA0_MMIO_WINDOW_START);
    safe = (rbCntl & (F_RB_ENABLE | F_RPTR_WB)) == 0u &&
        (pollCntl & F_POLL_ENABLE) == 0u &&
        rbBase == (uint32_t)(g_Bc250SdmaPresent.VramRingGpuAddress >> 8) &&
        rptrAddrLo == ((uint32_t)(g_Bc250SdmaPresent.VramWorkGpuAddress + BC250_SDMA_PRESENT_RPTR_OFFSET) & 0xFFFFFFFCu) &&
        pollAddrLo == (uint32_t)(g_Bc250SdmaPresent.VramWorkGpuAddress + BC250_SDMA_PRESENT_WPTR_OFFSET) &&
        (status & 0x1u) != 0u &&
        KeGetCurrentIrql() <= APC_LEVEL;
    if (g_Bc250SdmaPresent.SystemRingHook != NULL) {
        /* GART sai depois de a engine soltar a RAM e antes de liberar. */
        g_Bc250SdmaPresent.SystemRingHook(g_Bc250SdmaPresent.SystemRingHookContext,
            BC250_SDMA_RING_HOOK_TEARDOWN);
        g_Bc250SdmaPresent.SystemRingHook = NULL;
    }
    if (safe) {
        MmFreeContiguousMemorySpecifyCache(g_Bc250SdmaPresent.SystemRingWork,
            BC250_SDMA_PRESENT_BUFFER_TOTAL, MmNonCached);
        Bc250DisplayLog("[D2_ANEL_SISTEMA] TEARDOWN: engine desligada e enderecos devolvidos a VRAM com readback; RAM liberada (PA=0x%llX)\n",
            (unsigned long long)g_Bc250SdmaPresent.SystemRingWorkPhysical.QuadPart);
    } else {
        Bc250DisplayLog("[D2_ANEL_SISTEMA] TEARDOWN: NAO liberada (RB_CNTL=%08X POLL=%08X BASE=%08X RPTR_ADDR=%08X POLL_ADDR=%08X STATUS=%08X IRQL=%u); vazamento declarado de %u bytes em vez de risco de escrita da GPU em memoria devolvida\n",
            rbCntl, pollCntl, rbBase, rptrAddrLo, pollAddrLo, status,
            (uint32_t)KeGetCurrentIrql(), (uint32_t)BC250_SDMA_PRESENT_BUFFER_TOTAL);
    }
    g_Bc250SdmaPresent.SystemRingWork = NULL;
    g_Bc250SdmaPresent.RingInSystemMemory = FALSE;
    g_Bc250SdmaPresent.Ring = g_Bc250SdmaPresent.VramRing;
    g_Bc250SdmaPresent.RingGpuAddress = g_Bc250SdmaPresent.VramRingGpuAddress;
    g_Bc250SdmaPresent.Work = g_Bc250SdmaPresent.VramWork;
    g_Bc250SdmaPresent.WorkGpuAddress = g_Bc250SdmaPresent.VramWorkGpuAddress;
}
#endif

/*
 * =====================================================================
 * P3.1 (doc 609) -- anel 0 do IH, como navi10_ih.c (Linux v6.6) numa APU.
 *
 * Sequencia (navi10_ih_irq_init): desliga o anel; nbio_v2_3_ih_control
 * (INTERRUPT_CNTL2 = pagina dummy >> 8; DUMMY_RD_OVERRIDE=0, REQ_NONSNOOP=0);
 * navi10_ih_enable_ring (BASE = va >> 8, BASE_HI = va >> 40, RB_CNTL de
 * navi10_ih_rb_cntl com MC_SPACE=4 porque APU usa endereco virtual, writeback
 * do WPTR, WPTR=RPTR=0, DOORBELL_RPTR com ENABLE=0); liga RB_ENABLE,
 * RB_GPU_TS_ENABLE e ENABLE_INTR. P3.1 publicava o RPTR por MMIO (IH_RB_RPTR).
 * P3.3 (doc 635): com RptrByDoorbell, DOORBELL_RPTR = OFFSET|ENABLE em
 * navi10_ih_enable_ring, BIF_IH_DOORBELL_RANGE depois do anel e antes de ligar
 * (nbio_v2_3_ih_doorbell_range), e o ISR escreve o RPTR pela doorbell
 * (navi10_ih_set_rptr). Prova: IH_RB_RPTR lido = RPTR publicado (nao-zero),
 * a mesma conferencia de navi10_ih_irq_rearm. Sem prova, volta ao MMIO.
 * =====================================================================
 */
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
static uint32_t Bc250IhRead(uint32_t Reg)
{
    return Bc250SdmaRegRead(&g_Bc250SdmaPresent, Reg - BC250_SDMA0_MMIO_WINDOW_START);
}
static VOID Bc250IhWrite(uint32_t Reg, uint32_t Value)
{
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent, Reg - BC250_SDMA0_MMIO_WINDOW_START, Value);
}

/* C1 (doc 672): ver o header. */
BOOLEAN Bc250SdmaPresentDoorbellApertureReady(void)
{
    uint32_t aper;
    if (!g_Bc250SdmaPresent.Initialized || g_Bc250SdmaPresent.DoorbellVirtual == NULL) {
        return FALSE;
    }
    aper = Bc250IhRead(BC250_NBIO_REG_RCC_DOORBELL_APER_EN);
    return ((aper & BC250_NBIO_DOORBELL_APER_EN_MASK) != 0u) ? TRUE : FALSE;
}

BOOLEAN Bc250SdmaPresentDoorbellWrite64(_In_ uint32_t ByteOffset, _In_ uint64_t Value)
{
    if (g_Bc250SdmaPresent.DoorbellVirtual == NULL || (ByteOffset & 7u) != 0u ||
        ByteOffset > BC250_P2_DOORBELL_MAP_BYTES - 8u) {
        return FALSE;
    }
    KeMemoryBarrier();
    *(volatile uint64_t *)(g_Bc250SdmaPresent.DoorbellVirtual + ByteOffset) = Value;
    KeMemoryBarrier();
    return TRUE;
}
/* navi10_ih_set_rptr: doorbell quando provada, MMIO caso contrario. DIRQL-safe. */
static VOID Bc250IhPublishRptr(uint32_t Rptr)
{
    if (g_Bc250SdmaPresent.IhRptrDoorbellActive && g_Bc250SdmaPresent.DoorbellVirtual != NULL) {
        *(volatile uint32_t *)(g_Bc250SdmaPresent.DoorbellVirtual + BC250_P33_IH_DOORBELL_BYTE_OFFSET) = Rptr;
        InterlockedIncrement64(&g_Bc250SdmaPresent.IhRptrDoorbellWrites);
        return;
    }
    Bc250IhWrite(BC250_IH_REG_RB_RPTR, Rptr);
}
/* Devolve DOORBELL_RPTR e BIF_IH_DOORBELL_RANGE ao snapshot; RPTR volta ao MMIO. */
static VOID Bc250IhRptrDoorbellRestore(void)
{
    g_Bc250SdmaPresent.IhRptrDoorbellActive = FALSE;
    KeMemoryBarrier();
    if (!g_Bc250SdmaPresent.IhRptrDoorbellCaptured) {
        return;
    }
    Bc250IhWrite(BC250_IH_REG_DOORBELL_RPTR, g_Bc250SdmaPresent.IhDoorbellRptrBefore);
    Bc250IhWrite(BC250_NBIO_REG_BIF_IH_DOORBELL_RANGE, g_Bc250SdmaPresent.IhBifRangeBefore);
    g_Bc250SdmaPresent.IhRptrDoorbellCaptured = FALSE;
}
static uint32_t Bc250IhLog2(ULONG Value)
{
    uint32_t n = 0u;
    while (Value > 1u) { Value >>= 1u; ++n; }
    return n;
}
#endif

BOOLEAN Bc250SdmaPresentIhArm(
    _In_ volatile uint32_t *RingCpu,
    _In_ uint64_t RingGpuVa,
    _In_ ULONG RingBytes,
    _In_ volatile uint32_t *WptrCpu,
    _In_ uint64_t WptrGpuVa,
    _In_ uint64_t DummyPagePa,
    _In_ BOOLEAN Msi,
    _In_ BOOLEAN RptrByDoorbell)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    uint32_t rbCntl, intCntl, cntl, rd[4];
    ULONG poll;
    uint32_t wptrBefore, wptrAfter = 0u;
    BOOLEAN nopOk;
    BOOLEAN dbWanted = FALSE;
    uint32_t dbRptrWant = 0u, bifWant = 0u, dbRd[2] = { 0u, 0u };
    uint32_t dbProofRptr = 0u, dbProofReg = 0u;

    if (!g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled ||
        g_Bc250SdmaPresent.MmioVirtual == NULL || RingCpu == NULL || WptrCpu == NULL ||
        RingBytes < 4096u || (RingBytes & (RingBytes - 1u)) != 0u || RingBytes > 0x40000u ||
        (RingGpuVa & 0xFFull) != 0ull || (WptrGpuVa & 0x3ull) != 0ull || DummyPagePa == 0ull) {
        Bc250DisplayLog("[P3_IH] NAO TENTADO: pre-condicao (Init=%u Hw=%u Bytes=%lu Va=0x%llX Wptr=0x%llX Dummy=0x%llX)\n",
            g_Bc250SdmaPresent.Initialized ? 1u : 0u, g_Bc250SdmaPresent.HardwareEnabled ? 1u : 0u,
            RingBytes, RingGpuVa, WptrGpuVa, DummyPagePa);
        return FALSE;
    }

    /* Snapshot para o desarme. */
    g_Bc250SdmaPresent.IhRbCntlBefore = Bc250IhRead(BC250_IH_REG_RB_CNTL);
    g_Bc250SdmaPresent.IhIntCntlBefore = Bc250IhRead(BC250_NBIO_REG_INTERRUPT_CNTL);
    g_Bc250SdmaPresent.IhIntCntl2Before = Bc250IhRead(BC250_NBIO_REG_INTERRUPT_CNTL2);
    g_Bc250SdmaPresent.IhBeforeCaptured = TRUE;

    /* P3.3: so' com a BAR de doorbell mapeada e a abertura do BIF ligada (P2c). */
    g_Bc250SdmaPresent.IhRptrDoorbellActive = FALSE;
    g_Bc250SdmaPresent.IhRptrDoorbellCaptured = FALSE;
    if (RptrByDoorbell) {
        uint32_t aper = Bc250IhRead(BC250_NBIO_REG_RCC_DOORBELL_APER_EN);
        if (g_Bc250SdmaPresent.DoorbellVirtual == NULL || (aper & BC250_NBIO_DOORBELL_APER_EN_MASK) == 0u) {
            Bc250DisplayLog("[P3_3_IH_DOORBELL] NAO TENTADO: BAR de doorbell %s, RCC_DOORBELL_APER_EN=0x%08X -- RPTR do IH segue por MMIO\n",
                g_Bc250SdmaPresent.DoorbellVirtual != NULL ? "mapeada" : "NAO mapeada", aper);
        } else {
            g_Bc250SdmaPresent.IhDoorbellRptrBefore = Bc250IhRead(BC250_IH_REG_DOORBELL_RPTR);
            g_Bc250SdmaPresent.IhBifRangeBefore = Bc250IhRead(BC250_NBIO_REG_BIF_IH_DOORBELL_RANGE);
            g_Bc250SdmaPresent.IhRptrDoorbellCaptured = TRUE;
            dbRptrWant = (BC250_P33_IH_DOORBELL_INDEX & BC250_IH_DOORBELL_RPTR_OFFSET_MASK) |
                BC250_IH_DOORBELL_RPTR_ENABLE;
            bifWant = (g_Bc250SdmaPresent.IhBifRangeBefore &
                    ~(BC250_BIF_DOORBELL_RANGE_OFFSET_MASK | BC250_BIF_DOORBELL_RANGE_SIZE_MASK)) |
                ((BC250_P33_IH_DOORBELL_INDEX << 2) & BC250_BIF_DOORBELL_RANGE_OFFSET_MASK) |
                ((BC250_P33_IH_DOORBELL_RANGE_SIZE << 16) & BC250_BIF_DOORBELL_RANGE_SIZE_MASK);
            dbWanted = TRUE;
        }
    }

    g_Bc250SdmaPresent.IhRing = RingCpu;
    g_Bc250SdmaPresent.IhRingGpuVa = RingGpuVa;
    g_Bc250SdmaPresent.IhRingBytes = RingBytes;
    g_Bc250SdmaPresent.IhWptrCpu = WptrCpu;
    g_Bc250SdmaPresent.IhWptrGpuVa = WptrGpuVa;
    g_Bc250SdmaPresent.IhMsi = Msi;
    g_Bc250SdmaPresent.IhRptr = 0u;
    *WptrCpu = 0u;
    KeMemoryBarrier();

    /* 1. navi10_ih_toggle_interrupts(false) */
    rbCntl = g_Bc250SdmaPresent.IhRbCntlBefore;
    rbCntl &= ~(BC250_IH_RB_CNTL_RB_ENABLE | BC250_IH_RB_CNTL_ENABLE_INTR);
    Bc250IhWrite(BC250_IH_REG_RB_CNTL, rbCntl);
    Bc250IhWrite(BC250_IH_REG_RB_RPTR, 0u);
    Bc250IhWrite(BC250_IH_REG_RB_WPTR, 0u);

    /* 2. nbio_v2_3_ih_control */
    Bc250IhWrite(BC250_NBIO_REG_INTERRUPT_CNTL2, (uint32_t)(DummyPagePa >> 8));
    intCntl = g_Bc250SdmaPresent.IhIntCntlBefore &
        ~(BC250_INTERRUPT_CNTL_IH_DUMMY_RD_OVERRIDE | BC250_INTERRUPT_CNTL_IH_REQ_NONSNOOP_EN);
    Bc250IhWrite(BC250_NBIO_REG_INTERRUPT_CNTL, intCntl);

    /* 3. navi10_ih_enable_ring */
    Bc250IhWrite(BC250_IH_REG_RB_BASE, (uint32_t)(RingGpuVa >> 8));
    Bc250IhWrite(BC250_IH_REG_RB_BASE_HI, (uint32_t)((RingGpuVa >> 40) & 0xFFull));
    rbCntl = Bc250IhRead(BC250_IH_REG_RB_CNTL);
    rbCntl = (rbCntl & ~BC250_IH_RB_CNTL_MC_SPACE_MASK) | (4u << BC250_IH_RB_CNTL_MC_SPACE_SHIFT);
    rbCntl |= BC250_IH_RB_CNTL_WPTR_OVERFLOW_CLEAR | BC250_IH_RB_CNTL_WPTR_OVERFLOW_ENABLE;
    rbCntl = (rbCntl & ~BC250_IH_RB_CNTL_RB_SIZE_MASK) |
        ((Bc250IhLog2(RingBytes / 4u) << BC250_IH_RB_CNTL_RB_SIZE_SHIFT) & BC250_IH_RB_CNTL_RB_SIZE_MASK);
    rbCntl |= BC250_IH_RB_CNTL_WPTR_WRITEBACK_ENABLE | BC250_IH_RB_CNTL_MC_SNOOP;
    rbCntl &= ~(BC250_IH_RB_CNTL_MC_RO | BC250_IH_RB_CNTL_MC_VMID_MASK);
    if (Msi) rbCntl |= BC250_IH_RB_CNTL_RPTR_REARM; else rbCntl &= ~BC250_IH_RB_CNTL_RPTR_REARM;
    Bc250IhWrite(BC250_IH_REG_RB_CNTL, rbCntl);
    Bc250IhWrite(BC250_IH_REG_RB_WPTR_ADDR_LO, (uint32_t)WptrGpuVa);
    Bc250IhWrite(BC250_IH_REG_RB_WPTR_ADDR_HI, (uint32_t)((WptrGpuVa >> 32) & 0xFFFFull));
    Bc250IhWrite(BC250_IH_REG_RB_WPTR, 0u);
    Bc250IhWrite(BC250_IH_REG_RB_RPTR, 0u);
    Bc250IhWrite(BC250_IH_REG_DOORBELL_RPTR, dbWanted ? dbRptrWant : 0u);

    /* 3b. P3.3: nbio_v2_3_ih_doorbell_range, depois do anel e antes de ligar. */
    if (dbWanted) {
        Bc250IhWrite(BC250_NBIO_REG_BIF_IH_DOORBELL_RANGE, bifWant);
        dbRd[0] = Bc250IhRead(BC250_IH_REG_DOORBELL_RPTR);
        dbRd[1] = Bc250IhRead(BC250_NBIO_REG_BIF_IH_DOORBELL_RANGE);
        if ((dbRd[0] & (BC250_IH_DOORBELL_RPTR_OFFSET_MASK | BC250_IH_DOORBELL_RPTR_ENABLE)) != dbRptrWant ||
            (dbRd[1] & (BC250_BIF_DOORBELL_RANGE_OFFSET_MASK | BC250_BIF_DOORBELL_RANGE_SIZE_MASK)) !=
                (bifWant & (BC250_BIF_DOORBELL_RANGE_OFFSET_MASK | BC250_BIF_DOORBELL_RANGE_SIZE_MASK))) {
            Bc250DisplayLog("[P3_3_IH_DOORBELL] RECUSADO: readback DOORBELL_RPTR=0x%08X (quer 0x%08X) BIF_IH_DOORBELL_RANGE=0x%08X (quer 0x%08X) -- RPTR do IH segue por MMIO\n",
                dbRd[0], dbRptrWant, dbRd[1], bifWant);
            Bc250IhRptrDoorbellRestore();
            dbWanted = FALSE;
        } else {
            /* Provisorio: o ISR da prova ja' publica pela doorbell. */
            g_Bc250SdmaPresent.IhRptrDoorbellActive = TRUE;
            KeMemoryBarrier();
        }
    }

    /* 4. navi10_ih_toggle_interrupts(true) */
    rbCntl = Bc250IhRead(BC250_IH_REG_RB_CNTL);
    rbCntl |= BC250_IH_RB_CNTL_RB_ENABLE | BC250_IH_RB_CNTL_RB_GPU_TS_ENABLE | BC250_IH_RB_CNTL_ENABLE_INTR;
    Bc250IhWrite(BC250_IH_REG_RB_CNTL, rbCntl);

    rd[0] = Bc250IhRead(BC250_IH_REG_RB_CNTL);
    rd[1] = Bc250IhRead(BC250_IH_REG_RB_BASE);
    rd[2] = Bc250IhRead(BC250_IH_REG_RB_WPTR_ADDR_LO);
    rd[3] = Bc250IhRead(BC250_NBIO_REG_INTERRUPT_CNTL2);
    if ((rd[0] & BC250_IH_RB_CNTL_RB_ENABLE) == 0u || (rd[0] & BC250_IH_RB_CNTL_ENABLE_INTR) == 0u ||
        rd[1] != (uint32_t)(RingGpuVa >> 8) || rd[2] != (uint32_t)WptrGpuVa ||
        rd[3] != (uint32_t)(DummyPagePa >> 8)) {
        Bc250DisplayLog("[P3_IH] RECUSADO: readback RB_CNTL=0x%08X BASE=0x%08X WPTR_ADDR_LO=0x%08X INT_CNTL2=0x%08X\n",
            rd[0], rd[1], rd[2], rd[3]);
        Bc250SdmaPresentIhDisarm("readback");
        return FALSE;
    }
    g_Bc250SdmaPresent.IhArmed = TRUE;

    /* 5. TRAP do SDMA0 (sdma_v5_0_set_trap_irq_state) e a prova: um NOP com TRAP. */
    cntl = Bc250IhRead(BC250_SDMA0_REG_CNTL);
    Bc250IhWrite(BC250_SDMA0_REG_CNTL, cntl | BC250_SDMA0_CNTL_TRAP_ENABLE);
    g_Bc250SdmaPresent.TrapArmed = TRUE;
    KeMemoryBarrier();
    wptrBefore = *WptrCpu;
    nopOk = Bc250SdmaPresentSyncNop();
    for (poll = 0u; poll < 20000u; ++poll) {          /* ~100 ms */
        wptrAfter = *WptrCpu;
        if (wptrAfter != wptrBefore || g_Bc250SdmaPresent.IhSdmaTraps != 0) break;
        KeStallExecutionProcessor(5u);
    }
    if (wptrAfter == wptrBefore && g_Bc250SdmaPresent.IhSdmaTraps == 0) {
        Bc250DisplayLog("[P3_IH] NAO ATIVA: o NOP com TRAP (fence %s) nao fez o IH escrever no anel em ~100 ms "
            "(WPTR writeback 0x%08X, IH_RB_WPTR=0x%08X, RB_CNTL=0x%08X, SDMA0_CNTL=0x%08X)\n",
            nopOk ? "ok" : "FALHOU", wptrAfter, Bc250IhRead(BC250_IH_REG_RB_WPTR), rd[0],
            Bc250IhRead(BC250_SDMA0_REG_CNTL));
        Bc250SdmaPresentIhDisarm("prova");
        return FALSE;
    }
    /*
     * P3.3, prova: o ISR consumiu o vetor e publicou o RPTR pela doorbell; o
     * IH_RB_RPTR lido tem de ser esse valor (navi10_ih_irq_rearm faz a mesma
     * conferencia). RPTR 0 nao prova nada (zero contra zero): exige nao-zero.
     */
    if (dbWanted) {
        BOOLEAN dbOk = FALSE;
        for (poll = 0u; poll < 20000u; ++poll) {      /* ~100 ms */
            dbProofRptr = g_Bc250SdmaPresent.IhRptr;
            dbProofReg = Bc250IhRead(BC250_IH_REG_RB_RPTR) & 0x0003FFFCu;
            if (dbProofRptr != 0u && dbProofReg == dbProofRptr &&
                g_Bc250SdmaPresent.IhRptrDoorbellWrites != 0) {
                dbOk = TRUE;
                break;
            }
            KeStallExecutionProcessor(5u);
        }
        if (dbOk) {
            Bc250DisplayLog("[P3_3_IH_DOORBELL] ATIVO: DOORBELL_RPTR=0x%08X BIF_IH_DOORBELL_RANGE=0x%08X (indice 0x%X, byte 0x%X da BAR); prova: RPTR publicado pela doorbell=0x%X == IH_RB_RPTR lido=0x%X, escritas pela doorbell=%lld\n",
                dbRd[0], dbRd[1], BC250_P33_IH_DOORBELL_INDEX, BC250_P33_IH_DOORBELL_BYTE_OFFSET,
                dbProofRptr, dbProofReg, g_Bc250SdmaPresent.IhRptrDoorbellWrites);
        } else {
            Bc250DisplayLog("[P3_3_IH_DOORBELL] NAO ATIVA: em ~100 ms o IH_RB_RPTR lido=0x%X nao igualou o RPTR publicado=0x%X (escritas pela doorbell=%lld, ISR=%lld) -- doorbell desfeita, RPTR do IH volta ao MMIO\n",
                dbProofReg, dbProofRptr, g_Bc250SdmaPresent.IhRptrDoorbellWrites, g_Bc250SdmaPresent.IhIsrCalls);
            Bc250IhRptrDoorbellRestore();
            Bc250IhWrite(BC250_IH_REG_RB_RPTR, g_Bc250SdmaPresent.IhRptr);
            dbWanted = FALSE;
        }
    }
    Bc250DisplayLog("[P3_IH] ATIVO: anel 0 do IH na GART VA=0x%llX (%lu bytes, MC_SPACE=4, VMID 0), writeback do WPTR em VA=0x%llX, "
        "RB_CNTL=0x%08X, INTERRUPT_CNTL2=dummy 0x%llX>>8, %s; prova: NOP com TRAP -> IH escreveu (WPTR 0x%X -> 0x%X, ISR=%lld, traps vistos=%lld); "
        "RPTR %s\n",
        RingGpuVa, RingBytes, WptrGpuVa, rd[0], DummyPagePa, Msi ? "MSI (RPTR_REARM=1)" : "linha (RPTR_REARM=0)",
        wptrBefore, wptrAfter, g_Bc250SdmaPresent.IhIsrCalls, g_Bc250SdmaPresent.IhSdmaTraps,
        dbWanted ? "pela doorbell (P3.3, como o amdgpu)" : "por MMIO (diferenca declarada: amdgpu usa doorbell)");
    return TRUE;
#else
    UNREFERENCED_PARAMETER(RingCpu);
    UNREFERENCED_PARAMETER(RingGpuVa);
    UNREFERENCED_PARAMETER(RingBytes);
    UNREFERENCED_PARAMETER(WptrCpu);
    UNREFERENCED_PARAMETER(WptrGpuVa);
    UNREFERENCED_PARAMETER(DummyPagePa);
    UNREFERENCED_PARAMETER(Msi);
    UNREFERENCED_PARAMETER(RptrByDoorbell);
    return FALSE;
#endif
}

VOID Bc250SdmaPresentIhDisarm(_In_z_ const char *Why)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    uint32_t rbCntl;
    if (!g_Bc250SdmaPresent.IhBeforeCaptured || g_Bc250SdmaPresent.MmioVirtual == NULL) {
        return;
    }
    /* TRAP primeiro: nenhum vetor novo depois daqui. */
    g_Bc250SdmaPresent.TrapArmed = FALSE;
    KeMemoryBarrier();
    Bc250IhWrite(BC250_SDMA0_REG_CNTL, Bc250IhRead(BC250_SDMA0_REG_CNTL) & ~BC250_SDMA0_CNTL_TRAP_ENABLE);
    /* navi10_ih_irq_disable: ENABLE_INTR e RB_ENABLE a 0, RPTR=WPTR=0. */
    rbCntl = Bc250IhRead(BC250_IH_REG_RB_CNTL);
    Bc250IhWrite(BC250_IH_REG_RB_CNTL, rbCntl & ~(BC250_IH_RB_CNTL_RB_ENABLE | BC250_IH_RB_CNTL_ENABLE_INTR));
    /* P3.3: doorbell do RPTR e faixa do BIF de volta ao snapshot antes do RPTR=0 por MMIO. */
    Bc250IhRptrDoorbellRestore();
    Bc250IhWrite(BC250_IH_REG_RB_RPTR, 0u);
    Bc250IhWrite(BC250_IH_REG_RB_WPTR, 0u);
    g_Bc250SdmaPresent.IhArmed = FALSE;
    KeStallExecutionProcessor(1000u); /* navi10_ih_irq_disable: mdelay(1) */
    /* Doc 658 (auditoria 657): o ISR testa IhRing != NULL; zerar aqui fecha a
     * janela entre o desarme e o MmFreeContiguousMemory do anel. */
    InterlockedExchangePointer((PVOID volatile *)&g_Bc250SdmaPresent.IhRing, NULL);
    Bc250IhWrite(BC250_IH_REG_RB_CNTL, g_Bc250SdmaPresent.IhRbCntlBefore &
        ~(BC250_IH_RB_CNTL_RB_ENABLE | BC250_IH_RB_CNTL_ENABLE_INTR));
    Bc250IhWrite(BC250_NBIO_REG_INTERRUPT_CNTL, g_Bc250SdmaPresent.IhIntCntlBefore);
    Bc250IhWrite(BC250_NBIO_REG_INTERRUPT_CNTL2, g_Bc250SdmaPresent.IhIntCntl2Before);
    g_Bc250SdmaPresent.IhBeforeCaptured = FALSE;
    Bc250DisplayLog("[P3_IH] DESARMADO (%s): TRAP e anel desligados, NBIO de volta ao snapshot; ISR=%lld reclamadas=%lld vetores=%lld traps_sdma=%lld eop_cp=%lld outros=%lld overflows=%lld traps_emitidos=%lld\n",
        Why, g_Bc250SdmaPresent.IhIsrCalls, g_Bc250SdmaPresent.IhIsrClaimed, g_Bc250SdmaPresent.IhEntries,
        g_Bc250SdmaPresent.IhSdmaTraps, g_Bc250SdmaPresent.IhCpEops, g_Bc250SdmaPresent.IhOther,
        g_Bc250SdmaPresent.IhOverflows, g_Bc250SdmaPresent.TrapsEmitted);
#else
    UNREFERENCED_PARAMETER(Why);
#endif
}

/*
 * I.IH (doc 630): na 239 o IH recebeu 992 vetores SE0SH/SE1SH fonte 0xEF
 * (SQ_INTERRUPT_ID) depois do reset do GFX, e so' cliente/fonte ficavam
 * guardados. As primeiras entradas "outras" sao copiadas inteiras aqui (o
 * ISR roda em DIRQL e nao pode logar); o relatorio do IH as publica.
 */
#define BC250_IH_OTHER_SAMPLES 8u
typedef struct _BC250_IH_OTHER_SAMPLE {
    uint32_t Dw[BC250_IH_ENTRY_BYTES / 4u];
    ULONGLONG When100ns;
} BC250_IH_OTHER_SAMPLE;
static BC250_IH_OTHER_SAMPLE g_Bc250IhOtherSamples[BC250_IH_OTHER_SAMPLES];
static volatile LONG g_Bc250IhOtherCaptured = 0;
static volatile LONG g_Bc250IhOtherReady = 0;
static volatile LONG g_Bc250IhOtherLogged = 0;

/* DIRQL: sem log, sem lock de DISPATCH. So' leituras da RAM do anel e MMIO. */
BOOLEAN Bc250SdmaPresentIhIsr(ULONG *SdmaTrapsThisCall, ULONG *GfxEopsThisCall)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    uint32_t wptr, rptr, mask, budget;
    ULONG traps = 0u;
    ULONG gfxEops = 0u;
    BOOLEAN claimed;
    if (SdmaTrapsThisCall != NULL) *SdmaTrapsThisCall = 0u;
    if (GfxEopsThisCall != NULL) *GfxEopsThisCall = 0u;
    if (!g_Bc250SdmaPresent.IhArmed || g_Bc250SdmaPresent.IhRing == NULL ||
        g_Bc250SdmaPresent.IhWptrCpu == NULL) {
        return FALSE;
    }
    InterlockedIncrement64(&g_Bc250SdmaPresent.IhIsrCalls);
    mask = g_Bc250SdmaPresent.IhRingBytes - 1u;
    rptr = g_Bc250SdmaPresent.IhRptr;
    /* navi10_ih_get_wptr: writeback; em overflow, confere o registrador. */
    wptr = *g_Bc250SdmaPresent.IhWptrCpu;
    if ((wptr & BC250_IH_RB_WPTR_RB_OVERFLOW) != 0u) {
        wptr = Bc250IhRead(BC250_IH_REG_RB_WPTR);
        if ((wptr & BC250_IH_RB_WPTR_RB_OVERFLOW) != 0u) {
            wptr &= ~BC250_IH_RB_WPTR_RB_OVERFLOW;
            rptr = (wptr + BC250_IH_ENTRY_BYTES) & mask;
            InterlockedIncrement64(&g_Bc250SdmaPresent.IhOverflows);
            Bc250IhWrite(BC250_IH_REG_RB_CNTL,
                Bc250IhRead(BC250_IH_REG_RB_CNTL) | BC250_IH_RB_CNTL_WPTR_OVERFLOW_CLEAR);
        }
    }
    wptr &= mask;
    claimed = (wptr != rptr) ? TRUE : FALSE;
    budget = g_Bc250SdmaPresent.IhRingBytes / BC250_IH_ENTRY_BYTES;
    while (rptr != wptr && budget-- != 0u) {
        uint32_t dw0 = g_Bc250SdmaPresent.IhRing[rptr / 4u];
        uint32_t client = dw0 & 0xFFu;
        uint32_t src = (dw0 >> 8) & 0xFFu;
        InterlockedIncrement64(&g_Bc250SdmaPresent.IhEntries);
        if (client == BC250_IH_CLIENT_SDMA0 && src == BC250_IH_SRC_SDMA_TRAP) {
            InterlockedIncrement64(&g_Bc250SdmaPresent.IhSdmaTraps);
            ++traps;
        } else if (client == BC250_IH_CLIENT_GRBM_CP && src == BC250_IH_SRC_CP_EOP) {
            InterlockedIncrement64(&g_Bc250SdmaPresent.IhCpEops);
            /* gfx_v10_0_eop_irq: ring_id [23:16]; me = (ring_id & 0x0C) >> 2,
             * pipe = ring_id & 0x03. O anel grafico e' o me 0, pipe 0. */
            if ((((dw0 >> 16) & 0xFFu) & 0x0Fu) == 0u) {
                InterlockedIncrement64(&g_Bc250SdmaPresent.IhGfxEops);
                ++gfxEops;
            }
        } else if (client == BC250_IH_CLIENT_GRBM_CP &&
                   (src == BC250_IH_SRC_CP_PRIV_REG || src == BC250_IH_SRC_CP_PRIV_INSTR ||
                    src == BC250_IH_SRC_CP_BAD_OPCODE || src == BC250_IH_SRC_CP_RSVD_BIT)) {
            /* C06 (doc 764): so' conta e guarda a entrada (DIRQL, sem log). */
            if (src == BC250_IH_SRC_CP_PRIV_REG) InterlockedIncrement64(&g_Bc250SdmaPresent.IhCpPrivReg);
            else if (src == BC250_IH_SRC_CP_PRIV_INSTR) InterlockedIncrement64(&g_Bc250SdmaPresent.IhCpPrivInstr);
            else if (src == BC250_IH_SRC_CP_BAD_OPCODE) InterlockedIncrement64(&g_Bc250SdmaPresent.IhCpBadOpcode);
            else InterlockedIncrement64(&g_Bc250SdmaPresent.IhCpRsvdBit);
            InterlockedExchange(&g_Bc250SdmaPresent.IhCpErrLastDw0, (LONG)dw0);
            InterlockedExchange(&g_Bc250SdmaPresent.IhCpErrLastDw1,
                (LONG)g_Bc250SdmaPresent.IhRing[rptr / 4u + 1u]);
        } else if (client == 0x04u &&
                   Bc250DisplayDcnIsr(src, g_Bc250SdmaPresent.IhRing[rptr / 4u + 4u])) {
            /* DCN.2/DCN.3 (docs 713/714): cliente DCE; o ext_id e' o src_data[0]
             * (dw4 da entrada), como o amdgpu_dm passa a dc_interrupt_to_irq_source. */
            InterlockedIncrement64(&g_Bc250SdmaPresent.IhDcn);
        } else {
            LONG slot;
            InterlockedIncrement64(&g_Bc250SdmaPresent.IhOther);
            InterlockedExchange(&g_Bc250SdmaPresent.IhLastClientSrc, (LONG)((client << 8) | src));
            slot = InterlockedIncrement(&g_Bc250IhOtherCaptured) - 1;
            if (slot >= 0 && (ULONG)slot < BC250_IH_OTHER_SAMPLES) {
                uint32_t k;
                for (k = 0u; k < BC250_IH_ENTRY_BYTES / 4u; ++k) {
                    g_Bc250IhOtherSamples[slot].Dw[k] = g_Bc250SdmaPresent.IhRing[rptr / 4u + k];
                }
                g_Bc250IhOtherSamples[slot].When100ns = KeQueryInterruptTime();
                InterlockedIncrement(&g_Bc250IhOtherReady);
            }
        }
        rptr = (rptr + BC250_IH_ENTRY_BYTES) & mask;
    }
    g_Bc250SdmaPresent.IhRptr = rptr;
    Bc250IhPublishRptr(rptr);   /* navi10_ih_set_rptr: doorbell (P3.3) ou MMIO */
    if (claimed) InterlockedIncrement64(&g_Bc250SdmaPresent.IhIsrClaimed);
    if (SdmaTrapsThisCall != NULL) *SdmaTrapsThisCall = traps;
    if (GfxEopsThisCall != NULL) *GfxEopsThisCall = gfxEops;
    /* MSI: a mensagem e' nossa. Linha: so' se havia vetor. */
    return (g_Bc250SdmaPresent.IhMsi || claimed) ? TRUE : FALSE;
#else
    if (SdmaTrapsThisCall != NULL) *SdmaTrapsThisCall = 0u;
    if (GfxEopsThisCall != NULL) *GfxEopsThisCall = 0u;
    return FALSE;
#endif
}

VOID Bc250SdmaPresentIhReport(_In_z_ const char *Why)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    static LONG64 s_lastTraps = 0;
    static volatile LONG64 s_lastReport = 0;
    LONG64 now, last;
    if (!g_Bc250SdmaPresent.IhArmed) {
        return;
    }
    /* I.IH: entradas "outras" capturadas saem ja', antes do limite de 10 s
     * (na 239 a trava veio 9 s depois do ultimo TDR). */
    for (;;) {
        LONG logged = InterlockedCompareExchange(&g_Bc250IhOtherLogged, 0, 0);
        LONG ready = InterlockedCompareExchange(&g_Bc250IhOtherReady, 0, 0);
        const BC250_IH_OTHER_SAMPLE *s;
        if (logged >= ready || (ULONG)logged >= BC250_IH_OTHER_SAMPLES ||
            InterlockedCompareExchange(&g_Bc250IhOtherLogged, logged + 1, logged) != logged) {
            break;
        }
        s = &g_Bc250IhOtherSamples[logged];
        Bc250DisplayLog("[P3_IH] OUTRO #%ld t=%llu cliente=0x%02X fonte=0x%02X (0x0A/0x0B fonte 0xEF = SQ_INTERRUPT_ID) dw=%08X %08X %08X %08X %08X %08X %08X %08X\n",
            logged + 1, (unsigned long long)s->When100ns,
            s->Dw[0] & 0xFFu, (s->Dw[0] >> 8) & 0xFFu,
            s->Dw[0], s->Dw[1], s->Dw[2], s->Dw[3], s->Dw[4], s->Dw[5], s->Dw[6], s->Dw[7]);
    }
    /* Uma linha a cada 10 s, de qualquer CPU (o chamador roda em DPC). */
    now = (LONG64)KeQueryInterruptTime();
    last = s_lastReport;
    if (now - last < 100000000LL ||
        InterlockedCompareExchange64(&s_lastReport, now, last) != last) {
        return;
    }
    Bc250DisplayLog("[P3_IH] %s: ISR=%lld reclamadas=%lld vetores=%lld traps_sdma=%lld eop_cp=%lld outros=%lld (ultimo cliente/fonte 0x%04X) overflows=%lld traps_emitidos=%lld dpc_por_irq=%lld eop_gfx=%lld eop_emitidos=%lld dpc_gfx_por_irq=%lld RPTR=0x%X WPTR_wb=0x%X rptr_por_doorbell=%u escritas_doorbell=%lld\n",
        Why, g_Bc250SdmaPresent.IhIsrCalls, g_Bc250SdmaPresent.IhIsrClaimed, g_Bc250SdmaPresent.IhEntries,
        g_Bc250SdmaPresent.IhSdmaTraps, g_Bc250SdmaPresent.IhCpEops, g_Bc250SdmaPresent.IhOther,
        (unsigned)g_Bc250SdmaPresent.IhLastClientSrc, g_Bc250SdmaPresent.IhOverflows,
        g_Bc250SdmaPresent.TrapsEmitted, g_Bc250SdmaPresent.IhDpcKicks,
        g_Bc250SdmaPresent.IhGfxEops, g_Bc250SdmaPresent.CpEopIntsEmitted,
        g_Bc250SdmaPresent.IhGfxDpcKicks, g_Bc250SdmaPresent.IhRptr,
        g_Bc250SdmaPresent.IhWptrCpu != NULL ? *g_Bc250SdmaPresent.IhWptrCpu : 0u,
        g_Bc250SdmaPresent.IhRptrDoorbellActive ? 1u : 0u, g_Bc250SdmaPresent.IhRptrDoorbellWrites);
    if (g_Bc250SdmaPresent.IhCpPrivReg != 0 || g_Bc250SdmaPresent.IhCpPrivInstr != 0 ||
        g_Bc250SdmaPresent.IhCpBadOpcode != 0 || g_Bc250SdmaPresent.IhCpRsvdBit != 0) {
        /* C06 (doc 764): dw0 = cliente/fonte/ring_id[23:16] (me=(r&0x0C)>>2, pipe=r&3). */
        Bc250DisplayLog("[C06_CP_ERRO] priv_reg=%lld priv_instr=%lld bad_opcode=%lld rsvd_bit=%lld ultima dw0=0x%08X dw1=0x%08X (ring_id=0x%02X)\n",
            g_Bc250SdmaPresent.IhCpPrivReg, g_Bc250SdmaPresent.IhCpPrivInstr,
            g_Bc250SdmaPresent.IhCpBadOpcode, g_Bc250SdmaPresent.IhCpRsvdBit,
            (unsigned)g_Bc250SdmaPresent.IhCpErrLastDw0, (unsigned)g_Bc250SdmaPresent.IhCpErrLastDw1,
            ((unsigned)g_Bc250SdmaPresent.IhCpErrLastDw0 >> 16) & 0xFFu);
    }
    if (g_Bc250SdmaPresent.Sdma1Attached && g_Bc250SdmaPresent.Sdma1Work != NULL) {
        LONG64 rec = 0, unal = 0, drop = 0;
        LONG64 dNoSlot = 0, dPartial = 0, dOutFull = 0, dOrphan = 0;
        Bc250PagingSdma1RangeStats(&rec, &unal, &drop);
        Bc250PagingSdma1DropStats(&dNoSlot, &dPartial, &dOutFull, &dOrphan);
        Bc250DisplayLog("[P4_SDMA1] regime: build: registradas=%lld desalinhadas=%lld descartadas=%lld (sem_lugar=%lld parcial=%lld saida_cheia=%lld orfa=%lld) | envio: rotas=%lld inline=%lld sem_espaco=%lld token=%u Go=%u Done=%u RPTR1=%u cursor1=%u\n",
            rec, unal, drop, dNoSlot, dPartial, dOutFull, dOrphan,
            g_Bc250SdmaPresent.Sdma1RangesRouted, g_Bc250SdmaPresent.Sdma1RangesInline,
            g_Bc250SdmaPresent.Sdma1RangesNoSpace, g_Bc250SdmaPresent.Sdma1NextToken,
            g_Bc250SdmaPresent.Sdma1Work[BC250_SDMA1_WORK_GO_OFFSET / 4u],
            g_Bc250SdmaPresent.Sdma1Work[BC250_SDMA1_WORK_DONE_OFFSET / 4u],
            Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA1_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START),
            g_Bc250SdmaPresent.Sdma1CursorDwords * 4u);
    }
    /* Falha fechada: o hardware escreve vetores mas o ISR nunca roda -> o anel
     * so' transborda. Desarma em vez de deixar a interrupcao sem dono. */
    if (g_Bc250SdmaPresent.IhIsrCalls == 0 && g_Bc250SdmaPresent.TrapsEmitted > s_lastTraps + 64) {
        Bc250SdmaPresentIhDisarm("ISR nunca chamado com traps emitidos");
    }
    s_lastTraps = g_Bc250SdmaPresent.TrapsEmitted;
#else
    UNREFERENCED_PARAMETER(Why);
#endif
}

void Bc250SdmaPresentTeardown(void)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    if (!g_Bc250SdmaPresent.Initialized) {
        return;
    }

    g_Bc250SdmaPresent.HardwareEnabled = FALSE;

    /* P3.1: IH e TRAP desligados antes de tudo (o anel do IH e' do chamador). */
    if (g_Bc250SdmaPresent.MmioVirtual != NULL) {
        Bc250SdmaPresentIhDisarm("teardown");
    }

    /* P2: doorbell desarmada e BIF devolvido antes de soltar o anel. */
    g_Bc250SdmaPresent.DoorbellRequested = FALSE;
    if (g_Bc250SdmaPresent.MmioVirtual != NULL) {
        Bc250SdmaPresentP2Disarm("teardown");
    }
    if (g_Bc250SdmaPresent.DoorbellVirtual != NULL) {
        MmUnmapIoSpace((PVOID)g_Bc250SdmaPresent.DoorbellVirtual, BC250_P2_DOORBELL_MAP_BYTES);
        g_Bc250SdmaPresent.DoorbellVirtual = NULL;
    }

    if (g_Bc250SdmaPresent.MmioVirtual != NULL) {
        Bc250SdmaPresentReleaseSystemRingWork();
    }

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
VOID Bc250SdmaPresentSdma1Attach(
    volatile uint32_t *RingCpu, uint64_t RingGpu,
    volatile uint32_t *WorkCpu, uint64_t WorkGpu,
    uint32_t CursorDwords)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KIRQL oldIrql;
    if (RingCpu == NULL || WorkCpu == NULL || RingGpu == 0ull || WorkGpu == 0ull ||
        !g_Bc250SdmaPresent.Initialized) {
        return;
    }
    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);
    g_Bc250SdmaPresent.Sdma1Ring = RingCpu;
    g_Bc250SdmaPresent.Sdma1RingGpu = RingGpu;
    g_Bc250SdmaPresent.Sdma1Work = WorkCpu;
    g_Bc250SdmaPresent.Sdma1WorkGpu = WorkGpu;
    g_Bc250SdmaPresent.Sdma1CursorDwords = CursorDwords;
    WorkCpu[BC250_SDMA1_WORK_GO_OFFSET / 4u] = 0u;
    WorkCpu[BC250_SDMA1_WORK_DONE_OFFSET / 4u] = 0u;
    g_Bc250SdmaPresent.Sdma1NextToken = 0u;
    KeMemoryBarrier();
    g_Bc250SdmaPresent.Sdma1Attached = TRUE;
    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
    Bc250PagingSdma1RouteSet(TRUE);
    Bc250DisplayLog("[P4_SDMA1] ANEXADO ao envio do Node 0: faixas de PTE do paging buffer passam a rodar no SDMA1 (anel 0x%llX, Go/Done 0x%llX/0x%llX, cursor=%u)\n",
        (unsigned long long)RingGpu,
        (unsigned long long)(WorkGpu + BC250_SDMA1_WORK_GO_OFFSET),
        (unsigned long long)(WorkGpu + BC250_SDMA1_WORK_DONE_OFFSET), CursorDwords);
#else
    UNREFERENCED_PARAMETER(RingCpu);
    UNREFERENCED_PARAMETER(RingGpu);
    UNREFERENCED_PARAMETER(WorkCpu);
    UNREFERENCED_PARAMETER(WorkGpu);
    UNREFERENCED_PARAMETER(CursorDwords);
#endif
}

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
/* Com o RingLock tomado. */
static BOOLEAN Bc250SdmaPresentSdma1DetachLocked(BOOLEAN Halt, uint32_t *Go, uint32_t *Done,
    uint32_t *Rptr, uint32_t *Wptr)
{
    BOOLEAN was = g_Bc250SdmaPresent.Sdma1Attached;
    g_Bc250SdmaPresent.Sdma1Attached = FALSE;
    *Go = *Done = *Rptr = *Wptr = 0u;
    if (!was) return FALSE;
    *Go = g_Bc250SdmaPresent.Sdma1Work[BC250_SDMA1_WORK_GO_OFFSET / 4u];
    *Done = g_Bc250SdmaPresent.Sdma1Work[BC250_SDMA1_WORK_DONE_OFFSET / 4u];
    *Rptr = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA1_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);
    *Wptr = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA1_REG_RB_WPTR - BC250_SDMA0_MMIO_WINDOW_START);
    if (Halt) {
        /* Uma espera do SDMA1 sem quem a libere: anel e poll desligados, F32 parado. */
        uint32_t v = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA1_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA1_REG_RB_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
            v & ~BC250_SDMA1_RB_ENABLE);
        v = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA1_REG_F32_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA1_REG_F32_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
            v | BC250_SDMA1_F32_HALT);
    }
    return TRUE;
}
#endif

VOID Bc250SdmaPresentSdma1Detach(const char *Why, BOOLEAN Halt)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KIRQL oldIrql;
    uint32_t go, done, rptr, wptr;
    BOOLEAN was;
    Bc250PagingSdma1RouteSet(FALSE);
    if (!g_Bc250SdmaPresent.Initialized) return;
    KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);
    was = Bc250SdmaPresentSdma1DetachLocked(Halt, &go, &done, &rptr, &wptr);
    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
    if (was) {
        Bc250DisplayLog("[P4_SDMA1] DESANEXADO (%s, halt=%u): Go=%u Done=%u RPTR=%u WPTR=%u rotas=%lld inline=%lld sem_espaco=%lld -- faixas de PTE voltam ao SDMA0\n",
            Why, Halt ? 1u : 0u, go, done, rptr, wptr,
            g_Bc250SdmaPresent.Sdma1RangesRouted, g_Bc250SdmaPresent.Sdma1RangesInline,
            g_Bc250SdmaPresent.Sdma1RangesNoSpace);
    }
#else
    UNREFERENCED_PARAMETER(Why);
    UNREFERENCED_PARAMETER(Halt);
#endif
}

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
    /* P4.2 (doc 622): o SDMA1 pode estar esperando um Go que nao vem mais. */
    {
        uint32_t go1, done1, rptr1, wptr1;
        if (Bc250SdmaPresentSdma1DetachLocked(TRUE, &go1, &done1, &rptr1, &wptr1)) {
            Snapshot->Sdma1Detached = 1u;
            Snapshot->Sdma1Go = go1;
            Snapshot->Sdma1Done = done1;
        }
    }

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

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
/*
 * P2 (doc 598): desarma a doorbell do SDMA0 e devolve DOORBELL_OFFSET e
 * BIF_SDMA0_DOORBELL_RANGE aos valores lidos antes do primeiro arme.
 */
static void Bc250SdmaPresentP2Disarm(_In_z_ const char *Why)
{
    uint32_t v;

    g_Bc250SdmaPresent.DoorbellActive = FALSE;
    if (g_Bc250SdmaPresent.MmioVirtual == NULL ||
        !g_Bc250SdmaPresent.DoorbellBeforeCaptured) {
        return;
    }
    /* P3.3: a abertura do BIF vai voltar ao snapshot; o RPTR do IH desce para
     * o MMIO antes, senao as escritas do ISR pela doorbell se perdem. */
    if (g_Bc250SdmaPresent.IhRptrDoorbellActive) {
        Bc250IhRptrDoorbellRestore();
        Bc250IhWrite(BC250_IH_REG_RB_RPTR, g_Bc250SdmaPresent.IhRptr);
        Bc250DisplayLog("[P3_3_IH_DOORBELL] DESFEITA (%s): a abertura do BIF sai com a P2; RPTR do IH volta ao MMIO (0x%X)\n",
            Why, g_Bc250SdmaPresent.IhRptr);
    }
    v = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_DOORBELL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_DOORBELL - BC250_SDMA0_MMIO_WINDOW_START,
        v & ~F_DOORBELL_ENABLE);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_DOORBELL_OFFSET - BC250_SDMA0_MMIO_WINDOW_START,
        g_Bc250SdmaPresent.DoorbellOffsetRegBefore);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_NBIO_REG_BIF_SDMA0_DOORBELL_RANGE - BC250_SDMA0_MMIO_WINDOW_START,
        g_Bc250SdmaPresent.BifRangeBefore);
    /* P2c: abertura devolvida ao valor lido antes, como nv_common_hw_fini. */
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_NBIO_REG_RCC_DOORBELL_APER_EN - BC250_SDMA0_MMIO_WINDOW_START,
        g_Bc250SdmaPresent.DoorbellAperBefore);
    Bc250DisplayLog("[P2_DOORBELL] DESARMADA (%s): APER_EN=0x%08X (antes 0x%08X) DOORBELL=0x%08X OFFSET=0x%08X BIF_RANGE=0x%08X (antes 0x%08X/0x%08X/0x%08X); Kicks=%lld; WPTR volta ao MMIO\n",
        Why,
        Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_NBIO_REG_RCC_DOORBELL_APER_EN - BC250_SDMA0_MMIO_WINDOW_START),
        g_Bc250SdmaPresent.DoorbellAperBefore,
        Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_DOORBELL - BC250_SDMA0_MMIO_WINDOW_START),
        Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_DOORBELL_OFFSET - BC250_SDMA0_MMIO_WINDOW_START),
        Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_NBIO_REG_BIF_SDMA0_DOORBELL_RANGE - BC250_SDMA0_MMIO_WINDOW_START),
        g_Bc250SdmaPresent.DoorbellRegBefore,
        g_Bc250SdmaPresent.DoorbellOffsetRegBefore,
        g_Bc250SdmaPresent.BifRangeBefore,
        (long long)g_Bc250SdmaPresent.DoorbellKicks);
}

/*
 * P2b (doc 599): programa a doorbell DENTRO da reconstrucao do anel, com o F32
 * parado e o anel desligado, na mesma posicao de sdma_v5_0_gfx_resume (depois
 * de MINOR_PTR/WPTR, antes do unhalt e do RB_ENABLE). A Corrida 221 armou com
 * o anel vivo: os 3 registradores leram de volta o pedido e a doorbell nao
 * chegou ao SDMA0 (WPTR=RPTR=40, fence no poison, 1 doorbell escrita).
 * RMW (Lei 6) e readback. Chamar sob RingLock, com o F32 haltado.
 */
static uint32_t g_Bc250P2Rd[4];
static BOOLEAN Bc250SdmaPresentP2ProgramLocked(void)
{
    uint32_t db, dbOff, bif, aper, wantOff, wantBif;

    /* P2c: a abertura do BIF primeiro, como nv_common_hw_init antes dos motores. */
    aper = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_NBIO_REG_RCC_DOORBELL_APER_EN - BC250_SDMA0_MMIO_WINDOW_START);
    db = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_DOORBELL - BC250_SDMA0_MMIO_WINDOW_START);
    dbOff = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_DOORBELL_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
    bif = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_NBIO_REG_BIF_SDMA0_DOORBELL_RANGE - BC250_SDMA0_MMIO_WINDOW_START);
    if (!g_Bc250SdmaPresent.DoorbellBeforeCaptured) {
        g_Bc250SdmaPresent.DoorbellRegBefore = db;
        g_Bc250SdmaPresent.DoorbellOffsetRegBefore = dbOff;
        g_Bc250SdmaPresent.BifRangeBefore = bif;
        g_Bc250SdmaPresent.DoorbellAperBefore = aper;
        g_Bc250SdmaPresent.DoorbellBeforeCaptured = TRUE;
    }
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_NBIO_REG_RCC_DOORBELL_APER_EN - BC250_SDMA0_MMIO_WINDOW_START,
        aper | BC250_NBIO_DOORBELL_APER_EN_MASK);
    wantOff = (dbOff & ~BC250_SDMA0_DOORBELL_OFFSET_MASK) |
        ((BC250_P2_SDMA0_DOORBELL_INDEX << 2) & BC250_SDMA0_DOORBELL_OFFSET_MASK);
    wantBif = (bif & ~(BC250_BIF_DOORBELL_RANGE_OFFSET_MASK | BC250_BIF_DOORBELL_RANGE_SIZE_MASK)) |
        ((BC250_P2_SDMA0_DOORBELL_INDEX << 2) & BC250_BIF_DOORBELL_RANGE_OFFSET_MASK) |
        ((BC250_P2_SDMA0_DOORBELL_RANGE_SIZE << 16) & BC250_BIF_DOORBELL_RANGE_SIZE_MASK);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_DOORBELL - BC250_SDMA0_MMIO_WINDOW_START,
        db | F_DOORBELL_ENABLE);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_DOORBELL_OFFSET - BC250_SDMA0_MMIO_WINDOW_START, wantOff);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_NBIO_REG_BIF_SDMA0_DOORBELL_RANGE - BC250_SDMA0_MMIO_WINDOW_START, wantBif);
    g_Bc250P2Rd[0] = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_DOORBELL - BC250_SDMA0_MMIO_WINDOW_START);
    g_Bc250P2Rd[1] = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_DOORBELL_OFFSET - BC250_SDMA0_MMIO_WINDOW_START);
    g_Bc250P2Rd[2] = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_NBIO_REG_BIF_SDMA0_DOORBELL_RANGE - BC250_SDMA0_MMIO_WINDOW_START);
    g_Bc250P2Rd[3] = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_NBIO_REG_RCC_DOORBELL_APER_EN - BC250_SDMA0_MMIO_WINDOW_START);
    return (g_Bc250P2Rd[3] & BC250_NBIO_DOORBELL_APER_EN_MASK) != 0u &&
        (g_Bc250P2Rd[0] & F_DOORBELL_ENABLE) != 0u &&
        (g_Bc250P2Rd[1] & BC250_SDMA0_DOORBELL_OFFSET_MASK) == (wantOff & BC250_SDMA0_DOORBELL_OFFSET_MASK) &&
        (g_Bc250P2Rd[2] & (BC250_BIF_DOORBELL_RANGE_OFFSET_MASK | BC250_BIF_DOORBELL_RANGE_SIZE_MASK)) ==
            (wantBif & (BC250_BIF_DOORBELL_RANGE_OFFSET_MASK | BC250_BIF_DOORBELL_RANGE_SIZE_MASK));
}
#endif

NTSTATUS Bc250SdmaPresentEnableDoorbell(
    _In_ PHYSICAL_ADDRESS DoorbellBarPhysical,
    _In_ uint64_t DoorbellBarLength)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || DoorbellBarPhysical.QuadPart == 0 ||
        DoorbellBarLength < BC250_P2_DOORBELL_MAP_BYTES ||
        !g_Bc250SdmaPresent.Initialized || !g_Bc250SdmaPresent.HardwareEnabled) {
        Bc250DisplayLog("[P2_DOORBELL] NAO TENTADO: BAR=0x%llX Len=0x%llX Init=%u HwEnabled=%u -- WPTR segue por MMIO\n",
            (unsigned long long)DoorbellBarPhysical.QuadPart,
            (unsigned long long)DoorbellBarLength,
            g_Bc250SdmaPresent.Initialized ? 1u : 0u,
            g_Bc250SdmaPresent.HardwareEnabled ? 1u : 0u);
        return STATUS_DEVICE_NOT_READY;
    }
    if (g_Bc250SdmaPresent.DoorbellVirtual == NULL) {
        g_Bc250SdmaPresent.DoorbellVirtual = (volatile UCHAR *)MmMapIoSpace(
            DoorbellBarPhysical, BC250_P2_DOORBELL_MAP_BYTES, MmNonCached);
        if (g_Bc250SdmaPresent.DoorbellVirtual == NULL) {
            Bc250DisplayLog("[P2_DOORBELL] NAO TENTADO: falha ao mapear a BAR de doorbell em 0x%llX -- WPTR segue por MMIO\n",
                (unsigned long long)DoorbellBarPhysical.QuadPart);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    if (g_Bc250SdmaPresent.InFlightCount != 0u) {
        Bc250DisplayLog("[P2_DOORBELL] NAO TENTADO: %u submissoes em voo -- WPTR segue por MMIO\n",
            g_Bc250SdmaPresent.InFlightCount);
        return STATUS_DEVICE_BUSY;
    }
    g_Bc250SdmaPresent.DoorbellRequested = TRUE;
    Bc250DisplayLog("[P2_DOORBELL] PEDIDA: reconstruindo o anel com a doorbell programada com o F32 parado e o anel desligado, como sdma_v5_0_gfx_resume (P2b, doc 599)\n");
    if (!Bc250SdmaPresentRestartAfterTdr()) {
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    return g_Bc250SdmaPresent.DoorbellActive ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
#else
    UNREFERENCED_PARAMETER(DoorbellBarPhysical);
    UNREFERENCED_PARAMETER(DoorbellBarLength);
    return STATUS_NOT_SUPPORTED;
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
    BOOLEAN p2Program;
    BOOLEAN p2Readback = FALSE;
    LONG64 p2KicksBefore;

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

    /* P2: a reprogramacao abaixo desliga SDMA0_GFX_DOORBELL; a prova e' por MMIO. */
    g_Bc250SdmaPresent.DoorbellActive = FALSE;
    /* P2b: com a doorbell pedida, ela entra na reconstrucao abaixo. */
    p2Program = g_Bc250SdmaPresent.DoorbellRequested &&
        g_Bc250SdmaPresent.DoorbellVirtual != NULL;

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
        p2Program ? 0x00403000u : (0x00403000u | F_POLL_ENABLE)); /* P2b: prova so' pela doorbell */
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

    if (p2Program) {
        /* P2b: posicao de sdma_v5_0_gfx_resume, F32 parado, anel desligado. */
        p2Readback = Bc250SdmaPresentP2ProgramLocked();
    } else {
        value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_DOORBELL - BC250_SDMA0_MMIO_WINDOW_START);
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_DOORBELL - BC250_SDMA0_MMIO_WINDOW_START,
            value & ~F_DOORBELL_ENABLE);
    }
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CLK_CTRL - BC250_SDMA0_MMIO_WINDOW_START,
        value | 0xFF000000u);
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    /* P3.1: o soft-reset devolve o CNTL ao padrao; o TRAP volta com o IH armado. */
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        value | F_CNTL_UTC_L1 | (g_Bc250SdmaPresent.TrapArmed ? BC250_SDMA0_CNTL_TRAP_ENABLE : 0u));
    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_UTCL1_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
    Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
        BC250_SDMA0_REG_UTCL1_CNTL - BC250_SDMA0_MMIO_WINDOW_START,
        (value & ~(0x00000E00u | 0x0000003Eu | BC250_SDMA0_UTCL1_CNTL_FORCE_INVALIDATION)) | (3u << 9) | (9u << 1));
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
        pollCntl != (p2Program ? 0x00403000u : (0x00403000u | F_POLL_ENABLE)) ||
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
    if (p2Program && p2Readback) {
        g_Bc250SdmaPresent.DoorbellActive = TRUE;
    }
    p2KicksBefore = g_Bc250SdmaPresent.DoorbellKicks;
    proven = Bc250SdmaPresentSyncNop();
    if (p2Program) {
        BOOLEAN p2Ok = p2Readback && proven &&
            g_Bc250SdmaPresent.DoorbellKicks != p2KicksBefore;
        uint32_t pollBack;

        /* Poll da shadow de volta (como o v6.6), com a shadow igual ao WPTR. */
        KeAcquireSpinLock(&g_Bc250SdmaPresent.RingLock, &oldIrql);
        g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
            g_Bc250SdmaPresent.CurrentCursor * (uint32_t)sizeof(uint32_t);
        KeMemoryBarrier();
        Bc250SdmaRegWrite(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_WPTR_POLL_CNTL - BC250_SDMA0_MMIO_WINDOW_START, 0x00403000u | F_POLL_ENABLE);
        pollBack = Bc250SdmaRegRead(&g_Bc250SdmaPresent,
            BC250_SDMA0_REG_WPTR_POLL_CNTL - BC250_SDMA0_MMIO_WINDOW_START);
        KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);

        Bc250DisplayLog("[P2_DOORBELL] APLICADO NA RECONSTRUCAO (F32 parado): APER_EN=0x%08X (antes 0x%08X) DOORBELL=0x%08X OFFSET=0x%08X BIF_RANGE=0x%08X (antes 0x%08X/0x%08X/0x%08X) readback=%s; prova=%u Kicks %lld->%lld; POLL restaurado=0x%08X\n",
            g_Bc250P2Rd[3], g_Bc250SdmaPresent.DoorbellAperBefore,
            g_Bc250P2Rd[0], g_Bc250P2Rd[1], g_Bc250P2Rd[2],
            g_Bc250SdmaPresent.DoorbellRegBefore,
            g_Bc250SdmaPresent.DoorbellOffsetRegBefore,
            g_Bc250SdmaPresent.BifRangeBefore,
            p2Readback ? "confere" : "DIVERGE", proven ? 1u : 0u,
            (long long)p2KicksBefore, (long long)g_Bc250SdmaPresent.DoorbellKicks,
            pollBack);
        if (!p2Ok) {
            g_Bc250SdmaPresent.DoorbellRequested = FALSE;
            Bc250SdmaPresentP2Disarm("prova falhou na reconstrucao");
            Bc250DisplayLog("[P2_DOORBELL] NAO ATIVA: readback=%u prova=%u; reconstruindo de novo com o WPTR por MMIO (nao tenta de novo neste bind)\n",
                p2Readback ? 1u : 0u, proven ? 1u : 0u);
            return Bc250SdmaPresentRestartAfterTdr();
        }
        Bc250DisplayLog("[P2_DOORBELL] ATIVA: NOP com fence sobre poison entregue SO' pela doorbell, programada com o F32 parado; daqui em diante o WPTR do SDMA0 sai por WDOORBELL64, sem registrador (Kicks=%lld)\n",
            (long long)g_Bc250SdmaPresent.DoorbellKicks);
    }
    if (!proven) {
        g_Bc250SdmaPresent.HardwareEnabled = FALSE;
        g_Bc250SdmaPresent.TripodValidated = FALSE;
        Bc250DisplayLog("[SdmaPresent] TDR_RESTART FALHOU: anel rearmado, mas o NOP de prova nao retirou fence. HardwareEnabled volta a FALSE; adaptador continua recusando submissao\n");
        return FALSE;
    }
    /* CG.1: mesmo criterio do init -- so' depois da prova fisica. */
    Bc250SdmaPresentRestorePostClockGating("TDR_RESTART");

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
        g_Bc250SdmaPresent.DoorbellActive ?
            g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] :
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
        g_Bc250SdmaPresent.DoorbellActive ?
            g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] :
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

    {
        /* X2a (doc 523): pacotes no anel, escritas UC na VRAM. */
        ULONG ringWriteCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_RING_WRITE, startCursor);
        for (i = 0u; i < cursor; ++i) {
            g_Bc250SdmaPresent.Ring[startCursor + i] = packets[i];
        }
        Bc250CrumbAfter(ringWriteCrumb);
    }
    KeMemoryBarrier();

    g_Bc250SdmaPresent.CurrentCursor += cursor;
    g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_WPTR_OFFSET / 4u] =
        g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t);
    KeMemoryBarrier();

    Bc250SdmaPresentHdp(&g_Bc250SdmaPresent);
    Bc250SdmaPresentPublishWptr((uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t)));

    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);

    Bc250DisplayLog("[SdmaPresent] TDR_HANG_INDUZIDO ARMADO NO SILICIO: POLL_REGMEM esperando 0xDEADDEAD em 0x%llX, WPTR=%u. A engine NAO vai progredir; o completion NAO deve ser notificado para que o timeout do dxgkrnl dispare o TDR\n",
        (unsigned long long)(g_Bc250SdmaPresent.WorkGpuAddress + BC250_SDMA_PRESENT_FENCE_OFFSET),
        (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t)));
    return TRUE;
#else
    return FALSE;
#endif
}
