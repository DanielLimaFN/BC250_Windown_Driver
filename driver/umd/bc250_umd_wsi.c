/*
 * WSI.1/WSI.2 (docs 757 e 759) -- ponte da WSI Win32 da RADV com esta UMD no
 * mesmo processo. Contrato em bc250_umd_wsi.h.
 *
 * Incluido por bc250_d3d_umd.c (mesma unidade de compilacao), como
 * bc250_umd_resource_vk.c.
 *
 * Captura: uma por vez no processo; a WSI cria os recursos de uma swapchain
 * em sequencia, na thread que chamou vkCreateSwapchainKHR. Outra thread que
 * crie recursos ao mesmo tempo nao e' capturada (a thread armada e' comparada).
 */
#include "bc250_umd_wsi.h"

static SRWLOCK g_Bc250UmdWsiLock = SRWLOCK_INIT;
static volatile LONG g_Bc250UmdWsiThread = 0;
static BOOL g_Bc250UmdWsiFull = FALSE;
static BC250_UMD_WSI_SURFACE g_Bc250UmdWsiSlot;

static VOID Bc250UmdWsiNoteCreated(_In_ BC250_UMD_DEVICE *device, _In_ const BC250_UMD_RESOURCE *res)
{
    DWORD tid = GetCurrentThreadId();
    if (res == NULL || (DWORD)g_Bc250UmdWsiThread != tid) return;

    AcquireSRWLockExclusive(&g_Bc250UmdWsiLock);
    if ((DWORD)g_Bc250UmdWsiThread == tid && !g_Bc250UmdWsiFull) {
        memset(&g_Bc250UmdWsiSlot, 0, sizeof(g_Bc250UmdWsiSlot));
        g_Bc250UmdWsiSlot.Size = (UINT32)sizeof(g_Bc250UmdWsiSlot);
        g_Bc250UmdWsiSlot.Version = BC250_UMD_WSI_VERSION;
        g_Bc250UmdWsiSlot.Width = res->Width;
        g_Bc250UmdWsiSlot.Height = res->Height;
        g_Bc250UmdWsiSlot.RowPitch = res->RowPitch;
        g_Bc250UmdWsiSlot.DxgiFormat = (UINT32)res->Format;
        g_Bc250UmdWsiSlot.GpuAliased =
            (res->VkMemAliased && res->VkImg != VK_NULL_HANDLE) ? 1u : 0u;
        g_Bc250UmdWsiSlot.GpuVirtualAddress = (UINT64)res->Allocation.GpuVirtualAddress;
        g_Bc250UmdWsiSlot.ByteSize = (UINT64)res->Allocation.ByteSize;
        g_Bc250UmdWsiSlot.UmdResource = (void *)res;
        g_Bc250UmdWsiSlot.UmdDevice = (void *)device;
        g_Bc250UmdWsiFull = TRUE;
    }
    ReleaseSRWLockExclusive(&g_Bc250UmdWsiLock);

    Bc250UmdTrace("[WSI1] captura: res=%p %ux%u pitch=%u fmt=%u GpuVa=0x%llX bytes=%llu alias=%d",
        (void *)res, res->Width, res->Height, res->RowPitch, (unsigned)res->Format,
        (unsigned long long)res->Allocation.GpuVirtualAddress,
        (unsigned long long)res->Allocation.ByteSize, (int)res->VkMemAliased);

    /*
     * RES.1 (Corrida 301, doc 762): o blit do app escreve nesta alocacao pela
     * GPUVA, de OUTRO device, que nao a tem na propria residencia; o VidMm pode
     * move-la ou despeja-la com o blit em voo (299: fault + 1.102 Presents
     * recusados no TFM; 300: fault no back buffer do GravityMark). Prioridade
     * maxima a deixa por ultimo na fila de despejo. Mitigacao, nao garantia:
     * a garantia exige a alocacao compartilhada e aberta pelo device do app.
     */
    if (device != NULL && device->WinsysInitialized && res->Allocation.Created) {
        HRESULT prioHr = E_FAIL;
        BC250_WINSYS_STATUS st = Bc250WinsysSetPriority(&device->Winsys, &res->Allocation,
            D3DDDI_ALLOCATIONPRIORITY_MAXIMUM, &prioHr);
        Bc250UmdTrace("[RES1] back buffer da WSI com prioridade MAXIMA de residencia: hAlloc=0x%x status=%d hr=0x%08X",
            (unsigned)res->Allocation.KernelAllocation, (int)st, (unsigned)prioHr);
    }
}

