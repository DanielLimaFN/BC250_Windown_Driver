#include "bc250_gfx_ring_contract.h"

/* Larguras vindas de gc_10_1_0_sh_mask.h (classe P1). */
#define GFX_RB_BUFSZ_SHIFT      0u
#define GFX_RB_BUFSZ_MAX        0x3Fu
#define GFX_RB_BLKSZ_SHIFT      8u
#define GFX_RB_BLKSZ_MAX        0x3Fu
#define GFX_BUF_SWAP_SHIFT      16u
#define GFX_BUF_SWAP_MAX        0x3u
#define GFX_MIN_AVAILSZ_SHIFT   20u
#define GFX_MIN_AVAILSZ_MAX     0x3u
#define GFX_MIN_IB_AVAIL_SHIFT  22u
#define GFX_MIN_IB_AVAIL_MAX    0x3u
#define GFX_CACHE_POLICY_SHIFT  24u
#define GFX_CACHE_POLICY_MAX    0x3u
#define GFX_RB_VOLATILE_SHIFT   26u
#define GFX_RB_NO_UPDATE_SHIFT  27u
#define GFX_RB_EXE_SHIFT        28u
#define GFX_CE_HQD_NEQ_SHIFT    30u
#define GFX_RB_RPTR_WR_SHIFT    31u

/* Limites do anel. O piso de 4 KiB e' uma pagina; o teto de 1 MiB evita que
 * um plano peca uma reserva absurda dentro da janela de VRAM. */
#define GFX_RING_MIN_BYTES      0x00001000u
#define GFX_RING_MAX_BYTES      0x00100000u

/* CP_RB0_BASE carrega GPUVA >> 8. */
#define GFX_RING_BASE_ALIGN     0x100ull

static uint32_t Bc250GfxIsPowerOfTwo32(uint32_t Value)
{
    return (Value != 0u && (Value & (Value - 1u)) == 0u) ? 1u : 0u;
}

static uint32_t Bc250GfxLog2Exact32(uint32_t Value)
{
    uint32_t bit = 0u;
    while ((Value >> bit) > 1u) {
        bit++;
    }
    return bit;
}

/* TRUE se [AStart, AStart+ABytes) e [BStart, BStart+BBytes) se tocam. */
static uint32_t Bc250GfxRangesOverlap(
    uint64_t AStart, uint64_t ABytes, uint64_t BStart, uint64_t BBytes)
{
    if (ABytes == 0ull || BBytes == 0ull) {
        return 0u;
    }
    if (AStart + ABytes <= BStart) {
        return 0u;
    }
    if (BStart + BBytes <= AStart) {
        return 0u;
    }
    return 1u;
}

static uint32_t Bc250GfxRangeInWindow(
    uint64_t Start, uint64_t Bytes, uint64_t WindowBase, uint64_t WindowBytes)
{
    if (Bytes == 0ull || WindowBytes == 0ull) {
        return 0u;
    }
    if (Start < WindowBase) {
        return 0u;
    }
    if (Start + Bytes > WindowBase + WindowBytes) {
        return 0u;
    }
    return 1u;
}

BC250_GFX_RING_STATUS Bc250GfxRingDeriveBufsz(
    uint32_t RingBytes,
    uint32_t *RbBufsz,
    uint32_t *RbBlksz)
{
    uint32_t bufsz;

    if (RbBufsz == NULL || RbBlksz == NULL) {
        return BC250_GFX_RING_BAD_ARGUMENT;
    }
    *RbBufsz = 0u;
    *RbBlksz = 0u;

    if (!Bc250GfxIsPowerOfTwo32(RingBytes) ||
        RingBytes < GFX_RING_MIN_BYTES || RingBytes > GFX_RING_MAX_BYTES) {
        return BC250_GFX_RING_BAD_SIZE;
    }

    /* RB_BUFSZ = log2(RingBytes / 8) - 1. RingBytes >= 4096 garante >= 8. */
    bufsz = Bc250GfxLog2Exact32(RingBytes / 8u);
    if (bufsz < 3u) {
        return BC250_GFX_RING_BAD_SIZE;
    }
    bufsz -= 1u;

    if (bufsz > GFX_RB_BUFSZ_MAX || (bufsz - 2u) > GFX_RB_BLKSZ_MAX) {
        return BC250_GFX_RING_FIELD_OVERFLOW;
    }

    *RbBufsz = bufsz;
    *RbBlksz = bufsz - 2u;
    return BC250_GFX_RING_OK;
}

