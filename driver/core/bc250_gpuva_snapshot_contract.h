#ifndef BC250_GPUVA_SNAPSHOT_CONTRACT_H
#define BC250_GPUVA_SNAPSHOT_CONTRACT_H

#include "bc250_gpuva_space_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_GPUVA_SNAPSHOT_SIGNATURE 0x4E535647u /* "GVSN" */
#define BC250_GPUVA_SNAPSHOT_WRITER_LOCK (-2147483647L - 1L)

typedef struct BC250_GPUVA_SNAPSHOT_READER {
    const BC250_GPUVA_SPACE *Space;
    uint32_t Slot;
    uint32_t PublicationGeneration;
    uint32_t Acquired;
} BC250_GPUVA_SNAPSHOT_READER;

typedef struct BC250_GPUVA_SNAPSHOT_STORE {
    uint32_t Signature;
    volatile long ActiveSlot;
    volatile long ReaderState[2];
    volatile long PublicationGeneration;
    volatile long SlotGeneration[2];
    BC250_GPUVA_SPACE Slots[2];
} BC250_GPUVA_SNAPSHOT_STORE;

BC250_GPUVA_STATUS Bc250GpuvaSnapshotStoreInitialize(
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    uint32_t AddressBits);

/* Pode ser chamado por leitores no caminho de submissao em DISPATCH_LEVEL. */
BC250_GPUVA_STATUS Bc250GpuvaSnapshotAcquire(
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    BC250_GPUVA_SNAPSHOT_READER *Reader);

void Bc250GpuvaSnapshotRelease(
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    BC250_GPUVA_SNAPSHOT_READER *Reader);

/*
 * Escritor PASSIVE_LEVEL. Publica somente depois de construir integralmente o
 * slot inativo. Scratch deve ser memoria persistente/nonpaged fornecida pelo
 * chamador; a funcao nunca aloca nem espera por leitores.
 */
BC250_GPUVA_STATUS Bc250GpuvaSnapshotPublishReplace(
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    BC250_GPUVA_SPACE *Scratch,
    const BC250_GPUVA_RANGE_RECORD *Param,
    uint32_t MapRange,
    uint32_t *PublishedGeneration);

BC250_GPUVA_STATUS Bc250GpuvaSnapshotPublishBatch(
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    BC250_GPUVA_SPACE *Scratch,
    const BC250_GPUVA_RANGE_RECORD *Ranges,
    const uint32_t *MapRanges,
    uint32_t RangeCount,
    uint32_t *PublishedGeneration);

#ifdef __cplusplus
}
#endif

#endif
