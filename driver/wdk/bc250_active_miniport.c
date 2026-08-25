#include "bc250_active_miniport.h"
#include "bc250_mcdm_callbacks.h"
#include "bc250_display_ddi.h"
#include "bc250_probe_telemetry.h"
#include "../core/bc250_adapter_info_contract.h"
#include "../core/bc250_allocation_abi_contract.h"
#include "../core/bc250_mcdm_debug_contract.h"

DRIVER_INITIALIZATION_DATA g_Bc250ActiveInitializationData;

#define BC250_ACTIVE_MAX_ADAPTERS 8u
static EX_PUSH_LOCK g_Bc250AdapterDirectoryLock;
static BC250_DXGK_ACTIVE_CONTEXT *
    g_Bc250AdapterDirectory[BC250_ACTIVE_MAX_ADAPTERS];

static NTSTATUS Bc250ActiveRegisterAdapter(
    BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    uint32_t index;
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        if (g_Bc250AdapterDirectory[index] == NULL) {
            g_Bc250AdapterDirectory[index] = Context;
            status = STATUS_SUCCESS;
            break;
        }
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return status;
}

static VOID Bc250ActiveUnregisterAdapter(
    BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    uint32_t index;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        if (g_Bc250AdapterDirectory[index] == Context) {
            g_Bc250AdapterDirectory[index] = NULL;
            break;
        }
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
}

static NTSTATUS Bc250ObjectStatusToNtStatus(
    BC250_MCDM_OBJECT_STATUS Status)
{
    switch (Status) {
    case BC250_MCDM_OBJECT_OK:
        return STATUS_SUCCESS;
    case BC250_MCDM_OBJECT_NO_SPACE:
        return STATUS_INSUFFICIENT_RESOURCES;
    case BC250_MCDM_OBJECT_DUPLICATE_OWNER:
        return STATUS_OBJECT_NAME_COLLISION;
    case BC250_MCDM_OBJECT_STALE_HANDLE:
    case BC250_MCDM_OBJECT_INVALID_HANDLE:
    case BC250_MCDM_OBJECT_TYPE_MISMATCH:
    case BC250_MCDM_OBJECT_OWNER_MISMATCH:
        return STATUS_INVALID_HANDLE;
    case BC250_MCDM_OBJECT_CHILDREN_LIVE:
        return STATUS_DEVICE_BUSY;
    default:
        return STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS Bc250ActiveCreateProcessObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    const void *ExternalOwner,
    BC250_MCDM_HANDLE *Handle)
{
    BC250_MCDM_OBJECT_STATUS status;

    if (Handle != NULL) *Handle = NULL;
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        ExternalOwner == NULL || Handle == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Context->ObjectLock);
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        ExReleasePushLockExclusive(&Context->ObjectLock);
        KeLeaveCriticalRegion();
        return STATUS_DEVICE_NOT_READY;
    }
    status = Bc250McdmObjectCreateProcess(
        &Context->Objects, ExternalOwner, Handle);
    ExReleasePushLockExclusive(&Context->ObjectLock);
    KeLeaveCriticalRegion();
    return Bc250ObjectStatusToNtStatus(status);
}

static NTSTATUS Bc250ActiveCreateProcessLifecycle(
    HANDLE AdapterContext,
    const void *ExternalOwner,
    HANDLE *ProcessHandle)
{
    return Bc250ActiveCreateProcessObject(
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext,
        ExternalOwner,
        (BC250_MCDM_HANDLE *)ProcessHandle);
}

static NTSTATUS Bc250ActiveDestroyProcessLifecycle(
    HANDLE AdapterContext,
    HANDLE ProcessHandle)
{
    return Bc250ActiveDestroyProcessObject(
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext,
        (BC250_MCDM_HANDLE)ProcessHandle);
}

NTSTATUS Bc250ActiveDestroyProcessObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE Handle)
{
    BC250_MCDM_OBJECT_STATUS status;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        Handle == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Context->ObjectLock);
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        ExReleasePushLockExclusive(&Context->ObjectLock);
        KeLeaveCriticalRegion();
        return STATUS_DEVICE_NOT_READY;
    }
    status = Bc250McdmObjectDestroy(&Context->Objects, Handle,
        BC250_MCDM_OBJECT_PROCESS, Handle);
    ExReleasePushLockExclusive(&Context->ObjectLock);
    KeLeaveCriticalRegion();
    return Bc250ObjectStatusToNtStatus(status);
}

