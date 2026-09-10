#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bc250_d3d_umd.h"

typedef struct TEST_STATE {
    UINT CreateContext;
    UINT CreatePagingQueue;
    UINT Allocate;
    UINT Map;
    UINT MakeResident;
    UINT Wait;
    UINT Lock;
    UINT Unlock;
    UINT Submit;
    UINT Evict;
    UINT FreeGpuVa;
    UINT Deallocate;
    UINT DestroyContext;
    UINT DestroyPagingQueue;
} TEST_STATE;

static TEST_STATE g_State;
static volatile UINT64 g_PagingFence;
static unsigned char g_CommandAllocation[4096];

static HRESULT APIENTRY TestCreateContext(
    HANDLE Device, D3DDDICB_CREATECONTEXTVIRTUAL* Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->NodeOrdinal == BC250_WINSYS_NODE_COMPUTE);
    g_State.CreateContext++;
    Args->hContext = (HANDLE)(ULONG_PTR)0xC001u;
    return S_OK;
}

static HRESULT APIENTRY TestDestroyContext(
    HANDLE Device, const D3DDDICB_DESTROYCONTEXT* Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->hContext == (HANDLE)(ULONG_PTR)0xC001u);
    g_State.DestroyContext++;
    return S_OK;
}

static HRESULT APIENTRY TestCreatePagingQueue(
    HANDLE Device, D3DDDICB_CREATEPAGINGQUEUE* Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    g_State.CreatePagingQueue++;
    Args->hPagingQueue = 0x9001u;
    Args->hSyncObject = 0x9002u;
    Args->FenceValueCPUVirtualAddress = (VOID*)&g_PagingFence;
    return S_OK;
}

static HRESULT APIENTRY TestDestroyPagingQueue(
    HANDLE Device, const D3DDDI_DESTROYPAGINGQUEUE* Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->hPagingQueue == 0x9001u);
    g_State.DestroyPagingQueue++;
    return S_OK;
}

static HRESULT APIENTRY TestAllocate(HANDLE Device, D3DDDICB_ALLOCATE* Args)
{
    const BC250_ALLOCATION_PRIVATE_V1* privateData;
    D3DDDI_ALLOCATIONINFO2* info;
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->NumAllocations == 1u);
    info = Args->pAllocationInfo2;
    assert(info != NULL);
    privateData = (const BC250_ALLOCATION_PRIVATE_V1*)info->pPrivateDriverData;
    assert(info->PrivateDriverDataSize == sizeof(*privateData));
    assert(privateData->Magic == BC250_ALLOCATION_PRIVATE_MAGIC);
    assert(privateData->AllocationToken != 0u);
    assert(privateData->ByteSize == sizeof(g_CommandAllocation));
    assert(privateData->Alignment == 4096u);
    g_State.Allocate++;
    info->hAllocation = 0xA200u;
    return S_OK;
}

static HRESULT APIENTRY TestMapGpuVa(
    HANDLE Device, D3DDDI_MAPGPUVIRTUALADDRESS* Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->hPagingQueue == 0x9001u);
    assert(Args->hAllocation == 0xA200u && Args->Protection.Execute == 1u);
    g_State.Map++;
    Args->VirtualAddress = 0x220000u;
    Args->PagingFenceValue = 7u;
    return S_OK;
}

static HRESULT APIENTRY TestMakeResident(
    HANDLE Device, D3DDDI_MAKERESIDENT* Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->AllocationList[0] == 0xA200u);
    g_State.MakeResident++;
    Args->PagingFenceValue = 8u;
    return S_OK;
}

static HRESULT APIENTRY TestWait(
    HANDLE Device,
    const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU* Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->ObjectHandleArray[0] == 0x9002u);
    g_State.Wait++;
    g_PagingFence = Args->FenceValueArray[0];
    return S_OK;
}

static HRESULT APIENTRY TestLock(HANDLE Device, D3DDDICB_LOCK* Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->hAllocation == 0xA200u);
    assert(Args->Flags.WriteOnly == 1u && Args->Flags.LockEntire == 1u);
    g_State.Lock++;
    Args->pData = g_CommandAllocation;
    return S_OK;
}

