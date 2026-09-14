#include "bc250_wddm_winsys.h"

#include <stdio.h>
#include <string.h>

/*
 * Sink unico de trace da UMD.
 *
 * Corrida 13/09 05:50: o log arquivado nao tinha UMA linha da winsys, porque
 * BC250_WINSYS_LOG so' escrevia em OutputDebugStringA e stderr -- e o dwm.exe
 * nao tem stderr. Sabiamos que o compositor congelava entre o blt de tela
 * cheia e a submissao seguinte, e nao qual callback bloqueava. Um criterio de
 * corrida foi escrito sobre linhas que a instrumentacao nunca poderia produzir.
 *
 * Este sink e' o MESMO usado por Bc250UmdTrace (bc250_d3d_umd.c), para que
 * exista uma unica fonte de formato e um unico arquivo por processo. Fica em
 * C:\Windows\Temp porque a raiz de C: nao e' gravavel pela conta virtual do
 * dwm.exe; sobrevive a tela preta e ao reboot.
 */
void Bc250WinsysTraceRaw(const char *Message)
{
    char line[768];
    char imagePath[MAX_PATH];
    char logPath[MAX_PATH];
    const char *imageName = "?";
    int lineLength;
    HANDLE file;
    DWORD written = 0;
    SYSTEMTIME now;

    if (Message == NULL) {
        return;
    }
    if (GetModuleFileNameA(NULL, imagePath, (DWORD)sizeof(imagePath)) != 0) {
        const char *slash = strrchr(imagePath, '\\');
        imageName = (slash != NULL) ? (slash + 1) : imagePath;
    }

    GetLocalTime(&now);
    lineLength = _snprintf_s(line, sizeof(line), _TRUNCATE,
        "%02u:%02u:%02u.%03u [pid=%lu %s] %s\r\n",
        now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
        GetCurrentProcessId(), imageName, Message);
    if (lineLength < 0) {
        return;
    }

    OutputDebugStringA(line);

    if (_snprintf_s(logPath, sizeof(logPath), _TRUNCATE,
            "C:\\Windows\\Temp\\BC250_UMD_LOG_%lu.txt",
            GetCurrentProcessId()) < 0) {
        return;
    }
    file = CreateFileA(logPath,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    (void)WriteFile(file, line, (DWORD)lineLength, &written, NULL);
    (void)CloseHandle(file);
}

/*
 * O prefixo [WDDM_WINSYS] permanece para que as linhas da winsys continuem
 * separaveis das da UMD dentro do mesmo arquivo. stderr fica para as suites
 * offline, que rodam em console.
 */
#define BC250_WINSYS_LOG(fmt, ...) do { \
    char _log_buf[256]; \
    int _len = _snprintf_s(_log_buf, sizeof(_log_buf), _TRUNCATE, \
        "[WDDM_WINSYS] " fmt, ##__VA_ARGS__); \
    if (_len > 0) { \
        Bc250WinsysTraceRaw(_log_buf); \
        fprintf(stderr, "[WDDM_WINSYS pid=%lu] %s\n", \
            GetCurrentProcessId(), _log_buf); \
        fflush(stderr); \
    } \
} while (0)

static BC250_WINSYS_STATUS Bc250WinsysRememberFailure(
    BC250_WINSYS_CONTEXT *Context,
    HRESULT Hr)
{
    if (Context != NULL) Context->LastCallbackHr = Hr;
    return BC250_WINSYS_CALLBACK_FAILED;
}

BC250_WINSYS_STATUS Bc250WinsysInitialize(
    BC250_WINSYS_CONTEXT *Context,
    HANDLE RuntimeDevice,
    const D3DDDI_DEVICECALLBACKS *Callbacks)
{
    if (Context == NULL || RuntimeDevice == NULL || Callbacks == NULL) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    memset(Context, 0, sizeof(*Context));
    Context->RuntimeDevice = RuntimeDevice;
    Context->Callbacks = Callbacks;
    Context->LastCallbackHr = S_OK;
    Context->Initialized = TRUE;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysCreateVirtualContext(
    BC250_WINSYS_CONTEXT *Context,
    UINT NodeOrdinal)
{
    D3DDDICB_CREATECONTEXTVIRTUAL args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized ||
        NodeOrdinal > BC250_WINSYS_NODE_COMPUTE) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->ContextCreated) return BC250_WINSYS_STATE_CONFLICT;
    if (Context->Callbacks->pfnCreateContextVirtualCb == NULL ||
        Context->Callbacks->pfnDestroyContextCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.NodeOrdinal = NodeOrdinal;
    hr = Context->Callbacks->pfnCreateContextVirtualCb(
        Context->RuntimeDevice, &args);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);
    if (args.hContext == NULL) {
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }

    Context->KernelContext = args.hContext;
    Context->NodeOrdinal = NodeOrdinal;
    Context->ContextCreated = TRUE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysDestroyVirtualContext(
    BC250_WINSYS_CONTEXT *Context)
{
    D3DDDICB_DESTROYCONTEXT args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (!Context->ContextCreated) return BC250_WINSYS_OK;
    if (Context->Callbacks->pfnDestroyContextCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.hContext = Context->KernelContext;
    hr = Context->Callbacks->pfnDestroyContextCb(
        Context->RuntimeDevice, &args);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Context->KernelContext = NULL;
    Context->NodeOrdinal = 0u;
    Context->ContextCreated = FALSE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysCreatePresentContext(
    BC250_WINSYS_CONTEXT *Context,
    UINT NodeOrdinal)
{
    D3DDDICB_CREATECONTEXTVIRTUAL args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized ||
        NodeOrdinal > BC250_WINSYS_NODE_COMPUTE) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->PresentContextCreated) return BC250_WINSYS_STATE_CONFLICT;
    if (Context->Callbacks->pfnCreateContextVirtualCb == NULL ||
        Context->Callbacks->pfnDestroyContextCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.NodeOrdinal = NodeOrdinal;
    BC250_WINSYS_LOG("CreatePresentContext ENTER: Node=%u", NodeOrdinal);
    hr = Context->Callbacks->pfnCreateContextVirtualCb(
        Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("CreatePresentContext LEAVE: hr=0x%08X hContext=%p Node=%u",
        (unsigned)hr, (void*)args.hContext, NodeOrdinal);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);
    if (args.hContext == NULL) {
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }

    Context->PresentContext = args.hContext;
    Context->PresentNodeOrdinal = NodeOrdinal;
    Context->PresentContextCreated = TRUE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysDestroyPresentContext(
    BC250_WINSYS_CONTEXT *Context)
{
    D3DDDICB_DESTROYCONTEXT args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (!Context->PresentContextCreated) return BC250_WINSYS_OK;
    if (Context->Callbacks->pfnDestroyContextCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.hContext = Context->PresentContext;
    BC250_WINSYS_LOG("DestroyPresentContext ENTER: hContext=%p",
        (void*)Context->PresentContext);
    hr = Context->Callbacks->pfnDestroyContextCb(
        Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("DestroyPresentContext LEAVE: hr=0x%08X", (unsigned)hr);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Context->PresentContext = NULL;
    Context->PresentNodeOrdinal = 0u;
    Context->PresentContextCreated = FALSE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysCreatePagingQueue(
    BC250_WINSYS_CONTEXT *Context)
{
    D3DDDICB_CREATEPAGINGQUEUE args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->PagingQueueCreated) return BC250_WINSYS_STATE_CONFLICT;
    if (Context->Callbacks->pfnCreatePagingQueueCb == NULL ||
        Context->Callbacks->pfnDestroyPagingQueueCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    args.PhysicalAdapterIndex = 0u;
    hr = Context->Callbacks->pfnCreatePagingQueueCb(
        Context->RuntimeDevice, &args);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);
    if (args.hPagingQueue == 0u || args.hSyncObject == 0u ||
        args.FenceValueCPUVirtualAddress == NULL) {
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }

    Context->PagingQueue = args.hPagingQueue;
    Context->PagingSyncObject = args.hSyncObject;
    Context->PagingFenceCpuVa =
        (volatile UINT64 *)args.FenceValueCPUVirtualAddress;
    Context->PagingQueueCreated = TRUE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysDestroyPagingQueue(
    BC250_WINSYS_CONTEXT *Context)
{
    D3DDDI_DESTROYPAGINGQUEUE args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (!Context->PagingQueueCreated) return BC250_WINSYS_OK;
    if (Context->ActiveGpuVaMappings != 0u) {
        return BC250_WINSYS_STATE_CONFLICT;
    }
    if (Context->Callbacks->pfnDestroyPagingQueueCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.hPagingQueue = Context->PagingQueue;
    hr = Context->Callbacks->pfnDestroyPagingQueueCb(
        Context->RuntimeDevice, &args);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Context->PagingQueue = 0u;
    Context->PagingSyncObject = 0u;
    Context->PagingFenceCpuVa = NULL;
    Context->PagingQueueCreated = FALSE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysAllocate(
    BC250_WINSYS_CONTEXT *Context,
    uint64_t Token,
    uint64_t ByteSize,
    uint64_t Alignment,
    BC250_WINSYS_ALLOCATION *Allocation)
{
    return Bc250WinsysAllocateForResource(Context, Token, ByteSize, Alignment,
        NULL, Allocation);
}

BC250_WINSYS_STATUS Bc250WinsysAllocateForResource(
    BC250_WINSYS_CONTEXT *Context,
    uint64_t Token,
    uint64_t ByteSize,
    uint64_t Alignment,
    const BC250_WINSYS_RESOURCE_BINDING *Binding,
    BC250_WINSYS_ALLOCATION *Allocation)
{
    BC250_ALLOCATION_PRIVATE_V1 privateData;
    D3DDDI_ALLOCATIONINFO2 info;
    D3DDDICB_ALLOCATE args;
    HRESULT hr;
    BOOL bindPrimary = FALSE;
    BOOL tokenPrimary;

    if (Context == NULL || !Context->Initialized || Allocation == NULL ||
        Token == 0u || ByteSize == 0u ||
        Alignment < BC250_ALLOCATION_MIN_ALIGNMENT ||
        Alignment > BC250_ALLOCATION_MAX_ALIGNMENT ||
        (Alignment & (Alignment - 1u)) != 0u ||
        (ByteSize & (Alignment - 1u)) != 0u) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }

    /*
     * FONTE UNICA DE "ISTO E' PRIMARIA": a classe do token. O vinculo de recurso
     * nao pode DISCORDAR dela -- se discordasse, voltariamos a ter duas fontes de
     * verdade sobre primariedade, que e' exatamente a classe de defeito que esta
     * faixa corrige. Divergencia falha FECHADO.
     */
    tokenPrimary = (BC250_ALLOCATION_TOKEN_CLASS(Token) ==
                    BC250_ALLOCATION_TOKEN_CLASS_PRIMARY) ? TRUE : FALSE;
    if (Binding != NULL) {
        bindPrimary = Binding->Primary ? TRUE : FALSE;
        if (bindPrimary != tokenPrimary) {
            BC250_WINSYS_LOG("Allocate RECUSADO: divergencia de primariedade "
                "(token=%d binding=%d Token=0x%llX) -- duas fontes de verdade",
                (int)tokenPrimary, (int)bindPrimary,
                (unsigned long long)Token);
            return BC250_WINSYS_BAD_ARGUMENT;
        }
        /*
         * Primaria SEM handle de recurso do runtime e' o defeito medido em
         * 16:28:27: o dxgkrnl nao liga Flags.Resource, o KMD classifica como
         * nao-primaria e o FLIP do dwm.exe nao tem como ser honrado. Recusar e'
         * melhor que alocar uma "primaria" que o kernel nunca vera como tal.
         */
        if (bindPrimary && Binding->RuntimeResource == NULL) {
            BC250_WINSYS_LOG("Allocate RECUSADO: primaria sem hRTResource -- o "
                "dxgkrnl nao ligaria Flags.Resource e o KMD a trataria como "
                "nao-primaria (Token=0x%llX)", (unsigned long long)Token);
            return BC250_WINSYS_BAD_ARGUMENT;
        }
    } else if (tokenPrimary) {
        BC250_WINSYS_LOG("Allocate RECUSADO: token PRIMARY sem vinculo de recurso "
            "(Token=0x%llX) -- use Bc250WinsysAllocateForResource",
            (unsigned long long)Token);
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Allocation->Created) return BC250_WINSYS_STATE_CONFLICT;
    if (Context->Callbacks->pfnAllocateCb == NULL ||
        (Context->Callbacks->pfnDeallocate2Cb == NULL &&
         Context->Callbacks->pfnDeallocateCb == NULL)) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    BC250_ALLOCATION_ABI_STATUS abiStatus;
    uint32_t flags = 0u;
    uint32_t tokenClass = BC250_ALLOCATION_TOKEN_CLASS(Token);
    if (tokenClass == BC250_ALLOCATION_TOKEN_CLASS_STAGING ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_CPU_VISIBLE ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_COMMAND ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_PROOF) {
        flags |= BC250_ALLOCATION_FLAG_CPU_VISIBLE;
    }
    const BC250_ALLOCATION_GEOMETRY *geometry = NULL;
    if (Binding != NULL && Binding->HasGeometry) {
        geometry = &Binding->Geometry;
    }
    abiStatus = Bc250AllocationAbiFormat(Token, ByteSize, Alignment, flags,
        geometry, &privateData);
    if (abiStatus == BC250_ALLOCATION_ABI_BAD_GEOMETRY) {
        /*
         * Geometria incoerente e' RECUSADA aqui, antes do callback. Deixar
         * passar produziria uma alocacao cuja descricao ao dxgkrnl seria falsa
         * -- e o sintoma apareceria como imagem torta, nao como erro.
         */
        BC250_WINSYS_LOG("Allocate RECUSADO: geometria incoerente "
            "(%ux%u pitch=%u fmt=%u contra ByteSize=%llu)",
            Binding->Geometry.Width, Binding->Geometry.Height,
            Binding->Geometry.Pitch, Binding->Geometry.Format,
            (unsigned long long)ByteSize);
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (abiStatus != BC250_ALLOCATION_ABI_OK ||
        privateData.Magic != BC250_ALLOCATION_PRIVATE_MAGIC) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }

    memset(&info, 0, sizeof(info));
    info.pPrivateDriverData = &privateData;
    info.PrivateDriverDataSize = (UINT)sizeof(privateData);
    if (bindPrimary) {
        /*
         * d3dukmdt.h:554 e :549. Sem estes dois campos o kernel nao tem como
         * saber que a superficie e' candidata a scanout, por mais que o nosso
         * token privado diga que e'.
         */
        info.Flags.Primary = 1u;
        info.VidPnSourceId = (D3DDDI_VIDEO_PRESENT_SOURCE_ID)Binding->VidPnSourceId;
    }

    memset(&args, 0, sizeof(args));
    args.NumAllocations = 1u;
    args.pAllocationInfo2 = &info;
    if (Binding != NULL) {
        /* d3dumddi.h:3457 -- e' isto que faz o dxgkrnl ligar Flags.Resource. */
        args.hResource = Binding->RuntimeResource;
    }
    BC250_WINSYS_LOG("Allocate ENTER: Token=0x%llX Size=%llu Align=%llu "
        "hRTResource=%p Primary=%d VidPnSourceId=%u",
        (unsigned long long)Token, (unsigned long long)ByteSize,
        (unsigned long long)Alignment, (void*)args.hResource,
        (int)bindPrimary, (unsigned)info.VidPnSourceId);
    hr = Context->Callbacks->pfnAllocateCb(Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("Allocate LEAVE: hr=0x%08X hAlloc=0x%x hKMResource=0x%x",
        (unsigned)hr, (unsigned)info.hAllocation, (unsigned)args.hKMResource);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);
    if (info.hAllocation == 0u) {
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }

    memset(Allocation, 0, sizeof(*Allocation));
    Allocation->KernelAllocation = info.hAllocation;
    Allocation->GpuVirtualAddress = info.GpuVirtualAddress;
    Allocation->Token = Token;
    Allocation->ByteSize = ByteSize;
    Allocation->Alignment = Alignment;
    Allocation->RuntimeResource = (Binding != NULL) ? Binding->RuntimeResource : NULL;
    Allocation->KernelResource = args.hKMResource;
    Allocation->DeclaredPrimary = bindPrimary;
    Allocation->Created = TRUE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysDeallocate(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_ALLOCATION *Allocation)
{
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Allocation == NULL) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (!Allocation->Created) return BC250_WINSYS_OK;

    /*
     * No WDDM 2.0+, pfnDeallocate2Cb / pfnDeallocateCb realiza a eviccao
     * atomica e o desregistro seguro no kernel. Evitar a chamada explicita a
     * pfnEvictCb imediatamente antes da desalocacao para nao introduzir corridas
     * no gerenciador de memoria do kernel (dxgmms2 / VIDMM_RECYCLE_HEAP_MGR).
     */
    /* Preserve residency state if cleanup fails; retry must not add a reference. */
    if (Allocation->OwnsGpuVaMapping) {
        D3DDDICB_FREEGPUVIRTUALADDRESS freeArgs;
        BC250_WINSYS_STATUS waitStatus;
        if (Context->Callbacks->pfnFreeGpuVirtualAddressCb == NULL) {
            return BC250_WINSYS_MISSING_CALLBACK;
        }
        waitStatus = Bc250WinsysWaitPaging(Context, Allocation);
        if (waitStatus != BC250_WINSYS_OK) return waitStatus;
        memset(&freeArgs, 0, sizeof(freeArgs));
        freeArgs.BaseAddress = Allocation->GpuVirtualAddress;
        freeArgs.Size = Allocation->ByteSize;
        hr = Context->Callbacks->pfnFreeGpuVirtualAddressCb(
            Context->RuntimeDevice, &freeArgs);
        if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);
        Allocation->OwnsGpuVaMapping = FALSE;
        Allocation->GpuVirtualAddress = 0u;
        Allocation->PagingFenceValue = 0u;
        Context->ActiveGpuVaMappings--;
    }

    /*
     * SIMETRIA COM A CRIACAO (13/09): alocacao criada com hResource TEM de ser
     * devolvida com hResource. d3dumddi.h:3472 -- "NumAllocations: ignored if
     * hResource != NULL" --, ou seja o runtime libera o recurso inteiro. Passar
     * so' a lista de handles deixaria o objeto de recurso do kernel orfao.
     */
    if (Context->Callbacks->pfnDeallocate2Cb != NULL) {
        D3DDDICB_DEALLOCATE2 args2;
        memset(&args2, 0, sizeof(args2));
        if (Allocation->RuntimeResource != NULL) {
            args2.hResource = Allocation->RuntimeResource;
        } else {
            args2.NumAllocations = 1u;
            args2.HandleList = &Allocation->KernelAllocation;
        }
        hr = Context->Callbacks->pfnDeallocate2Cb(
            Context->RuntimeDevice, &args2);
    } else if (Context->Callbacks->pfnDeallocateCb != NULL) {
        D3DDDICB_DEALLOCATE args;
        memset(&args, 0, sizeof(args));
        if (Allocation->RuntimeResource != NULL) {
            args.hResource = Allocation->RuntimeResource;
        } else {
            args.NumAllocations = 1u;
            args.HandleList = &Allocation->KernelAllocation;
        }
        hr = Context->Callbacks->pfnDeallocateCb(Context->RuntimeDevice, &args);
    } else {
        return BC250_WINSYS_MISSING_CALLBACK;
    }
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    memset(Allocation, 0, sizeof(*Allocation));
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysMapAllocation(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_ALLOCATION *Allocation,
    BOOL Executable)
{
    D3DDDI_MAPGPUVIRTUALADDRESS args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Allocation == NULL ||
        !Allocation->Created || Allocation->ByteSize == 0u ||
        (Allocation->ByteSize & 0xFFFu) != 0u) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Allocation->GpuVirtualAddress != 0u) {
        return BC250_WINSYS_STATE_CONFLICT;
    }
    if (!Context->PagingQueueCreated ||
        Context->Callbacks->pfnMapGpuVirtualAddressCb == NULL ||
        Context->Callbacks->pfnFreeGpuVirtualAddressCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.hPagingQueue = Context->PagingQueue;
    args.MinimumAddress = 0x10000u;
    args.MaximumAddress = BC250_ADAPTER_MAX_GPUVA_ADDRESS;
    args.hAllocation = Allocation->KernelAllocation;
    args.SizeInPages = Allocation->ByteSize >> 12;
    args.Protection.Write = 1u;
    args.Protection.Execute = Executable ? 1u : 0u;
    BC250_WINSYS_LOG("MapAllocation ENTER: hAlloc=0x%x Min=0x%llX Max=0x%llX Pages=%llu Exec=%d",
        (unsigned)Allocation->KernelAllocation, (unsigned long long)args.MinimumAddress,
        (unsigned long long)args.MaximumAddress, (unsigned long long)args.SizeInPages, (int)Executable);
    hr = Context->Callbacks->pfnMapGpuVirtualAddressCb(
        Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("MapAllocation LEAVE: hr=0x%08X VA=0x%llX PagingFence=%llu",
        (unsigned)hr, (unsigned long long)args.VirtualAddress, (unsigned long long)args.PagingFenceValue);
    if (FAILED(hr) && hr != (HRESULT)0x8000000AL /* E_PENDING / STATUS_PENDING */) {
        return Bc250WinsysRememberFailure(Context, hr);
    }
    if (args.VirtualAddress == 0u || args.PagingFenceValue == 0u) {
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }

    Allocation->GpuVirtualAddress = args.VirtualAddress;
    Allocation->PagingFenceValue = args.PagingFenceValue;
    Allocation->OwnsGpuVaMapping = TRUE;
    Context->ActiveGpuVaMappings++;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysWaitPaging(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_ALLOCATION *Allocation)
{
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU args;
    D3DKMT_HANDLE objectHandle;
    UINT64 fenceValue;
    UINT64 currentFence = 0ull;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Allocation == NULL ||
        !Allocation->Created) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Allocation->PagingFenceValue == 0u) {
        return BC250_WINSYS_OK;
    }
    if (!Context->PagingQueueCreated ||
        Context->Callbacks->pfnWaitForSynchronizationObjectFromCpuCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }
    if (Context->PagingFenceCpuVa != NULL) {
        currentFence = *Context->PagingFenceCpuVa;
        if (currentFence >= Allocation->PagingFenceValue) {
            BC250_WINSYS_LOG("WaitPaging SKIP (already reached): TargetFence=%llu CurrentFence=%llu",
                (unsigned long long)Allocation->PagingFenceValue, (unsigned long long)currentFence);
            return BC250_WINSYS_OK;
        }
    }

    objectHandle = Context->PagingSyncObject;
    fenceValue = Allocation->PagingFenceValue;
    memset(&args, 0, sizeof(args));
    args.ObjectCount = 1u;
    args.ObjectHandleArray = &objectHandle;
    args.FenceValueArray = &fenceValue;
    BC250_WINSYS_LOG("WaitPaging ENTER: TargetFence=%llu CurrentFence=%llu SyncObj=0x%x",
        (unsigned long long)fenceValue, (unsigned long long)currentFence, (unsigned)objectHandle);
    hr = Context->Callbacks->pfnWaitForSynchronizationObjectFromCpuCb(
        Context->RuntimeDevice, &args);
    if (Context->PagingFenceCpuVa != NULL) {
        currentFence = *Context->PagingFenceCpuVa;
    }
    BC250_WINSYS_LOG("WaitPaging LEAVE: hr=0x%08X TargetFence=%llu CurrentFence=%llu",
        (unsigned)hr, (unsigned long long)fenceValue, (unsigned long long)currentFence);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysSubmitGpuVa(
    BC250_WINSYS_CONTEXT *Context,
    D3DGPU_VIRTUAL_ADDRESS CommandGpuVa,
    UINT CommandLength)
{
    D3DDDICB_SUBMITCOMMAND args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized ||
        !Context->ContextCreated || CommandGpuVa == 0u ||
        CommandLength == 0u || (CommandLength & 3u) != 0u) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->Callbacks->pfnSubmitCommandCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.Commands = CommandGpuVa;
    args.CommandLength = CommandLength;
    args.BroadcastContextCount = 1u;
    args.BroadcastContext[0] = Context->KernelContext;
    /*
     * ENTER/LEAVE aqui porque pfnSubmitCommandCb BLOQUEIA quando a fila do
     * contexto esta cheia, e esta era a unica funcao da winsys sem trace.
     * Na corrida 13/09 05:50 o compositor congelou entre o blt de tela cheia
     * e a submissao seguinte, e nao havia como saber se foi aqui.
     */
    BC250_WINSYS_LOG("SubmitGpuVa ENTER: hCtx=0x%p Gpuva=0x%llX Bytes=%u Node=%u",
        Context->KernelContext, (unsigned long long)CommandGpuVa,
        CommandLength, Context->NodeOrdinal);
    hr = Context->Callbacks->pfnSubmitCommandCb(
        Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("SubmitGpuVa LEAVE: hr=0x%08X Bytes=%u", (unsigned)hr, CommandLength);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysSubmitGpuVaForNode(
    BC250_WINSYS_CONTEXT *Context,
    UINT ExpectedNodeOrdinal,
    D3DGPU_VIRTUAL_ADDRESS CommandGpuVa,
    UINT CommandLength)
{
    if (Context == NULL || !Context->Initialized ||
        ExpectedNodeOrdinal > BC250_WINSYS_NODE_COMPUTE) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (!Context->ContextCreated ||
        Context->NodeOrdinal != ExpectedNodeOrdinal) {
        return BC250_WINSYS_STATE_CONFLICT;
    }
    return Bc250WinsysSubmitGpuVa(Context, CommandGpuVa, CommandLength);
}

BC250_WINSYS_STATUS Bc250WinsysUploadAllocation(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_ALLOCATION *Allocation,
    const void *Source,
    UINT ByteCount)
{
    D3DDDICB_LOCK lockArgs;
    D3DDDICB_UNLOCK unlockArgs;
    D3DKMT_HANDLE allocationHandle;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Allocation == NULL ||
        !Allocation->Created || Source == NULL || ByteCount == 0u ||
        (uint64_t)ByteCount > Allocation->ByteSize) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->Callbacks->pfnLockCb == NULL ||
        Context->Callbacks->pfnUnlockCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&lockArgs, 0, sizeof(lockArgs));
    lockArgs.hAllocation = Allocation->KernelAllocation;
    lockArgs.Flags.WriteOnly = 1u;
    lockArgs.Flags.LockEntire = 1u;
    BC250_WINSYS_LOG("Upload ENTER: hAlloc=0x%x Bytes=%u",
        (unsigned)Allocation->KernelAllocation, ByteCount);
    hr = Context->Callbacks->pfnLockCb(Context->RuntimeDevice, &lockArgs);
    if (FAILED(hr)) {
        BC250_WINSYS_LOG("Upload LOCK falhou: hr=0x%08X", (unsigned)hr);
        return Bc250WinsysRememberFailure(Context, hr);
    }
    if (lockArgs.pData == NULL) {
        allocationHandle = lockArgs.hAllocation;
        memset(&unlockArgs, 0, sizeof(unlockArgs));
        unlockArgs.NumAllocations = 1u;
        unlockArgs.phAllocations = &allocationHandle;
        (void)Context->Callbacks->pfnUnlockCb(
            Context->RuntimeDevice, &unlockArgs);
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }

    memcpy(lockArgs.pData, Source, ByteCount);

    allocationHandle = lockArgs.hAllocation;
    memset(&unlockArgs, 0, sizeof(unlockArgs));
    unlockArgs.NumAllocations = 1u;
    unlockArgs.phAllocations = &allocationHandle;
    hr = Context->Callbacks->pfnUnlockCb(Context->RuntimeDevice, &unlockArgs);
    BC250_WINSYS_LOG("Upload LEAVE: hr=0x%08X Bytes=%u",
        (unsigned)hr, ByteCount);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysReadAllocation(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_ALLOCATION *Allocation,
    void *Destination,
    UINT ByteCount)
{
    D3DDDICB_LOCK lockArgs;
    D3DDDICB_UNLOCK unlockArgs;
    D3DKMT_HANDLE allocationHandle;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Allocation == NULL ||
        !Allocation->Created || Destination == NULL || ByteCount == 0u ||
        (uint64_t)ByteCount > Allocation->ByteSize) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->Callbacks->pfnLockCb == NULL ||
        Context->Callbacks->pfnUnlockCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&lockArgs, 0, sizeof(lockArgs));
    lockArgs.hAllocation = Allocation->KernelAllocation;
    lockArgs.Flags.LockEntire = 1u;
    BC250_WINSYS_LOG("Read ENTER: hAlloc=0x%x Bytes=%u",
        (unsigned)Allocation->KernelAllocation, ByteCount);
    hr = Context->Callbacks->pfnLockCb(Context->RuntimeDevice, &lockArgs);
    if (FAILED(hr)) {
        BC250_WINSYS_LOG("Read LOCK falhou: hr=0x%08X", (unsigned)hr);
        return Bc250WinsysRememberFailure(Context, hr);
    }
    if (lockArgs.pData == NULL) {
        allocationHandle = lockArgs.hAllocation;
        memset(&unlockArgs, 0, sizeof(unlockArgs));
        unlockArgs.NumAllocations = 1u;
        unlockArgs.phAllocations = &allocationHandle;
        (void)Context->Callbacks->pfnUnlockCb(
            Context->RuntimeDevice, &unlockArgs);
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }

    memcpy(Destination, lockArgs.pData, ByteCount);
    allocationHandle = lockArgs.hAllocation;
    memset(&unlockArgs, 0, sizeof(unlockArgs));
    unlockArgs.NumAllocations = 1u;
    unlockArgs.phAllocations = &allocationHandle;
    hr = Context->Callbacks->pfnUnlockCb(Context->RuntimeDevice, &unlockArgs);
    BC250_WINSYS_LOG("Read LEAVE: hr=0x%08X Bytes=%u",
        (unsigned)hr, ByteCount);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysMakeResident(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_ALLOCATION *Allocation)
{
    D3DDDI_MAKERESIDENT args;
    HRESULT hr;
    BC250_WINSYS_STATUS waitStatus;

    if (Context == NULL || !Context->Initialized || Allocation == NULL ||
        !Allocation->Created) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Allocation->Resident) {
        return BC250_WINSYS_OK;
    }
    if (Allocation->ResidencyPending) goto complete_residency;
    if (!Context->PagingQueueCreated || Context->Callbacks->pfnMakeResidentCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.hPagingQueue = Context->PagingQueue;
    args.NumAllocations = 1u;
    args.AllocationList = &Allocation->KernelAllocation;
    args.Flags.CantTrimFurther = 1u;

    BC250_WINSYS_LOG("MakeResident ENTER: hAlloc=0x%x hPagingQueue=0x%x",
        (unsigned)Allocation->KernelAllocation, (unsigned)args.hPagingQueue);
    hr = Context->Callbacks->pfnMakeResidentCb(Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("MakeResident LEAVE: hr=0x%08X PagingFence=%llu",
        (unsigned)hr, (unsigned long long)args.PagingFenceValue);

    if (FAILED(hr) && hr != (HRESULT)0x8000000AL /* E_PENDING / STATUS_PENDING */) {
        return Bc250WinsysRememberFailure(Context, hr);
    }

    /* F3.3: E_PENDING accepts a residency reference; it does not complete it.
     * Keep a pending flag across wait failures to avoid adding a second
     * reference on retry. The existing API now returns OK only after the
     * paging fence, preserving callers which immediately consume Resident. */
    if (args.PagingFenceValue > Allocation->PagingFenceValue)
        Allocation->PagingFenceValue = args.PagingFenceValue;
    Allocation->ResidencyPending = TRUE;
    if (hr == E_PENDING && args.PagingFenceValue == 0u) {
        Allocation->ResidencyRequestInvalid = TRUE;
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }
complete_residency:
    if (Allocation->ResidencyRequestInvalid)
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    waitStatus = Bc250WinsysWaitPaging(Context, Allocation);
    if (waitStatus != BC250_WINSYS_OK) return waitStatus;
    Allocation->ResidencyPending = FALSE;
    Allocation->Resident = TRUE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysEvict(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_ALLOCATION *Allocation)
{
    D3DDDICB_EVICT args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Allocation == NULL ||
        !Allocation->Created) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (!Allocation->Resident && !Allocation->ResidencyPending) {
        return BC250_WINSYS_OK;
    }
    if (Context->Callbacks->pfnEvictCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.NumAllocations = 1u;
    args.AllocationList = &Allocation->KernelAllocation;

    BC250_WINSYS_LOG("Evict ENTER: hAlloc=0x%x", (unsigned)Allocation->KernelAllocation);
    hr = Context->Callbacks->pfnEvictCb(Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("Evict LEAVE: hr=0x%08X NumBytesToTrim=%llu",
        (unsigned)hr, (unsigned long long)args.NumBytesToTrim);

    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Allocation->Resident = FALSE;
    Allocation->ResidencyPending = FALSE;
    Allocation->ResidencyRequestInvalid = FALSE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BOOL Bc250WinsysAllocationIsPrimary(
    const BC250_WINSYS_ALLOCATION *Allocation)
{
    if (Allocation == NULL || !Allocation->Created) {
        return FALSE;
    }
    return (BC250_ALLOCATION_TOKEN_CLASS(Allocation->Token) == BC250_ALLOCATION_TOKEN_CLASS_PRIMARY);
}

BC250_WINSYS_STATUS Bc250WinsysCreateMonitoredFence(
    BC250_WINSYS_CONTEXT *Context,
    UINT64 InitialValue,
    BC250_WINSYS_MONITORED_FENCE *Fence)
{
    D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Fence == NULL) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Fence->Created) {
        return BC250_WINSYS_STATE_CONFLICT;
    }
    if (Context->Callbacks->pfnCreateSynchronizationObject2Cb == NULL ||
        Context->Callbacks->pfnDestroySynchronizationObjectCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.Info.Type = D3DDDI_MONITORED_FENCE;
    args.Info.MonitoredFence.InitialFenceValue = InitialValue;

    BC250_WINSYS_LOG("CreateMonitoredFence ENTER: InitialValue=%llu", (unsigned long long)InitialValue);
    hr = Context->Callbacks->pfnCreateSynchronizationObject2Cb(Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("CreateMonitoredFence LEAVE: hr=0x%08X hSync=0x%x CpuVa=%p GpuVa=0x%llX",
        (unsigned)hr, (unsigned)args.hSyncObject,
        (void*)args.Info.MonitoredFence.FenceValueCPUVirtualAddress,
        (unsigned long long)args.Info.MonitoredFence.FenceValueGPUVirtualAddress);

    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);
    if (args.hSyncObject == 0u ||
        args.Info.MonitoredFence.FenceValueCPUVirtualAddress == NULL ||
        args.Info.MonitoredFence.FenceValueGPUVirtualAddress == 0u) {
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }

    memset(Fence, 0, sizeof(*Fence));
    Fence->SyncObject = args.hSyncObject;
    Fence->CpuVirtualAddress = (volatile UINT64*)args.Info.MonitoredFence.FenceValueCPUVirtualAddress;
    Fence->GpuVirtualAddress = args.Info.MonitoredFence.FenceValueGPUVirtualAddress;
    Fence->InitialValue = InitialValue;
    Fence->Created = TRUE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysDestroyMonitoredFence(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_MONITORED_FENCE *Fence)
{
    D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Fence == NULL) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (!Fence->Created) {
        return BC250_WINSYS_OK;
    }
    if (Context->Callbacks->pfnDestroySynchronizationObjectCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.hSyncObject = Fence->SyncObject;

    BC250_WINSYS_LOG("DestroyMonitoredFence ENTER: hSync=0x%x", (unsigned)Fence->SyncObject);
    hr = Context->Callbacks->pfnDestroySynchronizationObjectCb(Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("DestroyMonitoredFence LEAVE: hr=0x%08X", (unsigned)hr);

    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    memset(Fence, 0, sizeof(*Fence));
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysSignalFenceFromCpu(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_MONITORED_FENCE *Fence,
    UINT64 FenceValue)
{
    D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMCPU args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Fence == NULL || !Fence->Created) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->Callbacks->pfnSignalSynchronizationObjectFromCpuCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.ObjectCount = 1u;
    args.ObjectHandleArray = &Fence->SyncObject;
    args.FenceValueArray = &FenceValue;

    BC250_WINSYS_LOG("SignalFenceFromCpu ENTER: hSync=0x%x Value=%llu",
        (unsigned)Fence->SyncObject, (unsigned long long)FenceValue);
    hr = Context->Callbacks->pfnSignalSynchronizationObjectFromCpuCb(Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("SignalFenceFromCpu LEAVE: hr=0x%08X", (unsigned)hr);

    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

/*
 * Sinalizacao de fence monitorada PELO CONTRATO WDDM.
 *
 * Corrida 12/09 22:57 (BugCheck 0x116, 5 TDRs): o blt da UMD escrevia o valor
 * da fence monitorada com um pacote SDMA montado por ela mesma, e o
 * D3DDDICB_SUBMITCOMMAND saia sem qualquer objeto de sincronizacao. O dxgkrnl
 * nunca ficava sabendo do sinal, entao pfnWaitForSynchronizationObjectFromCpuCb
 * nunca era satisfeito e o VidSch declarava hang com a engine OCIOSA e DRENADA
 * (RPTR==WPTR, STATUS1=0x3FF em todos os 5 quiesces).
 *
 * O caminho correto e' este: o runtime enfileira o sinal no MESMO contexto,
 * depois do comando, e o KMD materializa a escrita em
 * Bc250McdmBuildSignalMonitoredFence -- que ja emite Bc250SdmaEmitFence64,
 * largura correta para a fence de 64 bits do WDDM.
 *
 * Ordenacao: o dxgkrnl serializa os pacotes do contexto, logo o sinal so'
 * executa depois do command buffer submetido imediatamente antes.
 */
BC250_WINSYS_STATUS Bc250WinsysSignalMonitoredFenceFromGpu(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_MONITORED_FENCE *Fence,
    UINT64 FenceValue)
{
    D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || !Context->ContextCreated ||
        Fence == NULL || !Fence->Created || FenceValue == 0ull) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->Callbacks->pfnSignalSynchronizationObjectFromGpuCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.hContext = Context->KernelContext;
    args.ObjectCount = 1u;
    args.ObjectHandleArray = &Fence->SyncObject;
    args.MonitoredFenceValueArray = &FenceValue;

    BC250_WINSYS_LOG("SignalMonitoredFenceFromGpu ENTER: hCtx=0x%x hSync=0x%x Value=%llu",
        (unsigned)(ULONG_PTR)Context->KernelContext, (unsigned)Fence->SyncObject,
        (unsigned long long)FenceValue);
    hr = Context->Callbacks->pfnSignalSynchronizationObjectFromGpuCb(
        Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("SignalMonitoredFenceFromGpu LEAVE: hr=0x%08X", (unsigned)hr);

    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysWaitForFenceFromCpu(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_MONITORED_FENCE *Fence,
    UINT64 TargetFenceValue)
{
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU args;
    UINT64 currentFence = 0ull;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Fence == NULL || !Fence->Created) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->Callbacks->pfnWaitForSynchronizationObjectFromCpuCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    if (Fence->CpuVirtualAddress != NULL) {
        currentFence = *Fence->CpuVirtualAddress;
        if (currentFence >= TargetFenceValue) {
            BC250_WINSYS_LOG("WaitForFenceFromCpu SKIP (already reached): TargetFence=%llu CurrentFence=%llu",
                (unsigned long long)TargetFenceValue, (unsigned long long)currentFence);
            return BC250_WINSYS_OK;
        }
    }

    memset(&args, 0, sizeof(args));
    args.ObjectCount = 1u;
    args.ObjectHandleArray = &Fence->SyncObject;
    args.FenceValueArray = &TargetFenceValue;

    BC250_WINSYS_LOG("WaitForFenceFromCpu ENTER: TargetFence=%llu CurrentFence=%llu SyncObj=0x%x",
        (unsigned long long)TargetFenceValue, (unsigned long long)currentFence, (unsigned)Fence->SyncObject);
    hr = Context->Callbacks->pfnWaitForSynchronizationObjectFromCpuCb(Context->RuntimeDevice, &args);
    if (Fence->CpuVirtualAddress != NULL) {
        currentFence = *Fence->CpuVirtualAddress;
    }
    BC250_WINSYS_LOG("WaitForFenceFromCpu LEAVE: hr=0x%08X TargetFence=%llu CurrentFence=%llu",
        (unsigned)hr, (unsigned long long)TargetFenceValue, (unsigned long long)currentFence);

    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

const char *Bc250WinsysStatusName(BC250_WINSYS_STATUS Status)
{
    switch (Status) {
    case BC250_WINSYS_OK: return "OK";
    case BC250_WINSYS_BAD_ARGUMENT: return "BAD_ARGUMENT";
    case BC250_WINSYS_MISSING_CALLBACK: return "MISSING_CALLBACK";
    case BC250_WINSYS_STATE_CONFLICT: return "STATE_CONFLICT";
    case BC250_WINSYS_CALLBACK_FAILED: return "CALLBACK_FAILED";
    case BC250_WINSYS_BAD_CALLBACK_OUTPUT: return "BAD_CALLBACK_OUTPUT";
    default: return "UNKNOWN";
    }
}