BC250_GFX_RING_STATUS Bc250GfxRingValidatePlan(const BC250_GFX_RING_PLAN *Plan)
{
    uint32_t bufsz;
    uint32_t blksz;
    BC250_GFX_RING_STATUS status;

    if (Plan == NULL) {
        return BC250_GFX_RING_BAD_ARGUMENT;
    }
    if (Plan->RingGpuVa == 0ull || Plan->RptrWritebackGpuVa == 0ull ||
        Plan->WptrPollGpuVa == 0ull || Plan->VramWindowBytes == 0ull) {
        return BC250_GFX_RING_BAD_ARGUMENT;
    }

    status = Bc250GfxRingDeriveBufsz(Plan->RingBytes, &bufsz, &blksz);
    if (status != BC250_GFX_RING_OK) {
        return status;
    }

    if ((Plan->RingGpuVa & (GFX_RING_BASE_ALIGN - 1ull)) != 0ull) {
        return BC250_GFX_RING_RING_MISALIGNED;
    }
    if ((Plan->RptrWritebackGpuVa & 0x3ull) != 0ull ||
        (Plan->WptrPollGpuVa & 0x3ull) != 0ull) {
        return BC250_GFX_RING_WRITEBACK_MISALIGNED;
    }

    if (!Bc250GfxRangeInWindow(Plan->RingGpuVa, Plan->RingBytes,
            Plan->VramWindowBase, Plan->VramWindowBytes) ||
        !Bc250GfxRangeInWindow(Plan->RptrWritebackGpuVa, 4ull,
            Plan->VramWindowBase, Plan->VramWindowBytes) ||
        !Bc250GfxRangeInWindow(Plan->WptrPollGpuVa, 4ull,
            Plan->VramWindowBase, Plan->VramWindowBytes)) {
        return BC250_GFX_RING_OUT_OF_WINDOW;
    }

    if (Bc250GfxRangesOverlap(Plan->RingGpuVa, Plan->RingBytes,
            Plan->RptrWritebackGpuVa, 4ull) ||
        Bc250GfxRangesOverlap(Plan->RingGpuVa, Plan->RingBytes,
            Plan->WptrPollGpuVa, 4ull) ||
        Bc250GfxRangesOverlap(Plan->RptrWritebackGpuVa, 4ull,
            Plan->WptrPollGpuVa, 4ull)) {
        return BC250_GFX_RING_OVERLAP;
    }

    /*
     * Um anel com RB_EXE = 0 nao executa. Aceitar esse plano produziria um
     * bring-up que escreve todos os registradores, devolve sucesso e nunca
     * consome um pacote -- falso sucesso pela Lei 4, e exatamente o estado do
     * unico prototipo existente (bc250_compute_service.c, CP_RB0_CNTL =
     * 0x00A00709, RB_EXE = 0).
     */
    if (Plan->RbExe != 1u) {
        return BC250_GFX_RING_NOT_EXECUTABLE;
    }

    if (Plan->CachePolicy > GFX_CACHE_POLICY_MAX ||
        Plan->CeHqdNeqRbHqd > 1u || Plan->RbNoUpdate > 1u ||
        Plan->RbRptrWrEna > 1u || Plan->UnhaltMicroEngines > 1u ||
        Plan->MicroEngineFirmwareLoaded > 1u) {
        return BC250_GFX_RING_FIELD_OVERFLOW;
    }

    /*
     * Desaltar micro-engine sem ucode e' soltar a engine sobre memoria de
     * instrucao nao inicializada. O plano tem de DECLARAR que o firmware ja
     * foi carregado; declarar e' barato, e a ausencia da declaracao e' o caso
     * comum no ponto do bring-up em que este contrato e' chamado.
     */
    if (Plan->UnhaltMicroEngines == 1u && Plan->MicroEngineFirmwareLoaded != 1u) {
        return BC250_GFX_RING_FIRMWARE_NOT_LOADED;
    }

    return BC250_GFX_RING_OK;
}

