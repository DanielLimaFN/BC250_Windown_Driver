#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../umd/bc250_wddm_winsys.h"

typedef struct TEST_STATE {
    UINT CreateContextCalls;
    UINT DestroyContextCalls;
    UINT AllocateCalls;
    UINT DeallocateCalls;
    UINT SubmitCalls;
    UINT LockCalls;
    UINT UnlockCalls;
    UINT LastCreatedNode;
    UINT CreatePagingQueueCalls;
    UINT DestroyPagingQueueCalls;
    UINT MapCalls;
    UINT FreeGpuVaCalls;
    UINT WaitCalls;
    UINT MakeResidentCalls;
    UINT EvictCalls;
    UINT CreateSyncCalls;
    UINT DestroySyncCalls;
    UINT SignalSyncCalls;
    HRESULT ForcedResult;
} TEST_STATE;

static TEST_STATE g_State;
static volatile UINT64 g_PagingFence;
static volatile UINT64 g_MonitoredFence;
static unsigned char g_LockedAllocation[0x4000];

static HRESULT APIENTRY TestCreateContext(
    HANDLE Device,
    D3DDDICB_CREATECONTEXTVIRTUAL *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL);
    g_State.CreateContextCalls++;
    g_State.LastCreatedNode = Args->NodeOrdinal;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    Args->hContext = (HANDLE)(ULONG_PTR)0xC001u;
    return S_OK;
}

static HRESULT APIENTRY TestDestroyContext(
    HANDLE Device,
    const D3DDDICB_DESTROYCONTEXT *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->hContext == (HANDLE)(ULONG_PTR)0xC001u);
    g_State.DestroyContextCalls++;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    return S_OK;
}

static HRESULT APIENTRY TestAllocate(HANDLE Device, D3DDDICB_ALLOCATE *Args)
{
    const BC250_ALLOCATION_PRIVATE_V1 *privateData;
    D3DDDI_ALLOCATIONINFO2 *info;

    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->NumAllocations == 1u);
    info = Args->pAllocationInfo2;
    assert(info != NULL);
    privateData = (const BC250_ALLOCATION_PRIVATE_V1 *)info->pPrivateDriverData;
    assert(info->PrivateDriverDataSize == sizeof(*privateData));
    assert(privateData->Magic == BC250_ALLOCATION_PRIVATE_MAGIC);
    assert(privateData->Version == BC250_ALLOCATION_PRIVATE_VERSION);
    assert(privateData->StructSize == sizeof(*privateData));
    assert(privateData->AllocationToken == 0x1234u ||
           privateData->AllocationToken ==
               (((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_PRIMARY <<
                   BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 0x42u));
    assert(privateData->ByteSize == 0x4000u);
    assert(privateData->Alignment == 0x1000u);
    g_State.AllocateCalls++;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    info->hAllocation = 0xA110u;
    info->GpuVirtualAddress = 0u;
    return S_OK;
}

static HRESULT APIENTRY TestCreatePagingQueue(
    HANDLE Device,
    D3DDDICB_CREATEPAGINGQUEUE *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL);
    assert(Args->Priority == D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL);
    g_State.CreatePagingQueueCalls++;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    Args->hPagingQueue = 0x9001u;
    Args->hSyncObject = 0x9002u;
    Args->FenceValueCPUVirtualAddress = (VOID *)&g_PagingFence;
    return S_OK;
}

static HRESULT APIENTRY TestDestroyPagingQueue(
    HANDLE Device,
    const D3DDDI_DESTROYPAGINGQUEUE *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->hPagingQueue == 0x9001u);
    g_State.DestroyPagingQueueCalls++;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    return S_OK;
}

static HRESULT APIENTRY TestMapGpuVa(
    HANDLE Device,
    D3DDDI_MAPGPUVIRTUALADDRESS *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->hPagingQueue == 0x9001u);
    assert(Args->hAllocation == 0xA110u);
    assert(Args->SizeInPages == 4u);
    assert(Args->Protection.Write == 1u);
    assert(Args->MinimumAddress == 0x10000u);
    assert(Args->MaximumAddress == BC250_ADAPTER_MAX_GPUVA_ADDRESS);
    assert(Args->MaximumAddress == 0x00000003FFFFFFFFull);
    g_State.MapCalls++;
    if (g_State.ForcedResult == (HRESULT)0x8000000AL /* E_PENDING */) {
        Args->VirtualAddress = 0x200000u;
        Args->PagingFenceValue = 7u;
        return (HRESULT)0x8000000AL;
    }
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    Args->VirtualAddress = 0x200000u;
    Args->PagingFenceValue = 7u;
    return S_OK;
}

