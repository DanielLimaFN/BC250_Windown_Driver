#ifndef BC250_COMPUTE_BOOTSTRAP_H
#define BC250_COMPUTE_BOOTSTRAP_H

#include "bc250_psp_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_CP_BOOTSTRAP_FIRMWARE_COUNT 5u

typedef enum BC250_CP_BOOTSTRAP_STATE {
    BC250_CP_BOOTSTRAP_EMPTY = 0,
    BC250_CP_BOOTSTRAP_INVENTORY_OPEN,
    BC250_CP_BOOTSTRAP_INVENTORY_SEALED,
    BC250_CP_BOOTSTRAP_PREFLIGHT_READY
} BC250_CP_BOOTSTRAP_STATE;

typedef enum BC250_CP_BOOTSTRAP_STATUS {
    BC250_CP_BOOTSTRAP_OK = 0,
    BC250_CP_BOOTSTRAP_BAD_ARGUMENT,
    BC250_CP_BOOTSTRAP_WRONG_STATE,
    BC250_CP_BOOTSTRAP_INVENTORY_REJECTED,
    BC250_CP_BOOTSTRAP_SNAPSHOT_REJECTED,
    BC250_CP_BOOTSTRAP_INVENTORY_CHANGED,
    BC250_CP_BOOTSTRAP_SNAPSHOT_CHANGED,
    BC250_CP_BOOTSTRAP_MANIFEST_REJECTED,
    BC250_CP_BOOTSTRAP_ALLOCATION_REJECTED,
    BC250_CP_BOOTSTRAP_PAYLOAD_REJECTED
} BC250_CP_BOOTSTRAP_STATUS;

typedef struct BC250_CP_FIRMWARE_ENTRY {
    uint32_t FirmwareType;
    uint32_t PayloadBytes;
} BC250_CP_FIRMWARE_ENTRY;

typedef struct BC250_CP_BOOTSTRAP_POLICY {
    uint64_t TmrBytes;
    uint64_t TmrAlignment;
    uint64_t FirmwareAlignment;
} BC250_CP_BOOTSTRAP_POLICY;

typedef struct BC250_CP_BOOTSTRAP_REPORT {
    uint32_t SnapshotGeneration;
    uint32_t FirmwareCount;
    BC250_VRAM_TOKEN TmrToken;
    BC250_VRAM_TOKEN FirmwareStagingToken;
    BC250_PSP_TMR_PLAN TmrPlan;
    BC250_PSP_SETUP_TMR_PAYLOAD TmrPayload;
    BC250_PSP_LOAD_FW_PLAN FirmwarePlans[BC250_CP_BOOTSTRAP_FIRMWARE_COUNT];
    BC250_PSP_LOAD_FW_PAYLOAD FirmwarePayloads[BC250_CP_BOOTSTRAP_FIRMWARE_COUNT];
} BC250_CP_BOOTSTRAP_REPORT;

typedef struct BC250_CP_BOOTSTRAP {
    BC250_VRAM_ALLOCATOR Allocator;
    BC250_VRAM_ADDRESS_SPACE AddressSpace;
    uint32_t State;
    uint32_t SealedRegionCount;
    uint32_t SealedNextGeneration;
    uint32_t Reserved;
    uint64_t InventorySeal;
    uint64_t SnapshotSeal;
    uint64_t PreflightSeal;
} BC250_CP_BOOTSTRAP;

BC250_CP_BOOTSTRAP_STATUS Bc250CpBootstrapInit(
    BC250_CP_BOOTSTRAP *Bootstrap,
    uint64_t TotalBytes,
    uint64_t CpuVisibleBytes,
    BC250_VRAM_STATUS *VramStatus);

BC250_CP_BOOTSTRAP_STATUS Bc250CpBootstrapReserveInherited(
    BC250_CP_BOOTSTRAP *Bootstrap,
    uint64_t Offset,
    uint64_t Length,
    uint64_t Alignment,
    BC250_VRAM_ROLE Role,
    uint32_t Flags,
    BC250_VRAM_TOKEN *Token,
    BC250_VRAM_STATUS *VramStatus);

BC250_CP_BOOTSTRAP_STATUS Bc250CpBootstrapSealInventory(
    BC250_CP_BOOTSTRAP *Bootstrap,
    const BC250_VRAM_ADDRESS_SPACE *AddressSpace,
    BC250_VRAM_STATUS *VramStatus);

BC250_CP_BOOTSTRAP_STATUS Bc250CpBootstrapBuildPreflight(
    BC250_CP_BOOTSTRAP *Bootstrap,
    const BC250_CP_BOOTSTRAP_POLICY *Policy,
    const BC250_CP_FIRMWARE_ENTRY *Manifest,
    uint32_t ManifestCount,
    BC250_CP_BOOTSTRAP_REPORT *Report,
    BC250_VRAM_STATUS *VramStatus,
    BC250_ADDRESS_STATUS *AddressStatus,
    BC250_PSP_CONTRACT_STATUS *PspStatus);

BC250_CP_BOOTSTRAP_STATUS Bc250CpBootstrapValidatePreflight(
    const BC250_CP_BOOTSTRAP *Bootstrap,
    const BC250_CP_BOOTSTRAP_REPORT *Report,
    BC250_VRAM_STATUS *VramStatus);

const char *Bc250CpBootstrapStatusName(BC250_CP_BOOTSTRAP_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
