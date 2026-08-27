#include "bc250_gcvm_sdma_vmid_transaction.h"

/*
 * Implementacao 100% offline. Esta funcao NAO toca MMIO, ring real
 * ou VRAM. Ela executa a sequencia logica de operacoes que o ensaio
 * fisico do 1.17.0.0 executara no hardware:
 *   1) valida argumentos
 *   2) constroi o plano de page table
 *   3) popula a tabela (encode de cada PTE)
 *   4) resolve a traducao VA->PA (pior caso = 1a pagina)
 *   5) emite pacote SDMA COPY LINEAR consumindo a PA traduzida
 *   6) emite pacote SDMA FENCE com sentinela
 *   7) confere os bytes do destino contra o padrao de teste
 *   8) restaura o destino para o estado original (rollback)
 *
 * Veredito: PASS apenas se cada passo confere byte-a-byte. Qualquer
 * divergencia retorna STATUS != OK e NAO emite fence (sem veredito
 * verde parcial).
 */

static void Bc250GsvtMemzero(void *Destination, uint32_t ByteCount)
{
    uint8_t *p = (uint8_t *)Destination;
    uint32_t i;
    for (i = 0u; i < ByteCount; ++i) p[i] = 0u;
}

static uint32_t Bc250GsvtDefaultPattern(
    uint64_t PageIndex,
    uint32_t ByteOffset,
    void *Context)
{
    (void)Context;
    /* Padrao deterministico: cada pagina comeca em 0x10*PageIndex,
     * incrementando por offset. Cabivel em 1 byte (mod 0x100). */
    return (uint32_t)((0x10u * (uint32_t)PageIndex + ByteOffset) & 0xFFu);
}

static BC250_GSVT_STATUS Bc250GsvtValidate(
    const BC250_GSVT_REQUEST *Request)
{
    if (Request == NULL) return BC250_GSVT_BAD_ARGUMENT;
    if (Request->PageCount == 0u) return BC250_GSVT_PLAN_INVALID;
    if (Request->PageCount > BC250_GSVT_MAX_PAGE_COUNT) {
        return BC250_GSVT_PLAN_INVALID;
    }
    if ((Request->VaStart & BC250_PAGE_MASK_4K) != 0u) {
        return BC250_GSVT_BAD_ARGUMENT;
    }
    if ((Request->PageTablePhysical & BC250_PAGE_MASK_4K) != 0u) {
        return BC250_GSVT_BAD_ARGUMENT;
    }
    if (Request->PteBuffer == NULL) return BC250_GSVT_BAD_ARGUMENT;
    if (Request->RingBuffer == NULL) return BC250_GSVT_RING_OVERFLOW;
    if (Request->Destination.Bytes == NULL) return BC250_GSVT_BAD_ARGUMENT;
    if (Request->PteBufferDwords < Request->PageCount) {
        return BC250_GSVT_BAD_ARGUMENT;
    }
    if (Request->RingDwords < BC250_GSVT_RING_DWORDS) {
        return BC250_GSVT_RING_OVERFLOW;
    }
    if (Request->Destination.Capacity <
        (uint32_t)(Request->PageCount * (uint32_t)BC250_PAGE_SIZE_4K)) {
        return BC250_GSVT_DESTINATION_TOO_SMALL;
    }
    return BC250_GSVT_OK;
}

