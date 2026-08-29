#include "bc250_gpuva_sdma_broker_contract.h"

#include <string.h>

static BC250_GPUVA_SDMA_BROKER_STATUS Bc250BrokerCheck(
    const BC250_GPUVA_SDMA_BROKER *Broker)
{
    if (Broker == NULL || Broker->Signature != BC250_GPUVA_SDMA_BROKER_SIGNATURE ||
        Broker->NextLeaseId == 0ull) {
        return BC250_GPUVA_SDMA_BROKER_BAD_STATE;
    }
    return BC250_GPUVA_SDMA_BROKER_OK;
}

static const BC250_GPUVA_SDMA_LEASE_RECORD *Bc250FindLiveLease(
    const BC250_GPUVA_SDMA_BROKER *Broker,
    uint64_t LeaseId)
{
    uint32_t index;
    if (LeaseId == 0ull) return NULL;
    for (index = 0u; index < BC250_GPUVA_SDMA_BROKER_MAX_LEASES; ++index) {
        const BC250_GPUVA_SDMA_LEASE_RECORD *lease = &Broker->Leases[index];
        if (lease->Live != 0u && lease->LeaseId == LeaseId) return lease;
    }
    return NULL;
}

BC250_GPUVA_SDMA_BROKER_STATUS Bc250GpuvaSdmaBrokerInitialize(
    BC250_GPUVA_SDMA_BROKER *Broker,
    uint64_t InitialLeaseId)
{
    if (Broker == NULL || InitialLeaseId == 0ull) {
        return BC250_GPUVA_SDMA_BROKER_BAD_ARGUMENT;
    }
    memset(Broker, 0, sizeof(*Broker));
    Broker->Signature = BC250_GPUVA_SDMA_BROKER_SIGNATURE;
    Broker->NextLeaseId = InitialLeaseId;
    return BC250_GPUVA_SDMA_BROKER_OK;
}

BC250_GPUVA_SDMA_BROKER_STATUS Bc250GpuvaSdmaBrokerIssueLease(
    BC250_GPUVA_SDMA_BROKER *Broker,
    const BC250_MCDM_OBJECT_REGISTRY *Objects,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE OpenAllocation,
    uint64_t GpuVa,
    uint64_t ByteCount,
    uint64_t *LeaseId)
{
    BC250_MCDM_OBJECT_RECORD openRecord, allocationRecord, processRecord;
    BC250_GPUVA_SDMA_LEASE_RECORD *slot = NULL;
    BC250_MCDM_OBJECT_STATUS objectStatus;
    uint32_t index;

    if (LeaseId != NULL) *LeaseId = 0ull;
    if (Bc250BrokerCheck(Broker) != BC250_GPUVA_SDMA_BROKER_OK ||
        Objects == NULL || OwnerProcess == NULL || OpenAllocation == NULL ||
        LeaseId == NULL || GpuVa == 0ull || ByteCount == 0ull ||
        (GpuVa & (BC250_GPUVA_SDMA_BROKER_PAGE_BYTES - 1ull)) != 0ull ||
        (ByteCount & 3ull) != 0ull) {
        return BC250_GPUVA_SDMA_BROKER_BAD_ARGUMENT;
    }

    objectStatus = Bc250McdmObjectLookup(Objects, OwnerProcess,
        BC250_MCDM_OBJECT_PROCESS, OwnerProcess, &processRecord);
    if (objectStatus != BC250_MCDM_OBJECT_OK) {
        return BC250_GPUVA_SDMA_BROKER_OWNER_MISMATCH;
    }
    if (processRecord.RootPageTableSetCount == 0u ||
        processRecord.RootPageTableAddress == 0ull) {
        return BC250_GPUVA_SDMA_BROKER_MAPPING_NOT_READY;
    }
    objectStatus = Bc250McdmObjectLookup(Objects, OpenAllocation,
        BC250_MCDM_OBJECT_OPEN_ALLOCATION, OwnerProcess, &openRecord);
    if (objectStatus != BC250_MCDM_OBJECT_OK || openRecord.ReferencedObject == NULL) {
        return BC250_GPUVA_SDMA_BROKER_OWNER_MISMATCH;
    }
    objectStatus = Bc250McdmObjectLookup(Objects, openRecord.ReferencedObject,
        BC250_MCDM_OBJECT_ALLOCATION, NULL, &allocationRecord);
    if (objectStatus != BC250_MCDM_OBJECT_OK || ByteCount > allocationRecord.AllocationBytes) {
        return BC250_GPUVA_SDMA_BROKER_RANGE_INVALID;
    }
    for (index = 0u; index < BC250_GPUVA_SDMA_BROKER_MAX_LEASES; ++index) {
        if (Broker->Leases[index].Live == 0u) {
            slot = &Broker->Leases[index];
            break;
        }
    }
    if (slot == NULL) return BC250_GPUVA_SDMA_BROKER_NO_SPACE;

    ++Broker->NextGeneration;
    if (Broker->NextGeneration == 0u) ++Broker->NextGeneration;
    slot->LeaseId = Broker->NextLeaseId++;
    if (Broker->NextLeaseId == 0ull) ++Broker->NextLeaseId;
    slot->GpuVa = GpuVa;
    slot->ByteCount = ByteCount;
    slot->Generation = Broker->NextGeneration;
    slot->Live = 1u;
    slot->OwnerProcess = OwnerProcess;
    slot->OpenAllocation = OpenAllocation;
    slot->Allocation = openRecord.ReferencedObject;
    *LeaseId = slot->LeaseId;
    return BC250_GPUVA_SDMA_BROKER_OK;
}