BOOL WINAPI Bc250UmdWsiBeginCapture(void)
{
    AcquireSRWLockExclusive(&g_Bc250UmdWsiLock);
    InterlockedExchange(&g_Bc250UmdWsiThread, (LONG)GetCurrentThreadId());
    g_Bc250UmdWsiFull = FALSE;
    ReleaseSRWLockExclusive(&g_Bc250UmdWsiLock);
    return TRUE;
}

BOOL WINAPI Bc250UmdWsiEndCapture(BC250_UMD_WSI_SURFACE *Surface)
{
    BOOL ok = FALSE;
    UINT32 size;
    if (Surface == NULL || Surface->Size < (UINT32)FIELD_OFFSET(BC250_UMD_WSI_SURFACE, UmdDevice)) {
        Bc250UmdTrace("[WSI1] EndCapture RECUSADO: estrutura ausente ou menor que a v1");
        return FALSE;
    }
    size = Surface->Size < sizeof(BC250_UMD_WSI_SURFACE) ? Surface->Size : (UINT32)sizeof(BC250_UMD_WSI_SURFACE);
    AcquireSRWLockExclusive(&g_Bc250UmdWsiLock);
    if ((DWORD)g_Bc250UmdWsiThread == GetCurrentThreadId() && g_Bc250UmdWsiFull) {
        memcpy(Surface, &g_Bc250UmdWsiSlot, size);
        Surface->Size = size;
        ok = TRUE;
    }
    InterlockedExchange(&g_Bc250UmdWsiThread, 0);
    g_Bc250UmdWsiFull = FALSE;
    ReleaseSRWLockExclusive(&g_Bc250UmdWsiLock);
    if (!ok) Bc250UmdTrace("[WSI1] EndCapture: nenhuma alocacao criada nesta thread desde o BeginCapture");
    return ok;
}

/*
 * WSI.1: a RADV do app escreveu a textura por fora desta UMD. O layout
 * rastreado e' o da imagem linear desta UMD sobre a mesma alocacao: sem ele a
 * copia seguinte trataria a origem como "sem conteudo". Em imagem LINEAR o
 * layout nao muda bits na memoria.
 */
BOOL WINAPI Bc250UmdWsiMarkWritten(void *UmdResource)
{
    BC250_UMD_RESOURCE *res = (BC250_UMD_RESOURCE *)UmdResource;
    if (res == NULL || res->VkImg == VK_NULL_HANDLE || !res->VkMemAliased) return FALSE;
    res->VkLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    return TRUE;
}

/* OBJECT_ATTRIBUTES de winternl.h, so' para o D3DKMTShareObjects. */
typedef struct BC250_WSI_OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    void *ObjectName;
    ULONG Attributes;
    void *SecurityDescriptor;
    void *SecurityQualityOfService;
} BC250_WSI_OBJECT_ATTRIBUTES;
typedef LONG (APIENTRY *PFN_BC250_D3DKMT_SHARE_OBJECTS)(UINT cObjects,
    const D3DKMT_HANDLE *hObjects, void *pObjectAttributes, DWORD dwDesiredAccess,
    HANDLE *phSharedNtHandle);

/*
 * WSI.2: fence monitorada do runtime deste device, criada compartilhada (NT)
 * pelo callback do runtime e exportada com D3DKMTShareObjects. A RADV do app
 * importa o NT handle (vk_wddm2_monitored_fence, OpenSyncObjectFromNtHandle2)
 * e espera/sinaliza pelo escalonador, como faz com as suas.
 */