static HRESULT APIENTRY TestFreeGpuVa(
    HANDLE Device,
    const D3DDDICB_FREEGPUVIRTUALADDRESS *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->BaseAddress == 0x200000u);
    assert(Args->Size == 0x4000u);
    g_State.FreeGpuVaCalls++;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    return S_OK;
}

static HRESULT APIENTRY TestWaitPaging(
    HANDLE Device,
    const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->ObjectCount == 1u);
    assert(Args->ObjectHandleArray[0] == 0x9002u ||
           Args->ObjectHandleArray[0] == (D3DKMT_HANDLE)0x5001u);
    g_State.WaitCalls++;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    if (Args->ObjectHandleArray[0] == 0x9002u) {
        assert(Args->FenceValueArray[0] == 7u || Args->FenceValueArray[0] == 8u);
        g_PagingFence = Args->FenceValueArray[0];
    } else if (Args->ObjectHandleArray[0] == (D3DKMT_HANDLE)0x5001u) {
        g_MonitoredFence = Args->FenceValueArray[0];
    }
    return S_OK;
}

static HRESULT APIENTRY TestMakeResident(
    HANDLE Device,
    D3DDDI_MAKERESIDENT *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL);
    assert(Args->NumAllocations == 1u);
    assert(Args->AllocationList != NULL && Args->AllocationList[0] == 0xA110u);
    g_State.MakeResidentCalls++;
    if (g_State.ForcedResult == (HRESULT)0x8000000AL /* E_PENDING */) {
        Args->PagingFenceValue = 8u;
        return (HRESULT)0x8000000AL;
    }
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    Args->PagingFenceValue = 8u;
    return S_OK;
}

static HRESULT APIENTRY TestEvict(
    HANDLE Device,
    const D3DDDICB_EVICT *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL);
    assert(Args->NumAllocations == 1u);
    assert(Args->AllocationList != NULL && Args->AllocationList[0] == 0xA110u);
    g_State.EvictCalls++;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    return S_OK;
}

static HRESULT APIENTRY TestCreateSync(
    HANDLE Device,
    D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL);
    assert(Args->Info.Type == D3DDDI_MONITORED_FENCE);
    g_State.CreateSyncCalls++;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    Args->hSyncObject = (D3DKMT_HANDLE)0x5001u;
    Args->Info.MonitoredFence.FenceValueCPUVirtualAddress = (VOID *)&g_MonitoredFence;
    Args->Info.MonitoredFence.FenceValueGPUVirtualAddress = 0x300000ull;
    return S_OK;
}

static HRESULT APIENTRY TestDestroySync(
    HANDLE Device,
    const D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->hSyncObject == (D3DKMT_HANDLE)0x5001u);
    g_State.DestroySyncCalls++;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    return S_OK;
}

static HRESULT APIENTRY TestSignalSync(
    HANDLE Device,
    const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->ObjectCount == 1u);
    assert(Args->ObjectHandleArray[0] == (D3DKMT_HANDLE)0x5001u);
    g_State.SignalSyncCalls++;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    g_MonitoredFence = Args->FenceValueArray[0];
    return S_OK;
}

static HRESULT APIENTRY TestDeallocate2(
    HANDLE Device,
    const D3DDDICB_DEALLOCATE2 *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->NumAllocations == 1u);
    assert(Args->HandleList != NULL && Args->HandleList[0] == 0xA110u);
    g_State.DeallocateCalls++;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    return S_OK;
}

static HRESULT APIENTRY TestSubmit(
    HANDLE Device,
    const D3DDDICB_SUBMITCOMMAND *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL);
    assert(Args->Commands == 0x200000u);
    assert(Args->CommandLength == 64u);
    assert(Args->BroadcastContextCount == 1u);
    assert(Args->BroadcastContext[0] == (HANDLE)(ULONG_PTR)0xC001u);
    g_State.SubmitCalls++;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    return S_OK;
}

static HRESULT APIENTRY TestLock(
    HANDLE Device,
    D3DDDICB_LOCK *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->hAllocation == 0xA110u);
    assert(Args->Flags.WriteOnly == 1u);
    assert(Args->Flags.LockEntire == 1u);
    g_State.LockCalls++;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    Args->pData = g_LockedAllocation;
    return S_OK;
}

