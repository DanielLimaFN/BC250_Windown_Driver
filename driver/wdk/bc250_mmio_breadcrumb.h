#ifndef BC250_MMIO_BREADCRUMB_H
#define BC250_MMIO_BREADCRUMB_H

/*
 * DX.66 (Corrida 109) -- MIGALHAS DE MMIO QUE SOBREVIVEM AO REINICIO A QUENTE.
 *
 * MEDIDO: nas corridas 88-109 a maquina congela inteira (sem BugCheck,
 * Kernel-Power 41). O rastro DX.65 provou que a thread PASSIVE do escritor
 * parou no MESMO periodo de 100 ms da paginacao ativa -- todos os nucleos de
 * uma vez. H de alta prioridade: uma leitura MMIO a GPU que nunca completa.
 *
 * Antes de cada acesso MMIO do caminho quente, o KMD grava (sitio, offset,
 * fase=ANTES) no slot do processador numa pagina fixa de VRAM mapeada
 * UNCACHED (escrita postada, ordenada, nunca bloqueia a CPU); depois do
 * acesso, fase=DEPOIS. No StartDevice seguinte -- mesmo recusado pelo
 * fusivel -- a pagina e' lida e registrada. Se a VRAM sobreviver ao
 * reinicio a quente, o slot com fase=ANTES e maior sequencia nomeia o
 * acesso em que a CPU parou. Se nao sobreviver, o magic ausente diz isso.
 *
 * Somente instrumentacao: nenhum acesso MMIO muda de valor, ordem ou numero.
 * Fora da imagem do driver (suites offline) tudo vira no-op.
 */

/*
 * P1 (Corrida 218, doc 594) -- SILENCIO DA BAR: paridade com o amdgpu, que em
 * regime nao le nem escreve VRAM pela BAR para instrumentacao. Com 1:
 *   - a pagina de migalhas na VRAM NAO e' armada (o espelho em RAM segue para
 *     o rastro em disco a cada 100 ms, BCR1);
 *   - os eventos vao para um espelho em RAM e o rastro grava os 16 mais
 *     recentes por tique (BEV1);
 *   - a amostragem de VRAM do Present e as leituras de registrador do rastro
 *     (GFX, plataforma, SDMA0, clock gating) ficam desligadas.
 * 0 = comportamento anterior.
 */
#define BC250_P1_SILENCIO_DA_BAR 1

#define BC250_CRUMB_VRAM_OFFSET   0x0FA00000ull  /* reserva no inventario antes do bootstrap; DX.67 a exclui do VidMm */
#define BC250_CRUMB_PAGE_BYTES    0x1000u
#define BC250_CRUMB_MAGIC         0x42524D43u    /* "CMRB" */
#define BC250_CRUMB_VERSION       2u
#define BC250_CRUMB_CPUS          32u
#define BC250_CRUMB_BANKS         3u
#define BC250_CRUMB_SLOTS         (BC250_CRUMB_CPUS * BC250_CRUMB_BANKS)
#define BC250_CRUMB_BANK_INNER    0u
#define BC250_CRUMB_BANK_PAGING   1u
#define BC250_CRUMB_BANK_SUBMIT   2u
#define BC250_CRUMB_SLOT_BASE_DW  16u            /* dword do primeiro slot */
#define BC250_CRUMB_SLOT_DWORDS   4u             /* seq, site<<24|offset, fase, reservado */
#define BC250_CRUMB_STATE_RUNNING 0x52554E31u    /* "RUN1" */
#define BC250_CRUMB_STATE_CLEAN   0x434C4E31u    /* "CLN1" */

/* Cabecalho (dwords): 0 magic, 1 versao, 2 nonce lo, 3 nonce hi, 4 ultima seq, 5 estado. */
#define BC250_CRUMB_DW_MAGIC   0u
#define BC250_CRUMB_DW_VERSION 1u
#define BC250_CRUMB_DW_NONCE_LO 2u
#define BC250_CRUMB_DW_NONCE_HI 3u
#define BC250_CRUMB_DW_LASTSEQ 4u
#define BC250_CRUMB_DW_STATE   5u

#define BC250_CRUMB_PHASE_BEFORE 0xB0u
#define BC250_CRUMB_PHASE_AFTER  0xA0u

