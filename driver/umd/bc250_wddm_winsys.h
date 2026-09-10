#ifndef BC250_WDDM_WINSYS_H
#define BC250_WDDM_WINSYS_H

#pragma warning(push)
#pragma warning(disable: 4201)
#include <windows.h>
#include <d3d9types.h>
#include <d3dumddi.h>
#pragma warning(pop)

#include "../core/bc250_allocation_abi_contract.h"
#include "../core/bc250_adapter_info_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_WINSYS_NODE_SDMA 0u
#define BC250_WINSYS_NODE_COMPUTE 1u

typedef enum BC250_WINSYS_STATUS {
    BC250_WINSYS_OK = 0,
    BC250_WINSYS_BAD_ARGUMENT,
    BC250_WINSYS_MISSING_CALLBACK,
    BC250_WINSYS_STATE_CONFLICT,
    BC250_WINSYS_CALLBACK_FAILED,
    BC250_WINSYS_BAD_CALLBACK_OUTPUT
} BC250_WINSYS_STATUS;

typedef struct BC250_WINSYS_CONTEXT {
    HANDLE RuntimeDevice;
    const D3DDDI_DEVICECALLBACKS *Callbacks;
    HANDLE KernelContext;
    D3DKMT_HANDLE PagingQueue;
    D3DKMT_HANDLE PagingSyncObject;
    volatile UINT64 *PagingFenceCpuVa;
    UINT NodeOrdinal;
    UINT ActiveGpuVaMappings;
    HRESULT LastCallbackHr;
    BOOL Initialized;
    BOOL ContextCreated;
    BOOL PagingQueueCreated;
} BC250_WINSYS_CONTEXT;

typedef struct BC250_WINSYS_ALLOCATION {
    D3DKMT_HANDLE KernelAllocation;
    D3DGPU_VIRTUAL_ADDRESS GpuVirtualAddress;
    uint64_t Token;
    uint64_t ByteSize;
    uint64_t Alignment;
    UINT64 PagingFenceValue;
    BOOL Created;
    BOOL OwnsGpuVaMapping;
    BOOL Resident;
} BC250_WINSYS_ALLOCATION;

typedef struct BC250_WINSYS_MONITORED_FENCE {
    D3DKMT_HANDLE SyncObject;
    volatile UINT64 *CpuVirtualAddress;
    D3DGPU_VIRTUAL_ADDRESS GpuVirtualAddress;
    UINT64 InitialValue;
    BOOL Created;
} BC250_WINSYS_MONITORED_FENCE;

BC250_WINSYS_STATUS Bc250WinsysInitialize(
    BC250_WINSYS_CONTEXT *Context,
    HANDLE RuntimeDevice,
    const D3DDDI_DEVICECALLBACKS *Callbacks);

BC250_WINSYS_STATUS Bc250WinsysCreateVirtualContext(
    BC250_WINSYS_CONTEXT *Context,
    UINT NodeOrdinal);

BC250_WINSYS_STATUS Bc250WinsysDestroyVirtualContext(
    BC250_WINSYS_CONTEXT *Context);

BC250_WINSYS_STATUS Bc250WinsysCreatePagingQueue(
    BC250_WINSYS_CONTEXT *Context);

BC250_WINSYS_STATUS Bc250WinsysDestroyPagingQueue(
    BC250_WINSYS_CONTEXT *Context);

BC250_WINSYS_STATUS Bc250WinsysAllocate(
    BC250_WINSYS_CONTEXT *Context,
    uint64_t Token,
    uint64_t ByteSize,
    uint64_t Alignment,
    BC250_WINSYS_ALLOCATION *Allocation);

BC250_WINSYS_STATUS Bc250WinsysDeallocate(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_ALLOCATION *Allocation);

BC250_WINSYS_STATUS Bc250WinsysMakeResident(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_ALLOCATION *Allocation);

BC250_WINSYS_STATUS Bc250WinsysEvict(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_ALLOCATION *Allocation);

BOOL Bc250WinsysAllocationIsPrimary(
    const BC250_WINSYS_ALLOCATION *Allocation);

BC250_WINSYS_STATUS Bc250WinsysMapAllocation(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_ALLOCATION *Allocation,
    BOOL Executable);

BC250_WINSYS_STATUS Bc250WinsysWaitPaging(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_ALLOCATION *Allocation);

BC250_WINSYS_STATUS Bc250WinsysSubmitGpuVa(
    BC250_WINSYS_CONTEXT *Context,
    D3DGPU_VIRTUAL_ADDRESS CommandGpuVa,
    UINT CommandLength);

/*
 * Submit only through the context that was created for ExpectedNodeOrdinal.
 * This prevents a caller from labelling work as Node 1 while broadcasting it
 * through a Node 0 (or uncreated) context.
 */
BC250_WINSYS_STATUS Bc250WinsysSubmitGpuVaForNode(
    BC250_WINSYS_CONTEXT *Context,
    UINT ExpectedNodeOrdinal,
    D3DGPU_VIRTUAL_ADDRESS CommandGpuVa,
    UINT CommandLength);

/*
 * Copy CPU-built command bytes into the actual WDDM allocation by using the
 * runtime Lock/Unlock callbacks. A detached CPU shadow is never executable.
 */
BC250_WINSYS_STATUS Bc250WinsysUploadAllocation(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_ALLOCATION *Allocation,
    const void *Source,
    UINT ByteCount);

/* Read bytes back from the real WDDM allocation. This is deliberately a
 * separate operation from Upload so a proof cannot compare two CPU shadows. */
BC250_WINSYS_STATUS Bc250WinsysReadAllocation(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_ALLOCATION *Allocation,
    void *Destination,
    UINT ByteCount);

BC250_WINSYS_STATUS Bc250WinsysCreateMonitoredFence(
    BC250_WINSYS_CONTEXT *Context,
    UINT64 InitialValue,
    BC250_WINSYS_MONITORED_FENCE *Fence);

BC250_WINSYS_STATUS Bc250WinsysDestroyMonitoredFence(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_MONITORED_FENCE *Fence);

BC250_WINSYS_STATUS Bc250WinsysSignalFenceFromCpu(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_MONITORED_FENCE *Fence,
    UINT64 FenceValue);

BC250_WINSYS_STATUS Bc250WinsysWaitForFenceFromCpu(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_MONITORED_FENCE *Fence,
    UINT64 TargetFenceValue);

const char *Bc250WinsysStatusName(BC250_WINSYS_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
