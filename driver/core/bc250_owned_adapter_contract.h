#ifndef BC250_OWNED_ADAPTER_CONTRACT_H
#define BC250_OWNED_ADAPTER_CONTRACT_H

#include "bc250_compute_bootstrap.h"
#include "bc250_resource_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_OWNERSHIP_TRANSLATED_RESOURCES 0x00000001u
#define BC250_OWNERSHIP_PCI_BAR_IDENTITY     0x00000002u
#define BC250_OWNERSHIP_VRAM_TOTAL_RUNTIME   0x00000004u
#define BC250_OWNERSHIP_MC_RANGE_RUNTIME     0x00000008u
#define BC250_OWNERSHIP_FB_OFFSET_RUNTIME    0x00000010u
#define BC250_OWNERSHIP_EXCLUSIVE_ADAPTER    0x00000020u
#define BC250_OWNERSHIP_INVENTORY_COMPLETE   0x00000040u
#define BC250_OWNERSHIP_REQUIRED             0x0000007Fu

typedef enum BC250_OWNED_ADAPTER_STATUS {
    BC250_OWNED_ADAPTER_OK = 0,
    BC250_OWNED_ADAPTER_BAD_ARGUMENT,
    BC250_OWNED_ADAPTER_MISSING_EVIDENCE,
    BC250_OWNED_ADAPTER_APERTURE_NOT_UNIQUE,
    BC250_OWNED_ADAPTER_TOPOLOGY_REJECTED,
    BC250_OWNED_ADAPTER_RESERVATION_REJECTED,
    BC250_OWNED_ADAPTER_BOOTSTRAP_REJECTED
} BC250_OWNED_ADAPTER_STATUS;

typedef struct BC250_OWNED_ADAPTER_INPUT {
    const BC250_RESOLVED_BAR_RESOURCE *ResolvedBars;
    size_t ResolvedBarCount;
    uint32_t VramApertureBarIndex;
    uint32_t OwnershipEvidenceFlags;
    uint64_t VramTotalBytes;
    uint64_t VramMcStart;
    uint64_t VramMcEnd;
    uint64_t VramBaseOffset;
    uint32_t SnapshotGeneration;
    uint32_t Reserved;
} BC250_OWNED_ADAPTER_INPUT;

typedef struct BC250_INHERITED_VRAM_RESERVATION {
    uint64_t Offset;
    uint64_t Length;
    uint64_t Alignment;
    uint32_t Role;
    uint32_t Flags;
} BC250_INHERITED_VRAM_RESERVATION;

typedef struct BC250_OWNED_ADAPTER_BOOTSTRAP {
    BC250_CP_BOOTSTRAP Bootstrap;
    uint64_t CpuAperturePhysicalStart;
    uint64_t CpuApertureBytes;
    uint32_t OwnershipEvidenceFlags;
    uint32_t InheritedReservationCount;
} BC250_OWNED_ADAPTER_BOOTSTRAP;

BC250_OWNED_ADAPTER_STATUS Bc250BuildOwnedAdapterBootstrap(
    const BC250_OWNED_ADAPTER_INPUT *Input,
    const BC250_INHERITED_VRAM_RESERVATION *Reservations,
    size_t ReservationCount,
    BC250_OWNED_ADAPTER_BOOTSTRAP *Owned,
    BC250_CP_BOOTSTRAP_STATUS *BootstrapStatus,
    BC250_VRAM_STATUS *VramStatus);

const char *Bc250OwnedAdapterStatusName(BC250_OWNED_ADAPTER_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
