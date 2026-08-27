#include "bc250_mcdm_callbacks.h"
#include "../core/bc250_adapter_info_contract.h"
#include "../core/bc250_segment_contract.h"
#include "../core/bc250_tdr_recovery_contract.h"
#include "../core/bc250_context_dispatcher_contract.h"
#include "../core/bc250_paging_contract.h"
#include "../core/bc250_ih_contract.h"
#include "../core/bc250_preemption_contract.h"
#include "../core/bc250_gpu_clock_contract.h"

#define BC250_MCDM_UNSUPPORTED_1(Name, Type1, Arg1) \
    NTSTATUS APIENTRY Name(Type1 Arg1) \
    { \
        UNREFERENCED_PARAMETER(Arg1); \
        return STATUS_NOT_SUPPORTED; \
    }

#define BC250_MCDM_UNSUPPORTED_2(Name, Type1, Arg1, Type2, Arg2) \
    NTSTATUS APIENTRY Name(Type1 Arg1, Type2 Arg2) \
    { \
        UNREFERENCED_PARAMETER(Arg1); \
        UNREFERENCED_PARAMETER(Arg2); \
        return STATUS_NOT_SUPPORTED; \
    }

static BC250_MCDM_CREATE_PROCESS_OBJECT g_Bc250CreateProcessObject;
static BC250_MCDM_DESTROY_PROCESS_OBJECT g_Bc250DestroyProcessObject;
static BC250_MCDM_CREATE_DEVICE_OBJECT g_Bc250CreateDeviceObject;
static BC250_MCDM_DESTROY_DEVICE_OBJECT g_Bc250DestroyDeviceObject;
static BC250_MCDM_CREATE_CONTEXT_OBJECT g_Bc250CreateContextObject;
static BC250_MCDM_DESTROY_CONTEXT_OBJECT g_Bc250DestroyContextObject;
static BC250_MCDM_SET_ROOT_PAGE_TABLE_OBJECT g_Bc250SetRootPageTableObject;
static BC250_MCDM_CREATE_ALLOCATION_OBJECTS g_Bc250CreateAllocationObjects;
static BC250_MCDM_DESTROY_ALLOCATION_OBJECTS g_Bc250DestroyAllocationObjects;
static BC250_MCDM_OPEN_ALLOCATION_OBJECTS g_Bc250OpenAllocationObjects;
static BC250_MCDM_CLOSE_ALLOCATION_OBJECTS g_Bc250CloseAllocationObjects;
static BC250_MCDM_COLLECT_DEBUG_SNAPSHOT g_Bc250CollectDebugSnapshot;
static BC250_MCDM_QUERY_RUNTIME_CAPS g_Bc250QueryRuntimeCaps;

