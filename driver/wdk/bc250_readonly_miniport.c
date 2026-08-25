#include "bc250_readonly_miniport.h"
#include "bc250_mcdm_callbacks.h"
#include "bc250_probe_telemetry.h"

DRIVER_INITIALIZATION_DATA g_Bc250ReadonlyInitializationData;

NTSTATUS APIENTRY Bc250DxgkDdiAddDeviceReadonly(
    CONST PDEVICE_OBJECT PhysicalDeviceObject,
    PVOID *MiniportDeviceContext)
{
    BC250_DXGK_READONLY_CONTEXT *context;
    NTSTATUS status;

    Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_ENTERED, 1);
    if (MiniportDeviceContext != NULL) *MiniportDeviceContext = NULL;
    if (PhysicalDeviceObject == NULL || MiniportDeviceContext == NULL) {
        Bc250ProbeRecord(
            BC250_PROBE_ADD_DEVICE_STATUS, (ULONG)STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    context = (BC250_DXGK_READONLY_CONTEXT *)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(*context), BC250_READONLY_POOL_TAG);
    if (context == NULL) {
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS,
            (ULONG)STATUS_INSUFFICIENT_RESOURCES);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    status = Bc250DxgkReadonlyInitialize(context);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(context, BC250_READONLY_POOL_TAG);
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS, (ULONG)status);
        return status;
    }
    context->PhysicalDeviceObject = (PDEVICE_OBJECT)PhysicalDeviceObject;
    *MiniportDeviceContext = context;
    Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS, (ULONG)STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiRemoveDeviceReadonly(
    CONST PVOID MiniportDeviceContext)
{
    BC250_DXGK_READONLY_CONTEXT *context =
        (BC250_DXGK_READONLY_CONTEXT *)MiniportDeviceContext;
    NTSTATUS status;

    Bc250ProbeRecord(BC250_PROBE_REMOVE_DEVICE_ENTERED, 1);
    if (context == NULL ||
        context->Signature != BC250_DXGK_READONLY_SIGNATURE) {
        Bc250ProbeRecord(BC250_PROBE_REMOVE_DEVICE_STATUS,
            (ULONG)STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    status = Bc250DxgkDdiStopDeviceReadonly(context);
    if (!NT_SUCCESS(status)) {
        Bc250ProbeRecord(BC250_PROBE_REMOVE_DEVICE_STATUS, (ULONG)status);
        return status;
    }
    context->Signature = 0;
    context->PhysicalDeviceObject = NULL;
    ExFreePoolWithTag(context, BC250_READONLY_POOL_TAG);
    Bc250ProbeRecord(BC250_PROBE_REMOVE_DEVICE_STATUS, (ULONG)STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS Bc250BuildReadonlyInitializationData(
    DRIVER_INITIALIZATION_DATA *InitializationData)
{
    if (InitializationData == NULL) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(InitializationData, sizeof(*InitializationData));
    InitializationData->Version = BC250_DXGK_TARGET_INTERFACE_VERSION;
    InitializationData->DxgkDdiAddDevice = Bc250DxgkDdiAddDeviceReadonly;
    InitializationData->DxgkDdiStartDevice = Bc250DxgkDdiStartDeviceReadonly;
    InitializationData->DxgkDdiStopDevice = Bc250DxgkDdiStopDeviceReadonly;
    InitializationData->DxgkDdiRemoveDevice = Bc250DxgkDdiRemoveDeviceReadonly;
    return Bc250McdmPopulateRequiredCallbacks(InitializationData);
}

NTSTATUS DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;

    Bc250ProbeRecord(BC250_PROBE_DRIVER_ENTRY_REACHED, 1);
    if (DriverObject == NULL || RegistryPath == NULL) {
        Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS,
            (ULONG)STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    if (!NT_SUCCESS(Bc250BuildReadonlyInitializationData(
            &g_Bc250ReadonlyInitializationData))) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Bc250ProbeRecord(BC250_PROBE_INITIALIZATION_DATA_READY, 1);

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    status = DxgkInitialize(
        DriverObject, RegistryPath, &g_Bc250ReadonlyInitializationData);
#else
    /* Testes offline nao carregam nem simulam Dxgkrnl. */
    status = STATUS_NOT_SUPPORTED;
#endif
    Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS, (ULONG)status);
    return status;
}
