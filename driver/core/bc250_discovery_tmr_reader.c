#include "bc250_discovery_tmr_reader.h"

#include <string.h>

#define BC250_DISCOVERY_TMR_REGISTER_COUNT 3u
#define BC250_DISCOVERY_TMR_CAPTURE_PASSES 2u
#define BC250_UINT64_MAX_VALUE ((uint64_t)~(uint64_t)0)

static const uint32_t Bc250DiscoveryTmrOffsets[
    BC250_DISCOVERY_TMR_REGISTER_COUNT] = {
    BC250_DRIVER_SCRATCH_0_BYTE_OFFSET,
    BC250_DRIVER_SCRATCH_1_BYTE_OFFSET,
    BC250_DRIVER_SCRATCH_2_BYTE_OFFSET
};

BC250_DISCOVERY_TMR_STATUS Bc250CaptureDiscoveryTmrReadonly(
    const BC250_VRAM_TOPOLOGY_READER_INTERFACE *Interface,
    uint64_t VramTotalBytes,
    uint32_t SnapshotGeneration,
    BC250_DISCOVERY_TMR_EVIDENCE *Evidence)
{
    uint32_t values[BC250_DISCOVERY_TMR_CAPTURE_PASSES]
        [BC250_DISCOVERY_TMR_REGISTER_COUNT];
    BC250_DISCOVERY_TMR_EVIDENCE candidate;
    uint64_t offset;
    uint64_t length;
    uint32_t pass;
    uint32_t index;

    if (Evidence != NULL) memset(Evidence, 0, sizeof(*Evidence));
    if (Interface == NULL || Interface->Read32 == NULL || Evidence == NULL ||
        VramTotalBytes == 0 || SnapshotGeneration == 0) {
        return BC250_DISCOVERY_TMR_BAD_ARGUMENT;
    }
    if ((Interface->IdentityFlags &
         BC250_TOPOLOGY_READER_IDENTITY_REQUIRED) !=
        BC250_TOPOLOGY_READER_IDENTITY_REQUIRED) {
        return BC250_DISCOVERY_TMR_MISSING_IDENTITY;
    }

    memset(values, 0, sizeof(values));
    memset(&candidate, 0, sizeof(candidate));
    for (pass = 0; pass < BC250_DISCOVERY_TMR_CAPTURE_PASSES; ++pass) {
        for (index = 0; index < BC250_DISCOVERY_TMR_REGISTER_COUNT; ++index) {
            ++candidate.ReadCount;
            if (Interface->Read32(
                    Interface->Context, Bc250DiscoveryTmrOffsets[index],
                    &values[pass][index]) == 0) {
                return BC250_DISCOVERY_TMR_CALLBACK_FAILED;
            }
        }
        ++candidate.StablePasses;
    }
    for (index = 0; index < BC250_DISCOVERY_TMR_REGISTER_COUNT; ++index) {
        if (values[0][index] != values[1][index]) {
            return BC250_DISCOVERY_TMR_UNSTABLE_SNAPSHOT;
        }
    }
    if (values[0][2] == 0) return BC250_DISCOVERY_TMR_NOT_REPORTED;

    offset = ((uint64_t)values[0][1] << 32) | values[0][0];
    length = values[0][2];
    if (offset > BC250_UINT64_MAX_VALUE - (length - 1u)) {
        return BC250_DISCOVERY_TMR_RANGE_OVERFLOW;
    }
    if (offset >= VramTotalBytes || length > VramTotalBytes - offset) {
        return BC250_DISCOVERY_TMR_OUT_OF_RANGE;
    }

    candidate.ScratchOffsetLow = values[0][0];
    candidate.ScratchOffsetHigh = values[0][1];
    candidate.ScratchSize = values[0][2];
    candidate.SnapshotGeneration = SnapshotGeneration;
    candidate.Offset = offset;
    candidate.Length = length;
    *Evidence = candidate;
    return BC250_DISCOVERY_TMR_OK;
}

const char *Bc250DiscoveryTmrStatusName(BC250_DISCOVERY_TMR_STATUS Status)
{
    switch (Status) {
    case BC250_DISCOVERY_TMR_OK: return "ok";
    case BC250_DISCOVERY_TMR_BAD_ARGUMENT: return "bad_argument";
    case BC250_DISCOVERY_TMR_MISSING_IDENTITY: return "missing_identity";
    case BC250_DISCOVERY_TMR_CALLBACK_FAILED: return "callback_failed";
    case BC250_DISCOVERY_TMR_UNSTABLE_SNAPSHOT:
        return "unstable_snapshot";
    case BC250_DISCOVERY_TMR_NOT_REPORTED: return "not_reported";
    case BC250_DISCOVERY_TMR_RANGE_OVERFLOW: return "range_overflow";
    case BC250_DISCOVERY_TMR_OUT_OF_RANGE: return "out_of_range";
    default: return "unknown";
    }
}