BC250_GPUVA_SDMA_BROKER_STATUS Bc250GpuvaSdmaBrokerValidatePair(
    const BC250_GPUVA_SDMA_BROKER *Broker,
    uint64_t SourceLeaseId,
    uint64_t DestinationLeaseId,
    uint64_t FenceGpuVa,
    uint64_t ByteCount)
{
    const BC250_GPUVA_SDMA_LEASE_RECORD *source;
    const BC250_GPUVA_SDMA_LEASE_RECORD *destination;
    if (Bc250BrokerCheck(Broker) != BC250_GPUVA_SDMA_BROKER_OK ||
        FenceGpuVa == 0ull || (FenceGpuVa & 3ull) != 0ull || ByteCount == 0ull) {
        return BC250_GPUVA_SDMA_BROKER_BAD_ARGUMENT;
    }
    source = Bc250FindLiveLease(Broker, SourceLeaseId);
    destination = Bc250FindLiveLease(Broker, DestinationLeaseId);
    if (source == NULL || destination == NULL) return BC250_GPUVA_SDMA_BROKER_STALE_LEASE;
    if (source->OwnerProcess == destination->OwnerProcess ||
        source->Allocation == destination->Allocation ||
        source->GpuVa == destination->GpuVa || ByteCount > source->ByteCount ||
        ByteCount > destination->ByteCount) {
        return BC250_GPUVA_SDMA_BROKER_PAIR_NOT_ISOLATED;
    }
    return BC250_GPUVA_SDMA_BROKER_OK;
}

BC250_GPUVA_SDMA_BROKER_STATUS Bc250GpuvaSdmaBrokerRevokeLease(
    BC250_GPUVA_SDMA_BROKER *Broker,
    uint64_t LeaseId)
{
    BC250_GPUVA_SDMA_LEASE_RECORD *lease;
    uint32_t index;
    if (Bc250BrokerCheck(Broker) != BC250_GPUVA_SDMA_BROKER_OK || LeaseId == 0ull) {
        return BC250_GPUVA_SDMA_BROKER_BAD_ARGUMENT;
    }
    for (index = 0u; index < BC250_GPUVA_SDMA_BROKER_MAX_LEASES; ++index) {
        lease = &Broker->Leases[index];
        if (lease->Live != 0u && lease->LeaseId == LeaseId) {
            memset(lease, 0, sizeof(*lease));
            return BC250_GPUVA_SDMA_BROKER_OK;
        }
    }
    return BC250_GPUVA_SDMA_BROKER_STALE_LEASE;
}

uint32_t Bc250GpuvaSdmaBrokerRevokeForObject(
    BC250_GPUVA_SDMA_BROKER *Broker,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE OpenAllocation,
    BC250_MCDM_HANDLE Allocation)
{
    uint32_t index, revoked = 0u;
    if (Bc250BrokerCheck(Broker) != BC250_GPUVA_SDMA_BROKER_OK ||
        (OwnerProcess == NULL && OpenAllocation == NULL && Allocation == NULL)) {
        return 0u;
    }
    for (index = 0u; index < BC250_GPUVA_SDMA_BROKER_MAX_LEASES; ++index) {
        BC250_GPUVA_SDMA_LEASE_RECORD *lease = &Broker->Leases[index];
        if (lease->Live != 0u &&
            ((OwnerProcess != NULL && lease->OwnerProcess == OwnerProcess) ||
             (OpenAllocation != NULL && lease->OpenAllocation == OpenAllocation) ||
             (Allocation != NULL && lease->Allocation == Allocation))) {
            memset(lease, 0, sizeof(*lease));
            ++revoked;
        }
    }
    return revoked;
}

const char *Bc250GpuvaSdmaBrokerStatusName(BC250_GPUVA_SDMA_BROKER_STATUS Status)
{
    switch (Status) {
    case BC250_GPUVA_SDMA_BROKER_OK: return "OK";
    case BC250_GPUVA_SDMA_BROKER_BAD_ARGUMENT: return "BAD_ARGUMENT";
    case BC250_GPUVA_SDMA_BROKER_BAD_STATE: return "BAD_STATE";
    case BC250_GPUVA_SDMA_BROKER_NO_SPACE: return "NO_SPACE";
    case BC250_GPUVA_SDMA_BROKER_NOT_FOUND: return "NOT_FOUND";
    case BC250_GPUVA_SDMA_BROKER_STALE_LEASE: return "STALE_LEASE";
    case BC250_GPUVA_SDMA_BROKER_OWNER_MISMATCH: return "OWNER_MISMATCH";
    case BC250_GPUVA_SDMA_BROKER_MAPPING_NOT_READY: return "MAPPING_NOT_READY";
    case BC250_GPUVA_SDMA_BROKER_RANGE_INVALID: return "RANGE_INVALID";
    case BC250_GPUVA_SDMA_BROKER_PAIR_NOT_ISOLATED: return "PAIR_NOT_ISOLATED";
    default: return "UNKNOWN";
    }
}
