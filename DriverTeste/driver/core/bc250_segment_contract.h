#ifndef BC250_SEGMENT_CONTRACT_H
#define BC250_SEGMENT_CONTRACT_H

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

#define BC250_SEGMENT_COUNT_MAX           2u
#define BC250_SEGMENT_ID_VRAM             1u
#define BC250_SEGMENT_ID_GART             2u

#define BC250_SEGMENT_FLAG_CPU_VISIBLE    0x00000001u
#define BC250_SEGMENT_FLAG_APERTURE       0x00000002u
#define BC250_SEGMENT_FLAG_VRAM           0x00000004u

#define BC250_SEGMENT_MASK_VRAM           (1u << (BC250_SEGMENT_ID_VRAM - 1u))
#define BC250_SEGMENT_MASK_GART           (1u << (BC250_SEGMENT_ID_GART - 1u))
#define BC250_SEGMENT_MASK_ALL            (BC250_SEGMENT_MASK_VRAM | BC250_SEGMENT_MASK_GART)

typedef enum BC250_SEGMENT_STATUS {
    BC250_SEGMENT_OK = 0,
    BC250_SEGMENT_BAD_ARGUMENT,
    BC250_SEGMENT_INVALID_SIZE,
    BC250_SEGMENT_INVALID_ALIGNMENT,
    BC250_SEGMENT_OUT_OF_MEMORY,
    BC250_SEGMENT_INVALID_SEGMENT
} BC250_SEGMENT_STATUS;

typedef struct BC250_SEGMENT_DESCRIPTOR {
    uint32_t SegmentId;
    uint32_t Flags;
    uint64_t BaseAddress;
    uint64_t SizeBytes;
    uint32_t PageSizeBytes;
    uint64_t CpuVisibleSizeBytes;
} BC250_SEGMENT_DESCRIPTOR;

typedef struct BC250_SEGMENT_CONFIG {
    uint32_t SegmentCount;
    BC250_SEGMENT_DESCRIPTOR Segments[BC250_SEGMENT_COUNT_MAX];
} BC250_SEGMENT_CONFIG;

typedef struct BC250_ALLOCATION_REQUEST {
    uint32_t AllocationId;
    uint64_t SizeBytes;
    uint32_t AlignmentBytes;
    uint32_t PreferredSegmentId;
    uint32_t SupportedSegmentMask;
} BC250_ALLOCATION_REQUEST;

typedef struct BC250_ALLOCATION_DESCRIPTOR {
    uint32_t AllocationId;
    uint64_t AlignedSizeBytes;
    uint32_t AlignmentBytes;
    uint32_t AssignedSegmentId;
    uint64_t SegmentOffset;
    uint64_t GpuVirtualAddress;
    uint32_t IsValid;
} BC250_ALLOCATION_DESCRIPTOR;

BC250_SEGMENT_STATUS Bc250BuildSegmentConfig(
    uint64_t VramTotalBytes,
    uint64_t VramUsableBytes,
    uint64_t CpuApertureBytes,
    uint64_t GartSizeBytes,
    BC250_SEGMENT_CONFIG *Config);

BC250_SEGMENT_STATUS Bc250ValidateAllocationRequest(
    const BC250_SEGMENT_CONFIG *Config,
    const BC250_ALLOCATION_REQUEST *Request);

BC250_SEGMENT_STATUS Bc250DescribeAllocation(
    const BC250_SEGMENT_CONFIG *Config,
    const BC250_ALLOCATION_REQUEST *Request,
    uint64_t SegmentOffset,
    BC250_ALLOCATION_DESCRIPTOR *Descriptor);

const char *Bc250SegmentStatusName(BC250_SEGMENT_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
