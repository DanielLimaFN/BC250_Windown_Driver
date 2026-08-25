#ifndef BC250_ADDRESS_CONTRACT_H
#define BC250_ADDRESS_CONTRACT_H

#include "bc250_vram_allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_ADDR_EVIDENCE_TOTAL_RUNTIME       0x00000001u
#define BC250_ADDR_EVIDENCE_MC_RANGE_RUNTIME    0x00000002u
#define BC250_ADDR_EVIDENCE_FB_OFFSET_RUNTIME   0x00000004u
#define BC250_ADDR_EVIDENCE_CPU_WINDOW_RUNTIME  0x00000008u
#define BC250_ADDR_EVIDENCE_REQUIRED            0x0000000Fu

#define BC250_PSP_TMR_VIRT_PHY_ADDR 0x00000002u

typedef enum BC250_ADDRESS_STATUS {
    BC250_ADDRESS_OK = 0,
    BC250_ADDRESS_BAD_ARGUMENT,
    BC250_ADDRESS_MISSING_EVIDENCE,
    BC250_ADDRESS_TOPOLOGY_MISMATCH,
    BC250_ADDRESS_RANGE_OVERFLOW,
    BC250_ADDRESS_STALE_TOKEN,
    BC250_ADDRESS_WRONG_ROLE,
    BC250_ADDRESS_NOT_PERSISTENT,
    BC250_ADDRESS_SIZE_MISMATCH,
    BC250_ADDRESS_MISALIGNED
} BC250_ADDRESS_STATUS;

typedef struct BC250_VRAM_ADDRESS_SPACE {
    uint64_t TotalBytes;
    uint64_t CpuVisibleBytes;
    uint64_t VramMcStart;
    uint64_t VramMcEnd;
    uint64_t VramBaseOffset;
    uint32_t EvidenceFlags;
    uint32_t SnapshotGeneration;
} BC250_VRAM_ADDRESS_SPACE;

typedef struct BC250_VRAM_ADDRESS_BINDING {
    BC250_VRAM_TOKEN Token;
    uint64_t Offset;
    uint64_t Length;
    uint64_t McAddress;
    uint64_t SystemPhysicalAddress;
    uint64_t Alignment;
    uint32_t Role;
    uint32_t RegionFlags;
    uint32_t CpuVisible;
    uint32_t NaturallyAligned;
    uint32_t SnapshotGeneration;
    uint32_t Reserved;
} BC250_VRAM_ADDRESS_BINDING;

typedef struct BC250_PSP_TMR_PLAN {
    BC250_VRAM_ADDRESS_BINDING Binding;
    uint32_t BufferSize;
    uint32_t TmrFlags;
    uint64_t RequiredAlignment;
} BC250_PSP_TMR_PLAN;

BC250_ADDRESS_STATUS
Bc250BindVramRegion(
    const BC250_VRAM_ALLOCATOR *Allocator,
    BC250_VRAM_TOKEN Token,
    const BC250_VRAM_ADDRESS_SPACE *AddressSpace,
    BC250_VRAM_ADDRESS_BINDING *Binding);

BC250_ADDRESS_STATUS
Bc250BuildPspTmrPlan(
    const BC250_VRAM_ALLOCATOR *Allocator,
    BC250_VRAM_TOKEN Token,
    const BC250_VRAM_ADDRESS_SPACE *AddressSpace,
    uint64_t RequiredSize,
    uint64_t RequiredAlignment,
    BC250_PSP_TMR_PLAN *Plan);

const char *Bc250AddressStatusName(BC250_ADDRESS_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
