#ifndef BC250_VRAM_ALLOCATOR_H
#define BC250_VRAM_ALLOCATOR_H

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

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_VRAM_MAX_REGIONS 64u

#define BC250_VRAM_ALLOC_TOP_DOWN    0x00000001u
#define BC250_VRAM_ALLOC_CPU_VISIBLE 0x00000002u
#define BC250_VRAM_REGION_PERSISTENT 0x00000004u

typedef enum BC250_VRAM_STATUS {
    BC250_VRAM_OK = 0,
    BC250_VRAM_BAD_ARGUMENT,
    BC250_VRAM_RANGE_OVERFLOW,
    BC250_VRAM_OUT_OF_RANGE,
    BC250_VRAM_MISALIGNED,
    BC250_VRAM_OVERLAP,
    BC250_VRAM_NO_SPACE,
    BC250_VRAM_TABLE_FULL,
    BC250_VRAM_STALE_TOKEN,
    BC250_VRAM_PROTECTED
} BC250_VRAM_STATUS;

typedef enum BC250_VRAM_ROLE {
    BC250_VRAM_ROLE_UNKNOWN = 0,
    BC250_VRAM_ROLE_SCANOUT,
    BC250_VRAM_ROLE_PAGE_TABLE,
    BC250_VRAM_ROLE_RING,
    BC250_VRAM_ROLE_FIRMWARE,
    BC250_VRAM_ROLE_TMR,
    BC250_VRAM_ROLE_TEST_BUFFER,
    BC250_VRAM_ROLE_PLATFORM_RESERVED,
    BC250_VRAM_ROLE_GART,
    BC250_VRAM_ROLE_MQD,
    BC250_VRAM_ROLE_COMPUTE_QUEUE
} BC250_VRAM_ROLE;

#define BC250_VRAM_ROLE_MAX BC250_VRAM_ROLE_COMPUTE_QUEUE

typedef struct BC250_VRAM_TOKEN {
    uint32_t Slot;
    uint32_t Generation;
} BC250_VRAM_TOKEN;

typedef struct BC250_VRAM_REGION {
    uint64_t Offset;
    uint64_t Length;
    uint64_t Alignment;
    uint32_t Role;
    uint32_t Flags;
    uint32_t Generation;
    uint32_t InUse;
} BC250_VRAM_REGION;

typedef struct BC250_VRAM_ALLOCATOR {
    uint64_t TotalBytes;
    uint64_t CpuVisibleBytes;
    uint32_t NextGeneration;
    uint32_t RegionCount;
    BC250_VRAM_REGION Regions[BC250_VRAM_MAX_REGIONS];
} BC250_VRAM_ALLOCATOR;

BC250_VRAM_STATUS Bc250VramAllocatorInit(
    BC250_VRAM_ALLOCATOR *Allocator,
    uint64_t TotalBytes,
    uint64_t CpuVisibleBytes);

BC250_VRAM_STATUS Bc250VramReserveFixed(
    BC250_VRAM_ALLOCATOR *Allocator,
    uint64_t Offset,
    uint64_t Length,
    uint64_t Alignment,
    BC250_VRAM_ROLE Role,
    uint32_t Flags,
    BC250_VRAM_TOKEN *Token);

BC250_VRAM_STATUS Bc250VramAllocate(
    BC250_VRAM_ALLOCATOR *Allocator,
    uint64_t Length,
    uint64_t Alignment,
    BC250_VRAM_ROLE Role,
    uint32_t Flags,
    BC250_VRAM_TOKEN *Token);

BC250_VRAM_STATUS Bc250VramRelease(
    BC250_VRAM_ALLOCATOR *Allocator,
    BC250_VRAM_TOKEN Token);

BC250_VRAM_STATUS Bc250VramGetRegion(
    const BC250_VRAM_ALLOCATOR *Allocator,
    BC250_VRAM_TOKEN Token,
    BC250_VRAM_REGION *Region);

BC250_VRAM_STATUS Bc250VramValidate(const BC250_VRAM_ALLOCATOR *Allocator);
const char *Bc250VramStatusName(BC250_VRAM_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