static HRESULT APIENTRY TestUnlock(
    HANDLE Device,
    const D3DDDICB_UNLOCK *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->NumAllocations == 1u);
    assert(Args->phAllocations != NULL && Args->phAllocations[0] == 0xA110u);
    g_State.UnlockCalls++;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    return S_OK;
}

static D3DDDI_DEVICECALLBACKS TestCallbacks(void)
{
    D3DDDI_DEVICECALLBACKS callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.pfnCreateContextVirtualCb = TestCreateContext;
    callbacks.pfnDestroyContextCb = TestDestroyContext;
    callbacks.pfnAllocateCb = TestAllocate;
    callbacks.pfnDeallocate2Cb = TestDeallocate2;
    callbacks.pfnSubmitCommandCb = TestSubmit;
    callbacks.pfnLockCb = TestLock;
    callbacks.pfnUnlockCb = TestUnlock;
    callbacks.pfnCreatePagingQueueCb = TestCreatePagingQueue;
    callbacks.pfnDestroyPagingQueueCb = TestDestroyPagingQueue;
    callbacks.pfnMapGpuVirtualAddressCb = TestMapGpuVa;
    callbacks.pfnFreeGpuVirtualAddressCb = TestFreeGpuVa;
    callbacks.pfnWaitForSynchronizationObjectFromCpuCb = TestWaitPaging;
    callbacks.pfnMakeResidentCb = TestMakeResident;
    callbacks.pfnEvictCb = TestEvict;
    callbacks.pfnCreateSynchronizationObject2Cb = TestCreateSync;
    callbacks.pfnDestroySynchronizationObjectCb = TestDestroySync;
    callbacks.pfnSignalSynchronizationObjectFromCpuCb = TestSignalSync;
    return callbacks;
}

static void TestLifecycle(void)
{
    D3DDDI_DEVICECALLBACKS callbacks = TestCallbacks();
    BC250_WINSYS_CONTEXT context;
    BC250_WINSYS_ALLOCATION allocation;
    unsigned char commandBytes[64];

    memset(&g_State, 0, sizeof(g_State));
    memset(&context, 0, sizeof(context));
    memset(&allocation, 0, sizeof(allocation));
    memset(commandBytes, 0x5Au, sizeof(commandBytes));
    memset(g_LockedAllocation, 0, sizeof(g_LockedAllocation));
    g_PagingFence = 0u;
    assert(Bc250WinsysInitialize(&context,
        (HANDLE)(ULONG_PTR)0xD00Du, &callbacks) == BC250_WINSYS_OK);
    assert(Bc250WinsysCreateVirtualContext(&context,
        BC250_WINSYS_NODE_COMPUTE) == BC250_WINSYS_OK);
    assert(context.KernelContext == (HANDLE)(ULONG_PTR)0xC001u);
    assert(g_State.LastCreatedNode == BC250_WINSYS_NODE_COMPUTE);
    assert(Bc250WinsysCreatePagingQueue(&context) == BC250_WINSYS_OK);
    assert(Bc250WinsysAllocate(&context, 0x1234u, 0x4000u, 0x1000u,
        &allocation) == BC250_WINSYS_OK);
    assert(allocation.KernelAllocation == 0xA110u);
    assert(allocation.GpuVirtualAddress == 0u);
    assert(Bc250WinsysMapAllocation(&context, &allocation, TRUE) ==
        BC250_WINSYS_OK);
    assert(allocation.GpuVirtualAddress == 0x200000u);
    assert(allocation.PagingFenceValue == 7u);
    assert(context.ActiveGpuVaMappings == 1u);
    assert(Bc250WinsysDestroyPagingQueue(&context) ==
        BC250_WINSYS_STATE_CONFLICT);
    assert(Bc250WinsysWaitPaging(&context, &allocation) == BC250_WINSYS_OK);
    assert(Bc250WinsysUploadAllocation(&context, &allocation,
        commandBytes, sizeof(commandBytes)) == BC250_WINSYS_OK);
    assert(memcmp(g_LockedAllocation, commandBytes, sizeof(commandBytes)) == 0);
    assert(Bc250WinsysSubmitGpuVaForNode(&context,
        BC250_WINSYS_NODE_SDMA, allocation.GpuVirtualAddress, 64u) ==
        BC250_WINSYS_STATE_CONFLICT);
    assert(g_State.SubmitCalls == 0u);
    assert(Bc250WinsysSubmitGpuVaForNode(&context,
        BC250_WINSYS_NODE_COMPUTE,
        allocation.GpuVirtualAddress, 64u) == BC250_WINSYS_OK);
    assert(Bc250WinsysDeallocate(&context, &allocation) == BC250_WINSYS_OK);
    assert(context.ActiveGpuVaMappings == 0u);
    assert(Bc250WinsysDestroyVirtualContext(&context) == BC250_WINSYS_OK);
    assert(Bc250WinsysDestroyPagingQueue(&context) == BC250_WINSYS_OK);
    assert(g_State.CreateContextCalls == 1u);
    assert(g_State.AllocateCalls == 1u);
    assert(g_State.SubmitCalls == 1u);
    assert(g_State.LockCalls == 1u);
    assert(g_State.UnlockCalls == 1u);
    assert(g_State.DeallocateCalls == 1u);
    assert(g_State.DestroyContextCalls == 1u);
    assert(g_State.CreatePagingQueueCalls == 1u);
    assert(g_State.MapCalls == 1u);
    assert(g_State.WaitCalls == 1u);
    assert(g_State.FreeGpuVaCalls == 1u);
    assert(g_State.DestroyPagingQueueCalls == 1u);
}

