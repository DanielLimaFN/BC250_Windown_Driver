#include "bc250_active_miniport.h"
#include "bc250_mcdm_callbacks.h"
#include "bc250_display_ddi.h"
#include "bc250_probe_telemetry.h"
#include "bc250_cp_firmware_manifest.h"
#include "../core/bc250_adapter_info_contract.h"
#include "../core/bc250_allocation_abi_contract.h"
#include "../core/bc250_mcdm_debug_contract.h"

DRIVER_INITIALIZATION_DATA g_Bc250ActiveInitializationData;

#define BC250_ACTIVE_MAX_ADAPTERS 8u
static EX_PUSH_LOCK g_Bc250AdapterDirectoryLock;
static BC250_DXGK_ACTIVE_CONTEXT *
    g_Bc250AdapterDirectory[BC250_ACTIVE_MAX_ADAPTERS];

static uint32_t Bc250ActiveExecuteGcvmBacking(
    void *Context, uint64_t PageTablePhysical);

static NTSTATUS Bc250ActiveQueryRuntimeCaps(
    HANDLE AdapterContext,
    BC250_MCDM_RUNTIME_CAPS *Caps)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext;
    uint32_t i;
    uint64_t localStart = 0u;
    uint64_t localEnd;
    uint64_t cpuVisibleBytes;
    if (Caps == NULL) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Caps, sizeof(*Caps));
    if (context == NULL || context->Signature != BC250_ACTIVE_SIGNATURE ||
        context->RuntimeInventoryStatus != BC250_RUNTIME_INVENTORY_OK ||
        context->VramProbeStatus != BC250_VRAM_PROBE_OK) {
        return STATUS_DEVICE_NOT_READY;
    }
    cpuVisibleBytes = context->RuntimeInventoryReport.CpuVisibleBytes;
    localEnd = context->Core.StartDevice.Preflight.FirmwarePlans[0]
        .Binding.Offset;
    if (context->SecondPreflightStage != BC250_DXGK_STAGE_PREFLIGHT_READY ||
        context->Core.StartDevice.Preflight.FirmwareCount !=
            BC250_CP_BOOTSTRAP_FIRMWARE_COUNT ||
        localEnd == 0u || localEnd > cpuVisibleBytes) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    for (i = 0u;
         i < context->RuntimeInventoryReport.ReservationCount;
         ++i) {
        const BC250_INHERITED_VRAM_RESERVATION *reservation =
            &context->RuntimeInventoryReport.Reservations[i];
        uint64_t reservationEnd;
        if (reservation->Offset >= cpuVisibleBytes) continue;
        reservationEnd = reservation->Length >
            cpuVisibleBytes - reservation->Offset ?
            cpuVisibleBytes : reservation->Offset + reservation->Length;
        if (reservationEnd > localStart) localStart = reservationEnd;
    }
    if (localStart >= localEnd) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    for (i = 0u; i < context->Core.ResolvedBarCount; ++i) {
        if (context->Core.ResolvedBars[i].Index ==
            BC250_PCI_VRAM_APERTURE_BAR_INDEX) {
            Caps->CpuPhysicalStart =
                context->Core.ResolvedBars[i].Start + localStart;
            break;
        }
    }
    if (i == context->Core.ResolvedBarCount || Caps->CpuPhysicalStart == 0u) {
        RtlZeroMemory(Caps, sizeof(*Caps));
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Caps->LocalSegmentBaseOffset = localStart;
    Caps->LocalSegmentBytes = localEnd - localStart;
    Caps->NodeCount = BC250_ADAPTER_NODE_COUNT;
    Caps->Ready = 1u;
    return STATUS_SUCCESS;
}

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

