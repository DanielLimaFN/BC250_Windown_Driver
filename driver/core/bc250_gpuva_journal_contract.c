#include "bc250_gpuva_journal_contract.h"
#include <string.h>

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
        Process == NULL || ExpectedRootPageTable == 0u || Entries == NULL ||
        EntryCount == 0u || (FirstGpuVa & BC250_GPUVA_PAGE_MASK) != 0u) {
        return BC250_GPUVA_JOURNAL_BAD_ARGUMENT;
    }
    if ((uint64_t)EntryCount > ((uint64_t)~0ull - FirstGpuVa) / BC250_GPUVA_PAGE_BYTES) {
        return BC250_GPUVA_JOURNAL_BAD_ARGUMENT;
    }
    updateEnd = FirstGpuVa + ((uint64_t)EntryCount * BC250_GPUVA_PAGE_BYTES);
    if (updateEnd > BC250_GPUVA_MAX_LIMIT) return BC250_GPUVA_JOURNAL_BAD_ARGUMENT;
    if (Journal->Pending != 0u &&
        (Journal->Process != Process || Journal->ExpectedRootPageTable != ExpectedRootPageTable)) {
        return BC250_GPUVA_JOURNAL_BUSY;
    }
    if (EntryCount > BC250_GPUVA_JOURNAL_MAX_CHANGES - Journal->ChangeCount) {
        return BC250_GPUVA_JOURNAL_NO_SPACE;
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
    for (index = 0u; index < EntryCount; ++index) {
        BC250_GPUVA_RANGE_RECORD *change = &Journal->Changes[Journal->ChangeCount];
        const BC250_GPUVA_JOURNAL_PTE *entry = &Entries[index];
        memset(change, 0, sizeof(*change));
        change->BaseGpuVa = FirstGpuVa + ((uint64_t)index * BC250_GPUVA_PAGE_BYTES);
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
    if (Journal->Process != Process || Journal->ExpectedRootPageTable != RootPageTable) {
        return BC250_GPUVA_JOURNAL_MISMATCH;
    }
    if (!((FlushStartGpuVa == 0u && FlushEndGpuVa == 0u) ||
          (FlushStartGpuVa <= Journal->MinimumGpuVa &&
           FlushEndGpuVa >= Journal->MaximumGpuVa &&
           FlushEndGpuVa > FlushStartGpuVa))) {
        return BC250_GPUVA_JOURNAL_RANGE_NOT_FLUSHED;
    }
    status = Bc250GpuvaSnapshotPublishBatch(Store, Scratch, Journal->Changes,
        Journal->MapChanges, Journal->ChangeCount, PublishedGeneration);
    if (status != BC250_GPUVA_OK) return BC250_GPUVA_JOURNAL_PUBLISH_FAILED;
    Bc250GpuvaJournalAbort(Journal);
    return BC250_GPUVA_JOURNAL_OK;
}