static void TestFailClosed(void)
{
    D3DDDI_DEVICECALLBACKS callbacks = TestCallbacks();
    BC250_WINSYS_CONTEXT context;
    BC250_WINSYS_ALLOCATION allocation;

    memset(&g_State, 0, sizeof(g_State));
    memset(&context, 0, sizeof(context));
    memset(&allocation, 0, sizeof(allocation));
    assert(Bc250WinsysInitialize(&context,
        (HANDLE)(ULONG_PTR)0xD00Du, &callbacks) == BC250_WINSYS_OK);
    g_State.ForcedResult = E_OUTOFMEMORY;
    assert(Bc250WinsysCreateVirtualContext(&context,
        BC250_WINSYS_NODE_COMPUTE) == BC250_WINSYS_CALLBACK_FAILED);
    assert(!context.ContextCreated);
    assert(context.LastCallbackHr == E_OUTOFMEMORY);

    g_State.ForcedResult = S_OK;
    callbacks.pfnSubmitCommandCb = NULL;
    assert(Bc250WinsysCreateVirtualContext(&context,
        BC250_WINSYS_NODE_COMPUTE) == BC250_WINSYS_OK);
    assert(Bc250WinsysSubmitGpuVa(&context, 0x200000u, 64u) ==
        BC250_WINSYS_MISSING_CALLBACK);
    assert(Bc250WinsysSubmitGpuVaForNode(&context,
        BC250_WINSYS_NODE_SDMA, 0x200000u, 64u) ==
        BC250_WINSYS_STATE_CONFLICT);
    assert(Bc250WinsysAllocate(&context, 1u, 3u, 0x1000u, &allocation) ==
        BC250_WINSYS_BAD_ARGUMENT);
    assert(Bc250WinsysDestroyVirtualContext(&context) == BC250_WINSYS_OK);

    memset(&allocation, 0, sizeof(allocation));
    allocation.Created = TRUE;
    allocation.KernelAllocation = 0xA110u;
    allocation.ByteSize = sizeof(g_LockedAllocation);
    callbacks = TestCallbacks();
    callbacks.pfnLockCb = NULL;
    context.Callbacks = &callbacks;
    assert(Bc250WinsysUploadAllocation(&context, &allocation,
        g_LockedAllocation, 4u) == BC250_WINSYS_MISSING_CALLBACK);
    callbacks.pfnLockCb = TestLock;
    assert(Bc250WinsysUploadAllocation(&context, &allocation,
        g_LockedAllocation, (UINT)sizeof(g_LockedAllocation) + 1u) ==
        BC250_WINSYS_BAD_ARGUMENT);

    memset(&allocation, 0, sizeof(allocation));
    assert(Bc250WinsysMakeResident(NULL, &allocation) == BC250_WINSYS_BAD_ARGUMENT);
    assert(Bc250WinsysEvict(NULL, &allocation) == BC250_WINSYS_BAD_ARGUMENT);

    {
        BC250_WINSYS_MONITORED_FENCE fence;
        memset(&fence, 0, sizeof(fence));
        assert(Bc250WinsysCreateMonitoredFence(NULL, 0, &fence) == BC250_WINSYS_BAD_ARGUMENT);
        assert(Bc250WinsysDestroyMonitoredFence(NULL, &fence) == BC250_WINSYS_BAD_ARGUMENT);
        assert(Bc250WinsysSignalFenceFromCpu(NULL, &fence, 1) == BC250_WINSYS_BAD_ARGUMENT);
        assert(Bc250WinsysWaitForFenceFromCpu(NULL, &fence, 1) == BC250_WINSYS_BAD_ARGUMENT);
    }
}