NTSTATUS Bc250ActiveSetRootPageTableObject(
    BC250_DXGK_ACTIVE_CONTEXT *context,
    BC250_MCDM_HANDLE ContextHandle,
    uint32_t SegmentId,
    uint64_t Address,
    uint32_t NumEntries,
    uint32_t MaxEntries)
{
    BC250_MCDM_OBJECT_STATUS status;

    if (context == NULL || context->Signature != BC250_ACTIVE_SIGNATURE ||
        ContextHandle == NULL || MaxEntries == 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((SegmentId != 0u && SegmentId != 1u) ||
        ((SegmentId == 0u) != (Address == 0u)) ||
        (SegmentId == 1u &&
         (Address >= context->AllocationSegmentBytes ||
          (UINT64)NumEntries * sizeof(UINT64) >
            context->AllocationSegmentBytes - Address))) {
        return STATUS_INVALID_PARAMETER;
    }
    if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1 ||
        InterlockedCompareExchange(
            &context->RootPageTableAbiReady, 1, 1) != 1 ||
        context->RootPageTableMaxEntries != MaxEntries) {
        return STATUS_DEVICE_NOT_READY;
    }
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&context->ObjectLock);
    if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1 ||
        InterlockedCompareExchange(
            &context->RootPageTableAbiReady, 1, 1) != 1) {
        ExReleasePushLockExclusive(&context->ObjectLock);
        KeLeaveCriticalRegion();
        return STATUS_DEVICE_NOT_READY;
    }
    status = Bc250McdmObjectSetRootPageTable(&context->Objects,
        (BC250_MCDM_HANDLE)ContextHandle, SegmentId, Address,
        NumEntries, MaxEntries);
    ExReleasePushLockExclusive(&context->ObjectLock);
    KeLeaveCriticalRegion();
    return Bc250ObjectStatusToNtStatus(status);
}

