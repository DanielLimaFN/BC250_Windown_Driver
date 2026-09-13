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
    UINT SignalSyncGpuCalls;
    HRESULT ForcedResult;
    /*
     * 13/09 -- vinculo de recurso. Sem estes campos o teste nao podia distinguir
     * "a UMD passou hResource" de "a UMD nao passou", que e' exatamente o defeito
     * medido na corrida 16:28:27.
     */
    HANDLE LastAllocateHResource;
    UINT LastAllocatePrimary;
    UINT LastAllocateVidPnSourceId;
    HANDLE LastDeallocateHResource;
    UINT LastDeallocateNumAllocations;
    HANDLE LastDestroyedContext;
    HANDLE LastSignalContext;
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
    /*
     * Handles DISTINTOS por no. O contexto de trabalho (Node 1) e o de
     * apresentacao (Node 0) sao objetos de kernel diferentes, e um stub que
     * devolvesse o mesmo handle para os dois deixaria passar justamente o
     * defeito que esta faixa investiga: apresentar pelo contexto errado.
     */
    Args->hContext = (Args->NodeOrdinal == BC250_WINSYS_NODE_SDMA)
        ? (HANDLE)(ULONG_PTR)0xC000u
        : (HANDLE)(ULONG_PTR)0xC001u;
    return S_OK;
}

