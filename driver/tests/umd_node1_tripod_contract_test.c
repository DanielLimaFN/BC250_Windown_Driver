#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bc250_d3d_umd.h"

typedef enum TEST_MODE {
    TEST_PASS = 0,
    TEST_FENCE_STALE,
    TEST_DATA_STALE
} TEST_MODE;

typedef struct TEST_STATE {
    TEST_MODE Mode;
    UINT AllocateCount;
    UINT SubmitCount;
    UINT DestroyFenceCount;
    UINT DeallocateCount;
    volatile UINT64 PagingFence;
    volatile UINT64 ProofFence;
    unsigned char Command[4096];
    unsigned char Destination[4096];
} TEST_STATE;

static TEST_STATE g_State;

static HRESULT APIENTRY TestCreateContext(
    HANDLE Device, D3DDDICB_CREATECONTEXTVIRTUAL* Args)
{
    UNREFERENCED_PARAMETER(Device);
    assert(Args->NodeOrdinal == BC250_WINSYS_NODE_COMPUTE);
    Args->hContext = (HANDLE)(ULONG_PTR)0xC001u;
    return S_OK;
}

static HRESULT APIENTRY TestDestroyContext(
    HANDLE Device, const D3DDDICB_DESTROYCONTEXT* Args)
{
    UNREFERENCED_PARAMETER(Device);
    assert(Args->hContext == (HANDLE)(ULONG_PTR)0xC001u);
    return S_OK;
}

static HRESULT APIENTRY TestCreatePagingQueue(
    HANDLE Device, D3DDDICB_CREATEPAGINGQUEUE* Args)
{
    UNREFERENCED_PARAMETER(Device);
    Args->hPagingQueue = 0x9001u;
    Args->hSyncObject = 0x9002u;
    Args->FenceValueCPUVirtualAddress = (VOID*)&g_State.PagingFence;
    return S_OK;
}

static HRESULT APIENTRY TestDestroyPagingQueue(
    HANDLE Device, const D3DDDI_DESTROYPAGINGQUEUE* Args)
{
    UNREFERENCED_PARAMETER(Device);
    assert(Args->hPagingQueue == 0x9001u);
    return S_OK;
}

static HRESULT APIENTRY TestAllocate(HANDLE Device, D3DDDICB_ALLOCATE* Args)
{
    const BC250_ALLOCATION_PRIVATE_V1* privateData;
    D3DDDI_ALLOCATIONINFO2* info;
    UNREFERENCED_PARAMETER(Device);
    assert(Args->NumAllocations == 1u && Args->pAllocationInfo2 != NULL);
    info = Args->pAllocationInfo2;
    privateData = (const BC250_ALLOCATION_PRIVATE_V1*)info->pPrivateDriverData;
    assert(info->PrivateDriverDataSize == sizeof(*privateData));
    assert(privateData->Magic == BC250_ALLOCATION_PRIVATE_MAGIC);
    assert(privateData->AllocationToken != 0u);
    assert(privateData->ByteSize == 4096u);
    g_State.AllocateCount++;
    assert(g_State.AllocateCount <= 2u);
    info->hAllocation = (g_State.AllocateCount == 1u) ? 0xA200u : 0xA201u;
    return S_OK;
}

static HRESULT APIENTRY TestMapGpuVa(
    HANDLE Device, D3DDDI_MAPGPUVIRTUALADDRESS* Args)
{
    UNREFERENCED_PARAMETER(Device);
    assert(Args->hPagingQueue == 0x9001u);
    if (Args->hAllocation == 0xA200u) {
        assert(Args->Protection.Execute == 1u);
        Args->VirtualAddress = 0x220000u;
    } else {
        assert(Args->hAllocation == 0xA201u);
        assert(Args->Protection.Execute == 0u);
        Args->VirtualAddress = 0x500000u;
    }
    Args->PagingFenceValue = ++g_State.PagingFence;
    return S_OK;
}

static HRESULT APIENTRY TestMakeResident(
    HANDLE Device, D3DDDI_MAKERESIDENT* Args)
{
    UNREFERENCED_PARAMETER(Device);
    assert(Args->NumAllocations == 1u);
    Args->PagingFenceValue = ++g_State.PagingFence;
    return S_OK;
}

