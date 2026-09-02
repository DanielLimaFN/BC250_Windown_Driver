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
