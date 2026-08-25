#include "bc250_vram_topology_contract.h"

#include <string.h>

#define BC250_MIB_SHIFT 20u
#define BC250_GCMC_FIELD_SHIFT 24u
#define BC250_GCMC_FIELD_BYTES (1ull << BC250_GCMC_FIELD_SHIFT)

BC250_VRAM_TOPOLOGY_STATUS Bc250DecodeVramTopology(
    const BC250_VRAM_TOPOLOGY_SNAPSHOT *Snapshot,
    BC250_VRAM_ADDRESS_SPACE *AddressSpace)
{
    BC250_VRAM_ADDRESS_SPACE candidate;
    uint64_t topExclusive;
    uint64_t rangeBytes;

    if (AddressSpace != 0) {
        memset(AddressSpace, 0, sizeof(*AddressSpace));
    }
    if (Snapshot == 0 || AddressSpace == 0 ||
        Snapshot->SnapshotGeneration == 0) {
        return BC250_VRAM_TOPOLOGY_BAD_ARGUMENT;
    }
    if ((Snapshot->EvidenceFlags & BC250_TOPOLOGY_EVIDENCE_REQUIRED) !=
        BC250_TOPOLOGY_EVIDENCE_REQUIRED) {
        return BC250_VRAM_TOPOLOGY_MISSING_EVIDENCE;
    }
    if (Snapshot->RccConfigMemsizeMiB == 0 ||
        Snapshot->RccConfigMemsizeMiB == 0xFFFFFFFFu) {
        return BC250_VRAM_TOPOLOGY_INVALID_MEMSIZE;
    }
    if ((Snapshot->GcmcVmFbLocationBase & ~BC250_GCMC_FB_FIELD_MASK) != 0 ||
        (Snapshot->GcmcVmFbLocationTop & ~BC250_GCMC_FB_FIELD_MASK) != 0 ||
        (Snapshot->GcmcVmFbOffset & ~BC250_GCMC_FB_FIELD_MASK) != 0) {
        return BC250_VRAM_TOPOLOGY_RESERVED_BITS_SET;
    }
    if (Snapshot->GcmcVmFbLocationTop <
        Snapshot->GcmcVmFbLocationBase) {
        return BC250_VRAM_TOPOLOGY_REVERSED_RANGE;
    }

    memset(&candidate, 0, sizeof(candidate));
    candidate.TotalBytes =
        (uint64_t)Snapshot->RccConfigMemsizeMiB << BC250_MIB_SHIFT;
    candidate.VramMcStart =
        (uint64_t)Snapshot->GcmcVmFbLocationBase <<
        BC250_GCMC_FIELD_SHIFT;
    topExclusive =
        ((uint64_t)Snapshot->GcmcVmFbLocationTop + 1ull) <<
        BC250_GCMC_FIELD_SHIFT;
    candidate.VramMcEnd = topExclusive - 1ull;
    rangeBytes = topExclusive - candidate.VramMcStart;
    if (rangeBytes != candidate.TotalBytes) {
        return BC250_VRAM_TOPOLOGY_SIZE_MISMATCH;
    }
    if (Snapshot->CpuVisibleBytes == 0 ||
        Snapshot->CpuVisibleBytes > candidate.TotalBytes ||
        (Snapshot->CpuVisibleBytes & (BC250_GCMC_FIELD_BYTES - 1ull)) != 0) {
        return BC250_VRAM_TOPOLOGY_CPU_WINDOW_MISMATCH;
    }

    candidate.CpuVisibleBytes = Snapshot->CpuVisibleBytes;
    candidate.VramBaseOffset =
        (uint64_t)Snapshot->GcmcVmFbOffset << BC250_GCMC_FIELD_SHIFT;
    candidate.EvidenceFlags = BC250_ADDR_EVIDENCE_REQUIRED;
    candidate.SnapshotGeneration = Snapshot->SnapshotGeneration;
    *AddressSpace = candidate;
    return BC250_VRAM_TOPOLOGY_OK;
}

const char *Bc250VramTopologyStatusName(BC250_VRAM_TOPOLOGY_STATUS Status)
{
    switch (Status) {
    case BC250_VRAM_TOPOLOGY_OK: return "ok";
    case BC250_VRAM_TOPOLOGY_BAD_ARGUMENT: return "bad_argument";
    case BC250_VRAM_TOPOLOGY_MISSING_EVIDENCE: return "missing_evidence";
    case BC250_VRAM_TOPOLOGY_INVALID_MEMSIZE: return "invalid_memsize";
    case BC250_VRAM_TOPOLOGY_RESERVED_BITS_SET: return "reserved_bits_set";
    case BC250_VRAM_TOPOLOGY_REVERSED_RANGE: return "reversed_range";
    case BC250_VRAM_TOPOLOGY_SIZE_MISMATCH: return "size_mismatch";
    case BC250_VRAM_TOPOLOGY_CPU_WINDOW_MISMATCH:
        return "cpu_window_mismatch";
    default: return "unknown";
    }
}
