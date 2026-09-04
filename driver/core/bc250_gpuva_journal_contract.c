#include "bc250_gpuva_journal_contract.h"
#include <string.h>

/*
 * Duas paginas consecutivas pertencem ao mesmo range quando descrevem o mesmo
 * mapeamento: mesma validade, mesmas permissoes, mesma allocation, e -- se
 * mapeadas -- mesmo segmento com offset consecutivo. Para paginas invalidas o
 * offset e' irrelevante: um unmap e' um unmap.
 */
static uint32_t Bc250GpuvaJournalEntriesAreContiguous(
    const BC250_GPUVA_JOURNAL_PTE *Previous,
    const BC250_GPUVA_JOURNAL_PTE *Current)
{
    if ((Previous->Valid != 0u) != (Current->Valid != 0u)) return 0u;
    if (Previous->Permissions != Current->Permissions) return 0u;
    if (Previous->Allocation != Current->Allocation) return 0u;
    if (Previous->OpenAllocation != Current->OpenAllocation) return 0u;
    if (Previous->Valid == 0u) return 1u;
    if (Previous->SegmentId != Current->SegmentId) return 0u;
    if (Previous->SegmentOffset + BC250_GPUVA_PAGE_BYTES !=
        Current->SegmentOffset) {
        return 0u;
    }
    return 1u;
}

/* Mesma pergunta, entre um range ja' estagiado e a proxima PTE. */
static uint32_t Bc250GpuvaJournalEntryExtendsChange(
    const BC250_GPUVA_RANGE_RECORD *Change,
    uint32_t ChangeIsMap,
    uint64_t EntryGpuVa,
    const BC250_GPUVA_JOURNAL_PTE *Entry)
{
    uint32_t entryIsMap = Entry->Valid != 0u ? 1u : 0u;
    if (Change->BaseGpuVa + Change->Size != EntryGpuVa) return 0u;
    if ((ChangeIsMap != 0u) != (entryIsMap != 0u)) return 0u;
    if (Change->Permissions != Entry->Permissions) return 0u;
    if (Change->Allocation != Entry->Allocation) return 0u;
    if (Change->OpenAllocation != Entry->OpenAllocation) return 0u;
    if (entryIsMap == 0u) return 1u;
    if (Change->BackingSegmentId != Entry->SegmentId) return 0u;
    if (Change->BackingSegmentOffset + Change->Size != Entry->SegmentOffset) {
        return 0u;
    }
    return 1u;
}

BC250_GPUVA_JOURNAL_STATUS Bc250GpuvaJournalInitialize(
    BC250_GPUVA_JOURNAL *Journal)
{
    if (Journal == NULL) return BC250_GPUVA_JOURNAL_BAD_ARGUMENT;
    memset(Journal, 0, sizeof(*Journal));
    Journal->Signature = BC250_GPUVA_JOURNAL_SIGNATURE;
    return BC250_GPUVA_JOURNAL_OK;
}

void Bc250GpuvaJournalAbort(BC250_GPUVA_JOURNAL *Journal)
{
    if (Journal == NULL || Journal->Signature != BC250_GPUVA_JOURNAL_SIGNATURE) return;
    Journal->Pending = 0u;
    Journal->Process = NULL;
    Journal->ExpectedRootPageTable = 0u;
    Journal->MinimumGpuVa = 0u;
    Journal->MaximumGpuVa = 0u;
    Journal->ChangeCount = 0u;
}

uint32_t Bc250GpuvaJournalSetAnyPending(
    const BC250_GPUVA_JOURNAL *Journals,
    uint32_t JournalCount)
{
    uint32_t i;
    if (Journals == NULL) return 0u;
    for (i = 0u; i < JournalCount; ++i) {
        if (Journals[i].Signature == BC250_GPUVA_JOURNAL_SIGNATURE &&
            Journals[i].Pending != 0u) {
            return 1u;
        }
    }
    return 0u;
}