static NTSTATUS Bc250ActiveSetRootPageTableLifecycle(
    HANDLE AdapterContext,
    HANDLE ContextHandle,
    UINT32 SegmentId,
    UINT64 Address,
    UINT32 NumEntries,
    UINT32 MaxEntries)
{
    return Bc250ActiveSetRootPageTableObject(
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext,
        (BC250_MCDM_HANDLE)ContextHandle,
        SegmentId, Address, NumEntries, MaxEntries);
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
                    request.ByteSize > (uint64_t)(SIZE_T)~(SIZE_T)0 ||
                    context->AllocationSegmentSet != 1u ||
                    context->AllocationSegmentBytes == 0u ||
                    request.ByteSize > context->AllocationSegmentBytes) {
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
                info->PreferredSegment.Value = 0u;
                info->PreferredSegment.SegmentId0 = 1u;
                info->SupportedReadSegmentSet =
                    context->AllocationSegmentSet;
                info->SupportedWriteSegmentSet =
                    context->AllocationSegmentSet;
                info->EvictionSegmentSet = 0u;
                info->PhysicalAdapterIndex = 0u;
                info->FlagsWddm2.Value = 0u;
                info->FlagsWddm2.CpuVisible = 1u;
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
    InterlockedExchange(&context->RootPageTableAbiReady, 0);
    context->AllocationSegmentBytes = 0u;
    context->AllocationSegmentSet = 0u;
    context->RootPageTableMaxEntries = 0u;
    context->PostDisplayStatus = BC250_POST_DISPLAY_BAD_ARGUMENT;
    context->PostDisplayCallbackStatus = STATUS_NOT_SUPPORTED;
    RtlZeroMemory(&context->PostDisplayReservation,
                  sizeof(context->PostDisplayReservation));
    RtlZeroMemory(&context->PostDisplayEvidence,
                  sizeof(context->PostDisplayEvidence));
    context->RuntimeInventoryStatus = BC250_RUNTIME_INVENTORY_BAD_ARGUMENT;
    RtlZeroMemory(&context->RuntimeInventoryInput,
                  sizeof(context->RuntimeInventoryInput));
    RtlZeroMemory(&context->RuntimeInventoryReport,
                  sizeof(context->RuntimeInventoryReport));
    RtlZeroMemory(&context->SealedRuntime,
                  sizeof(context->SealedRuntime));
    context->RuntimeStageStatus = STATUS_NOT_SUPPORTED;
    context->SecondPreflightStage = BC250_DXGK_STAGE_EMPTY;
    context->VramProbeStatus = BC250_VRAM_PROBE_BAD_ARGUMENT;
    RtlZeroMemory(&context->VramProbeEvidence,
                  sizeof(context->VramProbeEvidence));
    context->TlbTransactionStatus = BC250_TLB_TRANSACTION_BAD_ARGUMENT;
    RtlZeroMemory(&context->TlbTransactionEvidence,
                  sizeof(context->TlbTransactionEvidence));
    context->GcvmBackingStatus = BC250_GCVM_BACKING_BAD_ARGUMENT;
    RtlZeroMemory(&context->GcvmBackingEvidence,
                  sizeof(context->GcvmBackingEvidence));
    context->GcvmVmid8Status = BC250_GCVM_VMID8_BAD_ARGUMENT;
    RtlZeroMemory(&context->GcvmVmid8Evidence,
                  sizeof(context->GcvmVmid8Evidence));
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

    if (preflightStatus == STATUS_NOT_SUPPORTED &&
        context->Core.LastStage == BC250_DXGK_STAGE_RUNTIME_MISSING &&
        context->Core.ResolvedBarCount != 0u &&
        context->Core.TopologyEvidence.AddressSpace.SnapshotGeneration != 0u) {
        context->PostDisplayStatus = Bc250AcquirePostDisplayReservation(
            &context->Core.DxgkInterface,
            context->Core.ResolvedBars,
            context->Core.ResolvedBarCount,
            BC250_PCI_VRAM_APERTURE_BAR_INDEX,
            context->Core.TopologyEvidence.AddressSpace.SnapshotGeneration,
            &context->PostDisplayReservation,
            &context->PostDisplayEvidence,
            &context->PostDisplayCallbackStatus);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_STATUS,
            (ULONG)context->PostDisplayStatus);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_CALLBACK_STATUS,
            (ULONG)context->PostDisplayCallbackStatus);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_USED_CALLBACK2,
            context->PostDisplayEvidence.UsedOwnershipCallback2);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_FRAMEBUFFER_STATE,
            context->PostDisplayEvidence.FrameBufferState);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_TARGET_ID,
            context->PostDisplayEvidence.TargetId);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_WIDTH,
            context->PostDisplayEvidence.Width);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_HEIGHT,
            context->PostDisplayEvidence.Height);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_PITCH,
            context->PostDisplayEvidence.Pitch);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_PHYSICAL_LOW32,
            (ULONG)context->PostDisplayEvidence.PhysicalStart);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_PHYSICAL_HIGH32,
            (ULONG)(context->PostDisplayEvidence.PhysicalStart >> 32));
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_BYTES,
            (ULONG)context->PostDisplayEvidence.Length);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_VRAM_OFFSET_LOW32,
            (ULONG)context->PostDisplayEvidence.VramOffset);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_VRAM_OFFSET_HIGH32,
            (ULONG)(context->PostDisplayEvidence.VramOffset >> 32));

        context->RuntimeInventoryInput.EvidenceFlags =
            BC250_RUNTIME_EVIDENCE_SCANOUT_PROBED |
            BC250_RUNTIME_EVIDENCE_DISCOVERY_TMR_PROBED |
            BC250_RUNTIME_EVIDENCE_PLATFORM_CARVEOUTS_PROBED;
        if (context->PostDisplayStatus == BC250_POST_DISPLAY_OK) {
            context->RuntimeInventoryInput.HasScanout = 1u;
            context->RuntimeInventoryInput.ScanoutReservation =
                context->PostDisplayReservation;
        }
        if (context->Core.LastDiscoveryTmrStatus ==
            BC250_DISCOVERY_TMR_OK) {
            context->RuntimeInventoryInput.HasDiscoveryTmr = 1u;
            context->RuntimeInventoryInput.DiscoveryTmrEvidence =
                context->Core.DiscoveryTmrEvidence;
        }
        if (context->Core.TopologyEvidence.AddressSpace.TotalBytes >
            context->Core.TopologyEvidence.AddressSpace.CpuVisibleBytes) {
            BC250_INHERITED_VRAM_RESERVATION *opaque =
                &context->RuntimeInventoryInput.PlatformCarveouts[0];
            opaque->Offset =
                context->Core.TopologyEvidence.AddressSpace.CpuVisibleBytes;
            opaque->Length =
                context->Core.TopologyEvidence.AddressSpace.TotalBytes -
                context->Core.TopologyEvidence.AddressSpace.CpuVisibleBytes;
            opaque->Alignment = 0x1000u;
            opaque->Role = BC250_VRAM_ROLE_PLATFORM_RESERVED;
            opaque->Flags = BC250_VRAM_REGION_PERSISTENT;
            context->RuntimeInventoryInput.PlatformCarveoutCount = 1u;
        }
        context->RuntimeInventoryStatus = Bc250BuildRuntimeVramInventory(
            &context->RuntimeInventoryInput,
            context->Core.TopologyEvidence.AddressSpace.TotalBytes,
            context->Core.TopologyEvidence.AddressSpace.CpuVisibleBytes,
            context->Core.TopologyEvidence.AddressSpace.SnapshotGeneration,
            &context->RuntimeInventoryReport);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_STATUS,
            (ULONG)context->RuntimeInventoryStatus);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_EVIDENCE_FLAGS,
            context->RuntimeInventoryReport.EvidenceFlags);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_RESERVATION_COUNT,
            context->RuntimeInventoryReport.ReservationCount);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_RESERVED_LOW32,
            (ULONG)context->RuntimeInventoryReport.TotalReservedBytes);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_RESERVED_HIGH32,
            (ULONG)(context->RuntimeInventoryReport.TotalReservedBytes >> 32));
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_COMPLETE,
            context->RuntimeInventoryStatus == BC250_RUNTIME_INVENTORY_OK ?
                1u : 0u);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_FREE_LOW32,
            (ULONG)context->RuntimeInventoryReport.TotalFreeBytes);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_FREE_HIGH32,
            (ULONG)(context->RuntimeInventoryReport.TotalFreeBytes >> 32));
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_CPU_FREE_LOW32,
            (ULONG)context->RuntimeInventoryReport.CpuVisibleFreeBytes);
        if (context->RuntimeInventoryStatus == BC250_RUNTIME_INVENTORY_OK) {
            RtlZeroMemory(&context->SealedRuntime,
                sizeof(context->SealedRuntime));
            context->SealedRuntime.ReservationCount =
                context->RuntimeInventoryReport.ReservationCount;
            RtlCopyMemory(context->SealedRuntime.Reservations,
                context->RuntimeInventoryReport.Reservations,
                sizeof(context->RuntimeInventoryReport.Reservations));
            context->SealedRuntime.VramApertureBarIndex =
                BC250_PCI_VRAM_APERTURE_BAR_INDEX;
            context->SealedRuntime.MmioBarIndex = BC250_PCI_MMIO_BAR_INDEX;
            context->SealedRuntime.TopologyReaderIdentityFlags =
                BC250_TOPOLOGY_READER_IDENTITY_REQUIRED;
            context->SealedRuntime.RuntimeOwnershipFlags =
                context->RuntimeInventoryReport.OwnershipEvidenceFlags;
            context->SealedRuntime.SnapshotGeneration =
                context->RuntimeInventoryReport.SnapshotGeneration;
            context->SealedRuntime.ManifestCount =
                BC250_CP_BOOTSTRAP_FIRMWARE_COUNT;
            Bc250GetCpFirmwareManifestContract(
                &context->SealedRuntime.Policy,
                context->SealedRuntime.Manifest);
            context->RuntimeStageStatus = Bc250DxgkReadonlyStageRuntime(
                &context->Core, &context->SealedRuntime);
            Bc250ProbeRecord(BC250_PROBE_RUNTIME_STAGE_STATUS,
                (ULONG)context->RuntimeStageStatus);
            if (NT_SUCCESS(context->RuntimeStageStatus)) {
                preflightStatus = Bc250DxgkDdiStartDeviceReadonly(
                    &context->Core, DxgkStartInfo, DxgkInterface,
                    NumberOfVideoPresentSources, NumberOfChildren);
                context->SecondPreflightStage = context->Core.LastStage;
                Bc250ProbeRecord(BC250_PROBE_SECOND_PREFLIGHT_STAGE,
                    context->SecondPreflightStage);
                Bc250ProbeRecord(BC250_PROBE_SECOND_READONLY_GATE_PASSED,
                    context->Core.ReadonlyGatePassed);
                if (context->Core.ReadonlyGatePassed == 1u &&
                    context->Core.LastStage == BC250_DXGK_STAGE_PREFLIGHT_READY) {
                    context->VramProbeStatus = Bc250RunVramProbeTransaction(
                        &context->Core.DxgkInterface,
                        context->Core.ResolvedBars,
                        context->Core.ResolvedBarCount,
                        BC250_PCI_VRAM_APERTURE_BAR_INDEX,
                        context->Core.StartDevice.Preflight.FirmwarePlans[0]
                            .Binding.Offset,
                        &context->VramProbeEvidence);
                    Bc250ProbeRecord(BC250_PROBE_VRAM_PROBE_STATUS,
                        (ULONG)context->VramProbeStatus);
                    Bc250ProbeRecord(BC250_PROBE_VRAM_PROBE_OFFSET_LOW32,
                        (ULONG)context->VramProbeEvidence.Offset);
                    Bc250ProbeRecord(BC250_PROBE_VRAM_PROBE_BYTES,
                        context->VramProbeEvidence.Bytes);
                    Bc250ProbeRecord(BC250_PROBE_VRAM_PROBE_READBACK_PASSED,
                        context->VramProbeEvidence.WriteReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_VRAM_PROBE_RESTORE_PASSED,
                        context->VramProbeEvidence.RestorePassed);
                    if (context->VramProbeStatus == BC250_VRAM_PROBE_OK &&
                        context->VramProbeEvidence.WriteReadbackPassed != 0u &&
                        context->VramProbeEvidence.RestorePassed != 0u) {
                        context->GcvmBackingStatus =
                            Bc250RunGcvmBackingTransaction(
                                &context->Core.StartDevice.Owned.Bootstrap
                                    .Allocator,
                                context->Core.StartDevice.Owned.Bootstrap
                                    .AddressSpace.VramBaseOffset,
                                &context->Core.DxgkInterface,
                                context->Core.ResolvedBars,
                                context->Core.ResolvedBarCount,
                                BC250_PCI_VRAM_APERTURE_BAR_INDEX,
                                Bc250ActiveExecuteGcvmBacking,
                                context,
                                &context->GcvmBackingEvidence);
                    }
                    Bc250ProbeRecord(BC250_PROBE_TLB_TRANSACTION_STATUS,
                        (ULONG)context->TlbTransactionStatus);
                    Bc250ProbeRecord(BC250_PROBE_TLB_VMID,
                        context->TlbTransactionEvidence.Vmid);
                    Bc250ProbeRecord(BC250_PROBE_TLB_ENGINE_ID,
                        context->TlbTransactionEvidence.EngineId);
                    Bc250ProbeRecord(BC250_PROBE_TLB_REQUEST_VALUE,
                        context->TlbTransactionEvidence.RequestValue);
                    Bc250ProbeRecord(BC250_PROBE_TLB_RANGE_LOW_BEFORE,
                        context->TlbTransactionEvidence.RangeLowBefore);
                    Bc250ProbeRecord(BC250_PROBE_TLB_RANGE_HIGH_BEFORE,
                        context->TlbTransactionEvidence.RangeHighBefore);
                    Bc250ProbeRecord(BC250_PROBE_TLB_RANGE_LOW_PROGRAMMED,
                        context->TlbTransactionEvidence.RangeLowProgrammed);
                    Bc250ProbeRecord(BC250_PROBE_TLB_RANGE_HIGH_PROGRAMMED,
                        context->TlbTransactionEvidence.RangeHighProgrammed);
                    Bc250ProbeRecord(BC250_PROBE_TLB_REQUEST_DUMMY_READ,
                        context->TlbTransactionEvidence.RequestDummyRead);
                    Bc250ProbeRecord(BC250_PROBE_TLB_ACK_BEFORE,
                        context->TlbTransactionEvidence.AckBefore);
                    Bc250ProbeRecord(BC250_PROBE_TLB_ACK_AFTER,
                        context->TlbTransactionEvidence.AckAfter);
                    Bc250ProbeRecord(BC250_PROBE_TLB_POLL_COUNT,
                        context->TlbTransactionEvidence.PollCount);
                    Bc250ProbeRecord(BC250_PROBE_TLB_REQUEST_WRITTEN,
                        context->TlbTransactionEvidence.RequestWritten);
                    Bc250ProbeRecord(BC250_PROBE_TLB_DUMMY_READ_COMPLETED,
                        context->TlbTransactionEvidence.DummyReadCompleted);
                    Bc250ProbeRecord(BC250_PROBE_TLB_ACK_OBSERVED,
                        context->TlbTransactionEvidence.AckObserved);
                    Bc250ProbeRecord(BC250_PROBE_TLB_RANGE_RESTORE_PASSED,
                        context->TlbTransactionEvidence.RangeRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_BACKING_STATUS,
                        (ULONG)context->GcvmBackingStatus);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_PT_OFFSET_LOW32,
                        (ULONG)context->GcvmBackingEvidence.PageTableOffset);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_PT_PHYSICAL_LOW32,
                        (ULONG)context->GcvmBackingEvidence.PageTablePhysical);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_PT_PHYSICAL_HIGH32,
                        (ULONG)(context->GcvmBackingEvidence.PageTablePhysical >> 32));
                    Bc250ProbeRecord(BC250_PROBE_GCVM_TARGET_OFFSET_LOW32,
                        (ULONG)context->GcvmBackingEvidence.TargetOffset);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_FIRST_PTE_LOW32,
                        (ULONG)context->GcvmBackingEvidence.FirstPte);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_FIRST_PTE_HIGH32,
                        (ULONG)(context->GcvmBackingEvidence.FirstPte >> 32));
                    Bc250ProbeRecord(BC250_PROBE_GCVM_BACKING_OWNED,
                        context->GcvmBackingEvidence.OwnershipPassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_BACKING_READBACK,
                        context->GcvmBackingEvidence.WriteReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_BACKING_RESTORE,
                        context->GcvmBackingEvidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_VMID8_STATUS,
                        (ULONG)context->GcvmVmid8Status);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_VMID8_CNTL_BEFORE,
                        context->GcvmVmid8Evidence.ContextControlBefore);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_VMID8_READBACK,
                        context->GcvmVmid8Evidence.ProgramReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_VMID8_TLB_PASSED,
                        context->GcvmVmid8Evidence.TlbFlushPassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_VMID8_RESTORE,
                        context->GcvmVmid8Evidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_INERT_PHYSICAL_READY,
                        context->GcvmVmid8Evidence.PhysicalReady != 0u &&
                        context->GcvmBackingEvidence.ExecutePassed != 0u &&
                        context->GcvmBackingEvidence.RestorePassed != 0u);
                    {
                        BC250_MCDM_RUNTIME_CAPS caps;
                        NTSTATUS capsStatus = Bc250ActiveQueryRuntimeCaps(
                            context, &caps);
                        Bc250ProbeRecord(BC250_PROBE_RUNTIME_CAPS_STATUS,
                            (ULONG)capsStatus);
                        Bc250ProbeRecord(BC250_PROBE_RUNTIME_CAPS_READY,
                            caps.Ready);
                        Bc250ProbeRecord(BC250_PROBE_RUNTIME_CAPS_BASE_LOW32,
                            (ULONG)caps.LocalSegmentBaseOffset);
                        Bc250ProbeRecord(BC250_PROBE_RUNTIME_CAPS_LOCAL_LOW32,
                            (ULONG)caps.LocalSegmentBytes);
                        Bc250ProbeRecord(BC250_PROBE_RUNTIME_CAPS_NODE_COUNT,
                            caps.NodeCount);
                        Bc250ProbeRecord(BC250_PROBE_MCDM_FULL_CAPS_READY,
                            caps.FullMcdmReady);
                        Bc250ProbeRecord(BC250_PROBE_MCDM_SEGMENT4_READY,
                            caps.Ready);
                        if (NT_SUCCESS(capsStatus) && caps.Ready != 0u) {
                            context->AllocationSegmentBytes =
                                caps.LocalSegmentBytes;
                            context->AllocationSegmentSet = 1u;
                        }
                        Bc250ProbeRecord(
                            BC250_PROBE_ALLOCATION_SEGMENT_SET,
                            context->AllocationSegmentSet);
                        Bc250ProbeRecord(
                            BC250_PROBE_ALLOCATION_MAX_LOW32,
                            (ULONG)context->AllocationSegmentBytes);
                        Bc250ProbeRecord(
                            BC250_PROBE_GPUVA_DESCRIPTOR_READY,
                            caps.GpuVaReady != 0u &&
                            caps.GpuVaBitCount != 0u &&
                            caps.PageTableLevelCount != 0u);
                        Bc250ProbeRecord(
                            BC250_PROBE_ROOT_PAGE_TABLE_ABI_READY,
                            (ULONG)InterlockedCompareExchange(
                                &context->RootPageTableAbiReady, 1, 1));
                        Bc250ProbeRecord(
                            BC250_PROBE_ROOT_PAGE_TABLE_MAX_ENTRIES,
                            context->RootPageTableMaxEntries);
                        Bc250ProbeRecord(
                            BC250_PROBE_PAGING_CPU_UPDATE_READY,
                            caps.GpuVaReady != 0u &&
                            caps.PagingReady != 0u);
                        Bc250ProbeRecord(
                            BC250_PROBE_TLB_FLUSH_PHYSICAL_READY,
                            context->TlbTransactionEvidence.PhysicalReady);
                    }
                }
            }
        }
    }

    InterlockedExchange(&context->IsActive, 0);
    InterlockedExchange(&context->ContextAbiReady, 0);
    InterlockedExchange(&context->AllocationAbiReady, 0);
    InterlockedExchange(&context->RootPageTableAbiReady, 0);
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
    InterlockedExchange(&context->RootPageTableAbiReady, 0);
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
    Bc250McdmInstallGpuVaLifecycle(
        Bc250ActiveSetRootPageTableLifecycle);
    Bc250McdmInstallAllocationLifecycle(
        Bc250ActiveCreateAllocationLifecycle,
        Bc250ActiveDestroyAllocationLifecycle,
        Bc250ActiveOpenAllocationLifecycle,
        Bc250ActiveCloseAllocationLifecycle);
    Bc250McdmInstallDebugLifecycle(Bc250ActiveCollectDebugLifecycle);
    Bc250McdmInstallRuntimeCapsProvider(Bc250ActiveQueryRuntimeCaps);

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
static void Bc250ActiveTlbStall(void *Context, uint32_t Microseconds)
{
    UNREFERENCED_PARAMETER(Context);
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeStallExecutionProcessor(Microseconds);
#else
    UNREFERENCED_PARAMETER(Microseconds);
#endif
}

