#ifndef BC250_READONLY_MINIPORT_H
#define BC250_READONLY_MINIPORT_H

#include "bc250_dxgk_readonly_ddi.h"
#include "bc250_mcdm_callbacks.h"

#if DXGKDDI_INTERFACE_VERSION != DXGKDDI_INTERFACE_VERSION_WDDM2_7
#error BC250 readonly must target the Windows 10 WDDM 2.7 interface explicitly.
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_READONLY_POOL_TAG 'r052'
#define BC250_DXGK_TARGET_INTERFACE_VERSION \
    DXGKDDI_INTERFACE_VERSION_WDDM2_7

extern DRIVER_INITIALIZATION_DATA g_Bc250ReadonlyInitializationData;

NTSTATUS APIENTRY Bc250DxgkDdiAddDeviceReadonly(
    _In_ CONST PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PVOID *MiniportDeviceContext);

NTSTATUS APIENTRY Bc250DxgkDdiRemoveDeviceReadonly(
    _In_ CONST PVOID MiniportDeviceContext);

NTSTATUS Bc250BuildReadonlyInitializationData(
    _Out_ DRIVER_INITIALIZATION_DATA *InitializationData);

DRIVER_INITIALIZE DriverEntry;

#ifdef __cplusplus
}
#endif

#endif
