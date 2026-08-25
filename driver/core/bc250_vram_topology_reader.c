#include "bc250_vram_topology_reader.h"

#include <string.h>

#define BC250_TOPOLOGY_REGISTER_COUNT 4u
#define BC250_TOPOLOGY_CAPTURE_PASSES 2u

static const uint32_t Bc250TopologyOffsets[BC250_TOPOLOGY_REGISTER_COUNT] = {
    BC250_RCC_CONFIG_MEMSIZE_BYTE_OFFSET,
    BC250_GCMC_VM_FB_LOCATION_BASE_BYTE_OFFSET,
    BC250_GCMC_VM_FB_LOCATION_TOP_BYTE_OFFSET,
    BC250_GCMC_VM_FB_OFFSET_BYTE_OFFSET
};

BC250_VRAM_TOPOLOGY_READER_STATUS Bc250CaptureVramTopologyReadonly(
    const BC250_VRAM_TOPOLOGY_READER_INTERFACE *Interface,
    uint64_t CpuVisibleBytes,
    uint32_t SnapshotGeneration,
    BC250_VRAM_TOPOLOGY_READER_EVIDENCE *Evidence)
{
    uint32_t values[BC250_TOPOLOGY_CAPTURE_PASSES]
        [BC250_TOPOLOGY_REGISTER_COUNT];
    BC250_VRAM_TOPOLOGY_READER_EVIDENCE candidate;
    uint32_t pass;
    uint32_t index;

    if (Evidence != 0) memset(Evidence, 0, sizeof(*Evidence));
    if (Interface == 0 || Interface->Read32 == 0 || Evidence == 0 ||
        CpuVisibleBytes == 0 || SnapshotGeneration == 0) {
        return BC250_VRAM_TOPOLOGY_READER_BAD_ARGUMENT;
    }
    if ((Interface->IdentityFlags &
         BC250_TOPOLOGY_READER_IDENTITY_REQUIRED) !=
        BC250_TOPOLOGY_READER_IDENTITY_REQUIRED) {
        return BC250_VRAM_TOPOLOGY_READER_MISSING_IDENTITY;
    }

    memset(&candidate, 0, sizeof(candidate));
    memset(values, 0, sizeof(values));
    for (pass = 0; pass < BC250_TOPOLOGY_CAPTURE_PASSES; ++pass) {
        for (index = 0; index < BC250_TOPOLOGY_REGISTER_COUNT; ++index) {
            ++candidate.ReadCount;
            if (Interface->Read32(
                    Interface->Context, Bc250TopologyOffsets[index],
                    &values[pass][index]) == 0) {
                return BC250_VRAM_TOPOLOGY_READER_CALLBACK_FAILED;
            }
        }
        ++candidate.StablePasses;
    }
    for (index = 0; index < BC250_TOPOLOGY_REGISTER_COUNT; ++index) {
        if (values[0][index] != values[1][index]) {
            return BC250_VRAM_TOPOLOGY_READER_UNSTABLE_SNAPSHOT;
        }
    }

    candidate.Snapshot.RccConfigMemsizeMiB = values[0][0];
    candidate.Snapshot.GcmcVmFbLocationBase = values[0][1];
    candidate.Snapshot.GcmcVmFbLocationTop = values[0][2];
    candidate.Snapshot.GcmcVmFbOffset = values[0][3];
    candidate.Snapshot.CpuVisibleBytes = CpuVisibleBytes;
    candidate.Snapshot.EvidenceFlags = BC250_TOPOLOGY_EVIDENCE_REQUIRED;
    candidate.Snapshot.SnapshotGeneration = SnapshotGeneration;
    candidate.TopologyStatus = Bc250DecodeVramTopology(
        &candidate.Snapshot, &candidate.AddressSpace);
    if (candidate.TopologyStatus != BC250_VRAM_TOPOLOGY_OK) {
        return BC250_VRAM_TOPOLOGY_READER_TOPOLOGY_REJECTED;
    }
    *Evidence = candidate;
    return BC250_VRAM_TOPOLOGY_READER_OK;
}

const char *Bc250VramTopologyReaderStatusName(
    BC250_VRAM_TOPOLOGY_READER_STATUS Status)
{
    switch (Status) {
    case BC250_VRAM_TOPOLOGY_READER_OK: return "ok";
    case BC250_VRAM_TOPOLOGY_READER_BAD_ARGUMENT: return "bad_argument";
    case BC250_VRAM_TOPOLOGY_READER_MISSING_IDENTITY:
        return "missing_identity";
    case BC250_VRAM_TOPOLOGY_READER_CALLBACK_FAILED:
        return "callback_failed";
    case BC250_VRAM_TOPOLOGY_READER_UNSTABLE_SNAPSHOT:
        return "unstable_snapshot";
    case BC250_VRAM_TOPOLOGY_READER_TOPOLOGY_REJECTED:
        return "topology_rejected";
    default: return "unknown";
    }
}