static uint32_t Bc250ActiveVmidRead32(
    void *Context, uint32_t ByteOffset, uint32_t *Value)
{
    BC250_DXGK_ACTIVE_CONTEXT *active =
        (BC250_DXGK_ACTIVE_CONTEXT *)Context;
    if (active == NULL) return 0u;
    return Bc250MmioTopologyRead32(
        &active->Core.MmioTransport, ByteOffset, Value);
}

static uint32_t Bc250ActiveVmidWrite32(
    void *Context, uint32_t ByteOffset, uint32_t Value)
{
    BC250_DXGK_ACTIVE_CONTEXT *active =
        (BC250_DXGK_ACTIVE_CONTEXT *)Context;
    if (active == NULL) return 0u;
    return Bc250MmioTopologyWrite32(
        &active->Core.MmioTransport, ByteOffset, Value);
}

static uint32_t Bc250ActiveFlushMappedGfxhubTlb(void *Context)
{
    BC250_DXGK_ACTIVE_CONTEXT *active =
        (BC250_DXGK_ACTIVE_CONTEXT *)Context;
    BC250_PAGING_FLUSH_TLB_PARAMS params;
    BC250_GFXHUB_TLB_FLUSH_PLAN plan;
    BC250_TLB_TRANSACTION_INTERFACE interfaceData;

    if (active == NULL) return 0u;
    RtlZeroMemory(&active->TlbTransactionEvidence,
        sizeof(active->TlbTransactionEvidence));
    RtlZeroMemory(&params, sizeof(params));
    params.Vmid = 8u;
    params.HubIndex = 0u;
    params.FlushType = 0u;
    if (Bc250PagingBuildGfxhubTlbFlushPlan(&params, &plan) !=
        BC250_PAGING_OK) {
        active->TlbTransactionStatus = BC250_TLB_TRANSACTION_BAD_PLAN;
        return 0u;
    }
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Stall = Bc250ActiveTlbStall;
    interfaceData.Context = &active->Core.MmioTransport;
    interfaceData.Identity = BC250_TLB_TRANSPORT_IDENTITY;
    active->TlbTransactionStatus = Bc250RunGfxhubTlbTransaction(
        &plan, &interfaceData, 1000u, &active->TlbTransactionEvidence);
    return active->TlbTransactionStatus == BC250_TLB_TRANSACTION_OK &&
        active->TlbTransactionEvidence.PhysicalReady != 0u;
}

