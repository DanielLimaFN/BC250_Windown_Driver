#ifndef BC250_GPUVA_SDMA_EXECUTOR_CONTRACT_H
#define BC250_GPUVA_SDMA_EXECUTOR_CONTRACT_H

#include "bc250_gpuva_sdma_broker_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum BC250_GPUVA_SDMA_EXECUTOR_STATUS {
    BC250_GPUVA_SDMA_EXECUTOR_OK = 0,
    BC250_GPUVA_SDMA_EXECUTOR_BAD_ARGUMENT,
    BC250_GPUVA_SDMA_EXECUTOR_LEASE_REJECTED,
    BC250_GPUVA_SDMA_EXECUTOR_SNAPSHOT_FAILED,
    BC250_GPUVA_SDMA_EXECUTOR_SUBMIT_FAILED,
    BC250_GPUVA_SDMA_EXECUTOR_READBACK_FAILED,
    BC250_GPUVA_SDMA_EXECUTOR_PHYSICAL_PROOF_FAILED,
    BC250_GPUVA_SDMA_EXECUTOR_ROLLBACK_FAILED
} BC250_GPUVA_SDMA_EXECUTOR_STATUS;

typedef struct BC250_GPUVA_SDMA_EXECUTOR_EVIDENCE {
    uint64_t SourceLeaseId;
    uint64_t DestinationLeaseId;
    uint64_t FenceGpuVa;
    uint64_t ByteCount;
    uint32_t FenceExpected;
    uint32_t FenceObserved;
    uint32_t RptrBefore;
    uint32_t WptrBefore;
    uint32_t RptrAfter;
    uint32_t WptrAfter;
    uint32_t DataMismatches;
    uint32_t SnapshotTaken;
    uint32_t SubmitAttempted;
    uint32_t ReadbackTaken;
    uint32_t RollbackPerformed;
    /* Contract result only. It is not hardware validation without a live backend. */
    uint32_t EvidenceTripletPassed;
} BC250_GPUVA_SDMA_EXECUTOR_EVIDENCE;

typedef BC250_GPUVA_SDMA_EXECUTOR_STATUS (*BC250_GPUVA_SDMA_SNAPSHOT_FN)(
    void *Context, BC250_GPUVA_SDMA_EXECUTOR_EVIDENCE *Evidence);
typedef BC250_GPUVA_SDMA_EXECUTOR_STATUS (*BC250_GPUVA_SDMA_SUBMIT_FN)(
    void *Context, const BC250_GPUVA_SDMA_EXECUTOR_EVIDENCE *Request);
typedef BC250_GPUVA_SDMA_EXECUTOR_STATUS (*BC250_GPUVA_SDMA_READBACK_FN)(
    void *Context, BC250_GPUVA_SDMA_EXECUTOR_EVIDENCE *Evidence);
typedef BC250_GPUVA_SDMA_EXECUTOR_STATUS (*BC250_GPUVA_SDMA_ROLLBACK_FN)(
    void *Context, BC250_GPUVA_SDMA_EXECUTOR_EVIDENCE *Evidence);

typedef struct BC250_GPUVA_SDMA_EXECUTOR {
    void *Context;
    BC250_GPUVA_SDMA_SNAPSHOT_FN Snapshot;
    BC250_GPUVA_SDMA_SUBMIT_FN Submit;
    BC250_GPUVA_SDMA_READBACK_FN Readback;
    BC250_GPUVA_SDMA_ROLLBACK_FN Rollback;
} BC250_GPUVA_SDMA_EXECUTOR;

BC250_GPUVA_SDMA_EXECUTOR_STATUS Bc250GpuvaSdmaExecuteTransaction(
    const BC250_GPUVA_SDMA_BROKER *Broker,
    const BC250_GPUVA_SDMA_EXECUTOR *Executor,
    BC250_GPUVA_SDMA_EXECUTOR_EVIDENCE *Evidence);

#ifdef __cplusplus
}
#endif

#endif
