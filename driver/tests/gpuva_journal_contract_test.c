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
    entries[0].AllocationToken = 0x5245530000000042ull;
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
    CHECK(journal.Changes[0].AllocationToken == 0x5245530000000042ull);
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

    /*
     * CONTROLE: reproduz o DEFEITO do journal unico.
     *
     * Este bloco nao testa a correcao -- ele demonstra, com o mesmo enredo da
     * corrida 04/09 18:25, que um journal unico PERDE a transacao do processo A
     * quando B intercala. Se algum dia este CHECK comecar a falhar porque o
     * range sobreviveu, o journal unico deixou de ser o defeito descrito e este
     * controle deve ser reescrito junto.
     */
    {
        static BC250_GPUVA_JOURNAL journalSolo;
        static BC250_GPUVA_SNAPSHOT_STORE storeSolo;
        static BC250_GPUVA_SPACE scratchSolo;
        BC250_GPUVA_JOURNAL_PTE entriesSolo[4];
        BC250_GPUVA_SNAPSHOT_READER readerSolo;
        BC250_MCDM_HANDLE procA = (BC250_MCDM_HANDLE)0x980;
        BC250_MCDM_HANDLE procB = (BC250_MCDM_HANDLE)0x740;
        uint32_t generationSolo = 0u;
        uint32_t segSolo = 0u;
        uint64_t offSolo = 0ull;

        CHECK(Bc250GpuvaJournalInitialize(&journalSolo) == BC250_GPUVA_JOURNAL_OK);
        CHECK(Bc250GpuvaSnapshotStoreInitialize(&storeSolo, 34u) == BC250_GPUVA_OK);
        CHECK(Bc250GpuvaSpaceInitialize(&scratchSolo, 34u) == BC250_GPUVA_OK);

        /* A estagia a primary do DWM: 0xAC0000 -> segmento 1, offset 0xAD0000. */
        memset(entriesSolo, 0, sizeof(entriesSolo));
        entriesSolo[0].Valid = 1u;
        entriesSolo[0].Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE;
        entriesSolo[0].SegmentId = 1u;
        entriesSolo[0].SegmentOffset = 0xAD0000ull;
        entriesSolo[0].Allocation = (BC250_MCDM_HANDLE)0xAD;
        CHECK(Bc250GpuvaJournalStageLeafUpdate(&journalSolo, procA,
            0x1F56F000ull, 0xAC0000ull, entriesSolo, 1u) ==
            BC250_GPUVA_JOURNAL_OK);
        CHECK(journalSolo.Process == procA);

        /* B intercala: o journal unico troca de dono e A e' descartado. */
        memset(entriesSolo, 0, sizeof(entriesSolo));
        entriesSolo[0].Valid = 1u;
        entriesSolo[0].Permissions = BC250_PTE_READABLE;
        entriesSolo[0].SegmentId = 1u;
        entriesSolo[0].SegmentOffset = 0x260000ull;
        CHECK(Bc250GpuvaJournalStageLeafUpdate(&journalSolo, procB,
            0ull, 0x260000ull, entriesSolo, 1u) == BC250_GPUVA_JOURNAL_OK);
        CHECK(journalSolo.Process == procB);

        /* A volta e comita: publica, mas 0xAC0000 nao esta mais la. */
        memset(entriesSolo, 0, sizeof(entriesSolo));
        entriesSolo[0].Valid = 1u;
        entriesSolo[0].Permissions = BC250_PTE_READABLE;
        entriesSolo[0].SegmentId = 1u;
        entriesSolo[0].SegmentOffset = 0xC00000ull;
        CHECK(Bc250GpuvaJournalStageLeafUpdate(&journalSolo, procA,
            0x1F56F000ull, 0xC00000ull, entriesSolo, 1u) ==
            BC250_GPUVA_JOURNAL_OK);
        CHECK(Bc250GpuvaJournalCommitFlush(&journalSolo, &storeSolo,
            &scratchSolo, procA, 0x1F56F000ull, 0ull, 0ull,
            &generationSolo) == BC250_GPUVA_JOURNAL_OK);

        CHECK(Bc250GpuvaSnapshotAcquire(&storeSolo, &readerSolo) == BC250_GPUVA_OK);
        /* 0xC00000 sobreviveu (foi estagiado depois da troca)... */
        CHECK(Bc250GpuvaSpaceResolveAddress(readerSolo.Space, procA, 0xC00000ull,
            BC250_PTE_READABLE, &segSolo, &offSolo) == BC250_GPUVA_OK);
        /* ...mas 0xAC0000 sumiu, com o mesmo NOT_FOUND do silicio (status=9). */
        CHECK(Bc250GpuvaSpaceResolveAddress(readerSolo.Space, procA, 0xAC0000ull,
            BC250_PTE_READABLE, &segSolo, &offSolo) == BC250_GPUVA_NOT_FOUND);
        Bc250GpuvaSnapshotRelease(&storeSolo, &readerSolo);
    }

    /*
     * REGRESSAO DA CORRIDA 04/09 18:25 -- intercalacao de processos.
     *
     * Com um journal unico, o padrao abaixo perdia a transacao do processo A:
     *   1335: [ActiveUpdatePT] FirstPteVA=0xAC0000, hProc=...980  (staged)
     *   1345: Journal process switch (old=...980, new=...740) -> Aborting
     *   1453: commit de ...980 publica a geracao 5 SEM 0xAC0000
     *   ->   [ResolveGpuVa] SpaceResolveAddress failed: status=9 (NOT_FOUND)
     *
     * Com journalsMp por processo, A sobrevive a intercalacao de B e comita.
     */
    {
        /* static: 4 journals sao ~112 KB e estourariam a sonda de pilha
         * (__chkstk nao existe no build freestanding dos testes). */
        static BC250_GPUVA_JOURNAL journalsMp[4];
        static BC250_GPUVA_SNAPSHOT_STORE storeMp;
        static BC250_GPUVA_SPACE scratchMp;
        BC250_GPUVA_JOURNAL_PTE entriesMp[4];
        BC250_GPUVA_JOURNAL *selected;
        BC250_GPUVA_SNAPSHOT_READER readerMp;
        BC250_MCDM_HANDLE procA = (BC250_MCDM_HANDLE)0x980;
        BC250_MCDM_HANDLE procB = (BC250_MCDM_HANDLE)0x740;
        uint32_t generationMp = 0u;
        uint32_t evictionKind = 0xFFFFFFFFu;
        uint32_t backingSegment = 0u;
        uint64_t backingOffset = 0ull;
        uint32_t i;

        for (i = 0u; i < 4u; ++i) {
            CHECK(Bc250GpuvaJournalInitialize(&journalsMp[i]) ==
                BC250_GPUVA_JOURNAL_OK);
        }
        CHECK(Bc250GpuvaSnapshotStoreInitialize(&storeMp, 34u) ==
            BC250_GPUVA_OK);
        CHECK(Bc250GpuvaSpaceInitialize(&scratchMp, 34u) == BC250_GPUVA_OK);
        CHECK(Bc250GpuvaJournalSetAnyPending(journalsMp, 4u) == 0u);

        /* A estagia a primary: 0xAC0000 -> segmento 1, offset 0xAD0000. */
        memset(entriesMp, 0, sizeof(entriesMp));
        entriesMp[0].Valid = 1u;
        entriesMp[0].Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE;
        entriesMp[0].SegmentId = 1u;
        entriesMp[0].SegmentOffset = 0xAD0000ull;
        entriesMp[0].Allocation = (BC250_MCDM_HANDLE)0xAD;
        selected = Bc250GpuvaJournalSetSelect(journalsMp, 4u, procA,
            0x1F56F000ull, 1u, &evictionKind);
        CHECK(selected != NULL);
        CHECK(evictionKind == BC250_GPUVA_JOURNAL_EVICT_NONE);
        CHECK(Bc250GpuvaJournalStageLeafUpdate(selected, procA,
            0x1F56F000ull, 0xAC0000ull, entriesMp, 1u) ==
            BC250_GPUVA_JOURNAL_OK);

        /* B intercala. Antes isso abortava A; agora cai em outro slot. */
        memset(entriesMp, 0, sizeof(entriesMp));
        entriesMp[0].Valid = 1u;
        entriesMp[0].Permissions = BC250_PTE_READABLE;
        entriesMp[0].SegmentId = 1u;
        entriesMp[0].SegmentOffset = 0x260000ull;
        entriesMp[0].Allocation = (BC250_MCDM_HANDLE)0x26;
        selected = Bc250GpuvaJournalSetSelect(journalsMp, 4u, procB,
            0ull, 1u, &evictionKind);
        CHECK(selected != NULL);
        CHECK(evictionKind == BC250_GPUVA_JOURNAL_EVICT_NONE);
        CHECK(Bc250GpuvaJournalStageLeafUpdate(selected, procB,
            0ull, 0x260000ull, entriesMp, 1u) == BC250_GPUVA_JOURNAL_OK);

        /* Os dois convivem: slots distintos, ambos pendentes. */
        CHECK(journalsMp[0].Pending == 1u && journalsMp[0].Process == procA);
        CHECK(journalsMp[1].Pending == 1u && journalsMp[1].Process == procB);

        /* A comita. O range 0xAC0000 TEM que estar publicado. */
        selected = Bc250GpuvaJournalSetSelect(journalsMp, 4u, procA,
            0x1F56F000ull, 0u, &evictionKind);
        CHECK(selected == &journalsMp[0]);
        CHECK(Bc250GpuvaJournalCommitFlush(selected, &storeMp, &scratchMp, procA,
            0x1F56F000ull, 0ull, 0ull, &generationMp) == BC250_GPUVA_JOURNAL_OK);
        CHECK(journalsMp[0].Pending == 0u);
        /* B continua intacto: o commit de A nao o tocou. */
        CHECK(journalsMp[1].Pending == 1u && journalsMp[1].Process == procB);

        CHECK(Bc250GpuvaSnapshotAcquire(&storeMp, &readerMp) == BC250_GPUVA_OK);
        CHECK(Bc250GpuvaSpaceResolveAddress(readerMp.Space, procA, 0xAC0000ull,
            BC250_PTE_READABLE, &backingSegment, &backingOffset) ==
            BC250_GPUVA_OK);
        CHECK(backingSegment == 1u && backingOffset == 0xAD0000ull);
        /* Isolamento entre processos continua valendo. */
        CHECK(Bc250GpuvaSpaceResolveAddress(readerMp.Space, procB, 0xAC0000ull,
            BC250_PTE_READABLE, &backingSegment, &backingOffset) !=
            BC250_GPUVA_OK);
        Bc250GpuvaSnapshotRelease(&storeMp, &readerMp);

        /* Sem slot livre, o despejo prefere quem nao tem root page table. */
        CHECK(Bc250GpuvaJournalSetSelect(journalsMp, 4u,
            (BC250_MCDM_HANDLE)0xAAA, 0x2000ull, 1u, &evictionKind) != NULL);
        CHECK(evictionKind == BC250_GPUVA_JOURNAL_EVICT_NONE);
        CHECK(Bc250GpuvaJournalSetAnyPending(journalsMp, 4u) == 1u);
    }

    /*
     * TETO TRADUZIVEL (regressao de 07/09).
     *
     * O journal recusava VA a partir de 2^48 -- o teto ARQUITETURAL. Mas o
     * contexto VMID8 traduz 2^34 (12 offset + 9 folha + 13 raiz, docs/03), e
     * registrar uma pagina acima disso produz PTE que a GPU nao alcanca. O
     * sintoma e' indistinguivel do B1: RPTR chega no WPTR e nada acontece.
     *
     * Nao era alcancavel pelo alocador vigente -- Bc250GpuvaSpaceInitialize e'
     * chamado com BC250_GPUVA_PT_VA_BITS -- mas era fail-OPEN, e o custo de
     * descobrir isso numa corrida seria uma corrida.
     */
    {
        /*
         * ATENCAO A ORDEM: StageLeafUpdate recebe
         *   (Journal, Process, ExpectedRootPageTable, FirstGpuVa, Entries, N)
         * -- a root vem ANTES do VA. Escrevi este bloco com os dois trocados na
         * primeira tentativa e as tres recusas "passaram" testando 0x40000000.
         */
        static BC250_GPUVA_JOURNAL limitJournal;
        BC250_MCDM_HANDLE procLimit = (BC250_MCDM_HANDLE)0x7000;
        const uint64_t lastPage =
            BC250_GPUVA_TRANSLATABLE_LIMIT - BC250_GPUVA_PAGE_BYTES;

        CHECK(BC250_GPUVA_TRANSLATABLE_LIMIT == (1ull << 34));
        CHECK(BC250_GPUVA_TRANSLATABLE_LIMIT < BC250_GPUVA_MAX_LIMIT);
        CHECK(Bc250GpuvaJournalInitialize(&limitJournal) ==
            BC250_GPUVA_JOURNAL_OK);

        /* A ultima pagina traduzivel entra. */
        CHECK(Bc250GpuvaJournalStageLeafUpdate(&limitJournal, procLimit,
            0x40000000u, lastPage, entries, 1u) == BC250_GPUVA_JOURNAL_OK);
        Bc250GpuvaJournalAbort(&limitJournal);

        /* A primeira pagina fora dele nao. */
        CHECK(Bc250GpuvaJournalStageLeafUpdate(&limitJournal, procLimit,
            0x40000000u, BC250_GPUVA_TRANSLATABLE_LIMIT, entries, 1u) ==
            BC250_GPUVA_JOURNAL_BAD_ARGUMENT);

        /* E uma faixa que COMECA dentro e ATRAVESSA o limite tambem nao. */
        CHECK(Bc250GpuvaJournalStageLeafUpdate(&limitJournal, procLimit,
            0x40000000u, lastPage, entries, 2u) ==
            BC250_GPUVA_JOURNAL_BAD_ARGUMENT);

        /* O antigo teto arquitetural continua recusado, obviamente. */
        CHECK(Bc250GpuvaJournalStageLeafUpdate(&limitJournal, procLimit,
            0x40000000u, 0x0000080000000000ull, entries, 1u) ==
            BC250_GPUVA_JOURNAL_BAD_ARGUMENT);
    }


    /*
     * C3.9 (12/09) -- VISTA POR PROCESSO DO JOURNAL.
     *
     * Medido na corrida 13:29: com o dwm.exe vivo 3 min 13 s, a pergunta global
     * "algum slot esta pendente?" nunca deu zero, o gate do Node 1 fechou em
     * BlockMask=0x1 e a submissao de compute do RADV foi RECUSADA -- o shader
     * nao rodou e o byte proof leu poison. Compute e desktop nao coexistiam por
     * GRANULARIDADE do gate.
     *
     * Este bloco trava a distincao: a pergunta GLOBAL continua respondendo 1
     * quando OUTRO processo esta pendente, e a pergunta POR PROCESSO responde 0.
     * Se as duas passarem a concordar, a correcao virou no-op e este teste
     * reprova.
     */
    {
        /*
         * static: cada BC250_GPUVA_JOURNAL carrega Changes[MAX_CHANGES], e tres
         * deles na pilha estouram o frame -- a suite linka sem CRT e o
         * resultado e' LNK2019 em __chkstk, nao um erro de logica.
         */
        static BC250_GPUVA_JOURNAL set[3];
        BC250_MCDM_HANDLE procA = (BC250_MCDM_HANDLE)0x1111;
        BC250_MCDM_HANDLE procB = (BC250_MCDM_HANDLE)0x2222;
        BC250_GPUVA_JOURNAL_PTE pte;
        uint32_t mask = 0xFFFFFFFFu;
        uint32_t count = 0xFFFFFFFFu;
        uint32_t i;

        for (i = 0u; i < 3u; ++i) {
            CHECK(Bc250GpuvaJournalInitialize(&set[i]) == BC250_GPUVA_JOURNAL_OK);
        }

        /* Nada pendente: as duas perguntas respondem 0. */
        CHECK(Bc250GpuvaJournalSetAnyPending(set, 3u) == 0u);
        CHECK(Bc250GpuvaJournalSetAnyPendingForProcess(set, 3u, procA) == 0u);
        CHECK(Bc250GpuvaJournalSetDescribePending(set, 3u, &mask, &count) == 0u);
        CHECK(mask == 0u && count == 0u);

        /* procB (o "dwm.exe" deste teste) fica pendente no slot 1. */
        memset(&pte, 0, sizeof(pte));
        pte.Valid = 1u;
        pte.Permissions = BC250_PTE_READABLE;
        pte.SegmentId = 1u;
        pte.SegmentOffset = 0x1000ull;
        CHECK(Bc250GpuvaJournalStageLeafUpdate(&set[1], procB, 0x40000000u,
            0x2000ull, &pte, 1u) == BC250_GPUVA_JOURNAL_OK);
        CHECK(set[1].Pending != 0u);

        /* ESTE E' O PONTO: global diz 1, por-processo de procA diz 0. */
        CHECK(Bc250GpuvaJournalSetAnyPending(set, 3u) == 1u);
        CHECK(Bc250GpuvaJournalSetAnyPendingForProcess(set, 3u, procA) == 0u);
        CHECK(Bc250GpuvaJournalSetAnyPendingForProcess(set, 3u, procB) == 1u);

        /* A telemetria nomeia o slot, que e' o que faltava no log. */
        CHECK(Bc250GpuvaJournalSetDescribePending(set, 3u, &mask, &count) == 1u);
        CHECK(mask == (1u << 1));
        CHECK(count == 1u);

        /* procA pendente tambem: agora as duas respondem 1 para procA. */
        CHECK(Bc250GpuvaJournalStageLeafUpdate(&set[0], procA, 0x40000000u,
            0x3000ull, &pte, 1u) == BC250_GPUVA_JOURNAL_OK);
        CHECK(Bc250GpuvaJournalSetAnyPendingForProcess(set, 3u, procA) == 1u);
        CHECK(Bc250GpuvaJournalSetDescribePending(set, 3u, &mask, &count) == 1u);
        CHECK(mask == ((1u << 0) | (1u << 1)));
        CHECK(count == 2u);

        /* Handle nulo nao pode virar curinga. */
        CHECK(Bc250GpuvaJournalSetAnyPendingForProcess(set, 3u, NULL) == 0u);
        CHECK(Bc250GpuvaJournalSetAnyPendingForProcess(NULL, 3u, procA) == 0u);

        Bc250GpuvaJournalAbort(&set[0]);
        Bc250GpuvaJournalAbort(&set[1]);
        CHECK(Bc250GpuvaJournalSetAnyPending(set, 3u) == 0u);
    }

    /*
     * C3.15 (12/09) -- REPLAY DA CORRIDA 18:21.
     *
     * O dxgkrnl entregou Num=461 com backing fisico esparso. O miniport
     * estagia em chunks de 256, mas ambos pertencem ao mesmo journal ate o
     * FlushTlb. Com MAX_CHANGES=256, o segundo chunk devolvia NO_SPACE e o
     * prefixo do primeiro chunk permanecia publicavel. Aqui as 461 paginas
     * sao deliberadamente disjuntas e precisam sobreviver como 461 runs.
     */
    {
        enum { C315_SPARSE_RUNS = 461, C315_FIRST_CHUNK = 256 };
        static BC250_GPUVA_JOURNAL c315Journal;
        static BC250_GPUVA_SNAPSHOT_STORE c315Store;
        static BC250_GPUVA_SPACE c315Scratch;
        static BC250_GPUVA_JOURNAL_PTE c315Entries[C315_SPARSE_RUNS];
        BC250_GPUVA_SNAPSHOT_READER c315View;
        BC250_GPUVA_SPACE_OCCUPANCY c315Occupancy;
        BC250_MCDM_HANDLE c315Process = (BC250_MCDM_HANDLE)(size_t)0xC315;
        uint32_t c315Generation = 0u;
        uint32_t i;

        CHECK(BC250_GPUVA_JOURNAL_MAX_CHANGES >= 1024u);
        CHECK(Bc250GpuvaJournalInitialize(&c315Journal) == BC250_GPUVA_JOURNAL_OK);
        CHECK(Bc250GpuvaSnapshotStoreInitialize(&c315Store, 34u) == BC250_GPUVA_OK);
        memset(&c315Scratch, 0, sizeof(c315Scratch));
        for (i = 0u; i < C315_SPARSE_RUNS; ++i) {
            c315Entries[i].Valid = 1u;
            c315Entries[i].Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE;
            c315Entries[i].SegmentId = 1u;
            c315Entries[i].SegmentOffset = 0x2000000ull +
                ((uint64_t)i * 2ull * BC250_GPUVA_PAGE_BYTES);
            c315Entries[i].Allocation =
                (BC250_MCDM_HANDLE)(size_t)0xC3150001u;
        }
        CHECK(Bc250GpuvaJournalStageLeafUpdate(&c315Journal, c315Process,
            0xC3150000ull, 0x70000000ull, c315Entries,
            C315_FIRST_CHUNK) == BC250_GPUVA_JOURNAL_OK);
        CHECK(Bc250GpuvaJournalStageLeafUpdate(&c315Journal, c315Process,
            0xC3150000ull,
            0x70000000ull +
                ((uint64_t)C315_FIRST_CHUNK * BC250_GPUVA_PAGE_BYTES),
            &c315Entries[C315_FIRST_CHUNK],
            C315_SPARSE_RUNS - C315_FIRST_CHUNK) == BC250_GPUVA_JOURNAL_OK);
        CHECK(c315Journal.ChangeCount == C315_SPARSE_RUNS);
        CHECK(Bc250GpuvaJournalCommitFlush(&c315Journal, &c315Store,
            &c315Scratch, c315Process, 0xC3150000ull, 0ull, 0ull,
            &c315Generation) == BC250_GPUVA_JOURNAL_OK);
        CHECK(Bc250GpuvaSnapshotAcquire(&c315Store, &c315View) == BC250_GPUVA_OK);
        CHECK(Bc250GpuvaSpaceDescribeOccupancy(c315View.Space,
            &c315Occupancy) == BC250_GPUVA_OK);
        CHECK(c315Occupancy.Mapped == C315_SPARSE_RUNS);
        Bc250GpuvaSnapshotRelease(&c315Store, &c315View);
    }

    /*
     * C3.16 (12/09) -- REPLAY DO PADRAO DA CORRIDA 18:45.
     *
     * Entre flushes apareceram ate 17.471 entradas historicas, embora o
     * snapshot final tivesse no maximo 876 ranges. Reescrever a mesma VA nao
     * pode consumir outro slot: o journal representa o ultimo estado.
     */
    {
        static BC250_GPUVA_JOURNAL overlayJournal;
        BC250_GPUVA_JOURNAL_PTE overlayPte;
        BC250_MCDM_HANDLE overlayProcess =
            (BC250_MCDM_HANDLE)(size_t)0xC316;
        uint32_t i;

        CHECK(Bc250GpuvaJournalInitialize(&overlayJournal) ==
            BC250_GPUVA_JOURNAL_OK);
        memset(&overlayPte, 0, sizeof(overlayPte));
        overlayPte.Valid = 1u;
        overlayPte.Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE;
        overlayPte.SegmentId = 1u;
        overlayPte.Allocation = (BC250_MCDM_HANDLE)(size_t)0xC3160001u;
        for (i = 0u; i < 17471u; ++i) {
            overlayPte.SegmentOffset = 0x3000000ull +
                ((uint64_t)i * BC250_GPUVA_PAGE_BYTES);
            CHECK(Bc250GpuvaJournalStageLeafUpdate(&overlayJournal,
                overlayProcess, 0xC3160000ull, 0x71000000ull,
                &overlayPte, 1u) == BC250_GPUVA_JOURNAL_OK);
            CHECK(overlayJournal.ChangeCount == 1u);
        }
        CHECK(overlayJournal.Changes[0].BackingSegmentOffset ==
            overlayPte.SegmentOffset);

        /* Substituir o meio de um range preserva as duas pontas. */
        Bc250GpuvaJournalAbort(&overlayJournal);
        overlayPte.SegmentOffset = 0x4000000ull;
        {
            BC250_GPUVA_JOURNAL_PTE four[4];
            BC250_GPUVA_JOURNAL_PTE middle[2];
            for (i = 0u; i < 4u; ++i) {
                four[i] = overlayPte;
                four[i].SegmentOffset +=
                    (uint64_t)i * BC250_GPUVA_PAGE_BYTES;
            }
            CHECK(Bc250GpuvaJournalStageLeafUpdate(&overlayJournal,
                overlayProcess, 0xC3160000ull, 0x72000000ull,
                four, 4u) == BC250_GPUVA_JOURNAL_OK);
            middle[0] = overlayPte;
            middle[0].Allocation =
                (BC250_MCDM_HANDLE)(size_t)0xC3160002u;
            middle[0].SegmentOffset = 0x5000000ull;
            middle[1] = middle[0];
            middle[1].SegmentOffset += BC250_GPUVA_PAGE_BYTES;
            CHECK(Bc250GpuvaJournalStageLeafUpdate(&overlayJournal,
                overlayProcess, 0xC3160000ull, 0x72001000ull,
                middle, 2u) == BC250_GPUVA_JOURNAL_OK);
            CHECK(overlayJournal.ChangeCount == 3u);
            CHECK(overlayJournal.Changes[0].BaseGpuVa == 0x72000000ull);
            CHECK(overlayJournal.Changes[0].Size == BC250_GPUVA_PAGE_BYTES);
            CHECK(overlayJournal.Changes[1].BaseGpuVa == 0x72001000ull);
            CHECK(overlayJournal.Changes[1].Size ==
                2u * BC250_GPUVA_PAGE_BYTES);
            CHECK(overlayJournal.Changes[2].BaseGpuVa == 0x72003000ull);
            CHECK(overlayJournal.Changes[2].Size == BC250_GPUVA_PAGE_BYTES);
            CHECK(overlayJournal.Changes[2].BackingSegmentOffset ==
                0x4003000ull);
        }
    }

    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void) { ExitProcess((unsigned long)Bc250RunGpuvaJournalContractTest()); }
#else
int main(void) { return Bc250RunGpuvaJournalContractTest(); }
#endif