static HRESULT APIENTRY TestUnlock(
    HANDLE Device, const D3DDDICB_UNLOCK* Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->phAllocations[0] == 0xA200u);
    g_State.Unlock++;
    return S_OK;
}

static HRESULT APIENTRY TestSubmit(
    HANDLE Device, const D3DDDICB_SUBMITCOMMAND* Args)
{
    const uint32_t* pm4 = (const uint32_t*)g_CommandAllocation;
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->Commands == 0x220000u);
    assert(Args->CommandLength == 17u * sizeof(uint32_t));
    assert(Args->BroadcastContextCount == 1u);
    assert(Args->BroadcastContext[0] == (HANDLE)(ULONG_PTR)0xC001u);
    assert(pm4[2] == 0x00500000u);
    assert(pm4[4] == 0xBC250C01u && pm4[8] == 0xBC250C05u);
    assert(pm4[9] == BC250_PM4_RELEASE_MEM_HEADER_PROVEN);
    assert(pm4[14] == 0xBC250F01u);
    g_State.Submit++;
    return S_OK;
}

static HRESULT APIENTRY TestEvict(HANDLE Device, const D3DDDICB_EVICT* Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->AllocationList[0] == 0xA200u);
    g_State.Evict++;
    return S_OK;
}

static HRESULT APIENTRY TestFreeGpuVa(
    HANDLE Device, const D3DDDICB_FREEGPUVIRTUALADDRESS* Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->BaseAddress == 0x220000u);
    g_State.FreeGpuVa++;
    return S_OK;
}

static HRESULT APIENTRY TestDeallocate(
    HANDLE Device, const D3DDDICB_DEALLOCATE2* Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->HandleList[0] == 0xA200u);
    g_State.Deallocate++;
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
    return callbacks;
}

int main(void)
{
    D3DDDI_DEVICECALLBACKS callbacks = TestCallbacks();
    BC250_UMD_DEVICE device;
    const uint32_t values[5] = {
        0xBC250C01u, 0xBC250C02u, 0xBC250C03u,
        0xBC250C04u, 0xBC250C05u
    };

    memset(&g_State, 0, sizeof(g_State));
    memset(&device, 0, sizeof(device));
    memset(g_CommandAllocation, 0xFE, sizeof(g_CommandAllocation));
    g_PagingFence = 0u;

    device.DeviceActive = TRUE;
    assert(Bc250WinsysInitialize(&device.Winsys,
        (HANDLE)(ULONG_PTR)0xD00Du, &callbacks) == BC250_WINSYS_OK);
    device.WinsysInitialized = TRUE;

    assert(SUCCEEDED(Bc250UmdStartNode1Submission(&device, 4096u)));
    assert(device.NativeNode1Ready == TRUE);
    assert(device.Winsys.NodeOrdinal == BC250_WINSYS_NODE_COMPUTE);
    assert(device.Node1CmdBuffer.Allocation.Resident == TRUE);

    assert(SUCCEEDED(Bc250UmdSubmitNode1Proof(&device,
        0x500000u, values, 5u, 0x600000u, 0xBC250F01u)));
    assert(device.Node1CmdBuffer.CursorDwords == 0u);
    assert(g_State.Submit == 1u && g_State.Lock == 1u && g_State.Unlock == 1u);

    Bc250UmdDeviceTeardown(&device);
    assert(g_State.CreateContext == 1u && g_State.DestroyContext == 1u);
    assert(g_State.CreatePagingQueue == 1u && g_State.DestroyPagingQueue == 1u);
    assert(g_State.Allocate == 1u && g_State.Deallocate == 1u);
    assert(g_State.Map == 1u && g_State.MakeResident == 1u);
    assert(g_State.Evict == 1u && g_State.FreeGpuVa == 1u);

    puts("UMD NODE1 SUBMIT CONTRACT: PASS (17 DWORDs uploaded and routed; offline)");
    puts("HARDWARE VERDICT: NAO VALIDADO EM HARDWARE");
    return 0;
}
