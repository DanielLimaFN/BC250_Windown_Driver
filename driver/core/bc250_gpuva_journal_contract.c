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

static uint32_t Bc250GpuvaJournalChangesAreContiguous(
    const BC250_GPUVA_RANGE_RECORD *Previous,
    uint32_t PreviousIsMap,
    const BC250_GPUVA_RANGE_RECORD *Current,
    uint32_t CurrentIsMap)
{
    if (Previous->BaseGpuVa + Previous->Size != Current->BaseGpuVa) return 0u;
    if ((PreviousIsMap != 0u) != (CurrentIsMap != 0u)) return 0u;
    if (Previous->OwnerProcess != Current->OwnerProcess) return 0u;
    if (Previous->Permissions != Current->Permissions) return 0u;
    if (Previous->Allocation != Current->Allocation) return 0u;
    if (Previous->OpenAllocation != Current->OpenAllocation) return 0u;
    if (CurrentIsMap == 0u) return 1u;
    if (Previous->BackingSegmentId != Current->BackingSegmentId) return 0u;
    if (Previous->BackingSegmentOffset + Previous->Size !=
        Current->BackingSegmentOffset) return 0u;
    return 1u;
}

static void Bc250GpuvaJournalRemoveChange(
    BC250_GPUVA_JOURNAL *Journal,
    uint32_t Index)
{
    uint32_t i;
    for (i = Index + 1u; i < Journal->ChangeCount; ++i) {
        Journal->Changes[i - 1u] = Journal->Changes[i];
        Journal->MapChanges[i - 1u] = Journal->MapChanges[i];
    }
    --Journal->ChangeCount;
}

/*
 * Aplica um range novo sobre o estado pendente. Os Changes ficam como
 * intervalos finais nao sobrepostos. Isto evita que milhares de reescritas
 * das mesmas PTEs consumam um slot novo por evento historico.
 */