BC250_GPUVA_JOURNAL *Bc250GpuvaJournalSetSelect(
    BC250_GPUVA_JOURNAL *Journals,
    uint32_t JournalCount,
    BC250_MCDM_HANDLE Process,
    uint64_t ExpectedRootPageTable,
    uint32_t AllowClaim,
    uint32_t *EvictionKind)
{
    uint32_t i;
    BC250_GPUVA_JOURNAL *freeSlot = NULL;
    BC250_GPUVA_JOURNAL *rootless = NULL;
    BC250_GPUVA_JOURNAL *smallest = NULL;

    if (EvictionKind != NULL) *EvictionKind = BC250_GPUVA_JOURNAL_EVICT_NONE;
    if (Journals == NULL || JournalCount == 0u || Process == NULL) return NULL;

    for (i = 0u; i < JournalCount; ++i) {
        BC250_GPUVA_JOURNAL *journal = &Journals[i];
        if (journal->Signature != BC250_GPUVA_JOURNAL_SIGNATURE) continue;
        if (journal->Pending == 0u) {
            if (freeSlot == NULL) freeSlot = journal;
            continue;
        }
        if (journal->Process != Process) {
            /*
             * Candidatos a despejo, so usados quando nao ha slot livre.
             * Um journal com ExpectedRootPageTable == 0 nao tem root page table
             * e portanto NUNCA podera comitar -- e o descarte menos danoso.
             */
            if (journal->ExpectedRootPageTable == 0u && rootless == NULL) {
                rootless = journal;
            }
            if (smallest == NULL ||
                journal->ChangeCount < smallest->ChangeCount) {
                smallest = journal;
            }
            continue;
        }
        /*
         * Mesmo processo. Se ambos os roots sao conhecidos e divergem, a
         * transacao pendente pertence a uma hierarquia que nao existe mais:
         * so nesse caso ela e' descartada, e ainda assim de forma explicita.
         */
        if (journal->ExpectedRootPageTable != 0u &&
            ExpectedRootPageTable != 0u &&
            journal->ExpectedRootPageTable != ExpectedRootPageTable) {
            if (AllowClaim == 0u) return NULL;
            Bc250GpuvaJournalAbort(journal);
            if (EvictionKind != NULL) {
                *EvictionKind = BC250_GPUVA_JOURNAL_EVICT_ROOTLESS;
            }
            return journal;
        }
        return journal;
    }

    if (AllowClaim == 0u) return NULL;
    if (freeSlot != NULL) return freeSlot;

    if (rootless != NULL) {
        Bc250GpuvaJournalAbort(rootless);
        if (EvictionKind != NULL) {
            *EvictionKind = BC250_GPUVA_JOURNAL_EVICT_ROOTLESS;
        }
        return rootless;
    }
    if (smallest != NULL) {
        Bc250GpuvaJournalAbort(smallest);
        if (EvictionKind != NULL) {
            *EvictionKind = BC250_GPUVA_JOURNAL_EVICT_SMALLEST;
        }
        return smallest;
    }
    return NULL;
}

BC250_GPUVA_JOURNAL_STATUS Bc250GpuvaJournalStageLeafUpdate(
    BC250_GPUVA_JOURNAL *Journal,
    BC250_MCDM_HANDLE Process,
    uint64_t ExpectedRootPageTable,
    uint64_t FirstGpuVa,
    const BC250_GPUVA_JOURNAL_PTE *Entries,
    uint32_t EntryCount)
{
    uint32_t index;
    uint64_t updateEnd;
    if (Journal == NULL || Journal->Signature != BC250_GPUVA_JOURNAL_SIGNATURE ||
        Process == NULL || Entries == NULL ||
        EntryCount == 0u || (FirstGpuVa & BC250_GPUVA_PAGE_MASK) != 0u) {
        return BC250_GPUVA_JOURNAL_BAD_ARGUMENT;
    }
    if ((uint64_t)EntryCount > ((uint64_t)~0ull - FirstGpuVa) / BC250_GPUVA_PAGE_BYTES) {
        return BC250_GPUVA_JOURNAL_BAD_ARGUMENT;
    }
    updateEnd = FirstGpuVa + ((uint64_t)EntryCount * BC250_GPUVA_PAGE_BYTES);
    if (updateEnd > BC250_GPUVA_MAX_LIMIT) return BC250_GPUVA_JOURNAL_BAD_ARGUMENT;
    if (Journal->Pending != 0u && Journal->Process != Process) {
        Bc250GpuvaJournalAbort(Journal);
    }
    if (Journal->Pending != 0u && Journal->ExpectedRootPageTable != 0u &&
        ExpectedRootPageTable != 0u &&
        Journal->ExpectedRootPageTable != ExpectedRootPageTable) {
        Bc250GpuvaJournalAbort(Journal);
    }
    if (Journal->Pending != 0u && Journal->ExpectedRootPageTable == 0u &&
        ExpectedRootPageTable != 0u) {
        Journal->ExpectedRootPageTable = ExpectedRootPageTable;
    }
    for (index = 0u; index < EntryCount; ++index) {
        if (Entries[index].Valid != 0u && Entries[index].Permissions == 0u) {
            return BC250_GPUVA_JOURNAL_BAD_ARGUMENT;
        }
    }
    /*
     * Contagem de RUNS, nao de paginas.
     *
     * Antes, cada PTE virava um BC250_GPUVA_RANGE_RECORD proprio, e a
     * capacidade era conferida em paginas. Isso torna o journal inutil sob
     * WDDM real: a primary allocation de 2560x1080x4 sao 2700 paginas contra
     * um teto de 256, e o CommitFlush publica esses mesmos registros em
     * BC250_GPUVA_SPACE, que tambem tem 256 ranges. Um primary sozinho
     * estourava os dois.
     *
     * Paginas contiguas em VA, com as mesmas permissoes, o mesmo segmento, a
     * mesma allocation e offset de segmento consecutivo descrevem UM range.
     * Uma allocation fisicamente contigua colapsa para 1 registro, que e'
     * exatamente o que o BC250_GPUVA_RANGE_RECORD (BaseGpuVa + Size) sempre
     * modelou.
     */
    {
        uint32_t runsNeeded = 0u;
        uint32_t mergesWithTail =
            (Journal->Pending != 0u && Journal->ChangeCount != 0u &&
             Bc250GpuvaJournalEntryExtendsChange(
                 &Journal->Changes[Journal->ChangeCount - 1u],
                 Journal->MapChanges[Journal->ChangeCount - 1u],
                 FirstGpuVa, &Entries[0])) ? 1u : 0u;
        for (index = 0u; index < EntryCount; ++index) {
            if (index == 0u) {
                if (mergesWithTail == 0u) ++runsNeeded;
            } else if (!Bc250GpuvaJournalEntriesAreContiguous(
                           &Entries[index - 1u], &Entries[index])) {
                ++runsNeeded;
            }
        }
        if (runsNeeded > BC250_GPUVA_JOURNAL_MAX_CHANGES - Journal->ChangeCount) {
            return BC250_GPUVA_JOURNAL_NO_SPACE;
        }
    }
    if (Journal->Pending == 0u) {
        Journal->Process = Process;
        Journal->ExpectedRootPageTable = ExpectedRootPageTable;
        Journal->MinimumGpuVa = FirstGpuVa;
        Journal->MaximumGpuVa = updateEnd;
        Journal->Pending = 1u;
    } else {
        if (FirstGpuVa < Journal->MinimumGpuVa) Journal->MinimumGpuVa = FirstGpuVa;
        if (updateEnd > Journal->MaximumGpuVa) Journal->MaximumGpuVa = updateEnd;
    }
    for (index = 0u; index < EntryCount; ++index) {
        const BC250_GPUVA_JOURNAL_PTE *entry = &Entries[index];
        uint64_t entryVa =
            FirstGpuVa + ((uint64_t)index * BC250_GPUVA_PAGE_BYTES);
        BC250_GPUVA_RANGE_RECORD *change;

        if (Journal->ChangeCount != 0u &&
            Bc250GpuvaJournalEntryExtendsChange(
                &Journal->Changes[Journal->ChangeCount - 1u],
                Journal->MapChanges[Journal->ChangeCount - 1u],
                entryVa, entry)) {
            Journal->Changes[Journal->ChangeCount - 1u].Size +=
                BC250_GPUVA_PAGE_BYTES;
            continue;
        }

        change = &Journal->Changes[Journal->ChangeCount];
        memset(change, 0, sizeof(*change));
        change->BaseGpuVa = entryVa;
        change->Size = BC250_GPUVA_PAGE_BYTES;
        change->OwnerProcess = Process;
        change->Allocation = entry->Allocation;
        change->OpenAllocation = entry->OpenAllocation;
        change->BackingSegmentId = entry->SegmentId;
        change->BackingSegmentOffset = entry->SegmentOffset;
        change->Permissions = entry->Permissions;
        Journal->MapChanges[Journal->ChangeCount] = entry->Valid != 0u ? 1u : 0u;
        ++Journal->ChangeCount;
    }
    return BC250_GPUVA_JOURNAL_OK;
}

