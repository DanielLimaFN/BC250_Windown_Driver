#include "bc250_segment_contract.h"

#include <string.h>

static int Bc250IsPowerOfTwo32(uint32_t Value)
{
    return Value != 0 && (Value & (Value - 1u)) == 0;
}

BC250_SEGMENT_STATUS Bc250BuildSegmentConfig(
    uint64_t VramTotalBytes,
    uint64_t VramUsableBytes,
    uint64_t CpuApertureBytes,
    uint64_t GartSizeBytes,
    BC250_SEGMENT_CONFIG *Config)
{
    if (Config == NULL) {
        return BC250_SEGMENT_BAD_ARGUMENT;
    }
    if (VramTotalBytes == 0 || VramUsableBytes == 0 || VramUsableBytes > VramTotalBytes) {
        return BC250_SEGMENT_INVALID_SIZE;
    }
    if (GartSizeBytes == 0) {
        return BC250_SEGMENT_INVALID_SIZE;
    }

    memset(Config, 0, sizeof(BC250_SEGMENT_CONFIG));
    Config->SegmentCount = 2u;

    /* Segment 1: Local VRAM */
    Config->Segments[0].SegmentId = BC250_SEGMENT_ID_VRAM;
    Config->Segments[0].Flags = BC250_SEGMENT_FLAG_VRAM;
    if (CpuApertureBytes > 0) {
        Config->Segments[0].Flags |= BC250_SEGMENT_FLAG_CPU_VISIBLE;
    }
    Config->Segments[0].BaseAddress = 0x0000000000000000ull;
    Config->Segments[0].SizeBytes = VramUsableBytes;
    Config->Segments[0].PageSizeBytes = 4096u;
    Config->Segments[0].CpuVisibleSizeBytes = CpuApertureBytes;

    /* Segment 2: GART / Aperture */
    Config->Segments[1].SegmentId = BC250_SEGMENT_ID_GART;
    Config->Segments[1].Flags = BC250_SEGMENT_FLAG_APERTURE | BC250_SEGMENT_FLAG_CPU_VISIBLE;
    Config->Segments[1].BaseAddress = VramTotalBytes;
    Config->Segments[1].SizeBytes = GartSizeBytes;
    Config->Segments[1].PageSizeBytes = 4096u;
    Config->Segments[1].CpuVisibleSizeBytes = GartSizeBytes;

    return BC250_SEGMENT_OK;
}

BC250_SEGMENT_STATUS Bc250ValidateAllocationRequest(
    const BC250_SEGMENT_CONFIG *Config,
    const BC250_ALLOCATION_REQUEST *Request)
{
    if (Config == NULL || Request == NULL) {
        return BC250_SEGMENT_BAD_ARGUMENT;
    }
    if (Request->SizeBytes == 0) {
        return BC250_SEGMENT_INVALID_SIZE;
    }
    if (Request->AlignmentBytes < 4096u || !Bc250IsPowerOfTwo32(Request->AlignmentBytes)) {
        return BC250_SEGMENT_INVALID_ALIGNMENT;
    }
    if (Request->PreferredSegmentId != BC250_SEGMENT_ID_VRAM &&
        Request->PreferredSegmentId != BC250_SEGMENT_ID_GART) {
        return BC250_SEGMENT_INVALID_SEGMENT;
    }
    if ((Request->SupportedSegmentMask & BC250_SEGMENT_MASK_ALL) == 0) {
        return BC250_SEGMENT_INVALID_SEGMENT;
    }

    if (Request->PreferredSegmentId == BC250_SEGMENT_ID_VRAM) {
        if (Request->SizeBytes > Config->Segments[0].SizeBytes) {
            return BC250_SEGMENT_OUT_OF_MEMORY;
        }
    } else {
        if (Request->SizeBytes > Config->Segments[1].SizeBytes) {
            return BC250_SEGMENT_OUT_OF_MEMORY;
        }
    }

    return BC250_SEGMENT_OK;
}

BC250_SEGMENT_STATUS Bc250DescribeAllocation(
    const BC250_SEGMENT_CONFIG *Config,
    const BC250_ALLOCATION_REQUEST *Request,
    uint64_t SegmentOffset,
    BC250_ALLOCATION_DESCRIPTOR *Descriptor)
{
    BC250_SEGMENT_STATUS status;
    uint64_t alignedSize;
    uint64_t segmentLimit;

    if (Descriptor == NULL) {
        return BC250_SEGMENT_BAD_ARGUMENT;
    }

    status = Bc250ValidateAllocationRequest(Config, Request);
    if (status != BC250_SEGMENT_OK) {
        return status;
    }

    if ((SegmentOffset & ((uint64_t)Request->AlignmentBytes - 1u)) != 0) {
        return BC250_SEGMENT_INVALID_ALIGNMENT;
    }

    alignedSize = (Request->SizeBytes + (uint64_t)Request->AlignmentBytes - 1u) &
                  ~((uint64_t)Request->AlignmentBytes - 1u);

    segmentLimit = (Request->PreferredSegmentId == BC250_SEGMENT_ID_VRAM) ?
                   Config->Segments[0].SizeBytes : Config->Segments[1].SizeBytes;

    if (SegmentOffset + alignedSize > segmentLimit) {
        return BC250_SEGMENT_OUT_OF_MEMORY;
    }

    memset(Descriptor, 0, sizeof(BC250_ALLOCATION_DESCRIPTOR));
    Descriptor->AllocationId = Request->AllocationId;
    Descriptor->AlignedSizeBytes = alignedSize;
    Descriptor->AlignmentBytes = Request->AlignmentBytes;
    Descriptor->AssignedSegmentId = Request->PreferredSegmentId;
    Descriptor->SegmentOffset = SegmentOffset;
    Descriptor->GpuVirtualAddress = 0x0000000000000000ull;
    Descriptor->IsValid = 1u;

    return BC250_SEGMENT_OK;
}

const char *Bc250SegmentStatusName(BC250_SEGMENT_STATUS Status)
{
    switch (Status) {
    case BC250_SEGMENT_OK: return "ok";
    case BC250_SEGMENT_BAD_ARGUMENT: return "bad_argument";
    case BC250_SEGMENT_INVALID_SIZE: return "invalid_size";
    case BC250_SEGMENT_INVALID_ALIGNMENT: return "invalid_alignment";
    case BC250_SEGMENT_OUT_OF_MEMORY: return "out_of_memory";
    case BC250_SEGMENT_INVALID_SEGMENT: return "invalid_segment";
    default: return "unknown";
    }
}
