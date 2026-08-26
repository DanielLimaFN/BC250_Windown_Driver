#ifndef BC250_GCVM_BACKING_TRANSACTION_H
#define BC250_GCVM_BACKING_TRANSACTION_H

#include <ntddk.h>
#include <dispmprt.h>

#include "../core/bc250_gcvm_contract.h"
#include "../core/bc250_resource_contract.h"

#define BC250_GCVM_BACKING_BYTES 0x1000u
#define BC250_GCVM_BACKING_POOL_TAG 'PGCB'

typedef uint32_t (*BC250_GCVM_BACKING_EXECUTE)(
    void *Context, uint64_t PageTablePhysical);

typedef enum BC250_GCVM_BACKING_STATUS {
    BC250_GCVM_BACKING_OK = 0,
    BC250_GCVM_BACKING_BAD_ARGUMENT,
    BC250_GCVM_BACKING_ALLOCATION_FAILED,
    BC250_GCVM_BACKING_REGION_FAILED,
    BC250_GCVM_BACKING_APERTURE_INVALID,
    BC250_GCVM_BACKING_MAP_FAILED,
    BC250_GCVM_BACKING_POOL_FAILED,
    BC250_GCVM_BACKING_PTE_FAILED,
    BC250_GCVM_BACKING_READBACK_FAILED,
    BC250_GCVM_BACKING_EXECUTE_FAILED,
    BC250_GCVM_BACKING_RESTORE_FAILED,
    BC250_GCVM_BACKING_UNMAP_FAILED,
    BC250_GCVM_BACKING_RELEASE_FAILED
} BC250_GCVM_BACKING_STATUS;

typedef struct BC250_GCVM_BACKING_EVIDENCE {
    uint64_t PageTableOffset;
    uint64_t PageTablePhysical;
    uint64_t TargetOffset;
    uint64_t TargetPhysical;
    uint64_t FirstPte;
    uint32_t PageTableBytes;
    uint32_t OwnershipPassed;
    uint32_t WriteReadbackPassed;
    uint32_t ExecutePassed;
    uint32_t RestorePassed;
} BC250_GCVM_BACKING_EVIDENCE;

BC250_GCVM_BACKING_STATUS Bc250RunGcvmBackingTransaction(
    BC250_VRAM_ALLOCATOR *Allocator,
    uint64_t VramBaseOffset,
    const DXGKRNL_INTERFACE *DxgkInterface,
    const BC250_RESOLVED_BAR_RESOURCE *ResolvedBars,
    size_t ResolvedBarCount,
    uint32_t VramApertureBarIndex,
    BC250_GCVM_BACKING_EXECUTE Execute,
    void *ExecuteContext,
    BC250_GCVM_BACKING_EVIDENCE *Evidence);

#endif
