#include "../core/bc250_gpuva_snapshot_contract.h"

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return __LINE__; } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) { printf("FAIL line %d: %s\n", __LINE__, #Expression); return __LINE__; } } while (0)
#endif

static int Bc250RunGpuvaSnapshotContractTest(void)
{
    static BC250_GPUVA_SNAPSHOT_STORE store;
    static BC250_GPUVA_SPACE scratch;
    BC250_GPUVA_SNAPSHOT_READER oldReader, newReader;
    BC250_GPUVA_RANGE_RECORD range;
    uint32_t generation;

    CHECK(Bc250GpuvaSnapshotStoreInitialize(&store, 48u) == BC250_GPUVA_OK);
    CHECK(Bc250GpuvaSnapshotAcquire(&store, &oldReader) == BC250_GPUVA_OK);
    CHECK(oldReader.Slot == 0u);

    range.State = 0u;
    range.Generation = 0u;
    range.RangeId = 0u;
    range.BaseGpuVa = 0x40000000ull;
    range.Size = 0x2000ull;
    range.EndGpuVa = 0u;
    range.OwnerProcess = (BC250_MCDM_HANDLE)0x1000;
    range.OwnerContext = (BC250_MCDM_HANDLE)0x1100;
    range.Allocation = NULL;
    range.OpenAllocation = NULL;
    range.BackingSegmentId = 1u;
    range.BackingSegmentOffset = 0x800000ull;
    range.Permissions = BC250_PTE_READABLE | BC250_PTE_EXECUTABLE;
    range.IsResident = 1u;
    range.PageTableEpoch = 0u;
    range.TeardownState = 0u;

    CHECK(Bc250GpuvaSnapshotPublishReplace(
        &store, &scratch, &range, 1u, &generation) == BC250_GPUVA_OK);
    CHECK(generation == 2u);
    CHECK(oldReader.Space->LiveCount == 0u);
    CHECK(Bc250GpuvaSnapshotAcquire(&store, &newReader) == BC250_GPUVA_OK);
    CHECK(newReader.Slot == 1u);
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(newReader.Space,
        range.OwnerProcess, range.OwnerContext, range.BaseGpuVa, range.Size,
        BC250_PTE_EXECUTABLE, newReader.Space->PageTableEpoch) == BC250_GPUVA_OK);

    /* O leitor antigo segura slot 0: a proxima publicacao deve falhar fechada. */
    range.BaseGpuVa = 0x50000000ull;
    CHECK(Bc250GpuvaSnapshotPublishReplace(
        &store, &scratch, &range, 1u, &generation) == BC250_GPUVA_BUSY);
    CHECK(store.ActiveSlot == 1L);

    Bc250GpuvaSnapshotRelease(&store, &oldReader);
    CHECK(Bc250GpuvaSnapshotPublishReplace(
        &store, &scratch, &range, 1u, &generation) == BC250_GPUVA_OK);
    CHECK(generation == 3u);
    CHECK(store.ActiveSlot == 0L);

    /* Leitor do snapshot anterior continua valido ate liberar sua referencia. */
    CHECK(newReader.Space->LiveCount == 1u);
    Bc250GpuvaSnapshotRelease(&store, &newReader);
    CHECK(store.ReaderState[0] == 0L && store.ReaderState[1] == 0L);

    /*
     * C3.8 (12/09) -- ASSIMETRIA ENTRE LEITOR E PUBLICADOR.
     *
     * MEDIDO na corrida 16:09: com o dwm.exe vivo, cada Present resolve uma
     * GPUVA e mantem contagem de leitor num slot. Quando esse slot vira o
     * inativo, a publicacao seguinte exige contagem ZERO. O leitor sempre
     * tentou 16 vezes; o publicador fazia UMA e desistia. Resultado: journal
     * PUBLISH_FAILED -> ActiveFlushTlb -> STATUS_IO_DEVICE_ERROR -> a admissao
     * do Node 1 nunca reabria -> STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE.
     *
     * Este bloco trava tres coisas:
     *   1. leitor preso no slot INATIVO faz a publicacao devolver BUSY, e o
     *      diagnostico separa BUSY de erro real (antes sairia o mesmo numero);
     *   2. o publicador REALMENTE retenta -- Attempts > 1 prova isso, e e' o
     *      que distingue a correcao de um no-op;
     *   3. liberado o leitor, a publicacao volta a funcionar na 1a tentativa.
     */
    {
        static BC250_GPUVA_SNAPSHOT_STORE c38Store;
        static BC250_GPUVA_SPACE c38Scratch;
        BC250_GPUVA_RANGE_RECORD c38Range;
        uint32_t c38MapRange = 1u;
        uint32_t c38Generation = 0u;
        long c38Inactive;

        CHECK(Bc250GpuvaSnapshotStoreInitialize(&c38Store, 34u) == BC250_GPUVA_OK);
        CHECK(Bc250GpuvaSpaceInitialize(&c38Scratch, 34u) == BC250_GPUVA_OK);

        memset(&c38Range, 0, sizeof(c38Range));
        c38Range.State = BC250_GPUVA_RANGE_MAPPED;
        c38Range.BaseGpuVa = 0x10000ull;
        c38Range.Size = 0x1000ull;
        c38Range.EndGpuVa = 0x11000ull;
        c38Range.OwnerProcess = (BC250_MCDM_HANDLE)0x4242;
        c38Range.BackingSegmentId = 1u;
        c38Range.BackingSegmentOffset = 0x2000ull;
        c38Range.Permissions = BC250_PTE_READABLE;
        c38Range.IsResident = 1u;

        /* Sem contencao: publica de primeira. */
        CHECK(Bc250GpuvaSnapshotPublishBatch(&c38Store, &c38Scratch, &c38Range,
            &c38MapRange, 1u, &c38Generation) == BC250_GPUVA_OK);
        CHECK(c38Store.LastPublishStatus == (long)BC250_GPUVA_OK);
        CHECK(c38Store.LastPublishBusyCount == 0L);
        CHECK(c38Store.LastPublishAttempts == 1L);

        /*
         * Simula o leitor preso no slot INATIVO -- exatamente o estado que o
         * dwm.exe produz ao segurar o slot que acabou de sair de ativo.
         */
        c38Inactive = 1L - c38Store.ActiveSlot;
        c38Store.ReaderState[c38Inactive] = 1L;

        CHECK(Bc250GpuvaSnapshotPublishBatch(&c38Store, &c38Scratch, &c38Range,
            &c38MapRange, 1u, &c38Generation) == BC250_GPUVA_BUSY);
        CHECK(c38Store.LastPublishStatus == (long)BC250_GPUVA_BUSY);
        /* O PONTO: o publicador retentou, nao desistiu na primeira. */
        CHECK(c38Store.LastPublishBusyCount ==
            (long)BC250_GPUVA_SNAPSHOT_PUBLISH_ATTEMPTS);
        CHECK(c38Store.LastPublishAttempts ==
            (long)BC250_GPUVA_SNAPSHOT_PUBLISH_ATTEMPTS);
        /* E BUSY nao pode ser confundido com sucesso nem com erro de faixa. */
        CHECK(c38Store.LastPublishStatus != (long)BC250_GPUVA_OK);

        /* Leitor sai: volta a publicar, e o diagnostico zera o BUSY. */
        c38Store.ReaderState[c38Inactive] = 0L;
        CHECK(Bc250GpuvaSnapshotPublishBatch(&c38Store, &c38Scratch, &c38Range,
            &c38MapRange, 1u, &c38Generation) == BC250_GPUVA_OK);
        CHECK(c38Store.LastPublishBusyCount == 0L);
        CHECK(c38Store.LastPublishAttempts == 1L);
        CHECK(c38Store.LastPublishStatus == (long)BC250_GPUVA_OK);
    }

    /* Corrida 41: PublishStatus=NO_SPACE sem dizer qual item do batch o
     * provocou. Preenche o snapshot ativo e prova a foto exata da falha. */
    {
        static BC250_GPUVA_SNAPSHOT_STORE fullStore;
        static BC250_GPUVA_SPACE fullScratch;
        BC250_GPUVA_RANGE_RECORD extra;
        uint32_t map = 1u;
        long activeSlot;

        CHECK(Bc250GpuvaSnapshotStoreInitialize(&fullStore, 34u) == BC250_GPUVA_OK);
        CHECK(Bc250GpuvaSpaceInitialize(&fullScratch, 34u) == BC250_GPUVA_OK);
        activeSlot = fullStore.ActiveSlot;
        for (uint32_t i = 0u; i < BC250_GPUVA_SPACE_MAX_RANGES; ++i) {
            BC250_GPUVA_RANGE_RECORD *r = &fullStore.Slots[activeSlot].Ranges[i];
            memset(r, 0, sizeof(*r));
            r->State = BC250_GPUVA_RANGE_MAPPED;
            r->BaseGpuVa = 0x100000ull + (uint64_t)i * 0x1000ull;
            r->Size = 0x1000ull;
            r->EndGpuVa = r->BaseGpuVa + r->Size;
            r->OwnerProcess = (BC250_MCDM_HANDLE)0x5151;
            r->Permissions = BC250_PTE_READABLE;
            r->IsResident = 1u;
            fullStore.Slots[activeSlot].SortedIndices[i] = i;
        }
        fullStore.Slots[activeSlot].SortedCount = BC250_GPUVA_SPACE_MAX_RANGES;
        memset(&extra, 0, sizeof(extra));
        extra.State = BC250_GPUVA_RANGE_MAPPED;
        extra.BaseGpuVa = 0x80000000ull;
        extra.Size = 0x2000ull;
        extra.EndGpuVa = extra.BaseGpuVa + extra.Size;
        extra.OwnerProcess = (BC250_MCDM_HANDLE)0x5151;
        extra.Permissions = BC250_PTE_READABLE;
        extra.IsResident = 1u;
        CHECK(Bc250GpuvaSnapshotPublishBatch(&fullStore, &fullScratch,
            &extra, &map, 1u, NULL) == BC250_GPUVA_NO_SPACE);
        CHECK(fullStore.LastPublishRangeCount == 1L);
        CHECK(fullStore.LastPublishFailedRangeIndex == 0L);
        CHECK(fullStore.LastPublishFailedMapRange == 1L);
        CHECK(fullStore.LastPublishFailedBaseGpuVa == extra.BaseGpuVa);
        CHECK(fullStore.LastPublishFailedSize == extra.Size);
    }

    /*
     * C3.14 (12/09) -- REPLAY DO ENVELOPE MEDIDO NA CORRIDA 17:45.
     *
     * O log integral chegou a Mapped=252 com o teto antigo de 256. A corrida
     * seguinte (18:21) revelou um update esparso real de 461 runs. O diagnostico anterior dizia que o
     * NO_SPACE era apenas do rebuild no Scratch, mas Scratch parte vazio e
     * contem exatamente o estado final: um replace in-place nao faria 713
     * faixas caberem em 256 slots. Este caso falha com o teto antigo e prova
     * que a publicacao inteira continua atomica ao ampliar a capacidade.
     */
    {
        enum { C314_EXISTING_RANGES = 252, C314_BATCH_RANGES = 461 };
        static BC250_GPUVA_SNAPSHOT_STORE c314Store;
        static BC250_GPUVA_SPACE c314Scratch;
        static BC250_GPUVA_RANGE_RECORD c314Ranges[C314_BATCH_RANGES];
        static uint32_t c314MapRanges[C314_BATCH_RANGES];
        BC250_GPUVA_RANGE_RECORD c314Range;
        BC250_GPUVA_SNAPSHOT_READER c314Reader;
        BC250_GPUVA_SPACE_OCCUPANCY c314Occupancy;
        uint64_t c314RangeId;
        uint32_t c314Generation = 0u;
        uint32_t c314Index;

        CHECK(BC250_GPUVA_SPACE_MAX_RANGES >=
            C314_EXISTING_RANGES + C314_BATCH_RANGES);
        CHECK(Bc250GpuvaSnapshotStoreInitialize(&c314Store, 34u) ==
            BC250_GPUVA_OK);
        CHECK(Bc250GpuvaSpaceInitialize(&c314Scratch, 34u) == BC250_GPUVA_OK);

        memset(&c314Range, 0, sizeof(c314Range));
        c314Range.OwnerProcess = (BC250_MCDM_HANDLE)0xC314;
        c314Range.Size = BC250_GPUVA_PAGE_BYTES;
        c314Range.BackingSegmentId = 1u;
        c314Range.Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE;
        for (c314Index = 0u; c314Index < C314_EXISTING_RANGES; ++c314Index) {
            c314Range.BaseGpuVa = 0x10000000ull +
                ((uint64_t)c314Index * 2ull * BC250_GPUVA_PAGE_BYTES);
            c314Range.BackingSegmentOffset =
                (uint64_t)c314Index * BC250_GPUVA_PAGE_BYTES;
            CHECK(Bc250GpuvaSpaceInsertRange(&c314Store.Slots[0], &c314Range,
                &c314RangeId) == BC250_GPUVA_OK);
        }

        memset(c314Ranges, 0, sizeof(c314Ranges));
        for (c314Index = 0u; c314Index < C314_BATCH_RANGES; ++c314Index) {
            c314Ranges[c314Index].OwnerProcess =
                (BC250_MCDM_HANDLE)0xC314;
            c314Ranges[c314Index].BaseGpuVa = 0x20000000ull +
                ((uint64_t)c314Index * 2ull * BC250_GPUVA_PAGE_BYTES);
            c314Ranges[c314Index].Size = BC250_GPUVA_PAGE_BYTES;
            c314Ranges[c314Index].BackingSegmentId = 0u;
            c314Ranges[c314Index].BackingSegmentOffset =
                (uint64_t)c314Index * 3ull * BC250_GPUVA_PAGE_BYTES;
            c314Ranges[c314Index].Permissions = BC250_PTE_READABLE;
            c314MapRanges[c314Index] = 1u;
        }

        CHECK(Bc250GpuvaSnapshotPublishBatch(&c314Store, &c314Scratch,
            c314Ranges, c314MapRanges, C314_BATCH_RANGES,
            &c314Generation) == BC250_GPUVA_OK);
        CHECK(c314Generation == 2u);
        CHECK(c314Store.ActiveSlot == 1L);
        CHECK(Bc250GpuvaSnapshotAcquire(&c314Store, &c314Reader) ==
            BC250_GPUVA_OK);
        CHECK(Bc250GpuvaSpaceDescribeOccupancy(c314Reader.Space,
            &c314Occupancy) == BC250_GPUVA_OK);
        CHECK(c314Occupancy.Mapped ==
            C314_EXISTING_RANGES + C314_BATCH_RANGES);
        CHECK(c314Occupancy.LiveCount == c314Occupancy.Mapped);
        CHECK(Bc250GpuvaSpaceValidateRangeCoverage(c314Reader.Space,
            (BC250_MCDM_HANDLE)0xC314, NULL,
            c314Ranges[C314_BATCH_RANGES - 1u].BaseGpuVa,
            BC250_GPUVA_PAGE_BYTES, BC250_PTE_READABLE,
            c314Reader.Space->PageTableEpoch) == BC250_GPUVA_OK);
        Bc250GpuvaSnapshotRelease(&c314Store, &c314Reader);
    }

    /*
     * D3 (20/09) -- REPLAY DA CORRIDA 44 E COMPOSICAO DO LOTE.
     *
     * Na Corrida 44 houve SEIS PUBLICACAO_FALHA e em TODAS FailedIndex saiu
     * exatamente igual a Reusable do slot ativo, com Mapped+Reusable=Cap=1024:
     *   Batch=832 FailedIndex=655 Mapped=369    Batch=657 FailedIndex=640 Mapped=384
     *   Batch=721 FailedIndex=603 Mapped=421    Batch=695 FailedIndex=460 Mapped=564
     *   Batch=513 FailedIndex=430 Mapped=594    Batch=657 FailedIndex=415 Mapped=609
     *
     * O mecanismo e' que Bc250GpuvaCopySnapshot copia o slot ATIVO para o
     * inativo e o lote entra POR CIMA: o teto util e' Cap menos Mapped. Este
     * caso reproduz o primeiro par (369 vivas, lote de 832 maps) e exige o
     * indice EXATO 655 -- se a aritmetica mudar, o teste reprova em vez de
     * aceitar "falhou em algum lugar".
     *
     * E exige a COMPOSICAO: Maps e Unmaps contados antes de qualquer mutacao.
     * Sem esse numero nao se sabe se aplicar unmaps antes de maps derrubaria o
     * pico ou se e' capacidade real, e propor a reordenacao seria fix de efeito
     * imprevisivel.
     */
    {
        static BC250_GPUVA_SNAPSHOT_STORE d3Store;
        static BC250_GPUVA_SPACE d3Scratch;
        static BC250_GPUVA_RANGE_RECORD d3Batch[832];
        static uint32_t d3Map[832];
        /* O teto util e' Cap - Mapped; testa a formula com margem exata de 655 */
        CHECK(BC250_GPUVA_SPACE_MAX_RANGES >= 1024u);
        const uint32_t kLive = BC250_GPUVA_SPACE_MAX_RANGES - 655u;
        const uint32_t kBatch = 832u;
        long d3Slot;

        CHECK(Bc250GpuvaSnapshotStoreInitialize(&d3Store, 34u) == BC250_GPUVA_OK);
        CHECK(Bc250GpuvaSpaceInitialize(&d3Scratch, 34u) == BC250_GPUVA_OK);

        d3Slot = d3Store.ActiveSlot;
        for (uint32_t i = 0u; i < kLive; ++i) {
            BC250_GPUVA_RANGE_RECORD *r = &d3Store.Slots[d3Slot].Ranges[i];
            memset(r, 0, sizeof(*r));
            r->State = BC250_GPUVA_RANGE_MAPPED;
            r->BaseGpuVa = 0x200000ull + (uint64_t)i * 0x1000ull;
            r->Size = 0x1000ull;
            r->EndGpuVa = r->BaseGpuVa + r->Size;
            r->OwnerProcess = (BC250_MCDM_HANDLE)0x4444;
            r->Permissions = BC250_PTE_READABLE;
            r->IsResident = 1u;
            d3Store.Slots[d3Slot].SortedIndices[i] = i;
        }
        d3Store.Slots[d3Slot].SortedCount = kLive;

        /* Lote disjunto das vivas, para que cada entrada consuma slot novo. */
        for (uint32_t i = 0u; i < kBatch; ++i) {
            memset(&d3Batch[i], 0, sizeof(d3Batch[i]));
            d3Batch[i].State = BC250_GPUVA_RANGE_MAPPED;
            d3Batch[i].BaseGpuVa = 0x40000000ull + (uint64_t)i * 0x1000ull;
            d3Batch[i].Size = 0x1000ull;
            d3Batch[i].EndGpuVa = d3Batch[i].BaseGpuVa + d3Batch[i].Size;
            d3Batch[i].OwnerProcess = (BC250_MCDM_HANDLE)0x4444;
            d3Batch[i].Permissions = BC250_PTE_READABLE;
            d3Batch[i].IsResident = 1u;
            d3Map[i] = 1u;
        }

        CHECK(Bc250GpuvaSnapshotPublishBatch(&d3Store, &d3Scratch,
            d3Batch, d3Map, kBatch, NULL) == BC250_GPUVA_NO_SPACE);
        /* O indice EXATO da Corrida 44: Cap - Mapped = 1024 - 369 = 655. */
        CHECK(d3Store.LastPublishFailedRangeIndex ==
              (long)(BC250_GPUVA_SPACE_MAX_RANGES - kLive));
        CHECK(d3Store.LastPublishFailedRangeIndex == 655L);
        CHECK(d3Store.LastPublishRangeCount == (long)kBatch);
        /* Composicao: lote 100% MAP -- reordenar unmaps NAO ajudaria aqui. */
        CHECK(d3Store.LastPublishMapCount == (long)kBatch);
        CHECK(d3Store.LastPublishUnmapCount == 0L);
        /* A publicacao falhada NAO pode trocar o slot ativo nem a geracao. */
        CHECK(d3Store.ActiveSlot == d3Slot);

        /*
         * CONTRAPONTO, para a assercao acima nao virar carimbo: o MESMO lote,
         * agora metade unmaps, tem composicao diferente -- e e' exatamente essa
         * diferenca que a proxima corrida vai medir no silicio.
         */
        for (uint32_t i = 0u; i < kBatch; i += 2u) { d3Map[i] = 0u; }
        (void)Bc250GpuvaSnapshotPublishBatch(&d3Store, &d3Scratch,
            d3Batch, d3Map, kBatch, NULL);
        CHECK(d3Store.LastPublishMapCount == (long)(kBatch / 2u));
        CHECK(d3Store.LastPublishUnmapCount == (long)(kBatch / 2u));
    }
    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess((unsigned long)Bc250RunGpuvaSnapshotContractTest());
}
#else
int main(void)
{
    return Bc250RunGpuvaSnapshotContractTest();
}
#endif
