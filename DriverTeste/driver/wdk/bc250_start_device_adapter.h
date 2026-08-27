#ifndef BC250_START_DEVICE_ADAPTER_H
#define BC250_START_DEVICE_ADAPTER_H

#include <ntddk.h>

#include "bc250_cm_resource_adapter.h"
#include "../core/bc250_owned_adapter_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_START_RUNTIME_EVIDENCE_REQUIRED \
    (BC250_OWNERSHIP_VRAM_TOTAL_RUNTIME | \
     BC250_OWNERSHIP_MC_RANGE_RUNTIME | \
     BC250_OWNERSHIP_FB_OFFSET_RUNTIME | \
     BC250_OWNERSHIP_EXCLUSIVE_ADAPTER | \
     BC250_OWNERSHIP_INVENTORY_COMPLETE)

typedef enum BC250_START_DEVICE_STATE {
    BC250_START_DEVICE_EMPTY = 0,
    BC250_START_DEVICE_OWNED_SEALED,
    BC250_START_DEVICE_PREFLIGHT_READY
} BC250_START_DEVICE_STATE;

typedef enum BC250_START_DEVICE_STATUS {
    BC250_START_DEVICE_OK = 0,
    BC250_START_DEVICE_BAD_ARGUMENT,
    BC250_START_DEVICE_WRONG_STATE,
    BC250_START_DEVICE_MISSING_RUNTIME_EVIDENCE,
    BC250_START_DEVICE_RESOURCES_REJECTED,
    BC250_START_DEVICE_OWNERSHIP_REJECTED,
    BC250_START_DEVICE_PREFLIGHT_REJECTED
} BC250_START_DEVICE_STATUS;

typedef struct BC250_START_DEVICE_TOPOLOGY {
    uint32_t VramApertureBarIndex;
    uint32_t RuntimeEvidenceFlags;
    uint64_t VramTotalBytes;
    uint64_t VramMcStart;
    uint64_t VramMcEnd;
    uint64_t VramBaseOffset;
    uint32_t SnapshotGeneration;
    uint32_t Reserved;
} BC250_START_DEVICE_TOPOLOGY;

typedef struct BC250_START_DEVICE_CONTEXT {
    uint32_t State;
    uint32_t ResolvedBarCount;
    BC250_RESOLVED_BAR_RESOURCE ResolvedBars[BC250_RESOURCE_MAX_BARS];
    BC250_OWNED_ADAPTER_BOOTSTRAP Owned;
    BC250_CP_BOOTSTRAP_REPORT Preflight;
} BC250_START_DEVICE_CONTEXT;

BC250_START_DEVICE_STATUS Bc250StartDeviceInitialize(
    _Out_ BC250_START_DEVICE_CONTEXT *Context);

BC250_START_DEVICE_STATUS Bc250StartDevicePrepareOwnership(
    _Inout_ BC250_START_DEVICE_CONTEXT *Context,
    _In_ const CM_RESOURCE_LIST *TranslatedResourceList,
    _In_reads_(BarCount) const BC250_PCI_BAR_RESOURCE *Bars,
    _In_ size_t BarCount,
    _In_ const BC250_START_DEVICE_TOPOLOGY *Topology,
    _In_reads_opt_(ReservationCount)
        const BC250_INHERITED_VRAM_RESERVATION *Reservations,
    _In_ size_t ReservationCount,
    _Out_opt_ BC250_CM_RESOURCE_STATUS *CmStatus,
    _Out_opt_ BC250_RESOURCE_STATUS *ResourceStatus,
    _Out_opt_ BC250_OWNED_ADAPTER_STATUS *OwnedStatus,
    _Out_opt_ BC250_CP_BOOTSTRAP_STATUS *BootstrapStatus,
    _Out_opt_ BC250_VRAM_STATUS *VramStatus);

BC250_START_DEVICE_STATUS Bc250StartDeviceBuildReadonlyPreflight(
    _Inout_ BC250_START_DEVICE_CONTEXT *Context,
    _In_ const BC250_CP_BOOTSTRAP_POLICY *Policy,
    _In_reads_(ManifestCount) const BC250_CP_FIRMWARE_ENTRY *Manifest,
    _In_ uint32_t ManifestCount,
    _Out_opt_ BC250_VRAM_STATUS *VramStatus,
    _Out_opt_ BC250_ADDRESS_STATUS *AddressStatus,
    _Out_opt_ BC250_PSP_CONTRACT_STATUS *PspStatus,
    _Out_opt_ BC250_CP_BOOTSTRAP_STATUS *BootstrapStatus);

BC250_START_DEVICE_STATUS Bc250StartDeviceTeardown(
    _Inout_ BC250_START_DEVICE_CONTEXT *Context);

const char *Bc250StartDeviceStatusName(BC250_START_DEVICE_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