static HRESULT APIENTRY TestDestroyContext(
    HANDLE Device,
    const D3DDDICB_DESTROYCONTEXT *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL);
    assert(Args->hContext == (HANDLE)(ULONG_PTR)0xC001u ||
           Args->hContext == (HANDLE)(ULONG_PTR)0xC000u);
    g_State.LastDestroyedContext = Args->hContext;
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
    /*
     * 0x43 e' o token dos casos que DEVEM ser recusados ANTES do callback. Ele
     * entra na allowlist de proposito: se a recusa for afrouxada, quem tem de
     * reprovar e' o invariante "AllocateCalls nao mudou", nomeando a causa --
     * e nao este assert do stub disparando primeiro por acidente.
     */
    assert(privateData->AllocationToken == 0x1234u ||
           privateData->AllocationToken ==
               (((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_PRIMARY <<
                   BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 0x42u) ||
           privateData->AllocationToken ==
               (((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_PRIMARY <<
                   BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 0x43u));
    assert(privateData->ByteSize == 0x4000u);
    assert(privateData->Alignment == 0x1000u);
    g_State.AllocateCalls++;
    g_State.LastAllocateHResource = Args->hResource;
    g_State.LastAllocatePrimary = info->Flags.Primary;
    g_State.LastAllocateVidPnSourceId = (UINT)info->VidPnSourceId;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    info->hAllocation = 0xA110u;
    info->GpuVirtualAddress = 0u;
    /*
     * O dxgkrnl so' devolve hKMResource quando a criacao e' baseada em recurso.
     * Espelhar isso aqui mantem o stub fiel ao contrato.
     */
    Args->hKMResource = (Args->hResource != NULL) ? 0x4B4Du : 0u;
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

static UINT64 g_SignalledGpuFenceValue;

/*
 * Sinal da fence monitorada PELO CONTRATO. Este stub e' o portao da regressao
 * da corrida 12/09 22:57 (0x116): antes da correcao ninguem chamava este
 * caminho, o dxgkrnl nunca sabia do sinal e o wait da CPU nunca era satisfeito.
 */
static HRESULT APIENTRY TestSignalSyncGpu(
    HANDLE Device,
    const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL && Args->ObjectCount == 1u);
    /*
     * O handle deixou de ser constante: desde 13/09 o stub devolve handles
     * DISTINTOS por no (0xC000 no Node 0, 0xC001 no Node 1), porque o contexto
     * de apresentacao e o de trabalho coexistem. Quem verifica QUAL contexto
     * sinalizou e' o teste, comparando com context.KernelContext -- aqui basta
     * exigir que seja um contexto valido, sem congelar o handle de um no.
     */
    assert(Args->hContext == (HANDLE)(ULONG_PTR)0xC001u ||
           Args->hContext == (HANDLE)(ULONG_PTR)0xC000u);
    assert(Args->ObjectHandleArray != NULL &&
        Args->ObjectHandleArray[0] == (D3DKMT_HANDLE)0x5001u);
    assert(Args->MonitoredFenceValueArray != NULL);
    g_State.LastSignalContext = Args->hContext;
    g_State.SignalSyncGpuCalls++;
    if (FAILED(g_State.ForcedResult)) return g_State.ForcedResult;
    g_SignalledGpuFenceValue = Args->MonitoredFenceValueArray[0];
    return S_OK;
}

static HRESULT APIENTRY TestDeallocate2(
    HANDLE Device,
    const D3DDDICB_DEALLOCATE2 *Args)
{
    assert(Device == (HANDLE)(ULONG_PTR)0xD00Du);
    assert(Args != NULL);
    /*
     * d3dumddi.h:4068 -- NumAllocations/HandleList sao IGNORADOS quando
     * hResource != NULL. O stub aceita as duas formas e registra qual chegou,
     * para o teste poder exigir SIMETRIA com a criacao.
     */
    if (Args->hResource != NULL) {
        assert(Args->NumAllocations == 0u);
    } else {
        assert(Args->NumAllocations == 1u);
        assert(Args->HandleList != NULL && Args->HandleList[0] == 0xA110u);
    }
    g_State.LastDeallocateHResource = Args->hResource;
    g_State.LastDeallocateNumAllocations = Args->NumAllocations;
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
    callbacks.pfnSignalSynchronizationObjectFromGpuCb = TestSignalSyncGpu;
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

    /*
     * CONTEXTO DE APRESENTACAO -- COEXISTE com o de trabalho, nao o substitui.
     *
     * Corrida 17:18:35: os dois unicos contextos pelos quais o dxgkrnl
     * apresentou estavam no Node 0; todo contexto nosso estava no Node 1 e
     * nunca apresentou. Este bloco trava que o de apresentacao nasce no no
     * pedido, que os DOIS handles sao distintos e que o de trabalho sobrevive
     * intacto -- se um sobrescrevesse o outro, o Present iria de novo pelo
     * contexto errado sem que nada reprovasse.
     */
    {
        UINT contextsBefore = g_State.CreateContextCalls;
        assert(Bc250WinsysCreatePresentContext(&context,
            BC250_WINSYS_NODE_SDMA) == BC250_WINSYS_OK);
        assert(g_State.CreateContextCalls == contextsBefore + 1u);
        assert(g_State.LastCreatedNode == BC250_WINSYS_NODE_SDMA);
        assert(context.PresentContextCreated);
        assert(context.PresentNodeOrdinal == BC250_WINSYS_NODE_SDMA);
        assert(context.PresentContext == (HANDLE)(ULONG_PTR)0xC000u);
        /* O contexto de trabalho NAO pode ter sido tocado. */
        assert(context.KernelContext == (HANDLE)(ULONG_PTR)0xC001u);
        assert(context.NodeOrdinal == BC250_WINSYS_NODE_COMPUTE);
        assert(context.ContextCreated);
        /* Idempotencia fail-closed: segunda criacao e' conflito, nao vazamento. */
        assert(Bc250WinsysCreatePresentContext(&context,
            BC250_WINSYS_NODE_SDMA) == BC250_WINSYS_STATE_CONFLICT);
        assert(g_State.CreateContextCalls == contextsBefore + 1u);
    }

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

    /*
     * O contexto de apresentacao TEM de ser destruido pelo SEU PROPRIO handle,
     * e destruir o de trabalho nao pode leva-lo junto. Na corrida 17:18:35 o
     * dwm.exe fez 61 CreateDevice/DestroyDevice em ~3 min; um contexto de kernel
     * nao destruido vaza uma vez por ciclo.
     */
    assert(context.PresentContextCreated);
    assert(context.PresentContext == (HANDLE)(ULONG_PTR)0xC000u);
    assert(Bc250WinsysDestroyPresentContext(&context) == BC250_WINSYS_OK);
    assert(g_State.LastDestroyedContext == (HANDLE)(ULONG_PTR)0xC000u);
    assert(!context.PresentContextCreated);
    assert(context.PresentContext == NULL);
    /* Segunda destruicao e' no-op, nao erro nem chamada duplicada. */
    assert(Bc250WinsysDestroyPresentContext(&context) == BC250_WINSYS_OK);
    assert(g_State.DestroyContextCalls == 2u);

    assert(Bc250WinsysDestroyPagingQueue(&context) == BC250_WINSYS_OK);
    /* Dois contextos: um de trabalho no Node 1, um de apresentacao no Node 0. */
    assert(g_State.CreateContextCalls == 2u);
    assert(g_State.AllocateCalls == 1u);
    assert(g_State.SubmitCalls == 1u);
    assert(g_State.LockCalls == 1u);
    assert(g_State.UnlockCalls == 1u);
    assert(g_State.DeallocateCalls == 1u);
    /* Dois contextos criados, dois destruidos: nenhum vaza. */
    assert(g_State.DestroyContextCalls == 2u);
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

    /* Make resident again and then deallocate -> no explicit evict (atomic in pfnDeallocate) */
    assert(Bc250WinsysMakeResident(&context, &allocation) == BC250_WINSYS_OK);
    assert(allocation.Resident);
    assert(g_State.MakeResidentCalls == 2u);

    assert(Bc250WinsysDeallocate(&context, &allocation) == BC250_WINSYS_OK);
    assert(!allocation.Resident);
    assert(g_State.EvictCalls == 1u);

    /*
     * PRIMARIA COM VINCULO DE RECURSO (13/09).
     *
     * O invariante que este bloco trava e' o defeito medido na corrida 16:28:27:
     * a UMD alocava a primaria do dwm.exe SEM hResource e SEM Flags.Primary, o
     * dxgkrnl nao ligava Flags.Resource, e o KMD registrava
     * "[CreateAllocation] Non-resource allocation" para toda superficie. Sem os
     * tres campos abaixo nao existe primaria do ponto de vista do kernel, e um
     * Present com FLIP nao tem como ser honrado.
     */
    {
        uint64_t primaryToken =
            ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_PRIMARY <<
                BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 0x42u;
        BC250_WINSYS_RESOURCE_BINDING binding;
        HANDLE rtResource = (HANDLE)(ULONG_PTR)0xB1D0u;

        memset(&binding, 0, sizeof(binding));
        binding.RuntimeResource = rtResource;
        binding.Primary = TRUE;
        binding.VidPnSourceId = 0u;

        assert(Bc250WinsysAllocateForResource(&context, primaryToken, 0x4000u,
            0x1000u, &binding, &primaryAllocation) == BC250_WINSYS_OK);
        assert(Bc250WinsysAllocationIsPrimary(&primaryAllocation));
        /* Os tres campos que faltavam, exigidos um a um. */
        assert(g_State.LastAllocateHResource == rtResource);
        assert(g_State.LastAllocatePrimary == 1u);
        assert(g_State.LastAllocateVidPnSourceId == 0u);
        assert(primaryAllocation.DeclaredPrimary);
        assert(primaryAllocation.RuntimeResource == rtResource);
        assert(primaryAllocation.KernelResource == 0x4B4Du);

        /* Simetria: criada com recurso, devolvida com recurso. */
        assert(Bc250WinsysDeallocate(&context, &primaryAllocation) == BC250_WINSYS_OK);
        assert(g_State.LastDeallocateHResource == rtResource);
        assert(g_State.LastDeallocateNumAllocations == 0u);
    }

    /*
     * FAIL-CLOSED, tres formas. Nenhuma delas pode virar uma "primaria" que o
     * kernel jamais vera como primaria -- suceder calado aqui e' o que produziu
     * 90 Present recusados e o laco de device-lost do dwm.exe.
     */
    {
        uint64_t primaryToken =
            ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_PRIMARY <<
                BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 0x43u;
        BC250_WINSYS_RESOURCE_BINDING binding;
        BC250_WINSYS_ALLOCATION rejected;
        UINT callsBefore = g_State.AllocateCalls;

        memset(&rejected, 0, sizeof(rejected));

        /* (a) token PRIMARY sem vinculo nenhum -> o caminho antigo, agora proibido. */
        assert(Bc250WinsysAllocate(&context, primaryToken, 0x4000u, 0x1000u,
            &rejected) == BC250_WINSYS_BAD_ARGUMENT);

        /* (b) primaria declarada sem hRTResource -> Flags.Resource ficaria 0. */
        memset(&binding, 0, sizeof(binding));
        binding.Primary = TRUE;
        binding.RuntimeResource = NULL;
        assert(Bc250WinsysAllocateForResource(&context, primaryToken, 0x4000u,
            0x1000u, &binding, &rejected) == BC250_WINSYS_BAD_ARGUMENT);

        /* (c) vinculo discordando do token -> duas fontes de verdade. */
        memset(&binding, 0, sizeof(binding));
        binding.Primary = FALSE;
        binding.RuntimeResource = (HANDLE)(ULONG_PTR)0xB1D1u;
        assert(Bc250WinsysAllocateForResource(&context, primaryToken, 0x4000u,
            0x1000u, &binding, &rejected) == BC250_WINSYS_BAD_ARGUMENT);

        /* Recusa e' ANTES do callback: o kernel nao pode ter sido tocado. */
        assert(g_State.AllocateCalls == callsBefore);
        assert(!rejected.Created);
    }

    /*
     * CONTRAPONTO -- o caso legitimo sem recurso continua funcionando. Command
     * buffer, staging de prova e scratch nunca sao apresentados e nao devem
     * carregar hResource; se este bloco reprovar, a correcao virou peneira.
     */
    {
        BC250_WINSYS_ALLOCATION plain;
        memset(&plain, 0, sizeof(plain));
        assert(Bc250WinsysAllocate(&context, 0x1234u, 0x4000u, 0x1000u,
            &plain) == BC250_WINSYS_OK);
        assert(g_State.LastAllocateHResource == NULL);
        assert(g_State.LastAllocatePrimary == 0u);
        assert(!plain.DeclaredPrimary);
        assert(plain.RuntimeResource == NULL);
        assert(plain.KernelResource == 0u);
        assert(Bc250WinsysDeallocate(&context, &plain) == BC250_WINSYS_OK);
        assert(g_State.LastDeallocateHResource == NULL);
        assert(g_State.LastDeallocateNumAllocations == 1u);
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

static void TestSignalMonitoredFenceFromGpu(void)
{
    D3DDDI_DEVICECALLBACKS callbacks = TestCallbacks();
    D3DDDI_DEVICECALLBACKS noSignal = TestCallbacks();
    BC250_WINSYS_CONTEXT context;
    BC250_WINSYS_CONTEXT bare;
    BC250_WINSYS_MONITORED_FENCE fence;
    BC250_WINSYS_MONITORED_FENCE unborn;

    memset(&g_State, 0, sizeof(g_State));
    memset(&context, 0, sizeof(context));
    memset(&bare, 0, sizeof(bare));
    memset(&fence, 0, sizeof(fence));
    memset(&unborn, 0, sizeof(unborn));
    g_MonitoredFence = 0u;
    g_SignalledGpuFenceValue = 0u;

    assert(Bc250WinsysInitialize(&context,
        (HANDLE)(ULONG_PTR)0xD00Du, &callbacks) == BC250_WINSYS_OK);
    assert(Bc250WinsysCreateVirtualContext(&context, 0u) == BC250_WINSYS_OK);
    assert(Bc250WinsysCreateMonitoredFence(&context, 0u, &fence) == BC250_WINSYS_OK);

    /* 1) Callback ausente falha FECHADO -- nunca silenciosamente OK. */
    noSignal.pfnSignalSynchronizationObjectFromGpuCb = NULL;
    assert(Bc250WinsysInitialize(&bare,
        (HANDLE)(ULONG_PTR)0xD00Du, &noSignal) == BC250_WINSYS_OK);
    assert(Bc250WinsysCreateVirtualContext(&bare, 0u) == BC250_WINSYS_OK);
    assert(Bc250WinsysSignalMonitoredFenceFromGpu(&bare, &fence, 1u) ==
        BC250_WINSYS_MISSING_CALLBACK);
    assert(g_State.SignalSyncGpuCalls == 0u);
    assert(Bc250WinsysDestroyVirtualContext(&bare) == BC250_WINSYS_OK);

    /* 2) Argumentos invalidos sao recusados ANTES de tocar no runtime. */
    assert(Bc250WinsysSignalMonitoredFenceFromGpu(NULL, &fence, 1u) ==
        BC250_WINSYS_BAD_ARGUMENT);
    assert(Bc250WinsysSignalMonitoredFenceFromGpu(&context, NULL, 1u) ==
        BC250_WINSYS_BAD_ARGUMENT);
    assert(Bc250WinsysSignalMonitoredFenceFromGpu(&context, &unborn, 1u) ==
        BC250_WINSYS_BAD_ARGUMENT);
    /* Valor 0 nunca avanca uma fence monitorada: recusa em vez de no-op mudo. */
    assert(Bc250WinsysSignalMonitoredFenceFromGpu(&context, &fence, 0u) ==
        BC250_WINSYS_BAD_ARGUMENT);
    assert(g_State.SignalSyncGpuCalls == 0u);

    /* 3) Caminho feliz: o runtime recebe contexto, objeto e valor exatos. */
    assert(Bc250WinsysSignalMonitoredFenceFromGpu(&context, &fence, 42u) ==
        BC250_WINSYS_OK);
    assert(g_State.SignalSyncGpuCalls == 1u);
    assert(g_SignalledGpuFenceValue == 42u);
    /*
     * O sinal sai pelo contexto de TRABALHO, nunca pelo de apresentacao. A
     * assercao antiga congelava o handle 0xC001; agora ela compara com a fonte,
     * entao continua valendo se o no do contexto de trabalho mudar.
     */
    assert(g_State.LastSignalContext == context.KernelContext);

    /* 4) Falha do runtime propaga; nao vira sucesso. */
    g_State.ForcedResult = E_FAIL;
    assert(Bc250WinsysSignalMonitoredFenceFromGpu(&context, &fence, 43u) ==
        BC250_WINSYS_CALLBACK_FAILED);
    assert(g_State.SignalSyncGpuCalls == 2u);
    assert(g_SignalledGpuFenceValue == 42u);
    g_State.ForcedResult = S_OK;

    /* 5) Sem contexto criado nao ha a quem enfileirar o sinal. */
    assert(Bc250WinsysDestroyVirtualContext(&context) == BC250_WINSYS_OK);
    assert(Bc250WinsysSignalMonitoredFenceFromGpu(&context, &fence, 44u) ==
        BC250_WINSYS_BAD_ARGUMENT);
    assert(g_State.SignalSyncGpuCalls == 2u);

    assert(Bc250WinsysDestroyMonitoredFence(&context, &fence) == BC250_WINSYS_OK);
}

int main(void)
{
    printf("=== BC250 WDDM WINSYS CONTRACT TEST ===\n");
    TestLifecycle();
    TestResidencyAndPrimaryClass();
    TestMonitoredFenceLifecycle();
    TestPagingPendingStatus();
    TestSignalMonitoredFenceFromGpu();
    TestFailClosed();
    printf("WDDM WINSYS CONTRACT: PASS (callbacks offline; hardware not touched)\n");
    return 0;
}
