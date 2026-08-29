#include "bc250_gpuva_sdma_executor_contract.h"

#include <string.h>

BC250_GPUVA_SDMA_EXECUTOR_STATUS Bc250GpuvaSdmaExecuteTransaction(
    const BC250_GPUVA_SDMA_BROKER *Broker,
    const BC250_GPUVA_SDMA_EXECUTOR *Executor,
    BC250_GPUVA_SDMA_EXECUTOR_EVIDENCE *Evidence)
{
    BC250_GPUVA_SDMA_EXECUTOR_EVIDENCE request;
    BC250_GPUVA_SDMA_BROKER_STATUS brokerStatus;
    BC250_GPUVA_SDMA_EXECUTOR_STATUS status;

    if (Broker == NULL || Executor == NULL || Evidence == NULL ||
        Executor->Snapshot == NULL || Executor->Submit == NULL ||
        Executor->Readback == NULL || Executor->Rollback == NULL ||
        Evidence->FenceExpected == 0u) {
        return BC250_GPUVA_SDMA_EXECUTOR_BAD_ARGUMENT;
    }
    request = *Evidence;
    memset(Evidence, 0, sizeof(*Evidence));
    Evidence->SourceLeaseId = request.SourceLeaseId;
    Evidence->DestinationLeaseId = request.DestinationLeaseId;
    Evidence->FenceGpuVa = request.FenceGpuVa;
    Evidence->ByteCount = request.ByteCount;
    Evidence->FenceExpected = request.FenceExpected;

    brokerStatus = Bc250GpuvaSdmaBrokerValidatePair(Broker,
        Evidence->SourceLeaseId, Evidence->DestinationLeaseId,
        Evidence->FenceGpuVa, Evidence->ByteCount);
    if (brokerStatus != BC250_GPUVA_SDMA_BROKER_OK) {
        return BC250_GPUVA_SDMA_EXECUTOR_LEASE_REJECTED;
    }
    status = Executor->Snapshot(Executor->Context, Evidence);
    if (status != BC250_GPUVA_SDMA_EXECUTOR_OK) {
        return BC250_GPUVA_SDMA_EXECUTOR_SNAPSHOT_FAILED;
    }
    Evidence->SnapshotTaken = 1u;
    Evidence->SubmitAttempted = 1u;
    status = Executor->Submit(Executor->Context, Evidence);
    if (status == BC250_GPUVA_SDMA_EXECUTOR_OK) {
        status = Executor->Readback(Executor->Context, Evidence);
        if (status == BC250_GPUVA_SDMA_EXECUTOR_OK) {
            Evidence->ReadbackTaken = 1u;
            if (Evidence->RptrAfter == Evidence->WptrAfter &&
                Evidence->FenceObserved == Evidence->FenceExpected &&
                Evidence->DataMismatches == 0u) {
                Evidence->EvidenceTripletPassed = 1u;
            } else {
                status = BC250_GPUVA_SDMA_EXECUTOR_PHYSICAL_PROOF_FAILED;
            }
        } else {
            status = BC250_GPUVA_SDMA_EXECUTOR_READBACK_FAILED;
        }
    } else {
        status = BC250_GPUVA_SDMA_EXECUTOR_SUBMIT_FAILED;
    }

    /* Teardown is mandatory after a snapshot, including a successful submit. */
    if (Executor->Rollback(Executor->Context, Evidence) !=
        BC250_GPUVA_SDMA_EXECUTOR_OK) {
        Evidence->EvidenceTripletPassed = 0u;
        return BC250_GPUVA_SDMA_EXECUTOR_ROLLBACK_FAILED;
    }
    Evidence->RollbackPerformed = 1u;
    return status;
}