static uint32_t Bc250ActiveExecuteGcvmBacking(
    void *Context, uint64_t PageTablePhysical)
{
    BC250_DXGK_ACTIVE_CONTEXT *active =
        (BC250_DXGK_ACTIVE_CONTEXT *)Context;
    BC250_GCVM_VMID8_INTERFACE interfaceData;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;

    if (active == NULL) return 0u;
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &active->Core.MmioTransport, &active->Core.DxgkInterface,
        active->Core.ResolvedBars, active->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) return 0u;
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250ActiveVmidRead32;
    interfaceData.Write32 = Bc250ActiveVmidWrite32;
    interfaceData.Flush = Bc250ActiveFlushMappedGfxhubTlb;
    interfaceData.Context = active;
    interfaceData.Identity = BC250_GCVM_VMID8_IDENTITY;
    active->GcvmVmid8Status = Bc250RunGcvmVmid8InertTransaction(
        PageTablePhysical, &interfaceData, &active->GcvmVmid8Evidence);

    mmioStatus = Bc250MmioTopologyUnmap(&active->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        active->GcvmVmid8Evidence.PhysicalReady = 0u;
        return 0u;
    }
    return active->GcvmVmid8Status == BC250_GCVM_VMID8_OK &&
        active->GcvmVmid8Evidence.PhysicalReady != 0u;
}
