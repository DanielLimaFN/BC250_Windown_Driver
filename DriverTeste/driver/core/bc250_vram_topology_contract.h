#ifndef BC250_VRAM_TOPOLOGY_CONTRACT_H
#define BC250_VRAM_TOPOLOGY_CONTRACT_H

#include "bc250_address_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_TOPOLOGY_EVIDENCE_RCC_MEMSIZE_RUNTIME 0x00000001u
#define BC250_TOPOLOGY_EVIDENCE_FB_BASE_RUNTIME     0x00000002u
#define BC250_TOPOLOGY_EVIDENCE_FB_TOP_RUNTIME      0x00000004u
#define BC250_TOPOLOGY_EVIDENCE_FB_OFFSET_RUNTIME   0x00000008u
#define BC250_TOPOLOGY_EVIDENCE_CPU_WINDOW_RUNTIME  0x00000010u
#define BC250_TOPOLOGY_EVIDENCE_SAME_SNAPSHOT       0x00000020u
#define BC250_TOPOLOGY_EVIDENCE_GC_10_1_3           0x00000040u
#define BC250_TOPOLOGY_EVIDENCE_REQUIRED            0x0000007Fu

#define BC250_GCMC_FB_FIELD_MASK 0x00FFFFFFu

typedef enum BC250_VRAM_TOPOLOGY_STATUS {
    BC250_VRAM_TOPOLOGY_OK = 0,
    BC250_VRAM_TOPOLOGY_BAD_ARGUMENT,
    BC250_VRAM_TOPOLOGY_MISSING_EVIDENCE,
    BC250_VRAM_TOPOLOGY_INVALID_MEMSIZE,
    BC250_VRAM_TOPOLOGY_RESERVED_BITS_SET,
    BC250_VRAM_TOPOLOGY_REVERSED_RANGE,
    BC250_VRAM_TOPOLOGY_SIZE_MISMATCH,
    BC250_VRAM_TOPOLOGY_CPU_WINDOW_MISMATCH
} BC250_VRAM_TOPOLOGY_STATUS;

/*
 * Typed, already captured register evidence.  This contract performs no MMIO
 * access.  RCC_CONFIG_MEMSIZE is expressed in MiB by the hardware; the three
 * GCMC fields are 24-bit values expressed in 16-MiB units.
 */
typedef struct BC250_VRAM_TOPOLOGY_SNAPSHOT {
    uint32_t RccConfigMemsizeMiB;
    uint32_t GcmcVmFbLocationBase;
    uint32_t GcmcVmFbLocationTop;
    uint32_t GcmcVmFbOffset;
    uint64_t CpuVisibleBytes;
    uint32_t EvidenceFlags;
    uint32_t SnapshotGeneration;
} BC250_VRAM_TOPOLOGY_SNAPSHOT;

BC250_VRAM_TOPOLOGY_STATUS Bc250DecodeVramTopology(
    const BC250_VRAM_TOPOLOGY_SNAPSHOT *Snapshot,
    BC250_VRAM_ADDRESS_SPACE *AddressSpace);

const char *Bc250VramTopologyStatusName(BC250_VRAM_TOPOLOGY_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