NTSTATUS Bc250ActiveCreateDeviceObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE ProcessHandle,
    BC250_MCDM_HANDLE *DeviceHandle)
{
    BC250_MCDM_OBJECT_STATUS status;

    if (DeviceHandle != NULL) *DeviceHandle = NULL;
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        ProcessHandle == NULL || DeviceHandle == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Context->ObjectLock);
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        status = BC250_MCDM_OBJECT_BAD_REGISTRY;
    } else {
        status = Bc250McdmObjectCreateChild(&Context->Objects,
            BC250_MCDM_OBJECT_DEVICE, ProcessHandle, ProcessHandle,
            DeviceHandle);
    }
    ExReleasePushLockExclusive(&Context->ObjectLock);
    KeLeaveCriticalRegion();
    if (status == BC250_MCDM_OBJECT_BAD_REGISTRY) {
        return STATUS_DEVICE_NOT_READY;
    }
    return Bc250ObjectStatusToNtStatus(status);
}

NTSTATUS Bc250ActiveDestroyDeviceObject(
    BC250_MCDM_HANDLE DeviceHandle)
{
    uint32_t index;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    if (DeviceHandle == NULL) return STATUS_INVALID_PARAMETER;
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[index];
        BC250_MCDM_OBJECT_RECORD record;
        BC250_MCDM_OBJECT_STATUS status;
        if (context == NULL) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        status = Bc250McdmObjectLookup(&context->Objects, DeviceHandle,
            BC250_MCDM_OBJECT_DEVICE, NULL, &record);
        if (status != BC250_MCDM_OBJECT_INVALID_HANDLE) {
            if (status == BC250_MCDM_OBJECT_OK &&
                InterlockedCompareExchange(&context->IsActive, 1, 1) != 1) {
                result = STATUS_DEVICE_NOT_READY;
            } else if (status == BC250_MCDM_OBJECT_OK) {
                status = Bc250McdmObjectDestroy(&context->Objects,
                    DeviceHandle, BC250_MCDM_OBJECT_DEVICE,
                    record.OwnerProcess);
                result = Bc250ObjectStatusToNtStatus(status);
            } else {
                result = Bc250ObjectStatusToNtStatus(status);
            }
            ExReleasePushLockExclusive(&context->ObjectLock);
            break;
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveCreateDeviceLifecycle(
    HANDLE AdapterContext,
    HANDLE ProcessHandle,
    HANDLE *DeviceHandle)
{
    return Bc250ActiveCreateDeviceObject(
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext,
        (BC250_MCDM_HANDLE)ProcessHandle,
        (BC250_MCDM_HANDLE *)DeviceHandle);
}

static NTSTATUS Bc250ActiveDestroyDeviceLifecycle(HANDLE DeviceHandle)
{
    return Bc250ActiveDestroyDeviceObject((BC250_MCDM_HANDLE)DeviceHandle);
}

NTSTATUS Bc250ActiveCreateContextObject(
    BC250_MCDM_HANDLE DeviceHandle,
    UINT NodeOrdinal,
    BC250_MCDM_HANDLE *ContextHandle,
    DXGK_CONTEXTINFO *ContextInfo)
{
    uint32_t index;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    if (ContextHandle != NULL) *ContextHandle = NULL;
    if (ContextInfo != NULL) RtlZeroMemory(ContextInfo, sizeof(*ContextInfo));
    if (DeviceHandle == NULL || ContextHandle == NULL || ContextInfo == NULL ||
        NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[index];
        BC250_MCDM_OBJECT_RECORD device;
        BC250_MCDM_OBJECT_STATUS status;
        if (context == NULL) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        status = Bc250McdmObjectLookup(&context->Objects, DeviceHandle,
            BC250_MCDM_OBJECT_DEVICE, NULL, &device);
        if (status != BC250_MCDM_OBJECT_INVALID_HANDLE) {
            if (status != BC250_MCDM_OBJECT_OK) {
                result = Bc250ObjectStatusToNtStatus(status);
            } else if (InterlockedCompareExchange(
                    &context->IsActive, 1, 1) != 1 ||
                InterlockedCompareExchange(
                    &context->ContextAbiReady, 1, 1) != 1) {
                result = STATUS_DEVICE_NOT_READY;
            } else {
                status = Bc250McdmObjectCreateChild(&context->Objects,
                    BC250_MCDM_OBJECT_CONTEXT, DeviceHandle,
                    device.OwnerProcess, ContextHandle);
                result = Bc250ObjectStatusToNtStatus(status);
                if (NT_SUCCESS(result)) *ContextInfo = context->ContextAbi;
            }
            ExReleasePushLockExclusive(&context->ObjectLock);
            break;
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

NTSTATUS Bc250ActiveDestroyContextObject(
    BC250_MCDM_HANDLE ContextHandle)
{
    uint32_t index;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    if (ContextHandle == NULL) return STATUS_INVALID_PARAMETER;
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[index];
        BC250_MCDM_OBJECT_RECORD record;
        BC250_MCDM_OBJECT_STATUS status;
        if (context == NULL) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        status = Bc250McdmObjectLookup(&context->Objects, ContextHandle,
            BC250_MCDM_OBJECT_CONTEXT, NULL, &record);
        if (status != BC250_MCDM_OBJECT_INVALID_HANDLE) {
            if (status == BC250_MCDM_OBJECT_OK &&
                InterlockedCompareExchange(&context->IsActive, 1, 1) == 1) {
                status = Bc250McdmObjectDestroy(&context->Objects,
                    ContextHandle, BC250_MCDM_OBJECT_CONTEXT,
                    record.OwnerProcess);
                result = Bc250ObjectStatusToNtStatus(status);
            } else if (status == BC250_MCDM_OBJECT_OK) {
                result = STATUS_DEVICE_NOT_READY;
            } else {
                result = Bc250ObjectStatusToNtStatus(status);
            }
            ExReleasePushLockExclusive(&context->ObjectLock);
            break;
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveCreateContextLifecycle(
    HANDLE DeviceHandle,
    UINT NodeOrdinal,
    HANDLE *ContextHandle,
    DXGK_CONTEXTINFO *ContextInfo)
{
    return Bc250ActiveCreateContextObject(
        (BC250_MCDM_HANDLE)DeviceHandle, NodeOrdinal,
        (BC250_MCDM_HANDLE *)ContextHandle, ContextInfo);
}

static NTSTATUS Bc250ActiveDestroyContextLifecycle(HANDLE ContextHandle)
{
    return Bc250ActiveDestroyContextObject(
        (BC250_MCDM_HANDLE)ContextHandle);
}

static NTSTATUS Bc250ActiveCreateAllocationLifecycle(
    HANDLE AdapterContext,
    DXGKARG_CREATEALLOCATION *CreateAllocation)
{
    uint32_t directoryIndex;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (directoryIndex = 0u;
         directoryIndex < BC250_ACTIVE_MAX_ADAPTERS;
         ++directoryIndex) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[directoryIndex];
        UINT index;
        UINT created = 0u;
        if (context != (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1 ||
            InterlockedCompareExchange(
                &context->AllocationAbiReady, 1, 1) != 1) {
            result = STATUS_DEVICE_NOT_READY;
        } else {
            result = STATUS_SUCCESS;
            for (index = 0u; index < CreateAllocation->NumAllocations; ++index) {
                BC250_ALLOCATION_PRIVATE_V1 request;
                DXGK_ALLOCATIONINFO *info =
                    &CreateAllocation->pAllocationInfo[index];
                if (Bc250AllocationAbiParse(info->pPrivateDriverData,
                        info->PrivateDriverDataSize, &request) !=
                        BC250_ALLOCATION_ABI_OK ||
                    request.ByteSize > (uint64_t)(SIZE_T)~(SIZE_T)0) {
                    result = STATUS_INVALID_PARAMETER;
                    break;
                }
            }
            for (index = 0u;
                 NT_SUCCESS(result) && index < CreateAllocation->NumAllocations;
                 ++index) {
                BC250_ALLOCATION_PRIVATE_V1 request;
                BC250_MCDM_HANDLE handle = NULL;
                BC250_MCDM_OBJECT_STATUS status;
                DXGK_ALLOCATIONINFO *info =
                    &CreateAllocation->pAllocationInfo[index];
                (void)Bc250AllocationAbiParse(info->pPrivateDriverData,
                    info->PrivateDriverDataSize, &request);
                status = Bc250McdmObjectCreateAdapterAllocation(
                    &context->Objects, info, request.AllocationToken,
                    request.ByteSize,
                    request.Alignment, &handle);
                result = Bc250ObjectStatusToNtStatus(status);
                if (!NT_SUCCESS(result)) break;
                info->Size = (SIZE_T)request.ByteSize;
                info->PitchAlignedSize = (SIZE_T)request.ByteSize;
                info->Alignment = (UINT)request.Alignment;
                info->SupportedReadSegmentSet = 0u;
                info->SupportedWriteSegmentSet = 0u;
                info->EvictionSegmentSet = 0u;
                info->hAllocation = handle;
                ++created;
            }
            if (!NT_SUCCESS(result)) {
                while (created != 0u) {
                    DXGK_ALLOCATIONINFO *info =
                        &CreateAllocation->pAllocationInfo[--created];
                    (void)Bc250McdmObjectDestroy(&context->Objects,
                        info->hAllocation, BC250_MCDM_OBJECT_ALLOCATION, NULL);
                    info->hAllocation = NULL;
                }
            }
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
        break;
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveDestroyAllocationLifecycle(
    HANDLE AdapterContext,
    const DXGKARG_DESTROYALLOCATION *DestroyAllocation)
{
    uint32_t directoryIndex;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (directoryIndex = 0u;
         directoryIndex < BC250_ACTIVE_MAX_ADAPTERS;
         ++directoryIndex) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[directoryIndex];
        UINT index, other;
        if (context != (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1) {
            result = STATUS_DEVICE_NOT_READY;
        } else {
            result = STATUS_SUCCESS;
            for (index = 0u; index < DestroyAllocation->NumAllocations; ++index) {
                HANDLE handle = DestroyAllocation->pAllocationList[index];
                if (handle == NULL) {
                    result = STATUS_INVALID_PARAMETER;
                    break;
                }
                for (other = 0u; other < index; ++other) {
                    if (DestroyAllocation->pAllocationList[other] == handle) {
                        result = STATUS_INVALID_PARAMETER;
                        break;
                    }
                }
                if (!NT_SUCCESS(result)) break;
            }
            for (index = 0u;
                 NT_SUCCESS(result) && index < DestroyAllocation->NumAllocations;
                 ++index) {
                BC250_MCDM_OBJECT_RECORD record;
                BC250_MCDM_OBJECT_STATUS status;
                HANDLE handle = DestroyAllocation->pAllocationList[index];
                status = Bc250McdmObjectLookup(&context->Objects, handle,
                    BC250_MCDM_OBJECT_ALLOCATION, NULL, &record);
                result = Bc250ObjectStatusToNtStatus(status);
                if (!NT_SUCCESS(result)) break;
                if (record.ChildCount != 0u) {
                    result = STATUS_DEVICE_BUSY;
                    break;
                }
            }
            for (index = 0u;
                 NT_SUCCESS(result) && index < DestroyAllocation->NumAllocations;
                 ++index) {
                BC250_MCDM_OBJECT_STATUS status = Bc250McdmObjectDestroy(
                    &context->Objects,
                    DestroyAllocation->pAllocationList[index],
                    BC250_MCDM_OBJECT_ALLOCATION, NULL);
                result = Bc250ObjectStatusToNtStatus(status);
            }
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
        break;
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveOpenAllocationLifecycle(
    HANDLE DeviceContext,
    const DXGKARG_OPENALLOCATION *OpenAllocation)
{
    uint32_t directoryIndex;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (directoryIndex = 0u;
         directoryIndex < BC250_ACTIVE_MAX_ADAPTERS;
         ++directoryIndex) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[directoryIndex];
        BC250_MCDM_OBJECT_RECORD deviceRecord;
        BC250_MCDM_OBJECT_STATUS objectStatus;
        UINT index, other, opened = 0u;
        if (context == NULL) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        objectStatus = Bc250McdmObjectLookup(&context->Objects,
            DeviceContext, BC250_MCDM_OBJECT_DEVICE, NULL, &deviceRecord);
        if (objectStatus != BC250_MCDM_OBJECT_OK) {
            ExReleasePushLockExclusive(&context->ObjectLock);
            continue;
        }
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1 ||
            InterlockedCompareExchange(
                &context->AllocationAbiReady, 1, 1) != 1) {
            result = STATUS_DEVICE_NOT_READY;
        } else {
            result = STATUS_SUCCESS;
            for (index = 0u; index < OpenAllocation->NumAllocations; ++index) {
                BC250_ALLOCATION_PRIVATE_V1 request;
                DXGK_OPENALLOCATIONINFO *info =
                    &OpenAllocation->pOpenAllocation[index];
                if (info->hAllocation == 0u ||
                    Bc250AllocationAbiParse(info->pPrivateDriverData,
                        info->PrivateDriverDataSize, &request) !=
                        BC250_ALLOCATION_ABI_OK) {
                    result = STATUS_INVALID_PARAMETER;
                    break;
                }
                for (other = 0u; other < index; ++other) {
                    BC250_ALLOCATION_PRIVATE_V1 previous;
                    DXGK_OPENALLOCATIONINFO *prior =
                        &OpenAllocation->pOpenAllocation[other];
                    (void)Bc250AllocationAbiParse(prior->pPrivateDriverData,
                        prior->PrivateDriverDataSize, &previous);
                    if (prior->hAllocation == info->hAllocation ||
                        previous.AllocationToken == request.AllocationToken) {
                        result = STATUS_INVALID_PARAMETER;
                        break;
                    }
                }
                if (!NT_SUCCESS(result)) break;
            }
            for (index = 0u;
                 NT_SUCCESS(result) && index < OpenAllocation->NumAllocations;
                 ++index) {
                BC250_ALLOCATION_PRIVATE_V1 request;
                BC250_MCDM_HANDLE allocation = NULL, openHandle = NULL;
                DXGK_OPENALLOCATIONINFO *info =
                    &OpenAllocation->pOpenAllocation[index];
                (void)Bc250AllocationAbiParse(info->pPrivateDriverData,
                    info->PrivateDriverDataSize, &request);
                objectStatus = Bc250McdmObjectFindAllocationByToken(
                    &context->Objects, request.AllocationToken, &allocation);
                if (objectStatus == BC250_MCDM_OBJECT_OK) {
                    BC250_MCDM_OBJECT_RECORD allocationRecord;
                    objectStatus = Bc250McdmObjectLookup(&context->Objects,
                        allocation, BC250_MCDM_OBJECT_ALLOCATION, NULL,
                        &allocationRecord);
                    if (objectStatus == BC250_MCDM_OBJECT_OK &&
                        (allocationRecord.AllocationBytes != request.ByteSize ||
                         allocationRecord.AllocationAlignment !=
                            request.Alignment)) {
                        result = STATUS_INVALID_PARAMETER;
                        break;
                    }
                }
                if (objectStatus == BC250_MCDM_OBJECT_OK) {
                    objectStatus = Bc250McdmObjectOpenAllocation(
                        &context->Objects, DeviceContext, allocation,
                        (const void *)(ULONG_PTR)info->hAllocation,
                        &openHandle);
                }
                result = Bc250ObjectStatusToNtStatus(objectStatus);
                if (!NT_SUCCESS(result)) break;
                info->hDeviceSpecificAllocation = openHandle;
                ++opened;
            }
            if (!NT_SUCCESS(result)) {
                while (opened != 0u) {
                    DXGK_OPENALLOCATIONINFO *info =
                        &OpenAllocation->pOpenAllocation[--opened];
                    (void)Bc250McdmObjectDestroy(&context->Objects,
                        info->hDeviceSpecificAllocation,
                        BC250_MCDM_OBJECT_OPEN_ALLOCATION,
                        deviceRecord.OwnerProcess);
                    info->hDeviceSpecificAllocation = NULL;
                }
            }
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
        break;
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveCloseAllocationLifecycle(
    HANDLE DeviceContext,
    const DXGKARG_CLOSEALLOCATION *CloseAllocation)
{
    uint32_t directoryIndex;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (directoryIndex = 0u;
         directoryIndex < BC250_ACTIVE_MAX_ADAPTERS;
         ++directoryIndex) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[directoryIndex];
        BC250_MCDM_OBJECT_RECORD deviceRecord;
        BC250_MCDM_OBJECT_STATUS objectStatus;
        UINT index, other;
        if (context == NULL) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        objectStatus = Bc250McdmObjectLookup(&context->Objects,
            DeviceContext, BC250_MCDM_OBJECT_DEVICE, NULL, &deviceRecord);
        if (objectStatus != BC250_MCDM_OBJECT_OK) {
            ExReleasePushLockExclusive(&context->ObjectLock);
            continue;
        }
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1) {
            result = STATUS_DEVICE_NOT_READY;
        } else {
            result = STATUS_SUCCESS;
            for (index = 0u; index < CloseAllocation->NumAllocations; ++index) {
                BC250_MCDM_OBJECT_RECORD openRecord;
                HANDLE handle = CloseAllocation->pOpenHandleList[index];
                if (handle == NULL) {
                    result = STATUS_INVALID_PARAMETER;
                    break;
                }
                for (other = 0u; other < index; ++other) {
                    if (CloseAllocation->pOpenHandleList[other] == handle) {
                        result = STATUS_INVALID_PARAMETER;
                        break;
                    }
                }
                if (!NT_SUCCESS(result)) break;
                objectStatus = Bc250McdmObjectLookup(&context->Objects,
                    handle, BC250_MCDM_OBJECT_OPEN_ALLOCATION,
                    deviceRecord.OwnerProcess, &openRecord);
                result = Bc250ObjectStatusToNtStatus(objectStatus);
                if (!NT_SUCCESS(result)) break;
                if (openRecord.Parent != DeviceContext) {
                    result = STATUS_INVALID_HANDLE;
                    break;
                }
            }
            for (index = 0u;
                 NT_SUCCESS(result) && index < CloseAllocation->NumAllocations;
                 ++index) {
                objectStatus = Bc250McdmObjectDestroy(&context->Objects,
                    CloseAllocation->pOpenHandleList[index],
                    BC250_MCDM_OBJECT_OPEN_ALLOCATION,
                    deviceRecord.OwnerProcess);
                result = Bc250ObjectStatusToNtStatus(objectStatus);
            }
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
        break;
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveCollectDebugLifecycle(
    HANDLE AdapterContext,
    const DXGKARG_COLLECTDBGINFO *CollectDebugInfo)
{
    BC250_MCDM_DEBUG_SNAPSHOT_V1 snapshot;
    uint32_t directoryIndex;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    if (CollectDebugInfo->BufferSize < sizeof(snapshot)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    RtlZeroMemory(&snapshot, sizeof(snapshot));
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (directoryIndex = 0u;
         directoryIndex < BC250_ACTIVE_MAX_ADAPTERS;
         ++directoryIndex) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[directoryIndex];
        BC250_PROBE_FIELD lastField = (BC250_PROBE_FIELD)0;
        ULONG lastValue = 0u;
        if (context != (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        snapshot.Magic = BC250_MCDM_DEBUG_MAGIC;
        snapshot.Version = BC250_MCDM_DEBUG_VERSION;
        snapshot.StructSize = sizeof(snapshot);
        snapshot.Reason = CollectDebugInfo->Reason;
        snapshot.StateFlags = BC250_MCDM_DEBUG_ADAPTER_REGISTERED;
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) == 1) {
            snapshot.StateFlags |= BC250_MCDM_DEBUG_ACTIVE;
        }
        if (InterlockedCompareExchange(&context->ContextAbiReady, 1, 1) == 1) {
            snapshot.StateFlags |= BC250_MCDM_DEBUG_CONTEXT_ABI_READY;
        }
        if (InterlockedCompareExchange(
                &context->AllocationAbiReady, 1, 1) == 1) {
            snapshot.StateFlags |= BC250_MCDM_DEBUG_ALLOCATION_READY;
        }
        snapshot.FirstFailureStatus =
            (uint32_t)Bc250ProbeGetFirstFailure();
        snapshot.ProbeSequence = Bc250ProbeGetOffline(&lastField, &lastValue);
        snapshot.LastProbeField = (uint32_t)lastField;
        snapshot.LastProbeValue = lastValue;
        snapshot.ObjectLiveCount = context->Objects.LiveCount;
        snapshot.ObjectNextSlot = context->Objects.NextSlot;
        snapshot.SubmissionsCount = context->SubmissionsCount;
        snapshot.ResetsCount = context->ResetsCount;
        RtlCopyMemory(CollectDebugInfo->pBuffer, &snapshot, sizeof(snapshot));
        if (CollectDebugInfo->pExtension != NULL) {
            RtlZeroMemory(CollectDebugInfo->pExtension,
                sizeof(*CollectDebugInfo->pExtension));
            CollectDebugInfo->pExtension->BucketingKey =
                BC250_MCDM_DEBUG_MAGIC;
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
        result = STATUS_SUCCESS;
        break;
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

NTSTATUS APIENTRY Bc250DxgkDdiAddDeviceActive(
    CONST PDEVICE_OBJECT PhysicalDeviceObject,
    PVOID *MiniportDeviceContext)
{
    BC250_DXGK_ACTIVE_CONTEXT *context;
    NTSTATUS status;

    Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_ENTERED, 1);
    if (MiniportDeviceContext != NULL) *MiniportDeviceContext = NULL;
    if (PhysicalDeviceObject == NULL || MiniportDeviceContext == NULL) {
        Bc250ProbeRecord(
            BC250_PROBE_ADD_DEVICE_STATUS, (ULONG)STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    context = (BC250_DXGK_ACTIVE_CONTEXT *)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(*context), BC250_ACTIVE_POOL_TAG);
    if (context == NULL) {
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS,
            (ULONG)STATUS_INSUFFICIENT_RESOURCES);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    status = Bc250DxgkReadonlyInitialize(&context->Core);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS, (ULONG)status);
        return status;
    }

    context->Signature = BC250_ACTIVE_SIGNATURE;
    context->PhysicalDeviceObject = (PDEVICE_OBJECT)PhysicalDeviceObject;
    context->Core.PhysicalDeviceObject = (PDEVICE_OBJECT)PhysicalDeviceObject;
    RtlZeroMemory(&context->Display, sizeof(context->Display));
    ExInitializePushLock(&context->ObjectLock);
    if (Bc250McdmObjectRegistryInitialize(&context->Objects) !=
        BC250_MCDM_OBJECT_OK) {
        ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS,
            (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    InterlockedExchange(&context->IsActive, 0);
    RtlZeroMemory(&context->ContextAbi, sizeof(context->ContextAbi));
    InterlockedExchange(&context->ContextAbiReady, 0);
    InterlockedExchange(&context->AllocationAbiReady, 0);
    context->SubmissionsCount = 0;
    context->ResetsCount = 0;

    status = Bc250ActiveRegisterAdapter(context);
    if (!NT_SUCCESS(status)) {
        context->Signature = 0u;
        ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
        return status;
    }

    *MiniportDeviceContext = context;
    Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS, (ULONG)STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceActive(
    CONST PVOID MiniportDeviceContext,
    PDXGK_START_INFO DxgkStartInfo,
    PDXGKRNL_INTERFACE DxgkInterface,
    PULONG NumberOfVideoPresentSources,
    PULONG NumberOfChildren)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)MiniportDeviceContext;
    NTSTATUS preflightStatus;

    Bc250ProbeRecord(BC250_PROBE_START_DEVICE_ENTERED, 1);
    if (context == NULL ||
        context->Signature != BC250_ACTIVE_SIGNATURE ||
        DxgkStartInfo == NULL ||
        DxgkInterface == NULL ||
        NumberOfVideoPresentSources == NULL ||
        NumberOfChildren == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Executa somente a auditoria readonly. O contexto nasce sem runtime
     * staged: ownership exclusivo, inventario de VRAM, reservas herdadas e
     * manifesto precisam vir de produtores fisicos no lifecycle PnP. Ate
     * esses produtores existirem, StartDevice deve falhar fechado.
     */
    preflightStatus = Bc250DxgkDdiStartDeviceReadonly(
        &context->Core,
        DxgkStartInfo,
        DxgkInterface,
        NumberOfVideoPresentSources,
        NumberOfChildren);

    InterlockedExchange(&context->IsActive, 0);
    InterlockedExchange(&context->ContextAbiReady, 0);
    InterlockedExchange(&context->AllocationAbiReady, 0);
    Bc250ProbeRecordFirstFailure(preflightStatus);
    return preflightStatus;
}

NTSTATUS APIENTRY Bc250DxgkDdiStopDeviceActive(
    CONST PVOID MiniportDeviceContext)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)MiniportDeviceContext;
    uint32_t destroyedObjects;
    Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_ENTERED, 1);
    if (context == NULL ||
        context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    InterlockedExchange(&context->IsActive, 0);
    InterlockedExchange(&context->ContextAbiReady, 0);
    InterlockedExchange(&context->AllocationAbiReady, 0);
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&context->ObjectLock);
    destroyedObjects = 0u;
    (void)Bc250McdmObjectRegistryTeardown(
        &context->Objects, &destroyedObjects);
    ExReleasePushLockExclusive(&context->ObjectLock);
    KeLeaveCriticalRegion();
    {
        NTSTATUS status = Bc250DxgkDdiStopDeviceReadonly(&context->Core);
        Bc250ProbeRecordFirstFailure(status);
        return status;
    }
}

NTSTATUS APIENTRY Bc250DxgkDdiRemoveDeviceActive(
    CONST PVOID MiniportDeviceContext)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)MiniportDeviceContext;
    NTSTATUS status;

    Bc250ProbeRecord(BC250_PROBE_REMOVE_DEVICE_ENTERED, 1);
    if (context == NULL ||
        context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    status = Bc250DxgkDdiStopDeviceActive(context);
    Bc250ActiveUnregisterAdapter(context);
    context->Signature = 0;
    context->PhysicalDeviceObject = NULL;
    context->Core.PhysicalDeviceObject = NULL;
    ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
    Bc250ProbeRecord(BC250_PROBE_REMOVE_DEVICE_STATUS, (ULONG)status);
    return status;
}

NTSTATUS Bc250BuildActiveInitializationData(
    DRIVER_INITIALIZATION_DATA *InitializationData)
{
    if (InitializationData == NULL) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(InitializationData, sizeof(*InitializationData));
    InitializationData->Version = BC250_DXGK_TARGET_INTERFACE_VERSION;
    InitializationData->DxgkDdiAddDevice = Bc250DxgkDdiAddDeviceActive;
    InitializationData->DxgkDdiStartDevice = Bc250DxgkDdiStartDeviceActive;
    InitializationData->DxgkDdiStopDevice = Bc250DxgkDdiStopDeviceActive;
    InitializationData->DxgkDdiRemoveDevice = Bc250DxgkDdiRemoveDeviceActive;

    Bc250McdmInstallProcessLifecycle(
        Bc250ActiveCreateProcessLifecycle,
        Bc250ActiveDestroyProcessLifecycle);
    Bc250McdmInstallDeviceLifecycle(
        Bc250ActiveCreateDeviceLifecycle,
        Bc250ActiveDestroyDeviceLifecycle);
    Bc250McdmInstallContextLifecycle(
        Bc250ActiveCreateContextLifecycle,
        Bc250ActiveDestroyContextLifecycle);
    Bc250McdmInstallAllocationLifecycle(
        Bc250ActiveCreateAllocationLifecycle,
        Bc250ActiveDestroyAllocationLifecycle,
        Bc250ActiveOpenAllocationLifecycle,
        Bc250ActiveCloseAllocationLifecycle);
    Bc250McdmInstallDebugLifecycle(Bc250ActiveCollectDebugLifecycle);

    return Bc250McdmPopulateRequiredCallbacks(InitializationData);
}

NTSTATUS DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath)
{
    Bc250ProbeRecord(BC250_PROBE_DRIVER_ENTRY_REACHED, 1);
    if (DriverObject == NULL || RegistryPath == NULL) {
        Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS,
            (ULONG)STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    if (!NT_SUCCESS(Bc250BuildActiveInitializationData(
            &g_Bc250ActiveInitializationData))) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Bc250ProbeRecord(BC250_PROBE_INITIALIZATION_DATA_READY, 1);

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    return DxgkInitialize(
        DriverObject, RegistryPath, &g_Bc250ActiveInitializationData);
#else
    return STATUS_SUCCESS;
#endif
}
