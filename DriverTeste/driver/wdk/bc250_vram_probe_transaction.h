#ifndef BC250_VRAM_PROBE_TRANSACTION_H
#define BC250_VRAM_PROBE_TRANSACTION_H

#include <ntddk.h>
#include <dispmprt.h>
#include "../core/bc250_resource_contract.h"

#define BC250_VRAM_PROBE_BYTES 64u

typedef enum BC250_VRAM_PROBE_STATUS {
    BC250_VRAM_PROBE_OK = 0,
    BC250_VRAM_PROBE_BAD_ARGUMENT,
    BC250_VRAM_PROBE_APERTURE_NOT_UNIQUE,
    BC250_VRAM_PROBE_OUT_OF_RANGE,
    BC250_VRAM_PROBE_MAP_FAILED,
    BC250_VRAM_PROBE_MAP_RETURNED_NULL,
    BC250_VRAM_PROBE_WRITE_READBACK_FAILED,
    BC250_VRAM_PROBE_RESTORE_FAILED,
    BC250_VRAM_PROBE_UNMAP_FAILED
} BC250_VRAM_PROBE_STATUS;

typedef struct BC250_VRAM_PROBE_EVIDENCE {
    uint64_t Offset;
    uint32_t Bytes;
    uint32_t WriteReadbackPassed;
    uint32_t RestorePassed;
    NTSTATUS MapStatus;
    NTSTATUS UnmapStatus;
} BC250_VRAM_PROBE_EVIDENCE;

BC250_VRAM_PROBE_STATUS Bc250RunVramProbeTransaction(
    const DXGKRNL_INTERFACE *DxgkInterface,
    const BC250_RESOLVED_BAR_RESOURCE *ResolvedBars,
    size_t ResolvedBarCount,
    uint32_t VramApertureBarIndex,
    uint64_t Offset,
    BC250_VRAM_PROBE_EVIDENCE *Evidence);

#endif