BC250_GFX_RING_STATUS Bc250GfxRingComputeCntl(
    const BC250_GFX_RING_PLAN *Plan,
    uint32_t *Cntl)
{
    uint32_t bufsz;
    uint32_t blksz;
    uint32_t value;
    BC250_GFX_RING_STATUS status;

    if (Cntl == NULL) {
        return BC250_GFX_RING_BAD_ARGUMENT;
    }
    *Cntl = 0u;

    status = Bc250GfxRingValidatePlan(Plan);
    if (status != BC250_GFX_RING_OK) {
        return status;
    }

    status = Bc250GfxRingDeriveBufsz(Plan->RingBytes, &bufsz, &blksz);
    if (status != BC250_GFX_RING_OK) {
        return status;
    }

    value = (bufsz << GFX_RB_BUFSZ_SHIFT) |
            (blksz << GFX_RB_BLKSZ_SHIFT) |
            (Plan->CachePolicy << GFX_CACHE_POLICY_SHIFT) |
            (Plan->RbNoUpdate << GFX_RB_NO_UPDATE_SHIFT) |
            (1u << GFX_RB_EXE_SHIFT) |
            (Plan->CeHqdNeqRbHqd << GFX_CE_HQD_NEQ_SHIFT) |
            (Plan->RbRptrWrEna << GFX_RB_RPTR_WR_SHIFT);

    *Cntl = value;
    return BC250_GFX_RING_OK;
}

BC250_GFX_RING_STATUS Bc250GfxRingDecodeCntl(
    uint32_t Raw,
    BC250_GFX_RING_CNTL_FIELDS *Fields)
{
    if (Fields == NULL) {
        return BC250_GFX_RING_BAD_ARGUMENT;
    }
    Fields->RbBufsz       = (Raw >> GFX_RB_BUFSZ_SHIFT) & GFX_RB_BUFSZ_MAX;
    Fields->RbBlksz       = (Raw >> GFX_RB_BLKSZ_SHIFT) & GFX_RB_BLKSZ_MAX;
    Fields->BufSwap       = (Raw >> GFX_BUF_SWAP_SHIFT) & GFX_BUF_SWAP_MAX;
    Fields->MinAvailsz    = (Raw >> GFX_MIN_AVAILSZ_SHIFT) & GFX_MIN_AVAILSZ_MAX;
    Fields->MinIbAvailsz  = (Raw >> GFX_MIN_IB_AVAIL_SHIFT) & GFX_MIN_IB_AVAIL_MAX;
    Fields->CachePolicy   = (Raw >> GFX_CACHE_POLICY_SHIFT) & GFX_CACHE_POLICY_MAX;
    Fields->RbVolatile    = (Raw >> GFX_RB_VOLATILE_SHIFT) & 1u;
    Fields->RbNoUpdate    = (Raw >> GFX_RB_NO_UPDATE_SHIFT) & 1u;
    Fields->RbExe         = (Raw >> GFX_RB_EXE_SHIFT) & 1u;
    Fields->CeHqdNeqRbHqd = (Raw >> GFX_CE_HQD_NEQ_SHIFT) & 1u;
    Fields->RbRptrWrEna   = (Raw >> GFX_RB_RPTR_WR_SHIFT) & 1u;
    return BC250_GFX_RING_OK;
}

/* Campos de CP_ME_CNTL (gc_10_1_0_sh_mask.h, classe P1). */
#define GFX_ME_CE_HALT_SHIFT    24u
#define GFX_ME_CE_STEP_SHIFT    25u
#define GFX_ME_PFP_HALT_SHIFT   26u
#define GFX_ME_PFP_STEP_SHIFT   27u
#define GFX_ME_ME_HALT_SHIFT    28u

BC250_GFX_RING_STATUS Bc250GfxRingDecodeMeCntl(
    uint32_t Raw,
    BC250_GFX_ME_CNTL_FIELDS *Fields)
{
    if (Fields == NULL) {
        return BC250_GFX_RING_BAD_ARGUMENT;
    }
    Fields->CeHalt  = (Raw >> GFX_ME_CE_HALT_SHIFT) & 1u;
    Fields->CeStep  = (Raw >> GFX_ME_CE_STEP_SHIFT) & 1u;
    Fields->PfpHalt = (Raw >> GFX_ME_PFP_HALT_SHIFT) & 1u;
    Fields->PfpStep = (Raw >> GFX_ME_PFP_STEP_SHIFT) & 1u;
    Fields->MeHalt  = (Raw >> GFX_ME_ME_HALT_SHIFT) & 1u;
    Fields->AllHalted =
        (Fields->CeHalt == 1u && Fields->PfpHalt == 1u && Fields->MeHalt == 1u)
            ? 1u : 0u;
    return BC250_GFX_RING_OK;
}