BOOL WINAPI Bc250UmdWsiCreateSharedFence(void *UmdDevice, HANDLE *NtHandle, UINT32 *FenceIndex)
{
    BC250_UMD_DEVICE *device = (BC250_UMD_DEVICE *)UmdDevice;
    D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 args;
    BC250_WSI_OBJECT_ATTRIBUTES oa;
    PFN_BC250_D3DKMT_SHARE_OBJECTS shareObjects;
    HMODULE gdi;
    HANDLE nt = NULL;
    HRESULT hr;
    LONG status;
    UINT32 index;

    if (NtHandle != NULL) *NtHandle = NULL;
    if (device == NULL || NtHandle == NULL || FenceIndex == NULL || !device->WinsysInitialized ||
        device->Winsys.Callbacks == NULL ||
        device->Winsys.Callbacks->pfnCreateSynchronizationObject2Cb == NULL) {
        Bc250UmdTrace("[WSI2] CreateSharedFence RECUSADO: device/winsys/callback ausente");
        return FALSE;
    }
    if (device->WsiFenceCount >= BC250_UMD_WSI_MAX_FENCES) {
        Bc250UmdTrace("[WSI2] CreateSharedFence RECUSADO: %u fences ja criadas neste device",
            device->WsiFenceCount);
        return FALSE;
    }
    gdi = GetModuleHandleA("gdi32.dll");
    if (gdi == NULL) gdi = LoadLibraryA("gdi32.dll");
    shareObjects = gdi != NULL ?
        (PFN_BC250_D3DKMT_SHARE_OBJECTS)(void *)GetProcAddress(gdi, "D3DKMTShareObjects") : NULL;
    if (shareObjects == NULL) {
        Bc250UmdTrace("[WSI2] CreateSharedFence RECUSADO: D3DKMTShareObjects ausente no gdi32");
        return FALSE;
    }

    memset(&args, 0, sizeof(args));
    args.Info.Type = D3DDDI_MONITORED_FENCE;
    args.Info.Flags.Shared = 1u;
    args.Info.Flags.NtSecuritySharing = 1u;
    args.Info.MonitoredFence.InitialFenceValue = 0u;
    hr = device->Winsys.Callbacks->pfnCreateSynchronizationObject2Cb(device->Winsys.RuntimeDevice, &args);
    if (FAILED(hr) || args.hSyncObject == 0u) {
        Bc250UmdTrace("[WSI2] CreateSharedFence RECUSADO: pfnCreateSynchronizationObject2Cb hr=0x%08X hSync=0x%x",
            (unsigned)hr, (unsigned)args.hSyncObject);
        return FALSE;
    }

    memset(&oa, 0, sizeof(oa));
    oa.Length = (ULONG)sizeof(oa);
    status = shareObjects(1u, &args.hSyncObject, &oa, D3DDDI_SYNC_OBJECT_ALL_ACCESS, &nt);
    if (status < 0 || nt == NULL) {
        D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT destroy;
        memset(&destroy, 0, sizeof(destroy));
        destroy.hSyncObject = args.hSyncObject;
        if (device->Winsys.Callbacks->pfnDestroySynchronizationObjectCb != NULL)
            (void)device->Winsys.Callbacks->pfnDestroySynchronizationObjectCb(device->Winsys.RuntimeDevice, &destroy);
        Bc250UmdTrace("[WSI2] CreateSharedFence RECUSADO: D3DKMTShareObjects status=0x%08X", (unsigned)status);
        return FALSE;
    }

    index = device->WsiFenceCount++;
    memset(&device->WsiFences[index], 0, sizeof(device->WsiFences[index]));
    device->WsiFences[index].SyncObject = args.hSyncObject;
    device->WsiFences[index].CpuVirtualAddress =
        (volatile UINT64 *)args.Info.MonitoredFence.FenceValueCPUVirtualAddress;
    device->WsiFences[index].GpuVirtualAddress = args.Info.MonitoredFence.FenceValueGPUVirtualAddress;
    device->WsiFences[index].Created = TRUE;
    *NtHandle = nt;
    *FenceIndex = index;
    Bc250UmdTrace("[WSI2] fence compartilhada #%u: hSync=0x%x NT=%p CpuVa=%p",
        index, (unsigned)args.hSyncObject, (void *)nt,
        (void *)args.Info.MonitoredFence.FenceValueCPUVirtualAddress);
    return TRUE;
}

BOOL WINAPI Bc250UmdWsiArmPresent(void *UmdDevice, void *UmdResource,
    UINT32 RenderFence, UINT64 RenderValue, UINT32 PresentFence, UINT64 PresentValue)
{
    BC250_UMD_DEVICE *device = (BC250_UMD_DEVICE *)UmdDevice;
    if (device == NULL || UmdResource == NULL ||
        RenderFence >= device->WsiFenceCount || PresentFence >= device->WsiFenceCount ||
        RenderValue == 0u || PresentValue == 0u) {
        Bc250UmdTrace("[WSI2] ArmPresent RECUSADO: argumentos invalidos (fences=%u render=%u/%llu present=%u/%llu)",
            device != NULL ? device->WsiFenceCount : 0u, RenderFence,
            (unsigned long long)RenderValue, PresentFence, (unsigned long long)PresentValue);
        return FALSE;
    }
    device->WsiArmResource = UmdResource;
    device->WsiArmRenderFence = RenderFence;
    device->WsiArmRenderValue = RenderValue;
    device->WsiArmPresentFence = PresentFence;
    device->WsiArmPresentValue = PresentValue;
    InterlockedExchange(&device->WsiArmed, 1);
    return TRUE;
}