/*
 * DX.96 (Corridas 152-157) -- ANEL DE EVENTOS DA GPU NA MESMA PAGINA.
 *
 * MEDIDO: a trava dura para o controlador de memoria (a VRAM decai durante a
 * trava, doc 506), com a GPU ociosa no ultimo batimento de 100 ms e nenhuma
 * migalha de MMIO pendente. Falta saber qual foi a ULTIMA operacao entregue a
 * GPU. Cada operacao de paginacao montada (fill/copia/descarte), cada submit
 * (Node 0 SDMA, Node 1 GFX) e cada conclusao grava um evento aqui, no instante
 * em que acontece. Os dwords 400..431 ficam livres; o anel usa 432..1007.
 *
 * Integridade: a pagina decai na trava (bits 0->1 isolados). Cada evento leva
 * um check = XOR dos campos com uma constante, e a sequencia e' escrita por
 * ULTIMO; o leitor so' confia em evento com check valido. So' escrita em RAM
 * de VRAM ja mapeada; nenhum acesso MMIO a registrador muda.
 */
#define BC250_CRUMB_EVT_BASE_DW   432u
#define BC250_CRUMB_EVT_COUNT     48u
#define BC250_CRUMB_EVT_DWORDS    12u
#define BC250_CRUMB_EVT_CHECK_KEY 0x5A96C3E1u
/* Campos de um evento (dwords). */
#define BC250_CRUMB_EVT_SEQ    0u
#define BC250_CRUMB_EVT_QPC_LO 1u
#define BC250_CRUMB_EVT_QPC_HI 2u
#define BC250_CRUMB_EVT_KIND   3u   /* kind<<24 | node<<16 | op */
#define BC250_CRUMB_EVT_SIZE   4u
#define BC250_CRUMB_EVT_ADDR_LO 5u
#define BC250_CRUMB_EVT_ADDR_HI 6u
#define BC250_CRUMB_EVT_FENCE_LO 7u
#define BC250_CRUMB_EVT_FENCE_HI 8u
#define BC250_CRUMB_EVT_CPU    9u
#define BC250_CRUMB_EVT_CHECK  11u
/* Tipos de evento. */
#define BC250_CRUMB_EVT_PAGING_FILL     1u
#define BC250_CRUMB_EVT_PAGING_TRANSFER 2u
#define BC250_CRUMB_EVT_PAGING_DISCARD  3u
#define BC250_CRUMB_EVT_SUBMIT_SDMA     4u
#define BC250_CRUMB_EVT_SUBMIT_GFX      5u
#define BC250_CRUMB_EVT_RETIRE_SDMA     6u
#define BC250_CRUMB_EVT_RETIRE_GFX      7u
#define BC250_CRUMB_EVT_PAGING_PTE      8u  /* DBG.1: UPDATE_PAGE_TABLE (op = seg<<8 | nivel) */
/* DBG.2 (doc 707), so' no modo debug. "link" = 8 bits no campo node que liga
 * o cabecalho aos seus dwords. */
#define BC250_CRUMB_EVT_PAGING_OP       9u  /* node=link op=operacao Size=bytes escritos Addr=GPUVA inicial Fence=NTSTATUS */
#define BC250_CRUMB_EVT_PAGING_DW       10u /* node=link op=indice do 1o dw Size=dw0 Addr=dw1|dw2<<32 Fence=dw3 */
#define BC250_CRUMB_EVT_SPLIT_REC       11u /* FlushTlb registrado: Size=VmidMask Addr=GPUVA do ponto */
#define BC250_CRUMB_EVT_SUBMIT_SPLIT    12u /* node=0xFF: op=pontos|orfaos<<8 Size=mascara antes do IB Addr=IB Fence=FenceId;
                                             * node=i: Size=dw do ponto Addr=IB Fence=VmidMask */
/* DBG.1 (doc 704): o espelho em RAM e' maior que o anel da pagina, para o
 * modo debug drenar mais eventos por tique sem perder os do meio. DBG.2: 2048,
 * porque cada operacao de paginacao passa a gravar ate 5 eventos. */
#define BC250_CRUMB_EVT_MIRROR_COUNT    2048u

typedef char Bc250CrumbEvtFitsPage[
    (BC250_CRUMB_EVT_BASE_DW + BC250_CRUMB_EVT_COUNT * BC250_CRUMB_EVT_DWORDS) * 4u
        <= BC250_CRUMB_PAGE_BYTES &&
    BC250_CRUMB_EVT_BASE_DW >=
        BC250_CRUMB_SLOT_BASE_DW + BC250_CRUMB_SLOTS * BC250_CRUMB_SLOT_DWORDS ? 1 : -1];

