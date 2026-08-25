#ifndef BC250_ACTIVE_MINIPORT_H
#define BC250_ACTIVE_MINIPORT_H

#include <ntddk.h>
#include <dispmprt.h>

#include "bc250_dxgk_readonly_ddi.h"
#include "bc250_readonly_miniport.h"
#include "bc250_display_ddi.h"
#include "../core/bc250_mcdm_object_contract.h"

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
    EX_PUSH_LOCK ObjectLock;
    BC250_MCDM_OBJECT_REGISTRY Objects;
    DXGK_CONTEXTINFO ContextAbi;
    volatile LONG ContextAbiReady;
    volatile LONG AllocationAbiReady;
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

#ifdef __cplusplus
}
#endif

#endif