static void TestResidencyAndPrimaryClass(void)
{
    D3DDDI_DEVICECALLBACKS callbacks = TestCallbacks();
    BC250_WINSYS_CONTEXT context;
    BC250_WINSYS_ALLOCATION allocation;
    BC250_WINSYS_ALLOCATION primaryAllocation;

    memset(&g_State, 0, sizeof(g_State));
    memset(&context, 0, sizeof(context));
    memset(&allocation, 0, sizeof(allocation));
    memset(&primaryAllocation, 0, sizeof(primaryAllocation));

    assert(Bc250WinsysInitialize(&context, (HANDLE)(ULONG_PTR)0xD00Du, &callbacks) == BC250_WINSYS_OK);
    assert(Bc250WinsysCreatePagingQueue(&context) == BC250_WINSYS_OK);

    /* Allocate normal resource */
    assert(Bc250WinsysAllocate(&context, 0x1234u, 0x4000u, 0x1000u, &allocation) == BC250_WINSYS_OK);
    assert(!Bc250WinsysAllocationIsPrimary(&allocation));
    assert(!allocation.Resident);

    /* Make resident */
    assert(Bc250WinsysMakeResident(&context, &allocation) == BC250_WINSYS_OK);
    assert(allocation.Resident);
    assert(allocation.PagingFenceValue == 8u);
    assert(g_State.MakeResidentCalls == 1u);

    /* Evict */
    assert(Bc250WinsysEvict(&context, &allocation) == BC250_WINSYS_OK);
    assert(!allocation.Resident);
    assert(g_State.EvictCalls == 1u);

    /* Make resident again and then deallocate -> should auto-evict */
    assert(Bc250WinsysMakeResident(&context, &allocation) == BC250_WINSYS_OK);
    assert(allocation.Resident);
    assert(g_State.MakeResidentCalls == 2u);

    assert(Bc250WinsysDeallocate(&context, &allocation) == BC250_WINSYS_OK);
    assert(!allocation.Resident);
    assert(g_State.EvictCalls == 2u);

    /* Test primary allocation */
    {
        uint64_t primaryToken =
            ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_PRIMARY <<
                BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 0x42u;
        assert(Bc250WinsysAllocate(&context, primaryToken, 0x4000u, 0x1000u, &primaryAllocation) == BC250_WINSYS_OK);
        assert(Bc250WinsysAllocationIsPrimary(&primaryAllocation));
        assert(Bc250WinsysDeallocate(&context, &primaryAllocation) == BC250_WINSYS_OK);
    }

    assert(Bc250WinsysDestroyPagingQueue(&context) == BC250_WINSYS_OK);
}

static void TestMonitoredFenceLifecycle(void)
{
    D3DDDI_DEVICECALLBACKS callbacks = TestCallbacks();
    BC250_WINSYS_CONTEXT context;
    BC250_WINSYS_MONITORED_FENCE fence;

    memset(&g_State, 0, sizeof(g_State));
    memset(&context, 0, sizeof(context));
    memset(&fence, 0, sizeof(fence));
    g_MonitoredFence = 100u;

    assert(Bc250WinsysInitialize(&context, (HANDLE)(ULONG_PTR)0xD00Du, &callbacks) == BC250_WINSYS_OK);

    /* Create fence */
    assert(Bc250WinsysCreateMonitoredFence(&context, 100u, &fence) == BC250_WINSYS_OK);
    assert(fence.Created);
    assert(fence.SyncObject == (D3DKMT_HANDLE)0x5001u);
    assert(fence.CpuVirtualAddress == &g_MonitoredFence);
    assert(fence.GpuVirtualAddress == 0x300000ull);
    assert(fence.InitialValue == 100u);
    assert(g_State.CreateSyncCalls == 1u);

    /* Conflict check: re-creating on active fence */
    assert(Bc250WinsysCreateMonitoredFence(&context, 100u, &fence) == BC250_WINSYS_STATE_CONFLICT);

    /* Signal from CPU */
    assert(Bc250WinsysSignalFenceFromCpu(&context, &fence, 105u) == BC250_WINSYS_OK);
    assert(g_State.SignalSyncCalls == 1u);
    assert(g_MonitoredFence == 105u);

    /* Fast-path wait (Current >= Target): no kernel callback */
    assert(Bc250WinsysWaitForFenceFromCpu(&context, &fence, 105u) == BC250_WINSYS_OK);
    assert(g_State.WaitCalls == 0u);

    /* Kernel wait when Target > Current */
    assert(Bc250WinsysWaitForFenceFromCpu(&context, &fence, 110u) == BC250_WINSYS_OK);
    assert(g_State.WaitCalls == 1u);
    assert(g_MonitoredFence == 110u);

    /* Destroy fence */
    assert(Bc250WinsysDestroyMonitoredFence(&context, &fence) == BC250_WINSYS_OK);
    assert(!fence.Created);
    assert(g_State.DestroySyncCalls == 1u);

    /* Idempotent destroy */
    assert(Bc250WinsysDestroyMonitoredFence(&context, &fence) == BC250_WINSYS_OK);
    assert(g_State.DestroySyncCalls == 1u);
}