/* Fonte unica do check, usada pelo escritor e pelo leitor. */
static __forceinline ULONG Bc250CrumbEvtCheck(
    ULONG Seq, ULONG QpcLo, ULONG QpcHi, ULONG Kind, ULONG Size,
    ULONG AddrLo, ULONG AddrHi, ULONG FenceLo, ULONG FenceHi, ULONG Cpu)
{
    return Seq ^ QpcLo ^ QpcHi ^ Kind ^ Size ^ AddrLo ^ AddrHi ^
        FenceLo ^ FenceHi ^ Cpu ^ BC250_CRUMB_EVT_CHECK_KEY;
}

enum {
    BC250_CRUMB_SITE_TRANSPORT_READ  = 1,
    BC250_CRUMB_SITE_TRANSPORT_WRITE = 2,
    BC250_CRUMB_SITE_SDMA_READ       = 3,
    BC250_CRUMB_SITE_SDMA_WRITE      = 4,
    BC250_CRUMB_SITE_OTG_READ        = 5,
    BC250_CRUMB_SITE_RINGWORK_READ   = 6,
    BC250_CRUMB_SITE_RINGWORK_WRITE  = 7,
    BC250_CRUMB_SITE_VRAM_SAMPLE     = 8,   /* laco de leitura de VRAM pela CPU (amostragem) */
    BC250_CRUMB_SITE_VRAM_COMPARE    = 9,   /* laco de leitura de VRAM pela CPU (byte proof) */
    BC250_CRUMB_SITE_PAGING_DDI      = 10,  /* escopo externo da DDI */
    BC250_CRUMB_SITE_SUBMIT_DDI      = 11,  /* escopo externo da submissao */
    BC250_CRUMB_SITE_VRAM_FENCE_READ = 12,  /* X1: leitura da fence do SDMA na VRAM pela BAR */
    /* X2a (doc 523): os acessos a VRAM pela BAR que vinham logo depois do HDP
     * sem migalha. Na Corrida 169 a ultima migalha da CPU00 foi a leitura de
     * volta da invalidacao do HDP, e o proximo acesso era um destes. */
    BC250_CRUMB_SITE_VRAM_CANARY_READ  = 13,  /* canarios do tripe lidos apos a conclusao */
    BC250_CRUMB_SITE_VRAM_CANARY_WRITE = 14,  /* canarios plantados antes do submit */
    BC250_CRUMB_SITE_VRAM_RING_WRITE   = 15,  /* pacotes copiados para o anel na VRAM */
    BC250_CRUMB_SITE_VRAM_PT_READ      = 16,  /* X3: entrada de tabela de pagina lida pela BAR */
    BC250_CRUMB_SITE_VRAM_IB_READ      = 17,  /* X3: conteudo de IB lido pela BAR (inspecao) */
    BC250_CRUMB_SITE_VRAM_PROOF_READ   = 18   /* X3: amostras de prova do escape de composicao */
};

#if defined(BC250_ENABLE_DXGK_REGISTRATION) && !defined(BC250_CRUMB_DISABLED)

/* Uma definicao por imagem (selectany): sem novo .c nos scripts de build. */
__declspec(selectany) volatile ULONG *g_Bc250CrumbPage = NULL;
__declspec(selectany) volatile LONG g_Bc250CrumbSeq = 0;

/*
 * DX.87 -- ESPELHO EM RAM, COPIADO PARA DISCO PELO ESCRITOR DO RASTRO DX.56.
 *
 * A pagina de VRAM so' serve se sobreviver ao reset, e nas travas duras das
 * Corridas 115, 128, 129 e 133 ela NAO sobreviveu ("ANTERIOR AUSENTE"). O
 * escritor do rastro, porem, as vezes vive depois da ultima paginacao (12,3 s
 * na 129). O mesmo layout vai para este vetor em pool nao-paginado (escrita
 * simples, sem MMIO) e o escritor o grava em disco a cada periodo: um slot em
 * fase ANTES que persista em registros seguidos nomeia CPU, sitio e offset do
 * acesso que nao terminou. Nenhum acesso MMIO muda de valor, ordem ou numero.
 */
__declspec(selectany) volatile ULONG g_Bc250CrumbMirror[
    BC250_CRUMB_SLOT_BASE_DW + BC250_CRUMB_SLOTS * BC250_CRUMB_SLOT_DWORDS] = { 0 };