/* Chamado pelo Bc250DxgiPresent: o Present armado pela WSI.2? */
static BOOL Bc250UmdWsiPresentIsArmed(_In_ BC250_UMD_DEVICE *device, _In_opt_ BC250_UMD_RESOURCE *srcRes)
{
    return device != NULL && srcRes != NULL &&
        InterlockedCompareExchange(&device->WsiArmed, 1, 1) == 1 &&
        device->WsiArmResource == (void *)srcRes;
}

/* Espera pela GPU, no contexto de apresentacao, o blit do app. */
static HRESULT Bc250UmdWsiPresentWaitRender(_In_ BC250_UMD_DEVICE *device)
{
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMGPU wait;
    const BC250_WINSYS_MONITORED_FENCE *fence = &device->WsiFences[device->WsiArmRenderFence];
    UINT64 value = device->WsiArmRenderValue;
    HRESULT hr;

    if (device->Winsys.Callbacks->pfnWaitForSynchronizationObjectFromGpuCb == NULL) return E_NOTIMPL;
    memset(&wait, 0, sizeof(wait));
    wait.hContext = device->Winsys.PresentContext;
    wait.ObjectCount = 1u;
    wait.ObjectHandleArray = &fence->SyncObject;
    wait.MonitoredFenceValueArray = &value;
    hr = device->Winsys.Callbacks->pfnWaitForSynchronizationObjectFromGpuCb(device->Winsys.RuntimeDevice, &wait);
    if (FAILED(hr) || value <= 16u || (value % 256u) == 0u) {
        Bc250UmdTrace("[WSI2] Present #%llu: espera pela GPU do blit do app (fence #%u >= %llu) hr=0x%08X",
            (unsigned long long)value, device->WsiArmRenderFence, (unsigned long long)value, (unsigned)hr);
    }
    return hr;
}

/* Sinaliza pela GPU, no contexto de apresentacao, que o Present terminou. */
static HRESULT Bc250UmdWsiPresentSignalDone(_In_ BC250_UMD_DEVICE *device)
{
    D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU signal;
    const BC250_WINSYS_MONITORED_FENCE *fence = &device->WsiFences[device->WsiArmPresentFence];
    UINT64 value = device->WsiArmPresentValue;
    HRESULT hr;

    if (device->Winsys.Callbacks->pfnSignalSynchronizationObjectFromGpuCb == NULL) return E_NOTIMPL;
    memset(&signal, 0, sizeof(signal));
    signal.hContext = device->Winsys.PresentContext;
    signal.ObjectCount = 1u;
    signal.ObjectHandleArray = &fence->SyncObject;
    signal.MonitoredFenceValueArray = &value;
    hr = device->Winsys.Callbacks->pfnSignalSynchronizationObjectFromGpuCb(device->Winsys.RuntimeDevice, &signal);
    if (FAILED(hr) || value <= 16u || (value % 256u) == 0u) {
        Bc250UmdTrace("[WSI2] Present #%llu: sinal pela GPU de present pronto (fence #%u = %llu) hr=0x%08X",
            (unsigned long long)value, device->WsiArmPresentFence, (unsigned long long)value, (unsigned)hr);
    }
    return hr;
}

static VOID Bc250UmdWsiDeviceTeardown(_Inout_ BC250_UMD_DEVICE *device)
{
    UINT32 i;
    if (device == NULL) return;
    InterlockedExchange(&device->WsiArmed, 0);
    if (!device->WinsysInitialized || device->Winsys.Callbacks == NULL ||
        device->Winsys.Callbacks->pfnDestroySynchronizationObjectCb == NULL) {
        device->WsiFenceCount = 0u;
        return;
    }
    for (i = 0u; i < device->WsiFenceCount; ++i) {
        if (device->WsiFences[i].Created) {
            D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT destroy;
            memset(&destroy, 0, sizeof(destroy));
            destroy.hSyncObject = device->WsiFences[i].SyncObject;
            (void)device->Winsys.Callbacks->pfnDestroySynchronizationObjectCb(device->Winsys.RuntimeDevice, &destroy);
            device->WsiFences[i].Created = FALSE;
        }
    }
    if (device->WsiFenceCount != 0u)
        Bc250UmdTrace("[WSI2] device=%p: %u fences compartilhadas destruidas", (void *)device, device->WsiFenceCount);
    device->WsiFenceCount = 0u;
}