static NTSTATUS Bc250McdmEncodeVidMmPte(
    const BC250_MCDM_RUNTIME_CAPS *Caps,
    const DXGK_PTE *Source,
    UINT64 *Encoded)
{
    UINT64 physicalAddress;
    UINT32 flags;

    if (Caps == NULL || Source == NULL || Encoded == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *Encoded = 0u;
    if (Source->Valid == 0u) return STATUS_SUCCESS;
    if (Source->Zero != 0u || Source->LargePage != 0u ||
        Source->PhysicalAdapterIndex != 0u ||
        Source->PageTablePageSize != DXGK_PTE_PAGE_TABLE_PAGE_4KB ||
        Source->Segment != 1u ||
        (Source->PageAddress & (BC250_PAGE_SIZE_4K - 1u)) != 0u ||
        Source->PageAddress >= Caps->LocalSegmentBytes) {
        return STATUS_NOT_SUPPORTED;
    }
    if (Caps->LocalSegmentBaseOffset >
        (UINT64)~(UINT64)0 - Source->PageAddress) {
        return STATUS_INTEGER_OVERFLOW;
    }
    physicalAddress = Caps->LocalSegmentBaseOffset + Source->PageAddress;
    flags = BC250_PTE_VALID | BC250_PTE_READABLE;
    if (Source->ReadOnly == 0u) flags |= BC250_PTE_WRITEABLE;
    if (Source->NoExecute == 0u) flags |= BC250_PTE_EXECUTABLE;
    if (Source->CacheCoherent != 0u) flags |= BC250_PTE_SNOOPED;
    return Bc250GcvmEncodePte(physicalAddress, flags, Encoded) ==
        BC250_GCVM_OK ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

static NTSTATUS Bc250McdmBuildCpuPageTableUpdate(
    const BC250_MCDM_RUNTIME_CAPS *Caps,
    DXGKARG_BUILDPAGINGBUFFER *Args)
{
    DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *update;
    UINT64 *destination;
    UINT32 index;
    UINT32 maxEntries;

    if (Caps == NULL || Args == NULL) return STATUS_INVALID_PARAMETER;
    update = &Args->UpdatePageTable;
    if (update->UpdateMode != DXGK_PAGETABLEUPDATE_CPU_VIRTUAL ||
        update->PageTableAddress.CpuVirtual == NULL ||
        update->pPageTableEntries == NULL ||
        update->NumPageTableEntries == 0u || update->Reserved0 != 0u ||
        update->Flags.Reserved != 0u || update->DriverProtection != 0u ||
        update->pPageTableEntries64KB != NULL ||
        update->PageTableLevel >= Caps->PageTableLevelCount ||
        Caps->PageTableIndexBitCount == 0u ||
        Caps->PageTableIndexBitCount >= 32u) {
        return STATUS_INVALID_PARAMETER;
    }
    maxEntries = 1u << Caps->PageTableIndexBitCount;
    if (update->StartIndex >= maxEntries ||
        update->NumPageTableEntries > maxEntries - update->StartIndex) {
        return STATUS_INVALID_PARAMETER;
    }
    for (index = 0u; index < update->NumPageTableEntries; ++index) {
        UINT64 encoded;
        NTSTATUS status = Bc250McdmEncodeVidMmPte(
            Caps, &update->pPageTableEntries[index], &encoded);
        if (!NT_SUCCESS(status)) return status;
    }
    destination = (UINT64 *)update->PageTableAddress.CpuVirtual;
    for (index = 0u; index < update->NumPageTableEntries; ++index) {
        UINT64 encoded;
        (void)Bc250McdmEncodeVidMmPte(
            Caps, &update->pPageTableEntries[index], &encoded);
        destination[update->StartIndex + index] = encoded;
    }
    return STATUS_SUCCESS;
}

VOID Bc250McdmInstallRuntimeCapsProvider(
    BC250_MCDM_QUERY_RUNTIME_CAPS Provider)
{
    g_Bc250QueryRuntimeCaps = Provider;
}

VOID Bc250McdmInstallProcessLifecycle(
    BC250_MCDM_CREATE_PROCESS_OBJECT CreateProcessObject,
    BC250_MCDM_DESTROY_PROCESS_OBJECT DestroyProcessObject)
{
    g_Bc250CreateProcessObject = CreateProcessObject;
    g_Bc250DestroyProcessObject = DestroyProcessObject;
}

VOID Bc250McdmInstallDeviceLifecycle(
    BC250_MCDM_CREATE_DEVICE_OBJECT CreateDeviceObject,
    BC250_MCDM_DESTROY_DEVICE_OBJECT DestroyDeviceObject)
{
    g_Bc250CreateDeviceObject = CreateDeviceObject;
    g_Bc250DestroyDeviceObject = DestroyDeviceObject;
}

VOID Bc250McdmInstallContextLifecycle(
    BC250_MCDM_CREATE_CONTEXT_OBJECT CreateContextObject,
    BC250_MCDM_DESTROY_CONTEXT_OBJECT DestroyContextObject)
{
    g_Bc250CreateContextObject = CreateContextObject;
    g_Bc250DestroyContextObject = DestroyContextObject;
}

VOID Bc250McdmInstallGpuVaLifecycle(
    BC250_MCDM_SET_ROOT_PAGE_TABLE_OBJECT SetRootPageTableObject)
{
    g_Bc250SetRootPageTableObject = SetRootPageTableObject;
}

VOID Bc250McdmInstallAllocationLifecycle(
    BC250_MCDM_CREATE_ALLOCATION_OBJECTS CreateAllocationObjects,
    BC250_MCDM_DESTROY_ALLOCATION_OBJECTS DestroyAllocationObjects,
    BC250_MCDM_OPEN_ALLOCATION_OBJECTS OpenAllocationObjects,
    BC250_MCDM_CLOSE_ALLOCATION_OBJECTS CloseAllocationObjects)
{
    g_Bc250CreateAllocationObjects = CreateAllocationObjects;
    g_Bc250DestroyAllocationObjects = DestroyAllocationObjects;
    g_Bc250OpenAllocationObjects = OpenAllocationObjects;
    g_Bc250CloseAllocationObjects = CloseAllocationObjects;
}

VOID Bc250McdmInstallDebugLifecycle(
    BC250_MCDM_COLLECT_DEBUG_SNAPSHOT CollectDebugSnapshot)
{
    g_Bc250CollectDebugSnapshot = CollectDebugSnapshot;
}

NTSTATUS APIENTRY Bc250McdmBuildPagingBuffer(
    CONST HANDLE hAdapter,
    DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer)
{
    BC250_MCDM_RUNTIME_CAPS caps;

    if (hAdapter == NULL || pBuildPagingBuffer == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pBuildPagingBuffer->Operation != DXGK_OPERATION_UPDATE_PAGE_TABLE) {
        if (pBuildPagingBuffer->pDmaBuffer == NULL) {
            return STATUS_INVALID_PARAMETER;
        }
        return STATUS_NOT_SUPPORTED;
    }
    if (g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.FullMcdmReady == 0u || caps.GpuVaReady == 0u ||
        caps.PagingReady == 0u) {
        return STATUS_DEVICE_NOT_READY;
    }
    return Bc250McdmBuildCpuPageTableUpdate(&caps, pBuildPagingBuffer);
}

NTSTATUS APIENTRY Bc250McdmCloseAllocation(
    CONST HANDLE hDevice,
    CONST DXGKARG_CLOSEALLOCATION *pCloseAllocation)
{
    if (hDevice == NULL || pCloseAllocation == NULL ||
        pCloseAllocation->NumAllocations == 0u ||
        pCloseAllocation->pOpenHandleList == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250CloseAllocationObjects == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250CloseAllocationObjects(hDevice, pCloseAllocation);
}

NTSTATUS APIENTRY Bc250McdmCollectDbgInfo(
    CONST HANDLE hAdapter,
    CONST DXGKARG_COLLECTDBGINFO *pCollectDbgInfo)
{
    if (hAdapter == NULL || pCollectDbgInfo == NULL ||
        pCollectDbgInfo->pBuffer == NULL || pCollectDbgInfo->BufferSize == 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250CollectDebugSnapshot == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250CollectDebugSnapshot(hAdapter, pCollectDbgInfo);
}

NTSTATUS APIENTRY Bc250McdmCreateAllocation(
    CONST HANDLE hAdapter,
    DXGKARG_CREATEALLOCATION *pCreateAllocation)
{
    UINT index;
    if (hAdapter == NULL || pCreateAllocation == NULL ||
        pCreateAllocation->pAllocationInfo == NULL ||
        pCreateAllocation->NumAllocations == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    for (index = 0u; index < pCreateAllocation->NumAllocations; ++index) {
        pCreateAllocation->pAllocationInfo[index].hAllocation = NULL;
    }
    if (g_Bc250CreateAllocationObjects == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250CreateAllocationObjects(hAdapter, pCreateAllocation);
}

NTSTATUS APIENTRY Bc250McdmCreateContext(
    CONST HANDLE hDevice,
    DXGKARG_CREATECONTEXT *pCreateContext)
{
    HANDLE contextHandle = NULL;
    NTSTATUS status;

    if (hDevice == NULL || pCreateContext == NULL ||
        pCreateContext->hContext == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pCreateContext->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }
    pCreateContext->hContext = NULL;
    RtlZeroMemory(&pCreateContext->ContextInfo,
        sizeof(pCreateContext->ContextInfo));
    if (g_Bc250CreateContextObject == NULL) return STATUS_NOT_SUPPORTED;
    status = g_Bc250CreateContextObject(hDevice,
        pCreateContext->NodeOrdinal, &contextHandle,
        &pCreateContext->ContextInfo);
    if (!NT_SUCCESS(status)) return status;
    pCreateContext->hContext = contextHandle;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250McdmCreateDevice(
    CONST HANDLE hAdapter,
    DXGKARG_CREATEDEVICE *pCreateDevice)
{
    HANDLE deviceHandle = NULL;
    NTSTATUS status;

    if (hAdapter == NULL || pCreateDevice == NULL ||
        pCreateDevice->hDevice == NULL || pCreateDevice->hKmdProcess == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    pCreateDevice->hDevice = NULL;
    if (g_Bc250CreateDeviceObject == NULL) return STATUS_NOT_SUPPORTED;
    status = g_Bc250CreateDeviceObject(
        hAdapter, pCreateDevice->hKmdProcess, &deviceHandle);
    if (!NT_SUCCESS(status)) return status;
    pCreateDevice->hDevice = deviceHandle;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250McdmCreateProcess(
    CONST HANDLE hAdapter,
    DXGKARG_CREATEPROCESS *pArgs)
{
    NTSTATUS status;
    HANDLE processHandle = NULL;

    if (hAdapter == NULL || pArgs == NULL || pArgs->hDxgkProcess == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    pArgs->hKmdProcess = NULL;
    if (g_Bc250CreateProcessObject == NULL) return STATUS_NOT_SUPPORTED;
    status = g_Bc250CreateProcessObject(
        hAdapter, pArgs->hDxgkProcess, &processHandle);
    if (!NT_SUCCESS(status)) return status;
    pArgs->hKmdProcess = processHandle;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250McdmDescribeAllocation(
    CONST HANDLE hAdapter,
    DXGKARG_DESCRIBEALLOCATION *pDescribeAllocation)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pDescribeAllocation == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmDestroyAllocation(
    CONST HANDLE hAdapter,
    CONST DXGKARG_DESTROYALLOCATION *pDestroyAllocation)
{
    if (hAdapter == NULL || pDestroyAllocation == NULL ||
        pDestroyAllocation->NumAllocations == 0u ||
        pDestroyAllocation->pAllocationList == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250DestroyAllocationObjects == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250DestroyAllocationObjects(hAdapter, pDestroyAllocation);
}

NTSTATUS APIENTRY Bc250McdmDestroyContext(CONST HANDLE hContext)
{
    if (hContext == NULL) return STATUS_INVALID_PARAMETER;
    if (g_Bc250DestroyContextObject == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250DestroyContextObject(hContext);
}

NTSTATUS APIENTRY Bc250McdmDestroyDevice(CONST HANDLE hDevice)
{
    if (hDevice == NULL) return STATUS_INVALID_PARAMETER;
    if (g_Bc250DestroyDeviceObject == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250DestroyDeviceObject(hDevice);
}

NTSTATUS APIENTRY Bc250McdmDestroyProcess(
    CONST HANDLE hAdapter,
    CONST HANDLE hKmdProcess)
{
    if (hAdapter == NULL || hKmdProcess == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250DestroyProcessObject == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250DestroyProcessObject(hAdapter, hKmdProcess);
}

NTSTATUS Bc250McdmDispatchIoRequest(
    CONST PVOID MiniportDeviceContext,
    ULONG VidPnSourceId,
    PVIDEO_REQUEST_PACKET VideoRequestPacket)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(VidPnSourceId);
    UNREFERENCED_PARAMETER(VideoRequestPacket);
    return STATUS_NOT_SUPPORTED;
}

BC250_MCDM_UNSUPPORTED_2(Bc250McdmFormatHistoryBuffer,
    CONST HANDLE, hContext,
    DXGKARG_FORMATHISTORYBUFFER *, pFormatData)

NTSTATUS APIENTRY Bc250McdmCalibrateGpuClock(
    CONST HANDLE hAdapter,
    UINT32 NodeOrdinal,
    UINT32 EngineOrdinal,
    DXGKARG_CALIBRATEGPUCLOCK *pClockCalibration)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(EngineOrdinal);

    if (pClockCalibration == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmGetNodeMetadata(
    CONST HANDLE hAdapter,
    UINT NodeOrdinalAndAdapterIndex,
    DXGKARG_GETNODEMETADATA *pGetNodeMetadata)
{
    UINT nodeIndex = NodeOrdinalAndAdapterIndex & 0xFFFFu;

    BC250_MCDM_RUNTIME_CAPS caps;

    if (pGetNodeMetadata == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (nodeIndex >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.Ready == 0u || caps.FullMcdmReady == 0u ||
        caps.SubmitReady == 0u || nodeIndex >= caps.NodeCount) {
        return STATUS_DEVICE_NOT_READY;
    }
    RtlZeroMemory(pGetNodeMetadata, sizeof(*pGetNodeMetadata));
    pGetNodeMetadata->EngineType = nodeIndex == BC250_ADAPTER_NODE_SDMA ?
        DXGK_ENGINE_TYPE_COPY : DXGK_ENGINE_TYPE_OTHER;
    if (nodeIndex == BC250_ADAPTER_NODE_SDMA) {
        static const WCHAR name[] = L"BC250 SDMA0";
        RtlCopyMemory(pGetNodeMetadata->FriendlyName, name, sizeof(name));
    } else {
        static const WCHAR name[] = L"BC250 Compute MEC";
        RtlCopyMemory(pGetNodeMetadata->FriendlyName, name, sizeof(name));
    }
    pGetNodeMetadata->GpuMmuSupported = caps.GpuVaReady != 0u;
    pGetNodeMetadata->IoMmuSupported = FALSE;
    return STATUS_SUCCESS;
}

SIZE_T APIENTRY Bc250McdmGetRootPageTableSize(
    CONST HANDLE hAdapter,
    DXGKARG_GETROOTPAGETABLESIZE *pArgs)
{
    BC250_MCDM_RUNTIME_CAPS caps;
    UINT64 bytes;
    UINT32 entries;

    if (hAdapter == NULL || pArgs == NULL ||
        pArgs->PhysicalAdapterIndex != 0u || pArgs->NumberOfPte == 0u ||
        g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.FullMcdmReady == 0u || caps.GpuVaReady == 0u ||
        caps.PageTableLevelCount != 2u ||
        caps.PageTableSizeBytes == 0u ||
        caps.PageTableAlignmentBytes == 0u) {
        if (pArgs != NULL) pArgs->NumberOfPte = 0u;
        return 0u;
    }
    entries = pArgs->NumberOfPte;
    bytes = (UINT64)entries * sizeof(UINT64);
    if (bytes > MAXULONG_PTR - (caps.PageTableAlignmentBytes - 1u)) {
        pArgs->NumberOfPte = 0u;
        return 0u;
    }
    bytes = (bytes + caps.PageTableAlignmentBytes - 1u) &
        ~((UINT64)caps.PageTableAlignmentBytes - 1u);
    pArgs->NumberOfPte = (UINT32)(bytes / sizeof(UINT64));
    return (SIZE_T)bytes;
}

BC250_MCDM_UNSUPPORTED_2(Bc250McdmGetStandardAllocationDriverData,
    CONST HANDLE, hAdapter,
    DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA *,
    pGetStandardAllocationDriverData)

NTSTATUS APIENTRY Bc250McdmOpenAllocation(
    CONST HANDLE hDevice,
    CONST DXGKARG_OPENALLOCATION *pOpenAllocation)
{
    UINT index;
    if (hDevice == NULL || pOpenAllocation == NULL ||
        pOpenAllocation->NumAllocations == 0u ||
        pOpenAllocation->pOpenAllocation == NULL ||
        pOpenAllocation->Flags.Reserved != 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    for (index = 0u; index < pOpenAllocation->NumAllocations; ++index) {
        pOpenAllocation->pOpenAllocation[index].hDeviceSpecificAllocation = NULL;
    }
    if (g_Bc250OpenAllocationObjects == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250OpenAllocationObjects(hDevice, pOpenAllocation);
}

NTSTATUS APIENTRY Bc250McdmPreemptCommand(
    CONST HANDLE hAdapter,
    CONST DXGKARG_PREEMPTCOMMAND *pPreemptCommand)
{
    UNREFERENCED_PARAMETER(hAdapter);

    if (pPreemptCommand == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pPreemptCommand->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmQueryAdapterInfo(
    CONST HANDLE hAdapter,
    CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo)
{
    BC250_MCDM_RUNTIME_CAPS runtimeCaps;
    NTSTATUS capsStatus;

    if (pQueryAdapterInfo == NULL || pQueryAdapterInfo->pOutputData == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(&runtimeCaps, sizeof(runtimeCaps));
    capsStatus = g_Bc250QueryRuntimeCaps != NULL ?
        g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &runtimeCaps) :
        STATUS_DEVICE_NOT_READY;

    switch (pQueryAdapterInfo->Type) {
    case DXGKQAITYPE_DRIVERCAPS: {
        DXGK_DRIVERCAPS *caps;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_DRIVERCAPS)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.Ready == 0u ||
            runtimeCaps.FullMcdmReady == 0u ||
            runtimeCaps.GpuVaReady == 0u ||
            runtimeCaps.PagingReady == 0u ||
            runtimeCaps.TdrReady == 0u ||
            runtimeCaps.PreemptionReady == 0u ||
            runtimeCaps.SubmitReady == 0u ||
            runtimeCaps.HistoryBufferReady == 0u) {
            return STATUS_DEVICE_NOT_READY;
        }
        caps = (DXGK_DRIVERCAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(caps, sizeof(*caps));
        caps->HighestAcceptableAddress.QuadPart = MAXLONGLONG;
        caps->WDDMVersion = DXGKDDI_WDDMv2_7;
        caps->SchedulingCaps.MultiEngineAware = 1u;
        caps->SchedulingCaps.PreemptionAware = 1u;
        caps->SchedulingCaps.LowIrqlPreemptCommand = 1u;
        caps->MemoryManagementCaps.VirtualAddressingSupported = 1u;
        caps->MemoryManagementCaps.GpuMmuSupported = 1u;
        caps->MemoryManagementCaps.ParavirtualizationSupported = 1u;
        caps->PreemptionCaps.ComputePreemptionGranularity =
            D3DKMDT_COMPUTE_PREEMPTION_DMA_BUFFER_BOUNDARY;
        caps->SupportPerEngineTDR = TRUE;
        caps->GpuEngineTopology.NbAsymetricProcessingNodes =
            runtimeCaps.NodeCount;
        caps->MiscCaps.ComputeOnly = 1u;
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_PHYSICALADAPTERCAPS: {
        DXGK_PHYSICALADAPTERCAPS *caps;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_PHYSICALADAPTERCAPS)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.Ready == 0u ||
            runtimeCaps.FullMcdmReady == 0u) {
            return STATUS_DEVICE_NOT_READY;
        }
        caps = (DXGK_PHYSICALADAPTERCAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(caps, sizeof(*caps));
        caps->NumExecutionNodes = (WORD)runtimeCaps.NodeCount;
        caps->PagingNodeIndex = (WORD)BC250_ADAPTER_NODE_SDMA;
        caps->VirtualCopyNodeIndex = BC250_ADAPTER_NODE_SDMA;
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_GPUMMUCAPS: {
        const DXGK_QUERYGPUMMUCAPSIN *in;
        DXGK_GPUMMUCAPS *caps;
        if (pQueryAdapterInfo->pInputData == NULL ||
            pQueryAdapterInfo->InputDataSize < sizeof(*in)) {
            return STATUS_INVALID_PARAMETER;
        }
        if (pQueryAdapterInfo->OutputDataSize < sizeof(*caps)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        in = (const DXGK_QUERYGPUMMUCAPSIN *)
            pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0u) {
            return STATUS_INVALID_PARAMETER;
        }
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.Ready == 0u ||
            runtimeCaps.FullMcdmReady == 0u ||
            runtimeCaps.GpuVaReady == 0u ||
            runtimeCaps.GpuVaBitCount == 0u ||
            runtimeCaps.PageTableLevelCount == 0u) {
            return STATUS_DEVICE_NOT_READY;
        }
        caps = (DXGK_GPUMMUCAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(caps, sizeof(*caps));
        caps->ReadOnlyMemorySupported = 1u;
        caps->NoExecuteMemorySupported = 1u;
        caps->ExplicitPageTableInvalidation = 1u;
        caps->PageTableUpdateMode = DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
        caps->VirtualAddressBitCount = runtimeCaps.GpuVaBitCount;
        caps->PageTableLevelCount = runtimeCaps.PageTableLevelCount;
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_PAGETABLELEVELDESC: {
        const DXGK_QUERYPAGETABLELEVELDESCIN *in;
        DXGK_PAGE_TABLE_LEVEL_DESC *desc;
        if (pQueryAdapterInfo->pInputData == NULL ||
            pQueryAdapterInfo->InputDataSize < sizeof(*in)) {
            return STATUS_INVALID_PARAMETER;
        }
        if (pQueryAdapterInfo->OutputDataSize < sizeof(*desc)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        in = (const DXGK_QUERYPAGETABLELEVELDESCIN *)
            pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0u) {
            return STATUS_INVALID_PARAMETER;
        }
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.Ready == 0u ||
            runtimeCaps.FullMcdmReady == 0u ||
            runtimeCaps.GpuVaReady == 0u ||
            runtimeCaps.PageTableLevelCount == 0u ||
            runtimeCaps.PageTableIndexBitCount == 0u ||
            runtimeCaps.PageTableSizeBytes == 0u) {
            return STATUS_DEVICE_NOT_READY;
        }
        if (in->LevelIndex >= runtimeCaps.PageTableLevelCount) {
            return STATUS_INVALID_PARAMETER;
        }
        desc = (DXGK_PAGE_TABLE_LEVEL_DESC *)
            pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(desc, sizeof(*desc));
        desc->PageTableIndexBitCount =
            runtimeCaps.PageTableIndexBitCount;
        desc->PageTableSegmentId = 1u;
        desc->PagingProcessPageTableSegmentId = 1u;
        desc->PageTableSizeInBytes = runtimeCaps.PageTableSizeBytes;
        desc->PageTableAlignmentInBytes =
            runtimeCaps.PageTableAlignmentBytes;
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYSEGMENT4: {
        DXGK_QUERYSEGMENTOUT4 *out;
        DXGK_QUERYSEGMENTIN4 *in;
        DXGK_SEGMENTDESCRIPTOR4 *segment;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(*out)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.Ready == 0u) {
            return STATUS_DEVICE_NOT_READY;
        }
        out = (DXGK_QUERYSEGMENTOUT4 *)pQueryAdapterInfo->pOutputData;
        if (out->NbSegment == 0u) {
            out->NbSegment = 1u;
            return STATUS_SUCCESS;
        }
        if (pQueryAdapterInfo->pInputData == NULL ||
            pQueryAdapterInfo->InputDataSize < sizeof(*in) ||
            out->NbSegment != 1u || out->pSegmentDescriptor == NULL ||
            out->SegmentDescriptorStride < sizeof(*segment)) {
            return STATUS_INVALID_PARAMETER;
        }
        in = (DXGK_QUERYSEGMENTIN4 *)pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0u) return STATUS_INVALID_PARAMETER;
        segment = (DXGK_SEGMENTDESCRIPTOR4 *)out->pSegmentDescriptor;
        RtlZeroMemory(segment, sizeof(*segment));
        segment->BaseAddress.QuadPart =
            (LONGLONG)runtimeCaps.LocalSegmentBaseOffset;
        segment->CpuTranslatedAddress.QuadPart =
            (LONGLONG)runtimeCaps.CpuPhysicalStart;
        segment->Size = (SIZE_T)runtimeCaps.LocalSegmentBytes;
        segment->CommitLimit = 0u;
        segment->Flags.CpuVisible = 1u;
        segment->Flags.LocalBudgetGroup = 1u;
        out->PagingBufferSegmentId = 1u;
        out->PagingBufferSize = 0x10000u;
        out->PagingBufferPrivateDataSize = 0u;
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_NUMPOWERCOMPONENTS:
        if (pQueryAdapterInfo->OutputDataSize < sizeof(UINT)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        *(UINT *)pQueryAdapterInfo->pOutputData = 0u;
        return STATUS_SUCCESS;

    case DXGKQAITYPE_HISTORYBUFFERPRECISION: {
        DXGKARG_HISTORYBUFFERPRECISION *precision;
        UINT index;
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.FullMcdmReady == 0u ||
            runtimeCaps.HistoryBufferReady == 0u) {
            return STATUS_DEVICE_NOT_READY;
        }
        if (pQueryAdapterInfo->OutputDataSize <
            sizeof(*precision) * runtimeCaps.NodeCount) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        precision = (DXGKARG_HISTORYBUFFERPRECISION *)
            pQueryAdapterInfo->pOutputData;
        for (index = 0u; index < runtimeCaps.NodeCount; ++index) {
            precision[index].PrecisionBits = 32u;
        }
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYSEGMENT:
    case DXGKQAITYPE_QUERYSEGMENT2:
    case DXGKQAITYPE_QUERYSEGMENT3:
        return STATUS_NOT_SUPPORTED;

    default:
        return STATUS_NOT_SUPPORTED;
    }
}

BC250_MCDM_UNSUPPORTED_2(Bc250McdmQueryDependentEngineGroup,
    CONST HANDLE, hAdapter,
    DXGKARG_QUERYDEPENDENTENGINEGROUP *, pQueryDependentEngineGroup)

NTSTATUS APIENTRY Bc250McdmQueryEngineStatus(
    CONST HANDLE hAdapter,
    DXGKARG_QUERYENGINESTATUS *pQueryEngineStatus)
{
    UNREFERENCED_PARAMETER(hAdapter);

    if (pQueryEngineStatus == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pQueryEngineStatus->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    pQueryEngineStatus->EngineStatus.Responsive = 0;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmResetEngine(
    CONST HANDLE hAdapter,
    DXGKARG_RESETENGINE *pResetEngine)
{
    UNREFERENCED_PARAMETER(hAdapter);

    if (pResetEngine == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pResetEngine->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    pResetEngine->LastAbortedFenceId = 0u;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmResetFromTimeout(CONST HANDLE hAdapter)
{
    UNREFERENCED_PARAMETER(hAdapter);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmRestartFromTimeout(CONST HANDLE hAdapter)
{
    UNREFERENCED_PARAMETER(hAdapter);
    return STATUS_NOT_SUPPORTED;
}

BC250_MCDM_UNSUPPORTED_2(Bc250McdmSetVirtualMachineData,
    CONST HANDLE, hAdapter,
    CONST DXGKARG_SETVIRTUALMACHINEDATA *, Args)

VOID APIENTRY Bc250McdmSetRootPageTable(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SETROOTPAGETABLE *pSetPageTable)
{
    BC250_MCDM_RUNTIME_CAPS caps;
    UINT32 maxEntries;

    if (hAdapter == NULL || pSetPageTable == NULL ||
        pSetPageTable->hContext == NULL ||
        g_Bc250SetRootPageTableObject == NULL ||
        g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.FullMcdmReady == 0u || caps.GpuVaReady == 0u ||
        caps.PageTableIndexBitCount == 0u ||
        caps.PageTableIndexBitCount >= 32u) {
        return;
    }
    maxEntries = 1u << caps.PageTableIndexBitCount;
    (void)g_Bc250SetRootPageTableObject((HANDLE)hAdapter,
        pSetPageTable->hContext, pSetPageTable->Address.SegmentId,
        pSetPageTable->Address.SegmentOffset,
        pSetPageTable->NumEntries, maxEntries);
}

NTSTATUS APIENTRY Bc250McdmSubmitCommandVirtual(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SUBMITCOMMANDVIRTUAL *pSubmitCommand)
{
    UNREFERENCED_PARAMETER(hAdapter);

    if (pSubmitCommand == NULL || pSubmitCommand->hContext == NULL ||
        pSubmitCommand->DmaBufferSize == 0u ||
        pSubmitCommand->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_NOT_SUPPORTED;
}

BOOLEAN Bc250McdmInterruptRoutine(
    CONST PVOID MiniportDeviceContext,
    ULONG MessageNumber)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(MessageNumber);
    return FALSE;
}

VOID Bc250McdmDpcRoutine(CONST PVOID MiniportDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
}

NTSTATUS Bc250McdmQueryChildRelations(
    CONST PVOID MiniportDeviceContext,
    PDXGK_CHILD_DESCRIPTOR ChildRelations,
    ULONG ChildRelationsSize)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    if (ChildRelations == NULL && ChildRelationsSize != 0) {
        return STATUS_INVALID_PARAMETER;
    }
    if (ChildRelations != NULL && ChildRelationsSize != 0) {
        RtlZeroMemory(ChildRelations, ChildRelationsSize);
    }
    return STATUS_SUCCESS;
}

NTSTATUS Bc250McdmQueryChildStatus(
    CONST PVOID MiniportDeviceContext,
    PDXGK_CHILD_STATUS ChildStatus,
    BOOLEAN NonDestructiveOnly)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildStatus);
    UNREFERENCED_PARAMETER(NonDestructiveOnly);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS Bc250McdmQueryDeviceDescriptor(
    CONST PVOID MiniportDeviceContext,
    ULONG ChildUid,
    PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildUid);
    UNREFERENCED_PARAMETER(DeviceDescriptor);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS Bc250McdmSetPowerState(
    CONST PVOID MiniportDeviceContext,
    ULONG DeviceUid,
    DEVICE_POWER_STATE DevicePowerState,
    POWER_ACTION ActionType)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(DeviceUid);
    UNREFERENCED_PARAMETER(ActionType);

    switch (DevicePowerState) {
    case PowerDeviceD0:
    case PowerDeviceD1:
    case PowerDeviceD2:
    case PowerDeviceD3:
        return STATUS_NOT_SUPPORTED;
    default:
        return STATUS_INVALID_PARAMETER;
    }
}

VOID Bc250McdmResetDevice(CONST PVOID MiniportDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
}

VOID APIENTRY Bc250McdmSetStablePowerState(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SETSTABLEPOWERSTATE *pArgs)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
}

VOID Bc250McdmUnload(VOID)
{
    Bc250McdmInstallRuntimeCapsProvider(NULL);
    Bc250McdmInstallProcessLifecycle(NULL, NULL);
    Bc250McdmInstallDeviceLifecycle(NULL, NULL);
    Bc250McdmInstallContextLifecycle(NULL, NULL);
    Bc250McdmInstallGpuVaLifecycle(NULL);
    Bc250McdmInstallAllocationLifecycle(NULL, NULL, NULL, NULL);
    Bc250McdmInstallDebugLifecycle(NULL);
}

NTSTATUS Bc250McdmPopulateRequiredCallbacks(
    DRIVER_INITIALIZATION_DATA *InitializationData)
{
    if (InitializationData == NULL) return STATUS_INVALID_PARAMETER;
    InitializationData->DxgkDdiBuildPagingBuffer =
        Bc250McdmBuildPagingBuffer;
    InitializationData->DxgkDdiCalibrateGpuClock =
        Bc250McdmCalibrateGpuClock;
    InitializationData->DxgkDdiCloseAllocation = Bc250McdmCloseAllocation;
    InitializationData->DxgkDdiCollectDbgInfo = Bc250McdmCollectDbgInfo;
    InitializationData->DxgkDdiCreateAllocation = Bc250McdmCreateAllocation;
    InitializationData->DxgkDdiCreateContext = Bc250McdmCreateContext;
    InitializationData->DxgkDdiCreateDevice = Bc250McdmCreateDevice;
    InitializationData->DxgkDdiCreateProcess = Bc250McdmCreateProcess;
    InitializationData->DxgkDdiDescribeAllocation =
        Bc250McdmDescribeAllocation;
    InitializationData->DxgkDdiDestroyAllocation =
        Bc250McdmDestroyAllocation;
    InitializationData->DxgkDdiDestroyContext = Bc250McdmDestroyContext;
    InitializationData->DxgkDdiDestroyDevice = Bc250McdmDestroyDevice;
    InitializationData->DxgkDdiDestroyProcess = Bc250McdmDestroyProcess;
    InitializationData->DxgkDdiDispatchIoRequest =
        Bc250McdmDispatchIoRequest;
    InitializationData->DxgkDdiDpcRoutine = Bc250McdmDpcRoutine;
    InitializationData->DxgkDdiFormatHistoryBuffer =
        Bc250McdmFormatHistoryBuffer;
    InitializationData->DxgkDdiGetNodeMetadata = Bc250McdmGetNodeMetadata;
    InitializationData->DxgkDdiGetRootPageTableSize =
        Bc250McdmGetRootPageTableSize;
    InitializationData->DxgkDdiGetStandardAllocationDriverData =
        Bc250McdmGetStandardAllocationDriverData;
    InitializationData->DxgkDdiInterruptRoutine = Bc250McdmInterruptRoutine;
    InitializationData->DxgkDdiOpenAllocation = Bc250McdmOpenAllocation;
    InitializationData->DxgkDdiPreemptCommand = Bc250McdmPreemptCommand;
    InitializationData->DxgkDdiQueryAdapterInfo =
        Bc250McdmQueryAdapterInfo;
    InitializationData->DxgkDdiQueryDependentEngineGroup =
        Bc250McdmQueryDependentEngineGroup;
    InitializationData->DxgkDdiQueryDeviceDescriptor =
        Bc250McdmQueryDeviceDescriptor;
    InitializationData->DxgkDdiQueryEngineStatus =
        Bc250McdmQueryEngineStatus;
    InitializationData->DxgkDdiResetDevice = Bc250McdmResetDevice;
    InitializationData->DxgkDdiResetEngine = Bc250McdmResetEngine;
    InitializationData->DxgkDdiResetFromTimeout =
        Bc250McdmResetFromTimeout;
    InitializationData->DxgkDdiRestartFromTimeout =
        Bc250McdmRestartFromTimeout;
    InitializationData->DxgkDdiSetPowerState = Bc250McdmSetPowerState;
    InitializationData->DxgkDdiSetRootPageTable = Bc250McdmSetRootPageTable;
    InitializationData->DxgkDdiSetStablePowerState =
        Bc250McdmSetStablePowerState;
    InitializationData->DxgkDdiSetVirtualMachineData =
        Bc250McdmSetVirtualMachineData;
    InitializationData->DxgkDdiSubmitCommandVirtual =
        Bc250McdmSubmitCommandVirtual;
    InitializationData->DxgkDdiQueryChildStatus = Bc250McdmQueryChildStatus;
    InitializationData->DxgkDdiQueryChildRelations =
        Bc250McdmQueryChildRelations;
    InitializationData->DxgkDdiUnload = Bc250McdmUnload;
    return STATUS_SUCCESS;
}