static BC250_GPUVA_JOURNAL_STATUS Bc250GpuvaJournalOverlayChange(
    BC250_GPUVA_JOURNAL *Journal,
    const BC250_GPUVA_RANGE_RECORD *Incoming,
    uint32_t IncomingIsMap)
{
    uint64_t incomingStart = Incoming->BaseGpuVa;
    uint64_t incomingEnd = Incoming->BaseGpuVa + Incoming->Size;
    uint32_t projected = Journal->ChangeCount + 1u;
    uint32_t i;

    /* Primeiro prova que o estado FINAL cabe; nenhuma mutacao parcial. */
    for (i = 0u; i < Journal->ChangeCount; ++i) {
        const BC250_GPUVA_RANGE_RECORD *old = &Journal->Changes[i];
        uint64_t oldStart = old->BaseGpuVa;
        uint64_t oldEnd = old->BaseGpuVa + old->Size;
        if (oldEnd <= incomingStart || oldStart >= incomingEnd) continue;
        if (oldStart >= incomingStart && oldEnd <= incomingEnd) {
            --projected; /* o novo range cobre este por inteiro */
        } else if (oldStart < incomingStart && oldEnd > incomingEnd) {
            ++projected; /* o antigo vira duas pontas */
        }
    }
    if (projected > BC250_GPUVA_JOURNAL_MAX_CHANGES) {
        return BC250_GPUVA_JOURNAL_NO_SPACE;
    }

    /* Libera primeiro os slots totalmente cobertos para os splits seguintes. */
    for (i = 0u; i < Journal->ChangeCount; ) {
        BC250_GPUVA_RANGE_RECORD *old = &Journal->Changes[i];
        uint64_t oldStart = old->BaseGpuVa;
        uint64_t oldEnd = old->BaseGpuVa + old->Size;
        if (oldStart >= incomingStart && oldEnd <= incomingEnd) {
            Bc250GpuvaJournalRemoveChange(Journal, i);
            continue;
        }
        ++i;
    }

    for (i = 0u; i < Journal->ChangeCount; ++i) {
        BC250_GPUVA_RANGE_RECORD *old = &Journal->Changes[i];
        uint64_t oldStart = old->BaseGpuVa;
        uint64_t oldEnd = old->BaseGpuVa + old->Size;
        if (oldEnd <= incomingStart || oldStart >= incomingEnd) continue;

        if (oldStart < incomingStart && oldEnd > incomingEnd) {
            BC250_GPUVA_RANGE_RECORD right = *old;
            uint32_t oldIsMap = Journal->MapChanges[i];
            uint32_t j;
            right.BaseGpuVa = incomingEnd;
            right.Size = oldEnd - incomingEnd;
            if (oldIsMap != 0u) {
                right.BackingSegmentOffset += incomingEnd - oldStart;
            }
            old->Size = incomingStart - oldStart;
            for (j = Journal->ChangeCount; j > i + 1u; --j) {
                Journal->Changes[j] = Journal->Changes[j - 1u];
                Journal->MapChanges[j] = Journal->MapChanges[j - 1u];
            }
            Journal->Changes[i + 1u] = right;
            Journal->MapChanges[i + 1u] = oldIsMap;
            ++Journal->ChangeCount;
        } else if (oldStart < incomingStart) {
            old->Size = incomingStart - oldStart;
        } else {
            uint64_t consumed = incomingEnd - oldStart;
            old->BaseGpuVa = incomingEnd;
            old->Size = oldEnd - incomingEnd;
            if (Journal->MapChanges[i] != 0u) {
                old->BackingSegmentOffset += consumed;
            }
        }
    }

    /* A lista ja e' ordenada; insere o novo range na posicao definitiva. */
    {
        uint32_t insert = 0u;
        uint32_t j;
        while (insert < Journal->ChangeCount &&
               Journal->Changes[insert].BaseGpuVa < incomingStart) {
            ++insert;
        }
        for (j = Journal->ChangeCount; j > insert; --j) {
            Journal->Changes[j] = Journal->Changes[j - 1u];
            Journal->MapChanges[j] = Journal->MapChanges[j - 1u];
        }
        Journal->Changes[insert] = *Incoming;
        Journal->MapChanges[insert] = IncomingIsMap;
        ++Journal->ChangeCount;
    }
    /* Funde vizinhos equivalentes. MAX=1024 e custo linear por run. */
    for (i = 1u; i < Journal->ChangeCount; ) {
        if (Bc250GpuvaJournalChangesAreContiguous(
                &Journal->Changes[i - 1u], Journal->MapChanges[i - 1u],
                &Journal->Changes[i], Journal->MapChanges[i])) {
            Journal->Changes[i - 1u].Size += Journal->Changes[i].Size;
            Bc250GpuvaJournalRemoveChange(Journal, i);
            continue;
        }
        ++i;
    }
    return BC250_GPUVA_JOURNAL_OK;
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

uint32_t Bc250GpuvaJournalSetAnyPendingForProcess(
    const BC250_GPUVA_JOURNAL *Journals,
    uint32_t JournalCount,
    BC250_MCDM_HANDLE Process)
{
    uint32_t i;
    if (Journals == NULL || Process == NULL) return 0u;
    for (i = 0u; i < JournalCount; ++i) {
        if (Journals[i].Signature == BC250_GPUVA_JOURNAL_SIGNATURE &&
            Journals[i].Pending != 0u &&
            Journals[i].Process == Process) {
            return 1u;
        }
    }
    return 0u;
}

uint32_t Bc250GpuvaJournalSetDescribePending(
    const BC250_GPUVA_JOURNAL *Journals,
    uint32_t JournalCount,
    uint32_t *PendingSlotMask,
    uint32_t *PendingCount)
{
    uint32_t i;
    uint32_t mask = 0u;
    uint32_t count = 0u;

    if (PendingSlotMask != NULL) *PendingSlotMask = 0u;
    if (PendingCount != NULL) *PendingCount = 0u;
    if (Journals == NULL) return 0u;

    for (i = 0u; i < JournalCount; ++i) {
        if (Journals[i].Signature == BC250_GPUVA_JOURNAL_SIGNATURE &&
            Journals[i].Pending != 0u) {
            if (i < 32u) mask |= (1u << i);
            ++count;
        }
    }

    if (PendingSlotMask != NULL) *PendingSlotMask = mask;
    if (PendingCount != NULL) *PendingCount = count;
    return (count != 0u) ? 1u : 0u;
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
    /*
     * Teto TRADUZIVEL, nao arquitetural. Ver BC250_GPUVA_TRANSLATABLE_LIMIT:
     * aqui ficava BC250_GPUVA_MAX_LIMIT (2^48) e isso era fail-open.
     */
    if (updateEnd > BC250_GPUVA_TRANSLATABLE_LIMIT) {
        return BC250_GPUVA_JOURNAL_BAD_ARGUMENT;
    }
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
    /*
     * C3.16: primeiro colapsa cada sequencia contigua em um run; depois aplica
     * esse run por SOBREPOSICAO ao estado pendente. O journal passa a guardar
     * o delta FINAL, nao o historico de todas as reescritas recebidas antes do
     * FlushTlb. A corrida 18:45 acumulou ate 17.471 entradas historicas, mas o
     * snapshot final nunca passou de 876 ranges.
     */
    for (index = 0u; index < EntryCount; ) {
        const BC250_GPUVA_JOURNAL_PTE *entry = &Entries[index];
        BC250_GPUVA_RANGE_RECORD incoming;
        BC250_GPUVA_JOURNAL_STATUS overlayStatus;
        uint32_t runPages = 1u;
        while (index + runPages < EntryCount &&
               Bc250GpuvaJournalEntriesAreContiguous(
                   &Entries[index + runPages - 1u],
                   &Entries[index + runPages])) {
            ++runPages;
        }

        memset(&incoming, 0, sizeof(incoming));
        incoming.BaseGpuVa = FirstGpuVa +
            ((uint64_t)index * BC250_GPUVA_PAGE_BYTES);
        incoming.Size = (uint64_t)runPages * BC250_GPUVA_PAGE_BYTES;
        incoming.OwnerProcess = Process;
        incoming.Allocation = entry->Allocation;
        incoming.OpenAllocation = entry->OpenAllocation;
        incoming.BackingSegmentId = entry->SegmentId;
        incoming.BackingSegmentOffset = entry->SegmentOffset;
        incoming.Permissions = entry->Permissions;
        overlayStatus = Bc250GpuvaJournalOverlayChange(Journal, &incoming,
            entry->Valid != 0u ? 1u : 0u);
        if (overlayStatus != BC250_GPUVA_JOURNAL_OK) {
            /* Nunca deixe um prefixo parcial apto a publicar. */
            Bc250GpuvaJournalAbort(Journal);
            return overlayStatus;
        }
        index += runPages;
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
