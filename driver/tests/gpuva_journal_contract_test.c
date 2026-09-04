#include "../core/bc250_gpuva_journal_contract.h"

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

static int Bc250RunGpuvaJournalContractTest(void)
{
    static BC250_GPUVA_JOURNAL journal;
    static BC250_GPUVA_SNAPSHOT_STORE store;
    static BC250_GPUVA_SPACE scratch;
    BC250_GPUVA_JOURNAL_PTE entries[2];
    BC250_GPUVA_SNAPSHOT_READER reader;
    BC250_MCDM_HANDLE process = (BC250_MCDM_HANDLE)0x1000;
    uint32_t generation = 0u;

    CHECK(Bc250GpuvaJournalInitialize(&journal) == BC250_GPUVA_JOURNAL_OK);
    CHECK(Bc250GpuvaSnapshotStoreInitialize(&store, 48u) == BC250_GPUVA_OK);
    memset(entries, 0, sizeof(entries));
    entries[0].Valid = 1u;
    entries[0].Permissions = BC250_PTE_READABLE | BC250_PTE_EXECUTABLE;
    entries[0].SegmentId = 1u;
    entries[0].SegmentOffset = 0x800000u;
    entries[0].Allocation = (BC250_MCDM_HANDLE)0x2000;
    entries[1] = entries[0];
    entries[1].SegmentOffset += BC250_GPUVA_PAGE_BYTES;

    CHECK(Bc250GpuvaJournalStageLeafUpdate(&journal, process, 0x900000u,
        0x40000000u, entries, 2u) == BC250_GPUVA_JOURNAL_OK);
    /*
     * Duas paginas contiguas, mesmas permissoes, mesmo segmento e offset
     * consecutivo sao UM range -- nao dois registros. Ver o comentario de
     * coalescencia em bc250_gpuva_journal_contract.c.
     */
    CHECK(journal.Pending == 1u && journal.ChangeCount == 1u);
    CHECK(journal.Changes[0].BaseGpuVa == 0x40000000u);
    CHECK(journal.Changes[0].Size == 2u * BC250_GPUVA_PAGE_BYTES);
    CHECK(Bc250GpuvaJournalCommitFlush(&journal, &store, &scratch, process,
        0xA00000u, 0u, 0u, &generation) == BC250_GPUVA_JOURNAL_MISMATCH);
    CHECK(journal.Pending == 1u);
    CHECK(Bc250GpuvaJournalCommitFlush(&journal, &store, &scratch, process,
        0x900000u, 0x40000000u, 0x40001000u, &generation) ==
        BC250_GPUVA_JOURNAL_RANGE_NOT_FLUSHED);
    CHECK(Bc250GpuvaJournalCommitFlush(&journal, &store, &scratch, process,
        0x900000u, 0x40000000u, 0x40002000u, &generation) ==
        BC250_GPUVA_JOURNAL_OK);
    CHECK(journal.Pending == 0u && generation == 2u);
    CHECK(Bc250GpuvaSnapshotAcquire(&store, &reader) == BC250_GPUVA_OK);
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(reader.Space, process, NULL,
        0x40000000u, 0x2000u, BC250_PTE_EXECUTABLE,
        reader.Space->PageTableEpoch) == BC250_GPUVA_OK);
    Bc250GpuvaSnapshotRelease(&store, &reader);

    /* Invalidar uma pagina e confirmar somente com flush global. */
    memset(entries, 0, sizeof(entries));
    CHECK(Bc250GpuvaJournalStageLeafUpdate(&journal, process, 0x900000u,
        0x40000000u, entries, 1u) == BC250_GPUVA_JOURNAL_OK);
    CHECK(Bc250GpuvaJournalCommitFlush(&journal, &store, &scratch, process,
        0x900000u, 0u, 0u, &generation) == BC250_GPUVA_JOURNAL_OK);
    CHECK(Bc250GpuvaSnapshotAcquire(&store, &reader) == BC250_GPUVA_OK);
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(reader.Space, process, NULL,
        0x40000000u, 0x1000u, BC250_PTE_READABLE,
        reader.Space->PageTableEpoch) != BC250_GPUVA_OK);
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(reader.Space, process, NULL,
        0x40001000u, 0x1000u, BC250_PTE_READABLE,
        reader.Space->PageTableEpoch) == BC250_GPUVA_OK);
    Bc250GpuvaSnapshotRelease(&store, &reader);

    /*
     * Coalescencia: o que a torna necessaria e o que a limita.
     */
    CHECK(Bc250GpuvaJournalInitialize(&journal) == BC250_GPUVA_JOURNAL_OK);

    /* Offset de segmento NAO consecutivo nao pode fundir. */
    memset(entries, 0, sizeof(entries));
    entries[0].Valid = 1u;
    entries[0].Permissions = BC250_PTE_READABLE;
    entries[0].SegmentId = 1u;
    entries[0].SegmentOffset = 0x800000u;
    entries[0].Allocation = (BC250_MCDM_HANDLE)0x2000;
    entries[1] = entries[0];
    entries[1].SegmentOffset = 0x900000u; /* salto: paginas fisicas disjuntas */
    CHECK(Bc250GpuvaJournalStageLeafUpdate(&journal, process, 0x900000u,
        0x50000000u, entries, 2u) == BC250_GPUVA_JOURNAL_OK);
    CHECK(journal.ChangeCount == 2u);

    /* Permissoes diferentes tampouco fundem. */
    Bc250GpuvaJournalAbort(&journal);
    entries[1] = entries[0];
    entries[1].SegmentOffset += BC250_GPUVA_PAGE_BYTES;
    entries[1].Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE;
    CHECK(Bc250GpuvaJournalStageLeafUpdate(&journal, process, 0x900000u,
        0x50000000u, entries, 2u) == BC250_GPUVA_JOURNAL_OK);
    CHECK(journal.ChangeCount == 2u);

    /*
     * REGRESSAO DO TETO DE 256: uma allocation grande e fisicamente contigua,
     * estagiada em pedacos do tamanho do buffer de staging (como o handler do
     * miniport faz), tem de caber em UM registro. Com um registro por pagina
     * isto estourava o journal e o BC250_GPUVA_SPACE. A primary allocation de
     * 2560x1080x4 sao 2700 paginas.
     */
    Bc250GpuvaJournalAbort(&journal);
    {
        static BC250_GPUVA_JOURNAL_PTE bulk[BC250_GPUVA_JOURNAL_MAX_CHANGES];
        const uint32_t totalPages = 2700u;
        uint32_t staged;
        uint32_t i;
        for (staged = 0u; staged < totalPages; ) {
            uint32_t chunk = totalPages - staged;
            if (chunk > BC250_GPUVA_JOURNAL_MAX_CHANGES) {
                chunk = BC250_GPUVA_JOURNAL_MAX_CHANGES;
            }
            memset(bulk, 0, sizeof(bulk));
            for (i = 0u; i < chunk; ++i) {
                bulk[i].Valid = 1u;
                bulk[i].Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE;
                bulk[i].SegmentId = 1u;
                bulk[i].SegmentOffset = 0x1000000u +
                    ((uint64_t)(staged + i) * BC250_GPUVA_PAGE_BYTES);
                bulk[i].Allocation = (BC250_MCDM_HANDLE)0x3000;
            }
            CHECK(Bc250GpuvaJournalStageLeafUpdate(&journal, process, 0x900000u,
                0x60000000u + ((uint64_t)staged * BC250_GPUVA_PAGE_BYTES),
                bulk, chunk) == BC250_GPUVA_JOURNAL_OK);
            staged += chunk;
        }
        CHECK(journal.ChangeCount == 1u);
        CHECK(journal.Changes[0].BaseGpuVa == 0x60000000u);
        CHECK(journal.Changes[0].Size ==
            (uint64_t)totalPages * BC250_GPUVA_PAGE_BYTES);
    }
    Bc250GpuvaJournalAbort(&journal);

    /*
     * Ordem observada no Windows em 04/09: leaf updates antes de
     * SetRootPageTable, seguidos por FlushTlb que traz a root oficial.
     */
    memset(entries, 0, sizeof(entries));
    entries[0].Valid = 1u;
    entries[0].Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE;
    entries[0].SegmentId = 1u;
    entries[0].SegmentOffset = 0xC10000u;
    entries[0].Allocation = (BC250_MCDM_HANDLE)0x4000;
    CHECK(Bc250GpuvaJournalStageLeafUpdate(&journal, process, 0u,
        0xAC0000u, entries, 1u) == BC250_GPUVA_JOURNAL_OK);
    CHECK(journal.ExpectedRootPageTable == 0u);
    CHECK(Bc250GpuvaJournalCommitFlush(&journal, &store, &scratch, process,
        0x1F56F000u, 0xAC0000u, 0xAD0000u, &generation) ==
        BC250_GPUVA_JOURNAL_OK);
    CHECK(Bc250GpuvaSnapshotAcquire(&store, &reader) == BC250_GPUVA_OK);
    {
        uint32_t segment = 0u;
        uint64_t offset = 0ull;
        CHECK(Bc250GpuvaSpaceResolveAddress(reader.Space, process, 0xAC0000u,
            BC250_PTE_READABLE, &segment, &offset) == BC250_GPUVA_OK);
        CHECK(segment == 1u && offset == 0xC10000u);
    }
    Bc250GpuvaSnapshotRelease(&store, &reader);

    /*
     * Transicao entre multiplos processos: Process 1 estagia updates sem
     * chamar FlushTlb. Em seguida, Process 2 chama StageLeafUpdate.
     * O contrato deve abortar o lote pendente de Process 1 e aceitar
     * o lote de Process 2 sem retornar BUSY.
     */
    {
        BC250_MCDM_HANDLE process1 = (BC250_MCDM_HANDLE)0x1001;
        BC250_MCDM_HANDLE process2 = (BC250_MCDM_HANDLE)0x1002;
        memset(entries, 0, sizeof(entries));
        entries[0].Valid = 1u;
        entries[0].Permissions = BC250_PTE_READABLE;
        entries[0].SegmentId = 1u;
        entries[0].SegmentOffset = 0x500000u;
        entries[0].Allocation = (BC250_MCDM_HANDLE)0x5001;
        CHECK(Bc250GpuvaJournalStageLeafUpdate(&journal, process1, 0u,
            0x10000000u, entries, 1u) == BC250_GPUVA_JOURNAL_OK);
        CHECK(journal.Pending == 1u && journal.Process == process1);

        /* Processo 2 chega: deve abortar Processo 1 e aceitar Processo 2 */
        entries[0].SegmentOffset = 0x600000u;
        entries[0].Allocation = (BC250_MCDM_HANDLE)0x5002;
        CHECK(Bc250GpuvaJournalStageLeafUpdate(&journal, process2, 0u,
            0x20000000u, entries, 1u) == BC250_GPUVA_JOURNAL_OK);
        CHECK(journal.Pending == 1u && journal.Process == process2);
        CHECK(journal.Changes[0].BaseGpuVa == 0x20000000u);

        /* Processo 2 comita com sucesso */
        CHECK(Bc250GpuvaJournalCommitFlush(&journal, &store, &scratch, process2,
            0x1000u, 0x20000000u, 0x20001000u, &generation) == BC250_GPUVA_JOURNAL_OK);
        CHECK(journal.Pending == 0u);
    }
    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void) { ExitProcess((unsigned long)Bc250RunGpuvaJournalContractTest()); }
#else
int main(void) { return Bc250RunGpuvaJournalContractTest(); }
#endif
