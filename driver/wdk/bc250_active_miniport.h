#ifndef BC250_ACTIVE_MINIPORT_H
#define BC250_ACTIVE_MINIPORT_H

#include <ntddk.h>
#include <dispmprt.h>

#include "bc250_dxgk_readonly_ddi.h"
#include "bc250_readonly_miniport.h"
#include "bc250_display_ddi.h"
#include "bc250_post_display_reservation.h"
#include "bc250_vram_probe_transaction.h"
#include "bc250_gcvm_backing_transaction.h"
#include "../core/bc250_mcdm_object_contract.h"
#include "../core/bc250_runtime_inventory.h"
#include "../core/bc250_gfxhub_tlb_transaction.h"
#include "../core/bc250_gcvm_vmid8_transaction.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_ACTIVE_SIGNATURE 0x4D434431u /* "MCD1" */
#define BC250_ACTIVE_POOL_TAG  'ADCB'

typedef struct BC250_DXGK_ACTIVE_CONTEXT {
    uint32_t Signature;
    PDEVICE_OBJECT PhysicalDeviceObject;
    BC250_DXGK_READONLY_CONTEXT Core;
    BC250_DISPLAY_STATE Display;
    BC250_POST_DISPLAY_STATUS PostDisplayStatus;
    NTSTATUS PostDisplayCallbackStatus;
    BC250_INHERITED_VRAM_RESERVATION PostDisplayReservation;
    BC250_POST_DISPLAY_EVIDENCE PostDisplayEvidence;
    BC250_RUNTIME_INVENTORY_STATUS RuntimeInventoryStatus;
    BC250_RUNTIME_INVENTORY_INPUT RuntimeInventoryInput;
    BC250_RUNTIME_INVENTORY_REPORT RuntimeInventoryReport;
    BC250_DXGK_READONLY_INPUT SealedRuntime;
    NTSTATUS RuntimeStageStatus;
    uint32_t SecondPreflightStage;
    BC250_VRAM_PROBE_STATUS VramProbeStatus;
    BC250_VRAM_PROBE_EVIDENCE VramProbeEvidence;
    BC250_TLB_TRANSACTION_STATUS TlbTransactionStatus;
    BC250_TLB_TRANSACTION_EVIDENCE TlbTransactionEvidence;
    BC250_GCVM_BACKING_STATUS GcvmBackingStatus;
    BC250_GCVM_BACKING_EVIDENCE GcvmBackingEvidence;
    BC250_GCVM_VMID8_STATUS GcvmVmid8Status;
    BC250_GCVM_VMID8_EVIDENCE GcvmVmid8Evidence;
    EX_PUSH_LOCK ObjectLock;
    BC250_MCDM_OBJECT_REGISTRY Objects;
    DXGK_CONTEXTINFO ContextAbi;
    volatile LONG ContextAbiReady;
    volatile LONG AllocationAbiReady;
    volatile LONG RootPageTableAbiReady;
    uint64_t AllocationSegmentBytes;
    uint32_t AllocationSegmentSet;
    uint32_t RootPageTableMaxEntries;
    volatile LONG IsActive;
    uint32_t SubmissionsCount;
    uint32_t ResetsCount;
} BC250_DXGK_ACTIVE_CONTEXT;

NTSTATUS Bc250BuildActiveInitializationData(
    DRIVER_INITIALIZATION_DATA *InitializationData);

NTSTATUS APIENTRY Bc250DxgkDdiAddDeviceActive(
    CONST PDEVICE_OBJECT PhysicalDeviceObject,
    PVOID *MiniportDeviceContext);

NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceActive(
    CONST PVOID MiniportDeviceContext,
    PDXGK_START_INFO DxgkStartInfo,
    PDXGKRNL_INTERFACE DxgkInterface,
    PULONG NumberOfVideoPresentSources,
    PULONG NumberOfChildren);

NTSTATUS APIENTRY Bc250DxgkDdiStopDeviceActive(
    CONST PVOID MiniportDeviceContext);

NTSTATUS APIENTRY Bc250DxgkDdiRemoveDeviceActive(
    CONST PVOID MiniportDeviceContext);

NTSTATUS Bc250ActiveCreateProcessObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    const void *ExternalOwner,
    BC250_MCDM_HANDLE *Handle);

NTSTATUS Bc250ActiveDestroyProcessObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE Handle);

NTSTATUS Bc250ActiveCreateDeviceObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE ProcessHandle,
    BC250_MCDM_HANDLE *DeviceHandle);

NTSTATUS Bc250ActiveDestroyDeviceObject(
    BC250_MCDM_HANDLE DeviceHandle);

NTSTATUS Bc250ActiveCreateContextObject(
    BC250_MCDM_HANDLE DeviceHandle,
    UINT NodeOrdinal,
    BC250_MCDM_HANDLE *ContextHandle,
    DXGK_CONTEXTINFO *ContextInfo);

NTSTATUS Bc250ActiveDestroyContextObject(
    BC250_MCDM_HANDLE ContextHandle);

NTSTATUS Bc250ActiveSetRootPageTableObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE ContextHandle,
    uint32_t SegmentId,
    uint64_t Address,
    uint32_t NumEntries,
    uint32_t MaxEntries);

#ifdef __cplusplus
}
#endif

#endif