static HRESULT APIENTRY TestWait(
    HANDLE Device,
    const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU* Args)
{
    UNREFERENCED_PARAMETER(Device);
    assert(Args->ObjectCount == 1u);
    if (Args->ObjectHandleArray[0] == 0x9002u) {
        g_State.PagingFence = Args->FenceValueArray[0];
    }
    return S_OK;
}

static HRESULT APIENTRY TestLock(HANDLE Device, D3DDDICB_LOCK* Args)
{
    UNREFERENCED_PARAMETER(Device);
    assert(Args->Flags.LockEntire == 1u);
    if (Args->hAllocation == 0xA200u) {
        Args->pData = g_State.Command;
    } else {
        assert(Args->hAllocation == 0xA201u);
        Args->pData = g_State.Destination;
    }
    return S_OK;
}

static HRESULT APIENTRY TestUnlock(
    HANDLE Device, const D3DDDICB_UNLOCK* Args)
{
    UNREFERENCED_PARAMETER(Device);
    assert(Args->NumAllocations == 1u);
    return S_OK;
}

static HRESULT APIENTRY TestCreateFence(
    HANDLE Device, D3DDDICB_CREATESYNCHRONIZATIONOBJECT2* Args)
{
    UNREFERENCED_PARAMETER(Device);
    assert(Args->Info.Type == D3DDDI_MONITORED_FENCE);
    assert(Args->Info.MonitoredFence.InitialFenceValue ==
        BC250_UMD_NODE1_PROOF_FENCE_POISON);
    g_State.ProofFence = Args->Info.MonitoredFence.InitialFenceValue;
    Args->hSyncObject = 0x5001u;
    Args->Info.MonitoredFence.FenceValueCPUVirtualAddress =
        (UINT64*)&g_State.ProofFence;
    Args->Info.MonitoredFence.FenceValueGPUVirtualAddress = 0x600000u;
    return S_OK;
}

static HRESULT APIENTRY TestDestroyFence(
    HANDLE Device, const D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT* Args)
{
    UNREFERENCED_PARAMETER(Device);
    assert(Args->hSyncObject == 0x5001u);
    g_State.DestroyFenceCount++;
    return S_OK;
}

static HRESULT APIENTRY TestSubmit(
    HANDLE Device, const D3DDDICB_SUBMITCOMMAND* Args)
{
    const uint32_t* pm4 = (const uint32_t*)g_State.Command;
    UNREFERENCED_PARAMETER(Device);
    assert(Args->Commands == 0x220000u);
    assert(Args->CommandLength == 17u * sizeof(uint32_t));
    assert(Args->BroadcastContext[0] == (HANDLE)(ULONG_PTR)0xC001u);
    assert(pm4[2] == 0x00500000u && pm4[9] == BC250_PM4_RELEASE_MEM_HEADER_PROVEN);
    g_State.SubmitCount++;
    if (g_State.Mode != TEST_DATA_STALE) {
        memcpy(g_State.Destination, &pm4[4],
            BC250_UMD_NODE1_PROOF_VALUE_COUNT * sizeof(uint32_t));
    }
    if (g_State.Mode != TEST_FENCE_STALE) {
        g_State.ProofFence = BC250_UMD_NODE1_PROOF_FENCE_VALUE;
    }
    return S_OK;
}

static HRESULT APIENTRY TestEvict(
    HANDLE Device, const D3DDDICB_EVICT* Args)
{
    UNREFERENCED_PARAMETER(Device);
    assert(Args->NumAllocations == 1u);
    return S_OK;
}

static HRESULT APIENTRY TestFreeGpuVa(
    HANDLE Device, const D3DDDICB_FREEGPUVIRTUALADDRESS* Args)
{
    UNREFERENCED_PARAMETER(Device);
    assert(Args->BaseAddress == 0x220000u || Args->BaseAddress == 0x500000u);
    return S_OK;
}

