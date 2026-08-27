#ifndef BC250_DISCOVERY_TMR_READER_H
#define BC250_DISCOVERY_TMR_READER_H

#include "bc250_vram_topology_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_DRIVER_SCRATCH_0_BYTE_OFFSET 0x00000250u
#define BC250_DRIVER_SCRATCH_1_BYTE_OFFSET 0x00000254u
#define BC250_DRIVER_SCRATCH_2_BYTE_OFFSET 0x00000258u

typedef enum BC250_DISCOVERY_TMR_STATUS {
    BC250_DISCOVERY_TMR_OK = 0,
    BC250_DISCOVERY_TMR_BAD_ARGUMENT,
    BC250_DISCOVERY_TMR_MISSING_IDENTITY,
    BC250_DISCOVERY_TMR_CALLBACK_FAILED,
    BC250_DISCOVERY_TMR_UNSTABLE_SNAPSHOT,
    BC250_DISCOVERY_TMR_NOT_REPORTED,
    BC250_DISCOVERY_TMR_RANGE_OVERFLOW,
    BC250_DISCOVERY_TMR_OUT_OF_RANGE
} BC250_DISCOVERY_TMR_STATUS;

typedef struct BC250_DISCOVERY_TMR_EVIDENCE {
    uint32_t ScratchOffsetLow;
    uint32_t ScratchOffsetHigh;
    uint32_t ScratchSize;
    uint32_t SnapshotGeneration;
    uint32_t ReadCount;
    uint32_t StablePasses;
    uint64_t Offset;
    uint64_t Length;
} BC250_DISCOVERY_TMR_EVIDENCE;

BC250_DISCOVERY_TMR_STATUS Bc250CaptureDiscoveryTmrReadonly(
    const BC250_VRAM_TOPOLOGY_READER_INTERFACE *Interface,
    uint64_t VramTotalBytes,
    uint32_t SnapshotGeneration,
    BC250_DISCOVERY_TMR_EVIDENCE *Evidence);

const char *Bc250DiscoveryTmrStatusName(BC250_DISCOVERY_TMR_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