uint32_t Bc250GfxRingMicroEnginesAreHalted(uint32_t MeCntl)
{
    /*
     * QUALQUER uma halted ja impede a execucao do anel grafico -- nao e'
     * preciso que as tres estejam. Exigir as tres aqui deixaria passar o caso
     * em que so' o PFP esta parado, que e' igualmente inutil e mais dificil
     * de diagnosticar.
     */
    if (((MeCntl >> GFX_ME_CE_HALT_SHIFT) & 1u) != 0u) return 1u;
    if (((MeCntl >> GFX_ME_PFP_HALT_SHIFT) & 1u) != 0u) return 1u;
    if (((MeCntl >> GFX_ME_ME_HALT_SHIFT) & 1u) != 0u) return 1u;
    return 0u;
}

BC250_GFX_RING_STATUS Bc250GfxRingComputeMeCntlUnhalt(
    uint32_t CurrentMeCntl,
    uint32_t *UnhaltedMeCntl)
{
    const uint32_t haltMask =
        (1u << GFX_ME_CE_HALT_SHIFT) |
        (1u << GFX_ME_PFP_HALT_SHIFT) |
        (1u << GFX_ME_ME_HALT_SHIFT);

    if (UnhaltedMeCntl == NULL) {
        return BC250_GFX_RING_BAD_ARGUMENT;
    }
    /* Lei 6: val = (lido & ~mask) | (or_val & mask), com or_val = 0. */
    *UnhaltedMeCntl = (CurrentMeCntl & ~haltMask) | (0u & haltMask);
    return BC250_GFX_RING_OK;
}

uint32_t Bc250GfxRingCntlRefusesNonExecutable(uint32_t Cntl)
{
    return (((Cntl >> GFX_RB_EXE_SHIFT) & 1u) == 0u) ? 1u : 0u;
}

uint32_t Bc250GfxRingPollCntlIsBorrowed(uint32_t PollCntl)
{
    return (PollCntl == BC250_GFX_RING_POLL_CNTL_SDMA_BORROWED) ? 1u : 0u;
}

BC250_TRIPOD_VERDICT Bc250GfxRingClassifyTripod(
    uint32_t RingBytes,
    uint32_t TargetWptrBytes,
    uint32_t RptrWriteback,
    uint32_t RptrRegister,
    uint32_t FenceExpected,
    uint32_t FenceObserved,
    uint32_t DataMismatches,
    BC250_TRIPOD_RPTR_SOURCE *RptrSource)
{
    uint32_t target;
    uint32_t wb;
    uint32_t reg;

    /*
     * A reducao por volta do anel usa a FONTE UNICA da A15. Repetir a
     * aritmetica aqui recriaria o defeito que ela fechou.
     */
    target = Bc250TripodWrapRingPointer(TargetWptrBytes, RingBytes);
    wb = Bc250TripodWrapRingPointer(RptrWriteback, RingBytes);
    reg = Bc250TripodWrapRingPointer(RptrRegister, RingBytes);

    return Bc250ClassifySdmaTripod(target, wb, reg, FenceExpected,
        FenceObserved, DataMismatches, RptrSource);
}

const char *Bc250GfxRingStatusName(BC250_GFX_RING_STATUS Status)
{
    switch (Status) {
    case BC250_GFX_RING_OK:                   return "OK";
    case BC250_GFX_RING_BAD_ARGUMENT:         return "BAD_ARGUMENT";
    case BC250_GFX_RING_BAD_SIZE:             return "BAD_SIZE";
    case BC250_GFX_RING_RING_MISALIGNED:      return "RING_MISALIGNED";
    case BC250_GFX_RING_WRITEBACK_MISALIGNED: return "WRITEBACK_MISALIGNED";
    case BC250_GFX_RING_OUT_OF_WINDOW:        return "OUT_OF_WINDOW";
    case BC250_GFX_RING_OVERLAP:              return "OVERLAP";
    case BC250_GFX_RING_FIELD_OVERFLOW:       return "FIELD_OVERFLOW";
    case BC250_GFX_RING_NOT_EXECUTABLE:       return "NOT_EXECUTABLE";
    case BC250_GFX_RING_POLL_CNTL_BORROWED:   return "POLL_CNTL_BORROWED";
    case BC250_GFX_RING_FIRMWARE_NOT_LOADED:  return "FIRMWARE_NOT_LOADED";
    default:                                  return "DESCONHECIDO";
    }
}
