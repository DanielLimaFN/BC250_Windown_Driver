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
    BC250_CRUMB_SITE_SUBMIT_DDI      = 11   /* escopo externo da submissao */
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

#else
#define Bc250CrumbBefore(Site, Offset) (0xFFFFFFFFu)
#define Bc250CrumbBeforeBank(Site, Offset, Bank) (0xFFFFFFFFu)
#define Bc250CrumbAfter(Slot) ((void)(Slot))
#endif

#endif /* BC250_MMIO_BREADCRUMB_H */
