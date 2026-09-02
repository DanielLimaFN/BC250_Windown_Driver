#include "bc250_gpuva_snapshot_contract.h"
#include <intrin.h>

#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedIncrement)
#pragma intrinsic(_InterlockedDecrement)

static long Bc250GpuvaAtomicRead(volatile long *Value)
{
    return _InterlockedCompareExchange(Value, 0L, 0L);
}

static void Bc250GpuvaCopySnapshot(
    BC250_GPUVA_SPACE *Destination,
    const BC250_GPUVA_SPACE *Source)
{
    volatile uint8_t *destination = (volatile uint8_t *)Destination;
    const volatile uint8_t *source = (const volatile uint8_t *)Source;
    size_t index;
    for (index = 0u; index < sizeof(*Destination); ++index) {
        destination[index] = source[index];
    }
}

BC250_GPUVA_STATUS Bc250GpuvaSnapshotStoreInitialize(
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    uint32_t AddressBits)
{
    BC250_GPUVA_STATUS status;
    if (Store == NULL) return BC250_GPUVA_BAD_ARGUMENT;
    Store->Signature = 0u;
    Store->ActiveSlot = 0L;
    Store->ReaderState[0] = 0L;
    Store->ReaderState[1] = 0L;
    Store->PublicationGeneration = 1L;
    Store->SlotGeneration[0] = 1L;
    Store->SlotGeneration[1] = 0L;
    status = Bc250GpuvaSpaceInitialize(&Store->Slots[0], AddressBits);
    if (status != BC250_GPUVA_OK) return status;
    status = Bc250GpuvaSpaceInitialize(&Store->Slots[1], AddressBits);
    if (status != BC250_GPUVA_OK) return status;
    Store->Signature = BC250_GPUVA_SNAPSHOT_SIGNATURE;
    return BC250_GPUVA_OK;
}

BC250_GPUVA_STATUS Bc250GpuvaSnapshotAcquire(
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    BC250_GPUVA_SNAPSHOT_READER *Reader)
{
    uint32_t attempts;
    if (Store == NULL || Reader == NULL ||
        Store->Signature != BC250_GPUVA_SNAPSHOT_SIGNATURE) {
        return BC250_GPUVA_BAD_ARGUMENT;
    }
    Reader->Space = NULL;
    Reader->Acquired = 0u;

    for (attempts = 0u; attempts < 16u; ++attempts) {
        long slot = Bc250GpuvaAtomicRead(&Store->ActiveSlot);
        volatile long *state;
        long observed;
        if (slot < 0L || slot > 1L) return BC250_GPUVA_BAD_STATE;
        state = &Store->ReaderState[slot];
        observed = Bc250GpuvaAtomicRead(state);
        while (observed >= 0L) {
            if (observed == 0x7FFFFFFFL) return BC250_GPUVA_BUSY;
            if (_InterlockedCompareExchange(state, observed + 1L, observed) == observed) {
                long verifiedSlot = Bc250GpuvaAtomicRead(&Store->ActiveSlot);
                if (verifiedSlot == slot) {
                    Reader->Space = &Store->Slots[slot];
                    Reader->Slot = (uint32_t)slot;
                    Reader->PublicationGeneration =
                        (uint32_t)Bc250GpuvaAtomicRead(&Store->SlotGeneration[slot]);
                    Reader->Acquired = 1u;
                    return BC250_GPUVA_OK;
                }
                _InterlockedDecrement(state);
                break;
            }
            observed = Bc250GpuvaAtomicRead(state);
        }
    }
    return BC250_GPUVA_BUSY;
}

void Bc250GpuvaSnapshotRelease(
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    BC250_GPUVA_SNAPSHOT_READER *Reader)
{
    if (Store == NULL || Reader == NULL || Reader->Acquired == 0u ||
        Store->Signature != BC250_GPUVA_SNAPSHOT_SIGNATURE || Reader->Slot > 1u) {
        return;
    }
    _InterlockedDecrement(&Store->ReaderState[Reader->Slot]);
    Reader->Space = NULL;
    Reader->Acquired = 0u;
}

BC250_GPUVA_STATUS Bc250GpuvaSnapshotPublishReplace(
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    BC250_GPUVA_SPACE *Scratch,
    const BC250_GPUVA_RANGE_RECORD *Param,
    uint32_t MapRange,
    uint32_t *PublishedGeneration)
{
    return Bc250GpuvaSnapshotPublishBatch(Store, Scratch, Param, &MapRange,
        1u, PublishedGeneration);
}

BC250_GPUVA_STATUS Bc250GpuvaSnapshotPublishBatch(
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    BC250_GPUVA_SPACE *Scratch,
    const BC250_GPUVA_RANGE_RECORD *Ranges,
    const uint32_t *MapRanges,
    uint32_t RangeCount,
    uint32_t *PublishedGeneration)
{
    long active;
    long inactive;
    BC250_GPUVA_STATUS status;
    long generation;
    uint32_t index;

    if (PublishedGeneration != NULL) *PublishedGeneration = 0u;
    if (Store == NULL || Scratch == NULL || Ranges == NULL ||
        MapRanges == NULL || RangeCount == 0u ||
        Store->Signature != BC250_GPUVA_SNAPSHOT_SIGNATURE) {
        return BC250_GPUVA_BAD_ARGUMENT;
    }
    active = Bc250GpuvaAtomicRead(&Store->ActiveSlot);
    if (active < 0L || active > 1L) return BC250_GPUVA_BAD_STATE;
    inactive = 1L - active;

    if (_InterlockedCompareExchange(
            &Store->ReaderState[inactive],
            BC250_GPUVA_SNAPSHOT_WRITER_LOCK,
            0L) != 0L) {
        return BC250_GPUVA_BUSY;
    }

    Bc250GpuvaCopySnapshot(&Store->Slots[inactive], &Store->Slots[active]);
    for (index = 0u; index < RangeCount; ++index) {
        status = Bc250GpuvaSpaceReplaceRangeTransactional(
            &Store->Slots[inactive], Scratch, &Ranges[index], MapRanges[index]);
        if (status != BC250_GPUVA_OK) {
            _InterlockedExchange(&Store->ReaderState[inactive], 0L);
            return status;
        }
    }

    generation = _InterlockedIncrement(&Store->PublicationGeneration);
    _InterlockedExchange(&Store->SlotGeneration[inactive], generation);
    _InterlockedExchange(&Store->ReaderState[inactive], 0L);
    _InterlockedExchange(&Store->ActiveSlot, inactive);
    if (PublishedGeneration != NULL) *PublishedGeneration = (uint32_t)generation;
    return BC250_GPUVA_OK;
}
