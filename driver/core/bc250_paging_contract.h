#ifndef BC250_PAGING_CONTRACT_H
#define BC250_PAGING_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#include "bc250_sdma_packet_builder.h"
#include "bc250_gcvm_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum BC250_PAGING_OPERATION_TYPE {
    BC250_PAGING_OP_TRANSFER = 0,
    BC250_PAGING_OP_FILL = 1,
    BC250_PAGING_OP_UPDATE_PAGE_TABLE = 2,
    BC250_PAGING_OP_FLUSH_TLB = 3
} BC250_PAGING_OPERATION_TYPE;

typedef enum BC250_PAGING_STATUS {
    BC250_PAGING_OK = 0,
    BC250_PAGING_BAD_ARGUMENT,
    BC250_PAGING_BUFFER_TOO_SMALL,
    BC250_PAGING_MISALIGNED_ADDRESS,
    BC250_PAGING_UNSUPPORTED_OPERATION
} BC250_PAGING_STATUS;

typedef struct BC250_PAGING_TRANSFER_PARAMS {
    uint64_t SourceGpuAddress;
    uint64_t DestinationGpuAddress;
    uint32_t TransferSizeBytes;
} BC250_PAGING_TRANSFER_PARAMS;

typedef struct BC250_PAGING_FILL_PARAMS {
    uint64_t DestinationGpuAddress;
    uint32_t FillValue;
    uint32_t FillSizeBytes;
} BC250_PAGING_FILL_PARAMS;

typedef struct BC250_PAGING_UPDATE_PAGE_TABLE_PARAMS {
    uint64_t PageTableGpuAddress;
    uint32_t StartIndex;
    uint32_t EntryCount;
    uint64_t PhysicalAddressBase;
    uint32_t PteFlags;
} BC250_PAGING_UPDATE_PAGE_TABLE_PARAMS;

typedef struct BC250_PAGING_FLUSH_TLB_PARAMS {
    uint32_t Vmid;
    uint32_t HubIndex; /* 0 = GFXHUB, 1 = MMHUB */
    uint32_t FlushType;
} BC250_PAGING_FLUSH_TLB_PARAMS;

typedef struct BC250_GFXHUB_TLB_FLUSH_PLAN {
    uint32_t Vmid;
    uint32_t EngineId;
    uint32_t FlushType;
    uint32_t RequestRegisterOffset;
    uint32_t AckRegisterOffset;
    uint32_t RangeLowRegisterOffset;
    uint32_t RangeHighRegisterOffset;
    uint32_t RequestValue;
    uint32_t AckMask;
    uint32_t ExpectedRangeLow;
    uint32_t ExpectedRangeHigh;
    uint32_t RequiresDummyRequestRead;
    uint32_t UsesSemaphore;
} BC250_GFXHUB_TLB_FLUSH_PLAN;

typedef struct BC250_PAGING_REQUEST {
    BC250_PAGING_OPERATION_TYPE Operation;
    union {
        BC250_PAGING_TRANSFER_PARAMS Transfer;
        BC250_PAGING_FILL_PARAMS Fill;
        BC250_PAGING_UPDATE_PAGE_TABLE_PARAMS UpdatePageTable;
        BC250_PAGING_FLUSH_TLB_PARAMS FlushTlb;
    } Params;
} BC250_PAGING_REQUEST;

BC250_PAGING_STATUS Bc250PagingBuildTransfer(
    const BC250_PAGING_TRANSFER_PARAMS *Params,
    uint32_t *PagingBuffer,
    uint32_t BufferDwords,
    uint32_t *CursorDwords);

BC250_PAGING_STATUS Bc250PagingBuildFill(
    const BC250_PAGING_FILL_PARAMS *Params,
    uint32_t *PagingBuffer,
    uint32_t BufferDwords,
    uint32_t *CursorDwords);

BC250_PAGING_STATUS Bc250PagingBuildUpdatePageTable(
    const BC250_PAGING_UPDATE_PAGE_TABLE_PARAMS *Params,
    uint64_t *PteOutputBuffer,
    uint32_t MaxPteCount,
    uint32_t *WrittenPteCount);

BC250_PAGING_STATUS Bc250PagingBuildFlushTlb(
    const BC250_PAGING_FLUSH_TLB_PARAMS *Params,
    uint32_t *PagingBuffer,
    uint32_t BufferDwords,
    uint32_t *CursorDwords);

BC250_PAGING_STATUS Bc250PagingBuildGfxhubTlbFlushPlan(
    const BC250_PAGING_FLUSH_TLB_PARAMS *Params,
    BC250_GFXHUB_TLB_FLUSH_PLAN *Plan);

/* DX.32: plano para o engine 0 (anel grafico) ou 17 (CPU). */
#define BC250_GFX_RING_INVALIDATE_ENGINE 0u
BC250_PAGING_STATUS Bc250PagingBuildGfxhubTlbFlushPlanForEngine(
    const BC250_PAGING_FLUSH_TLB_PARAMS *Params,
    uint32_t EngineId,
    BC250_GFXHUB_TLB_FLUSH_PLAN *Plan);

BC250_PAGING_STATUS Bc250PagingBuildRequest(
    const BC250_PAGING_REQUEST *Request,
    uint32_t *PagingBuffer,
    uint32_t BufferDwords,
    uint32_t *CursorDwords);

const char *Bc250PagingStatusName(BC250_PAGING_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