static HRESULT APIENTRY TestDeallocate(
    HANDLE Device, const D3DDDICB_DEALLOCATE2* Args)
{
    UNREFERENCED_PARAMETER(Device);
    assert(Args->NumAllocations == 1u);
    g_State.DeallocateCount++;
    return S_OK;
}

static D3DDDI_DEVICECALLBACKS TestCallbacks(void)
{
    D3DDDI_DEVICECALLBACKS callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.pfnCreateContextVirtualCb = TestCreateContext;
    callbacks.pfnDestroyContextCb = TestDestroyContext;
    callbacks.pfnCreatePagingQueueCb = TestCreatePagingQueue;
    callbacks.pfnDestroyPagingQueueCb = TestDestroyPagingQueue;
    callbacks.pfnAllocateCb = TestAllocate;
    callbacks.pfnDeallocate2Cb = TestDeallocate;
    callbacks.pfnMapGpuVirtualAddressCb = TestMapGpuVa;
    callbacks.pfnMakeResidentCb = TestMakeResident;
    callbacks.pfnWaitForSynchronizationObjectFromCpuCb = TestWait;
    callbacks.pfnLockCb = TestLock;
    callbacks.pfnUnlockCb = TestUnlock;
    callbacks.pfnSubmitCommandCb = TestSubmit;
    callbacks.pfnEvictCb = TestEvict;
    callbacks.pfnFreeGpuVirtualAddressCb = TestFreeGpuVa;
    callbacks.pfnCreateSynchronizationObject2Cb = TestCreateFence;
    callbacks.pfnDestroySynchronizationObjectCb = TestDestroyFence;
    return callbacks;
}

static void RunCase(TEST_MODE mode, BOOL expectSuccess)
{
    D3DDDI_DEVICECALLBACKS callbacks = TestCallbacks();
    BC250_UMD_DEVICE device;
    BC250_UMD_NODE1_PROOF_RESULT result;
    HRESULT hr;

    memset(&g_State, 0, sizeof(g_State));
    memset(&device, 0, sizeof(device));
    g_State.Mode = mode;
    memset(g_State.Command, 0xCC, sizeof(g_State.Command));
    memset(g_State.Destination, 0xDD, sizeof(g_State.Destination));

    device.DeviceActive = TRUE;
    assert(Bc250WinsysInitialize(&device.Winsys,
        (HANDLE)(ULONG_PTR)0xD00Du, &callbacks) == BC250_WINSYS_OK);
    device.WinsysInitialized = TRUE;
    assert(SUCCEEDED(Bc250UmdStartNode1Submission(&device, 4096u)));

    hr = Bc250UmdRunNode1ProofTransaction(&device, &result);
    assert((SUCCEEDED(hr) ? TRUE : FALSE) == expectSuccess);
    assert(result.PoisonVerifiedBeforeSubmit == TRUE);
    assert(result.Submitted == TRUE);
    assert(result.CleanupComplete == TRUE);
    assert(result.DestinationGpuVa == 0x500000u);
    assert(result.FenceGpuVa == 0x600000u);
    if (mode == TEST_FENCE_STALE) {
        assert(result.FenceMatched == FALSE && result.DataMatched == FALSE);
    } else if (mode == TEST_DATA_STALE) {
        assert(result.FenceMatched == TRUE && result.DataMatched == FALSE);
    } else {
        assert(result.FenceMatched == TRUE && result.DataMatched == TRUE);
        assert(result.OperationHr == S_OK);
    }
    assert(g_State.SubmitCount == 1u);
    assert(g_State.DestroyFenceCount == 1u);
    assert(g_State.DeallocateCount == 1u);

    Bc250UmdDeviceTeardown(&device);
    assert(g_State.DeallocateCount == 2u);
}

int main(void)
{
    RunCase(TEST_PASS, TRUE);
    RunCase(TEST_FENCE_STALE, FALSE);
    RunCase(TEST_DATA_STALE, FALSE);
    puts("UMD NODE1 TRIPOD CONTRACT: PASS (success + stale fence/data rejected; offline)");
    puts("HARDWARE VERDICT: NAO VALIDADO EM HARDWARE");
    return 0;
}
