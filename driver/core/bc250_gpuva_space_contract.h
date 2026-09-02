#ifndef BC250_GPUVA_SPACE_CONTRACT_H
#define BC250_GPUVA_SPACE_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT8 uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#include "bc250_mcdm_object_contract.h"
#include "bc250_gcvm_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_GPUVA_SPACE_SIGNATURE 0x53415647u /* "GVAS" */
#define BC250_GPUVA_SPACE_MAX_RANGES 256u
#define BC250_GPUVA_PAGE_BYTES 4096ull
#define BC250_GPUVA_PAGE_MASK (BC250_GPUVA_PAGE_BYTES - 1ull)
#define BC250_GPUVA_DEFAULT_BITS 48u
#define BC250_GPUVA_MAX_LIMIT 0x0001000000000000ull /* 2^48 */

typedef enum BC250_GPUVA_STATUS {
    BC250_GPUVA_OK = 0,
    BC250_GPUVA_BAD_ARGUMENT,
    BC250_GPUVA_BAD_STATE,
    BC250_GPUVA_NO_SPACE,
    BC250_GPUVA_INVALID_ADDRESS,
    BC250_GPUVA_MISALIGNED,
    BC250_GPUVA_RANGE_OVERFLOW,
    BC250_GPUVA_OUT_OF_BOUNDS,
    BC250_GPUVA_INVALID_OVERLAP,
    BC250_GPUVA_NOT_FOUND,
    BC250_GPUVA_OWNER_MISMATCH,
    BC250_GPUVA_PERMISSION_DENIED,
    BC250_GPUVA_GAP_DETECTED,
    BC250_GPUVA_NOT_RESIDENT,
    BC250_GPUVA_STALE_EPOCH,
    BC250_GPUVA_IN_TEARDOWN,
    BC250_GPUVA_BUSY
} BC250_GPUVA_STATUS;

typedef enum BC250_GPUVA_RANGE_STATE {
    BC250_GPUVA_RANGE_FREE = 0,
    BC250_GPUVA_RANGE_MAPPED = 1,
    BC250_GPUVA_RANGE_UNMAPPED = 2,
    BC250_GPUVA_RANGE_TEARDOWN = 3
} BC250_GPUVA_RANGE_STATE;

typedef struct BC250_GPUVA_RANGE_RECORD {
    uint32_t State;             /* BC250_GPUVA_RANGE_STATE */
    uint32_t Generation;
    uint64_t RangeId;
    uint64_t BaseGpuVa;
    uint64_t Size;
    uint64_t EndGpuVa;          /* BaseGpuVa + Size (com overflow check) */
    BC250_MCDM_HANDLE OwnerProcess;
    BC250_MCDM_HANDLE OwnerContext;
    BC250_MCDM_HANDLE Allocation;
    BC250_MCDM_HANDLE OpenAllocation;
    uint32_t BackingSegmentId;
    uint64_t BackingSegmentOffset;
    uint32_t Permissions;       /* BC250_PTE_READABLE | BC250_PTE_WRITEABLE | BC250_PTE_EXECUTABLE */
    uint32_t IsResident;
    uint32_t PageTableEpoch;
    uint32_t TeardownState;
} BC250_GPUVA_RANGE_RECORD;

typedef struct BC250_GPUVA_SPACE {
    uint32_t Signature;
    uint32_t NextGeneration;
    uint64_t NextRangeId;
    uint32_t AddressBits;
    uint64_t MaxAddress;
    uint32_t PageTableEpoch;
    uint32_t Teardown;
    uint32_t LiveCount;
    uint32_t SortedCount;
    uint32_t SortedIndices[BC250_GPUVA_SPACE_MAX_RANGES];
    BC250_GPUVA_RANGE_RECORD Ranges[BC250_GPUVA_SPACE_MAX_RANGES];
} BC250_GPUVA_SPACE;

BC250_GPUVA_STATUS Bc250GpuvaSpaceInitialize(
    BC250_GPUVA_SPACE *Space,
    uint32_t AddressBits);

BC250_GPUVA_STATUS Bc250GpuvaSpaceInsertRange(
    BC250_GPUVA_SPACE *Space,
    const BC250_GPUVA_RANGE_RECORD *Param,
    uint64_t *RangeId);

BC250_GPUVA_STATUS Bc250GpuvaSpaceRemoveRange(
    BC250_GPUVA_SPACE *Space,
    BC250_MCDM_HANDLE OwnerProcess,
    uint64_t BaseGpuVa,
    uint64_t Size);

/*
 * Reconstrói uma cópia completa em Scratch e só publica em Space no sucesso.
 * Deve ser usado em PASSIVE_LEVEL sobre o snapshot INATIVO; leitores nunca
 * podem observar Space enquanto a cópia final é publicada.
 */
BC250_GPUVA_STATUS Bc250GpuvaSpaceReplaceRangeTransactional(
    BC250_GPUVA_SPACE *Space,
    BC250_GPUVA_SPACE *Scratch,
    const BC250_GPUVA_RANGE_RECORD *Param,
    uint32_t MapRange);

BC250_GPUVA_STATUS Bc250GpuvaSpaceInvalidateForObject(
    BC250_GPUVA_SPACE *Space,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE Context,
    BC250_MCDM_HANDLE Allocation);

BC250_GPUVA_STATUS Bc250GpuvaSpaceValidateRangeCoverage(
    const BC250_GPUVA_SPACE *Space,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE Context,
    uint64_t BaseGpuVa,
    uint64_t Size,
    uint32_t RequiredPermissions,
    uint32_t ExpectedPageTableEpoch);

const char *Bc250GpuvaStatusName(BC250_GPUVA_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
