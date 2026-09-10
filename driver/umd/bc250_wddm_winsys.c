#include "bc250_wddm_winsys.h"

#include <stdio.h>
#include <string.h>

#define BC250_WINSYS_LOG(fmt, ...) do { \
    char _log_buf[256]; \
    int _len = _snprintf_s(_log_buf, sizeof(_log_buf), _TRUNCATE, \
        "[WDDM_WINSYS pid=%lu] " fmt "\n", GetCurrentProcessId(), ##__VA_ARGS__); \
    if (_len > 0) { \
        OutputDebugStringA(_log_buf); \
        fprintf(stderr, "%s", _log_buf); \
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
    BC250_ALLOCATION_PRIVATE_V1 privateData;
    D3DDDI_ALLOCATIONINFO2 info;
    D3DDDICB_ALLOCATE args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Allocation == NULL ||
        Token == 0u || ByteSize == 0u ||
        Alignment < BC250_ALLOCATION_MIN_ALIGNMENT ||
        Alignment > BC250_ALLOCATION_MAX_ALIGNMENT ||
        (Alignment & (Alignment - 1u)) != 0u ||
        (ByteSize & (Alignment - 1u)) != 0u) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Allocation->Created) return BC250_WINSYS_STATE_CONFLICT;
    if (Context->Callbacks->pfnAllocateCb == NULL ||
        (Context->Callbacks->pfnDeallocate2Cb == NULL &&
         Context->Callbacks->pfnDeallocateCb == NULL)) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    BC250_ALLOCATION_ABI_STATUS abiStatus;
    abiStatus = Bc250AllocationAbiFormat(Token, ByteSize, Alignment, 0u, &privateData);
    if (abiStatus != BC250_ALLOCATION_ABI_OK ||
        privateData.Magic != BC250_ALLOCATION_PRIVATE_MAGIC) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }

    memset(&info, 0, sizeof(info));
    info.pPrivateDriverData = &privateData;
    info.PrivateDriverDataSize = (UINT)sizeof(privateData);

    memset(&args, 0, sizeof(args));
    args.NumAllocations = 1u;
    args.pAllocationInfo2 = &info;
    BC250_WINSYS_LOG("Allocate ENTER: Token=0x%llX Size=%llu Align=%llu",
        (unsigned long long)Token, (unsigned long long)ByteSize, (unsigned long long)Alignment);
    hr = Context->Callbacks->pfnAllocateCb(Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("Allocate LEAVE: hr=0x%08X hAlloc=0x%x",
        (unsigned)hr, (unsigned)info.hAllocation);
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

    if (Allocation->Resident) {
        if (Context->Callbacks->pfnEvictCb != NULL) {
            BC250_WINSYS_STATUS evictStatus = Bc250WinsysEvict(Context, Allocation);
            if (evictStatus != BC250_WINSYS_OK) return evictStatus;
        } else {
            Allocation->Resident = FALSE;
        }
    }

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

    if (Context->Callbacks->pfnDeallocate2Cb != NULL) {
        D3DDDICB_DEALLOCATE2 args2;
        memset(&args2, 0, sizeof(args2));
        args2.NumAllocations = 1u;
        args2.HandleList = &Allocation->KernelAllocation;
        hr = Context->Callbacks->pfnDeallocate2Cb(
            Context->RuntimeDevice, &args2);
    } else if (Context->Callbacks->pfnDeallocateCb != NULL) {
        D3DDDICB_DEALLOCATE args;
        memset(&args, 0, sizeof(args));
        args.NumAllocations = 1u;
        args.HandleList = &Allocation->KernelAllocation;
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
    hr = Context->Callbacks->pfnSubmitCommandCb(
        Context->RuntimeDevice, &args);
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

    if (Context == NULL || !Context->Initialized || Allocation == NULL ||
        !Allocation->Created) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Allocation->Resident) {
        return BC250_WINSYS_OK;
    }
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

    Allocation->Resident = TRUE;
    if (args.PagingFenceValue != 0ull) {
        Allocation->PagingFenceValue = args.PagingFenceValue;
    }
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
    if (!Allocation->Resident) {
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