static void TestPagingPendingStatus(void)
{
    D3DDDI_DEVICECALLBACKS callbacks = TestCallbacks();
    BC250_WINSYS_CONTEXT context;
    BC250_WINSYS_ALLOCATION allocation;

    memset(&g_State, 0, sizeof(g_State));
    memset(&context, 0, sizeof(context));
    memset(&allocation, 0, sizeof(allocation));
    g_PagingFence = 0u;

    assert(Bc250WinsysInitialize(&context,
        (HANDLE)(ULONG_PTR)0xD00Du, &callbacks) == BC250_WINSYS_OK);
    assert(Bc250WinsysCreatePagingQueue(&context) == BC250_WINSYS_OK);
    assert(Bc250WinsysAllocate(&context, 0x1234u, 0x4000u, 0x1000u,
        &allocation) == BC250_WINSYS_OK);

    /* Test 1: WaitPaging with PagingFenceValue==0 is a no-op (returns OK, 0 WaitCalls) */
    assert(allocation.PagingFenceValue == 0u);
    assert(Bc250WinsysWaitPaging(&context, &allocation) == BC250_WINSYS_OK);
    assert(g_State.WaitCalls == 0u);

    /* Test 2: MapAllocation returns E_PENDING (0x8000000A) */
    g_State.ForcedResult = (HRESULT)0x8000000AL;
    assert(Bc250WinsysMapAllocation(&context, &allocation, TRUE) == BC250_WINSYS_OK);
    assert(allocation.GpuVirtualAddress == 0x200000u);
    assert(allocation.PagingFenceValue == 7u);
    assert(allocation.OwnsGpuVaMapping);
    assert(context.ActiveGpuVaMappings == 1u);

    /* Test 3: WaitPaging must wait on PagingFenceValue 7 */
    g_State.ForcedResult = S_OK;
    assert(Bc250WinsysWaitPaging(&context, &allocation) == BC250_WINSYS_OK);
    assert(g_State.WaitCalls == 1u);
    assert(g_PagingFence == 7u);

    /* Test 4: MakeResident returns E_PENDING (0x8000000A) */
    g_State.ForcedResult = (HRESULT)0x8000000AL;
    assert(Bc250WinsysMakeResident(&context, &allocation) == BC250_WINSYS_OK);
    assert(allocation.Resident);
    assert(allocation.PagingFenceValue == 8u);

    /* Test 5: WaitPaging must wait on updated PagingFenceValue 8 */
    g_State.ForcedResult = S_OK;
    assert(Bc250WinsysWaitPaging(&context, &allocation) == BC250_WINSYS_OK);
    assert(g_State.WaitCalls == 2u);
    assert(g_PagingFence == 8u);

    /* Cleanup */
    assert(Bc250WinsysDeallocate(&context, &allocation) == BC250_WINSYS_OK);
    assert(Bc250WinsysDestroyPagingQueue(&context) == BC250_WINSYS_OK);
}

int main(void)
{
    printf("=== BC250 WDDM WINSYS CONTRACT TEST ===\n");
    TestLifecycle();
    TestResidencyAndPrimaryClass();
    TestMonitoredFenceLifecycle();
    TestPagingPendingStatus();
    TestFailClosed();
    printf("WDDM WINSYS CONTRACT: PASS (callbacks offline; hardware not touched)\n");
    return 0;
}
