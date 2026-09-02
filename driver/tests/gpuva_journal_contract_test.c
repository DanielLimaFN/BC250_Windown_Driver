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
    CHECK(journal.Pending == 1u && journal.ChangeCount == 2u);
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
    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void) { ExitProcess((unsigned long)Bc250RunGpuvaJournalContractTest()); }
#else
int main(void) { return Bc250RunGpuvaJournalContractTest(); }
#endif
