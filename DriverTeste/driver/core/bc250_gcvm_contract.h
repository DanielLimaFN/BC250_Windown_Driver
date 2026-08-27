#ifndef BC250_GCVM_CONTRACT_H
#define BC250_GCVM_CONTRACT_H

#include "bc250_vram_allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_PAGE_SIZE_4K 0x1000ull
#define BC250_PAGE_MASK_4K (BC250_PAGE_SIZE_4K - 1ull)

#define BC250_PTE_VALID      0x00000001u
#define BC250_PTE_SYSTEM     0x00000002u
#define BC250_PTE_SNOOPED    0x00000004u
#define BC250_PTE_TMZ        0x00000008u
#define BC250_PTE_READABLE   0x00000020u
#define BC250_PTE_WRITEABLE  0x00000040u
#define BC250_PTE_EXECUTABLE 0x00000080u
#define BC250_PTE_PRT        0x00000100u

#define BC250_PTE_RW_DEFAULT \
    (BC250_PTE_VALID | BC250_PTE_READABLE | BC250_PTE_WRITEABLE)

#define BC250_PTE_RX_DEFAULT \
    (BC250_PTE_VALID | BC250_PTE_READABLE | BC250_PTE_EXECUTABLE)

#define BC250_PTE_RWX_DEFAULT \
    (BC250_PTE_VALID | BC250_PTE_READABLE | BC250_PTE_WRITEABLE | \
     BC250_PTE_EXECUTABLE)

#define BC250_GCVM_HUB_GFXHUB 0u
#define BC250_GCVM_HUB_MMHUB  1u

#define BC250_GCVM_ENGINE_GFX  0u
#define BC250_GCVM_ENGINE_SDMA 17u

typedef enum BC250_GCVM_STATUS {
    BC250_GCVM_OK = 0,
    BC250_GCVM_BAD_ARGUMENT,
    BC250_GCVM_MISALIGNED,
    BC250_GCVM_RANGE_OVERFLOW,
    BC250_GCVM_OUT_OF_RANGE,
    BC250_GCVM_PAGE_COUNT_ZERO,
    BC250_GCVM_BUFFER_TOO_SMALL,
    BC250_GCVM_TRANSLATION_FAULT
} BC250_GCVM_STATUS;

typedef struct BC250_GCVM_PAGE_TABLE_PLAN {
    uint64_t VaStart;
    uint64_t VaEnd;
    uint64_t PhysicalStart;
    uint64_t Length;
    uint32_t PageCount;
    uint32_t TableSizeBytes;
    uint32_t TableAlignment;
    uint32_t PteFlags;
    BC250_VRAM_TOKEN TableToken;
    uint64_t TableVramOffset;
} BC250_GCVM_PAGE_TABLE_PLAN;

typedef struct BC250_GCVM_TLB_INVALIDATION_PLAN {
    uint32_t Hub;
    uint32_t EngineId;
    uint32_t VmId;
    uint32_t FlushType;
} BC250_GCVM_TLB_INVALIDATION_PLAN;

BC250_GCVM_STATUS Bc250GcvmEncodePte(
    uint64_t PhysicalAddress,
    uint32_t Flags,
    uint64_t *PteOut);

BC250_GCVM_STATUS Bc250GcvmDecodePte(
    uint64_t Pte,
    uint64_t *PhysicalAddressOut,
    uint32_t *FlagsOut);

BC250_GCVM_STATUS Bc250GcvmBuildPageTablePlan(
    uint64_t VaStart,
    uint64_t PhysicalStart,
    uint64_t Length,
    uint32_t PteFlags,
    BC250_GCVM_PAGE_TABLE_PLAN *Plan);

BC250_GCVM_STATUS Bc250GcvmPopulatePageTable(
    const BC250_GCVM_PAGE_TABLE_PLAN *Plan,
    uint64_t *PteBuffer,
    size_t BufferBytes);

BC250_GCVM_STATUS Bc250GcvmTranslateVa(
    const BC250_GCVM_PAGE_TABLE_PLAN *Plan,
    const uint64_t *PteBuffer,
    uint64_t Va,
    uint64_t *PhysicalAddressOut);

const char *Bc250GcvmStatusName(BC250_GCVM_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
