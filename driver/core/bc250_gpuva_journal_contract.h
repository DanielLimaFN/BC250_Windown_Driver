#ifndef BC250_GPUVA_JOURNAL_CONTRACT_H
#define BC250_GPUVA_JOURNAL_CONTRACT_H

#include "bc250_gpuva_snapshot_contract.h"

#define BC250_GPUVA_JOURNAL_SIGNATURE 0x4A415647u /* "GVAJ" */
#define BC250_GPUVA_JOURNAL_MAX_CHANGES 256u

typedef enum BC250_GPUVA_JOURNAL_STATUS {
    BC250_GPUVA_JOURNAL_OK = 0,
    BC250_GPUVA_JOURNAL_BAD_ARGUMENT,
    BC250_GPUVA_JOURNAL_BAD_STATE,
    BC250_GPUVA_JOURNAL_BUSY,
    BC250_GPUVA_JOURNAL_NO_SPACE,
    BC250_GPUVA_JOURNAL_MISMATCH,
    BC250_GPUVA_JOURNAL_RANGE_NOT_FLUSHED,
    BC250_GPUVA_JOURNAL_PUBLISH_FAILED
} BC250_GPUVA_JOURNAL_STATUS;

typedef struct BC250_GPUVA_JOURNAL_PTE {
    uint32_t Valid;
    uint32_t Permissions;
    uint32_t SegmentId;
    uint64_t SegmentOffset;
    BC250_MCDM_HANDLE Allocation;
    BC250_MCDM_HANDLE OpenAllocation;
} BC250_GPUVA_JOURNAL_PTE;

typedef struct BC250_GPUVA_JOURNAL {
    uint32_t Signature;
    uint32_t Pending;
    BC250_MCDM_HANDLE Process;
    uint64_t ExpectedRootPageTable;
    uint64_t MinimumGpuVa;
    uint64_t MaximumGpuVa;
    uint32_t ChangeCount;
    BC250_GPUVA_RANGE_RECORD Changes[BC250_GPUVA_JOURNAL_MAX_CHANGES];
    uint32_t MapChanges[BC250_GPUVA_JOURNAL_MAX_CHANGES];
} BC250_GPUVA_JOURNAL;

BC250_GPUVA_JOURNAL_STATUS Bc250GpuvaJournalInitialize(
    BC250_GPUVA_JOURNAL *Journal);

BC250_GPUVA_JOURNAL_STATUS Bc250GpuvaJournalStageLeafUpdate(
    BC250_GPUVA_JOURNAL *Journal,
    BC250_MCDM_HANDLE Process,
    uint64_t ExpectedRootPageTable,
    uint64_t FirstGpuVa,
    const BC250_GPUVA_JOURNAL_PTE *Entries,
    uint32_t EntryCount);

BC250_GPUVA_JOURNAL_STATUS Bc250GpuvaJournalCommitFlush(
    BC250_GPUVA_JOURNAL *Journal,
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    BC250_GPUVA_SPACE *Scratch,
    BC250_MCDM_HANDLE Process,
    uint64_t RootPageTable,
    uint64_t FlushStartGpuVa,
    uint64_t FlushEndGpuVa,
    uint32_t *PublishedGeneration);

void Bc250GpuvaJournalAbort(BC250_GPUVA_JOURNAL *Journal);

#endif
