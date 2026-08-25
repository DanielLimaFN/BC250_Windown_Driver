#ifndef BC250_RUNTIME_INVENTORY_H
#define BC250_RUNTIME_INVENTORY_H

#include "bc250_owned_adapter_contract.h"
#include "bc250_discovery_tmr_reader.h"
#include "bc250_staging_lease.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum BC250_RUNTIME_INVENTORY_STATUS {
    BC250_RUNTIME_INVENTORY_OK = 0,
    BC250_RUNTIME_INVENTORY_BAD_ARGUMENT,
    BC250_RUNTIME_INVENTORY_SCANOUT_INVALID,
    BC250_RUNTIME_INVENTORY_DISCOVERY_TMR_INVALID,
    BC250_RUNTIME_INVENTORY_CARVEOUT_INVALID,
    BC250_RUNTIME_INVENTORY_OVERLAP,
    BC250_RUNTIME_INVENTORY_OUT_OF_RANGE,
    BC250_RUNTIME_INVENTORY_TABLE_FULL
} BC250_RUNTIME_INVENTORY_STATUS;

typedef struct BC250_RUNTIME_INVENTORY_INPUT {
    uint32_t HasScanout;
    BC250_INHERITED_VRAM_RESERVATION ScanoutReservation;
    uint32_t HasDiscoveryTmr;
    BC250_DISCOVERY_TMR_EVIDENCE DiscoveryTmrEvidence;
    uint32_t PlatformCarveoutCount;
    BC250_INHERITED_VRAM_RESERVATION PlatformCarveouts[BC250_VRAM_MAX_REGIONS];
} BC250_RUNTIME_INVENTORY_INPUT;

typedef struct BC250_RUNTIME_INVENTORY_REPORT {
    uint32_t SnapshotGeneration;
    uint32_t ReservationCount;
    BC250_INHERITED_VRAM_RESERVATION Reservations[BC250_VRAM_MAX_REGIONS];
    uint64_t VramTotalBytes;
    uint64_t CpuVisibleBytes;
    uint64_t TotalReservedBytes;
    uint64_t TotalFreeBytes;
    uint64_t CpuVisibleReservedBytes;
    uint64_t CpuVisibleFreeBytes;
    uint32_t OwnershipEvidenceFlags;
    uint32_t StagingEvidenceFlags;
} BC250_RUNTIME_INVENTORY_REPORT;

BC250_RUNTIME_INVENTORY_STATUS Bc250BuildRuntimeVramInventory(
    const BC250_RUNTIME_INVENTORY_INPUT *Input,
    uint64_t VramTotalBytes,
    uint64_t CpuVisibleBytes,
    uint32_t SnapshotGeneration,
    BC250_RUNTIME_INVENTORY_REPORT *Report);

const char *Bc250RuntimeInventoryStatusName(
    BC250_RUNTIME_INVENTORY_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