BC250_GPUVA_JOURNAL_STATUS Bc250GpuvaJournalCommitFlush(
    BC250_GPUVA_JOURNAL *Journal,
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    BC250_GPUVA_SPACE *Scratch,
    BC250_MCDM_HANDLE Process,
    uint64_t RootPageTable,
    uint64_t FlushStartGpuVa,
    uint64_t FlushEndGpuVa,
    uint32_t *PublishedGeneration)
{
    BC250_GPUVA_STATUS status;
    if (Journal == NULL || Journal->Signature != BC250_GPUVA_JOURNAL_SIGNATURE ||
        Store == NULL || Scratch == NULL || Process == NULL || RootPageTable == 0u) {
        return BC250_GPUVA_JOURNAL_BAD_ARGUMENT;
    }
    if (Journal->Pending == 0u || Journal->ChangeCount == 0u) {
        return BC250_GPUVA_JOURNAL_BAD_STATE;
    }
    if (Journal->Process != Process ||
        (Journal->ExpectedRootPageTable != 0u &&
         Journal->ExpectedRootPageTable != RootPageTable)) {
        return BC250_GPUVA_JOURNAL_MISMATCH;
    }
    if (!((FlushStartGpuVa == 0u && FlushEndGpuVa == 0u) ||
          (FlushEndGpuVa >= Journal->MaximumGpuVa &&
           FlushEndGpuVa > FlushStartGpuVa))) {
        return BC250_GPUVA_JOURNAL_RANGE_NOT_FLUSHED;
    }
    status = Bc250GpuvaSnapshotPublishBatch(Store, Scratch, Journal->Changes,
        Journal->MapChanges, Journal->ChangeCount, PublishedGeneration);
    if (status != BC250_GPUVA_OK) return BC250_GPUVA_JOURNAL_PUBLISH_FAILED;
    Bc250GpuvaJournalAbort(Journal);
    return BC250_GPUVA_JOURNAL_OK;
}