static __forceinline ULONG Bc250CrumbBeforeBank(ULONG Site, ULONG Offset, ULONG Bank)
{
    volatile ULONG *page = g_Bc250CrumbPage;
    ULONG slot;
    ULONG seq;
    ULONG siteOffset;
    volatile ULONG *entry;
    volatile ULONG *mirror;
    if (Bank >= BC250_CRUMB_BANKS) return 0xFFFFFFFFu;
    seq = (ULONG)InterlockedIncrement(&g_Bc250CrumbSeq);
    slot = Bank * BC250_CRUMB_CPUS +
        KeGetCurrentProcessorNumberEx(NULL) % BC250_CRUMB_CPUS;
    siteOffset = (Site << 24) | (Offset & 0x00FFFFFFu);
    mirror = g_Bc250CrumbMirror + BC250_CRUMB_SLOT_BASE_DW + slot * BC250_CRUMB_SLOT_DWORDS;
    mirror[1] = siteOffset;
    mirror[2] = BC250_CRUMB_PHASE_BEFORE;
    mirror[0] = seq;
    g_Bc250CrumbMirror[BC250_CRUMB_DW_LASTSEQ] = seq;
    if (page != NULL) {
        entry = page + BC250_CRUMB_SLOT_BASE_DW + slot * BC250_CRUMB_SLOT_DWORDS;
        entry[1] = siteOffset;
        entry[2] = BC250_CRUMB_PHASE_BEFORE;
        entry[0] = seq;
        page[BC250_CRUMB_DW_LASTSEQ] = seq;
    }
    return slot;
}

static __forceinline ULONG Bc250CrumbBefore(ULONG Site, ULONG Offset)
{
    return Bc250CrumbBeforeBank(Site, Offset, BC250_CRUMB_BANK_INNER);
}

/*
 * DX.92 (Corrida 147): ETAPA dentro de um escopo ja aberto. A trava da 147
 * deixou a CPU 7 dentro de SUBMIT_DDI (Node 0) sem acesso MMIO em andamento, e
 * o escopo nao dizia ONDE. Reescreve so' o sitio/offset do slot ja aberto
 * (mesma sequencia, fase ANTES mantida), para que "records_in_flight" continue
 * contando a mesma submissao. Somente escrita em RAM; nenhum MMIO.
 */
static __forceinline VOID Bc250CrumbStage(ULONG Slot, ULONG Site, ULONG Offset)
{
    volatile ULONG *page = g_Bc250CrumbPage;
    ULONG siteOffset = (Site << 24) | (Offset & 0x00FFFFFFu);
    if (Slot >= BC250_CRUMB_SLOTS) return;
    g_Bc250CrumbMirror[BC250_CRUMB_SLOT_BASE_DW + Slot * BC250_CRUMB_SLOT_DWORDS + 1u] =
        siteOffset;
    if (page != NULL) {
        page[BC250_CRUMB_SLOT_BASE_DW + Slot * BC250_CRUMB_SLOT_DWORDS + 1u] = siteOffset;
    }
}

/* Slot do banco de submissao da CPU corrente (DISPATCH: sem migracao). */
static __forceinline ULONG Bc250CrumbSubmitSlot(VOID)
{
    return BC250_CRUMB_BANK_SUBMIT * BC250_CRUMB_CPUS +
        KeGetCurrentProcessorNumberEx(NULL) % BC250_CRUMB_CPUS;
}

static __forceinline VOID Bc250CrumbAfter(ULONG Slot)
{
    volatile ULONG *page = g_Bc250CrumbPage;
    if (Slot >= BC250_CRUMB_SLOTS) return;
    g_Bc250CrumbMirror[BC250_CRUMB_SLOT_BASE_DW + Slot * BC250_CRUMB_SLOT_DWORDS + 2u] =
        BC250_CRUMB_PHASE_AFTER;
    if (page == NULL) return;
    page[BC250_CRUMB_SLOT_BASE_DW + Slot * BC250_CRUMB_SLOT_DWORDS + 2u] =
        BC250_CRUMB_PHASE_AFTER;
}

/*
 * DX.96: grava um evento no anel. Qualquer IRQL (so' escrita em memoria ja'
 * mapeada). A sequencia vai por ULTIMO, depois de uma barreira: um evento
 * pela metade fica com a sequencia antiga e o check nao fecha.
 */