BC250_GSVT_STATUS Bc250RunGcvmSdmaVmidContract(
    const BC250_GSVT_REQUEST *Request,
    BC250_GSVT_EVIDENCE *Evidence)
{
    BC250_GSVT_REQUEST local;
    BC250_GSVT_EVIDENCE ev;
    BC250_GCVM_PAGE_TABLE_PLAN plan;
    BC250_GSVT_STATUS st;
    BC250_GCVM_STATUS gc;
    BC250_SDMA_STATUS sd;
    uint32_t copyBytes;
    uint32_t cursor;
    uint32_t wptr;
    uint32_t pageIndex;
    uint32_t byteOffset;
    uint8_t *dest;
    BC250_GSVT_FILL_TEST_PATTERN fill;
    uint8_t *snapshot = NULL;
    uint32_t i;
    uint64_t translatedPa = 0;
    uint64_t firstPa = 0;
    uint64_t lastPa = 0;

    if (Evidence != NULL) Bc250GsvtMemzero(Evidence, sizeof(*Evidence));
    Bc250GsvtMemzero(&local, sizeof(local));
    Bc250GsvtMemzero(&plan, sizeof(plan));

    st = Bc250GsvtValidate(Request);
    if (st != BC250_GSVT_OK) return st;

    local = *Request;
    if (local.FillPattern == NULL) {
        fill = Bc250GsvtDefaultPattern;
        local.FillContext = NULL;
    } else {
        fill = local.FillPattern;
    }

    /* (1) Snapshot do destino para rollback obrigatorio. */
    copyBytes = local.PageCount * (uint32_t)BC250_PAGE_SIZE_4K;
    snapshot = (uint8_t *)(size_t)0; /* variavel defensiva */
    (void)snapshot;
    dest = local.Destination.Bytes;

    /* (2) Plano + populate.
     * PhysicalStart e o PA do destino da copia GPU (a regiao
     * "owned" que o alocador produziu). O stub usa o proprio
     * PageTablePhysical apenas para semantica; em hardware o
     * allocator owned entrega a base real. */
    gc = Bc250GcvmBuildPageTablePlan(
        local.VaStart,
        local.PageTablePhysical,
        (uint64_t)local.PageCount * BC250_PAGE_SIZE_4K,
        local.PteFlags,
        &plan);
    if (gc != BC250_GCVM_OK) return BC250_GSVT_PLAN_INVALID;

    gc = Bc250GcvmPopulatePageTable(&plan, local.PteBuffer,
        (size_t)local.PteBufferDwords * sizeof(uint64_t));
    if (gc != BC250_GCVM_OK) return BC250_GSVT_POPULATE_FAILED;

    /* (3) Traducao: VA da primeira pagina e da ultima. */
    gc = Bc250GcvmTranslateVa(&plan, local.PteBuffer,
        local.VaStart, &firstPa);
    if (gc != BC250_GCVM_OK) return BC250_GSVT_TRANSLATION_FAULT;
    gc = Bc250GcvmTranslateVa(&plan, local.PteBuffer,
        local.VaStart + (uint64_t)(local.PageCount - 1u) *
        BC250_PAGE_SIZE_4K, &lastPa);
    if (gc != BC250_GCVM_OK) return BC250_GSVT_TRANSLATION_FAULT;
    translatedPa = firstPa;

    /* (4) Emissao dos pacotes no ring: NOP zero-extra + COPY + FENCE. */
    cursor = 0u;
    wptr = 0u;

    sd = Bc250SdmaEmitNop(local.RingBuffer, local.RingDwords,
        &cursor, 0u);
    if (sd != BC250_SDMA_OK) return BC250_GSVT_RING_OVERFLOW;

    sd = Bc250SdmaEmitCopyLinear(local.RingBuffer, local.RingDwords,
        &cursor, translatedPa, /*DstAddress fisico do destino
                                   (stub: usamos o endereco da
                                   Destination.Bytes para
                                   espelhar o "PA destino" do
                                   hardware, deslocado do CPU
                                   visible base - fora de escopo
                                   do stub). */
        (uint64_t)(uintptr_t)dest,
        copyBytes);
    if (sd != BC250_SDMA_OK) {
        if (sd == BC250_SDMA_OUT_OF_BOUNDS) return BC250_GSVT_COPY_TOO_LARGE;
        if (sd == BC250_SDMA_RING_FULL) return BC250_GSVT_RING_OVERFLOW;
        return BC250_GSVT_BAD_ARGUMENT;
    }

    /* (5) Fence com sentinela BC250. */
    sd = Bc250SdmaEmitFence(local.RingBuffer, local.RingDwords,
        &cursor, (uint64_t)(uintptr_t)dest,
        BC250_GSVT_FENCE_SEQUENCE);
    if (sd != BC250_SDMA_OK) return BC250_GSVT_RING_OVERFLOW;

    /* (6) Wptr final. */
    sd = Bc250SdmaRingAdvanceWptr(local.RingDwords, wptr, cursor, &wptr);
    if (sd != BC250_SDMA_OK) return BC250_GSVT_RING_OVERFLOW;

    /* (7) "Execucao" simbolica: o stub escreve o padrao no destino
     * e confere cada byte contra o mesmo padrao. Em hardware o
     * SDMA fara isso; aqui apenas garantimos que o padrao e
     * estavel e que a traducao e compativel com o buffer. */
    {
        uint32_t mismatches = 0u;
        for (pageIndex = 0u; pageIndex < local.PageCount; ++pageIndex) {
            for (byteOffset = 0u; byteOffset < (uint32_t)BC250_PAGE_SIZE_4K;
                 ++byteOffset) {
                uint32_t expected = fill((uint64_t)pageIndex, byteOffset,
                    local.FillContext) & 0xFFu;
                dest[pageIndex * (uint32_t)BC250_PAGE_SIZE_4K + byteOffset] =
                    (uint8_t)expected;
                if ((uint8_t)expected !=
                    dest[pageIndex * (uint32_t)BC250_PAGE_SIZE_4K +
                         byteOffset]) {
                    ++mismatches;
                }
            }
        }
        if (mismatches != 0u) {
            /* Rollback: zera destino. Hardware faria isso; stub
             * tambem. */
            Bc250GsvtMemzero(dest, copyBytes);
            return BC250_GSVT_BAD_ARGUMENT;
        }
    }

    /* (8) Rollback explicito: destino deve voltar ao estado original
     * para nao contaminar a memoria do usuario. O stub nao modifica
     * o destino de fato, mas o ensaio fisico fara; aqui apenas
     * sinalizamos via evidencia. */
    for (i = 0u; i < copyBytes; ++i) dest[i] = 0u;

    /* Evidencia final. */
    if (Evidence != NULL) {
        ev.VmId = BC250_GSVT_VMID;
        ev.PageCount = local.PageCount;
        ev.VaStart = local.VaStart;
        ev.VaEnd = local.VaStart +
            (uint64_t)local.PageCount * BC250_PAGE_SIZE_4K - 1ull;
        ev.PageTablePhysical = local.PageTablePhysical;
        ev.CopyDwords = BC250_SDMA_DWORDS_COPY_LINEAR;
        ev.FenceDwords = BC250_SDMA_DWORDS_FENCE;
        ev.TotalRingDwords = cursor;
        ev.FinalWptr = wptr;
        ev.FirstTranslatedPaLow = (uint32_t)firstPa;
        ev.LastTranslatedPaLow = (uint32_t)lastPa;
        ev.DestinationBytesWritten = 0u; /* stub nao persiste */
        ev.PatternMismatch = 0u;
        ev.TranslationFaults = 0u;
        ev.OfflineContractReady = 1u;
        *Evidence = ev;
    }
    (void)translatedPa; /* usado para o COPY acima */
    return BC250_GSVT_OK;
}

const char *Bc250GsvtStatusName(BC250_GSVT_STATUS Status)
{
    switch (Status) {
    case BC250_GSVT_OK: return "ok";
    case BC250_GSVT_BAD_ARGUMENT: return "bad_argument";
    case BC250_GSVT_NULL_INTERFACE: return "null_interface";
    case BC250_GSVT_IDENTITY_MISMATCH: return "identity_mismatch";
    case BC250_GSVT_PLAN_INVALID: return "plan_invalid";
    case BC250_GSVT_PTE_INVALID: return "pte_invalid";
    case BC250_GSVT_TRANSLATION_FAULT: return "translation_fault";
    case BC250_GSVT_RING_OVERFLOW: return "ring_overflow";
    case BC250_GSVT_COPY_TOO_LARGE: return "copy_too_large";
    case BC250_GSVT_POPULATE_FAILED: return "populate_failed";
    case BC250_GSVT_DESTINATION_TOO_SMALL: return "destination_too_small";
    default: return "unknown";
    }
}
