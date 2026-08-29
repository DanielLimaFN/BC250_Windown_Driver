#ifndef BC250_GPUVA_SDMA_BROKER_CONTRACT_H
#define BC250_GPUVA_SDMA_BROKER_CONTRACT_H

#include "bc250_mcdm_object_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_GPUVA_SDMA_BROKER_SIGNATURE 0x42565347u /* "GSVB" */
#define BC250_GPUVA_SDMA_BROKER_MAX_LEASES 32u
#define BC250_GPUVA_SDMA_BROKER_PAGE_BYTES 4096ull

typedef enum BC250_GPUVA_SDMA_BROKER_STATUS {
    BC250_GPUVA_SDMA_BROKER_OK = 0,
    BC250_GPUVA_SDMA_BROKER_BAD_ARGUMENT,
    BC250_GPUVA_SDMA_BROKER_BAD_STATE,
    BC250_GPUVA_SDMA_BROKER_NO_SPACE,
    BC250_GPUVA_SDMA_BROKER_NOT_FOUND,
    BC250_GPUVA_SDMA_BROKER_STALE_LEASE,
    BC250_GPUVA_SDMA_BROKER_OWNER_MISMATCH,
    BC250_GPUVA_SDMA_BROKER_MAPPING_NOT_READY,
    BC250_GPUVA_SDMA_BROKER_RANGE_INVALID,
    BC250_GPUVA_SDMA_BROKER_PAIR_NOT_ISOLATED
} BC250_GPUVA_SDMA_BROKER_STATUS;

typedef struct BC250_GPUVA_SDMA_LEASE_RECORD {
    uint64_t LeaseId;
    uint64_t GpuVa;
    uint64_t ByteCount;
    uint32_t Generation;
    uint32_t Live;
    BC250_MCDM_HANDLE OwnerProcess;
    BC250_MCDM_HANDLE OpenAllocation;
    BC250_MCDM_HANDLE Allocation;
} BC250_GPUVA_SDMA_LEASE_RECORD;

typedef struct BC250_GPUVA_SDMA_BROKER {
    uint32_t Signature;
    uint32_t NextGeneration;
    uint64_t NextLeaseId;
    BC250_GPUVA_SDMA_LEASE_RECORD Leases[BC250_GPUVA_SDMA_BROKER_MAX_LEASES];
} BC250_GPUVA_SDMA_BROKER;

BC250_GPUVA_SDMA_BROKER_STATUS Bc250GpuvaSdmaBrokerInitialize(
    BC250_GPUVA_SDMA_BROKER *Broker,
    uint64_t InitialLeaseId);

/* Call only from the adapter-owned MCDM broker, never from a user IOCTL. */
BC250_GPUVA_SDMA_BROKER_STATUS Bc250GpuvaSdmaBrokerIssueLease(
    BC250_GPUVA_SDMA_BROKER *Broker,
    const BC250_MCDM_OBJECT_REGISTRY *Objects,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE OpenAllocation,
    uint64_t GpuVa,
    uint64_t ByteCount,
    uint64_t *LeaseId);

BC250_GPUVA_SDMA_BROKER_STATUS Bc250GpuvaSdmaBrokerValidatePair(
    const BC250_GPUVA_SDMA_BROKER *Broker,
    uint64_t SourceLeaseId,
    uint64_t DestinationLeaseId,
    uint64_t FenceGpuVa,
    uint64_t ByteCount);

BC250_GPUVA_SDMA_BROKER_STATUS Bc250GpuvaSdmaBrokerRevokeLease(
    BC250_GPUVA_SDMA_BROKER *Broker,
    uint64_t LeaseId);

/* Lifecycle teardown: revoke every lease referencing any supplied object. */
uint32_t Bc250GpuvaSdmaBrokerRevokeForObject(
    BC250_GPUVA_SDMA_BROKER *Broker,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE OpenAllocation,
    BC250_MCDM_HANDLE Allocation);

const char *Bc250GpuvaSdmaBrokerStatusName(
    BC250_GPUVA_SDMA_BROKER_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