__declspec(selectany) volatile LONG g_Bc250CrumbEvtSeq = 0;
/* P1: espelho dos eventos em RAM, mesmo layout do anel da pagina. */
__declspec(selectany) volatile ULONG g_Bc250CrumbEvtMirror[
    BC250_CRUMB_EVT_MIRROR_COUNT * BC250_CRUMB_EVT_DWORDS] = { 0 };

static __forceinline VOID Bc250CrumbEvent(
    ULONG Kind, ULONG Node, ULONG Op, ULONG Size, ULONG64 Addr, ULONG64 Fence)
{
    volatile ULONG *page = g_Bc250CrumbPage;
    volatile ULONG *e;
    ULONG seq, kindWord, qpcLo, qpcHi, addrLo, addrHi, fenceLo, fenceHi, cpu;
    LARGE_INTEGER qpc;
    seq = (ULONG)InterlockedIncrement(&g_Bc250CrumbEvtSeq);
    qpc = KeQueryPerformanceCounter(NULL);
    qpcLo = qpc.LowPart;
    qpcHi = (ULONG)qpc.HighPart;
    kindWord = ((Kind & 0xFFu) << 24) | ((Node & 0xFFu) << 16) | (Op & 0xFFFFu);
    addrLo = (ULONG)Addr;
    addrHi = (ULONG)(Addr >> 32);
    fenceLo = (ULONG)Fence;
    fenceHi = (ULONG)(Fence >> 32);
    cpu = KeGetCurrentProcessorNumberEx(NULL);
    {
        volatile ULONG *m = g_Bc250CrumbEvtMirror +
            (seq % BC250_CRUMB_EVT_MIRROR_COUNT) * BC250_CRUMB_EVT_DWORDS;
        m[BC250_CRUMB_EVT_SEQ] = 0u;
        m[BC250_CRUMB_EVT_QPC_LO] = qpcLo;
        m[BC250_CRUMB_EVT_QPC_HI] = qpcHi;
        m[BC250_CRUMB_EVT_KIND] = kindWord;
        m[BC250_CRUMB_EVT_SIZE] = Size;
        m[BC250_CRUMB_EVT_ADDR_LO] = addrLo;
        m[BC250_CRUMB_EVT_ADDR_HI] = addrHi;
        m[BC250_CRUMB_EVT_FENCE_LO] = fenceLo;
        m[BC250_CRUMB_EVT_FENCE_HI] = fenceHi;
        m[BC250_CRUMB_EVT_CPU] = cpu;
        KeMemoryBarrier();
        m[BC250_CRUMB_EVT_SEQ] = seq;
    }
    if (page == NULL) return;
    e = page + BC250_CRUMB_EVT_BASE_DW +
        (seq % BC250_CRUMB_EVT_COUNT) * BC250_CRUMB_EVT_DWORDS;
    e[BC250_CRUMB_EVT_SEQ] = 0u;
    e[BC250_CRUMB_EVT_QPC_LO] = qpcLo;
    e[BC250_CRUMB_EVT_QPC_HI] = qpcHi;
    e[BC250_CRUMB_EVT_KIND] = kindWord;
    e[BC250_CRUMB_EVT_SIZE] = Size;
    e[BC250_CRUMB_EVT_ADDR_LO] = addrLo;
    e[BC250_CRUMB_EVT_ADDR_HI] = addrHi;
    e[BC250_CRUMB_EVT_FENCE_LO] = fenceLo;
    e[BC250_CRUMB_EVT_FENCE_HI] = fenceHi;
    e[BC250_CRUMB_EVT_CPU] = cpu;
    e[BC250_CRUMB_EVT_CHECK] = Bc250CrumbEvtCheck(seq, qpcLo, qpcHi, kindWord,
        Size, addrLo, addrHi, fenceLo, fenceHi, cpu);
    KeMemoryBarrier();
    e[BC250_CRUMB_EVT_SEQ] = seq;
}

#else
#define Bc250CrumbEvent(Kind, Node, Op, Size, Addr, Fence) ((void)0)
#define Bc250CrumbBefore(Site, Offset) (0xFFFFFFFFu)
#define Bc250CrumbBeforeBank(Site, Offset, Bank) (0xFFFFFFFFu)
#define Bc250CrumbAfter(Slot) ((void)(Slot))
#define Bc250CrumbStage(Slot, Site, Offset) ((void)(Slot))
#define Bc250CrumbSubmitSlot() (0xFFFFFFFFu)
#endif

#endif /* BC250_MMIO_BREADCRUMB_H */
