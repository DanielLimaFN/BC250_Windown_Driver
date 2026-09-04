#ifndef BC250_MCDM_OBJECT_CONTRACT_H
#define BC250_MCDM_OBJECT_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT8 uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
#endif
#else
#include <stdint.h>
#if defined(_MSC_VER)
long __cdecl _InterlockedIncrement(long volatile *Addend);
long __cdecl _InterlockedDecrement(long volatile *Addend);
long __cdecl _InterlockedCompareExchange(long volatile *Destination, long Exchange, long Comparand);
#pragma intrinsic(_InterlockedIncrement)
#pragma intrinsic(_InterlockedDecrement)
#pragma intrinsic(_InterlockedCompareExchange)
#ifndef InterlockedIncrement
#define InterlockedIncrement _InterlockedIncrement
#endif
#ifndef InterlockedDecrement
#define InterlockedDecrement _InterlockedDecrement
#endif
#ifndef InterlockedCompareExchange
#define InterlockedCompareExchange _InterlockedCompareExchange
#endif
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_MCDM_OBJECT_REGISTRY_SIGNATURE 0x524A424Fu /* "OBJR" */
#define BC250_MCDM_OBJECT_LIVE_SIGNATURE     0x564A424Fu /* "OBJV" */
#define BC250_MCDM_OBJECT_DEAD_SIGNATURE     0x444A424Fu /* "OBJD" */
#define BC250_MCDM_MAX_OBJECTS 1024u
#define BC250_MCDM_SUBMISSION_RUNDOWN_CLOSING 0x40000000L
#define BC250_MCDM_SUBMISSION_RUNDOWN_COUNT_MASK 0x3FFFFFFFL

typedef void *BC250_MCDM_HANDLE;

typedef enum BC250_MCDM_OBJECT_TYPE {
    BC250_MCDM_OBJECT_NONE = 0,
    BC250_MCDM_OBJECT_PROCESS,
    BC250_MCDM_OBJECT_DEVICE,
    BC250_MCDM_OBJECT_CONTEXT,
    BC250_MCDM_OBJECT_ALLOCATION,
    BC250_MCDM_OBJECT_OPEN_ALLOCATION
} BC250_MCDM_OBJECT_TYPE;

typedef enum BC250_MCDM_OBJECT_STATE {
    BC250_MCDM_OBJECT_STATE_NONE = 0,
    BC250_MCDM_OBJECT_STATE_LIVE = 1,
    BC250_MCDM_OBJECT_STATE_DESTROYING = 2,
    BC250_MCDM_OBJECT_STATE_DEAD = 3
} BC250_MCDM_OBJECT_STATE;

typedef enum BC250_MCDM_OBJECT_STATUS {
    BC250_MCDM_OBJECT_OK = 0,
    BC250_MCDM_OBJECT_BAD_ARGUMENT,
    BC250_MCDM_OBJECT_BAD_REGISTRY,
    BC250_MCDM_OBJECT_NO_SPACE,
    BC250_MCDM_OBJECT_DUPLICATE_OWNER,
    BC250_MCDM_OBJECT_INVALID_HANDLE,
    BC250_MCDM_OBJECT_STALE_HANDLE,
    BC250_MCDM_OBJECT_TYPE_MISMATCH,
    BC250_MCDM_OBJECT_OWNER_MISMATCH,
    BC250_MCDM_OBJECT_PARENT_MISMATCH,
    BC250_MCDM_OBJECT_CHILDREN_LIVE,
    BC250_MCDM_OBJECT_NODE_MISMATCH,
    BC250_MCDM_OBJECT_NO_ROOT_PAGE_TABLE,
    BC250_MCDM_OBJECT_IN_TEARDOWN,
    BC250_MCDM_OBJECT_IN_FLIGHT_SUBMISSIONS
} BC250_MCDM_OBJECT_STATUS;

typedef struct BC250_MCDM_OBJECT_RECORD {
    uint32_t Signature;
    uint32_t Generation;
    uint32_t Type;
    uint32_t State;
    uint32_t ChildCount;
    const void *ExternalOwner;
    const void *OwnerAdapter;
    BC250_MCDM_HANDLE OwnerProcess;
    BC250_MCDM_HANDLE Parent;
    BC250_MCDM_HANDLE ReferencedObject;
    uint32_t NodeOrdinal;
    uint32_t EngineOrdinal;
    uint32_t ApplicableSegmentId;
    volatile long InFlightSubmissions;
    volatile long SubmissionRundown;
    uint32_t TeardownEpoch;
    uint32_t PageTableEpoch;
    uint64_t AllocationToken;
    uint64_t AllocationBytes;
    uint64_t AllocationAlignment;
    uint64_t RootPageTableAddress;
    uint32_t RootPageTableSegmentId;
    uint32_t RootPageTableEntries;
    uint32_t RootPageTableSetCount;
} BC250_MCDM_OBJECT_RECORD;

typedef struct BC250_MCDM_OBJECT_REGISTRY {
    uint32_t Signature;
    uint32_t NextGeneration;
    uint32_t NextSlot;
    uint32_t LiveCount;
    uint32_t Teardown;
    uint32_t TeardownEpoch;
    BC250_MCDM_OBJECT_RECORD Objects[BC250_MCDM_MAX_OBJECTS];
} BC250_MCDM_OBJECT_REGISTRY;

/*
 * Visão imutável de submissão do contexto, segura para consulta no IRQL
 * da DDI DxgkDdiSubmitCommandVirtual (DISPATCH_LEVEL). Não aloca e não usa push lock.
 */
typedef struct BC250_CONTEXT_SUBMISSION_VIEW {
    BC250_MCDM_HANDLE ContextHandle;
    BC250_MCDM_HANDLE ParentDevice;
    BC250_MCDM_HANDLE OwnerProcess;
    const void *OwnerAdapter;
    uint32_t Generation;
    uint32_t NodeOrdinal;
    uint32_t EngineOrdinal;
    uint32_t ApplicableSegmentId;
    uint64_t RootPageTableAddress;
    uint32_t RootPageTableSegmentId;
    uint32_t RootPageTableEntries;
    uint32_t PageTableEpoch;
    uint32_t TeardownEpoch;
} BC250_CONTEXT_SUBMISSION_VIEW;

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectRegistryInitialize(
    BC250_MCDM_OBJECT_REGISTRY *Registry);

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectCreateProcess(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    const void *ExternalOwner,
    BC250_MCDM_HANDLE *Handle);

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectCreateChild(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_OBJECT_TYPE Type,
    BC250_MCDM_HANDLE Parent,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE *Handle);

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectCreateContext(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    const void *OwnerAdapter,
    BC250_MCDM_HANDLE ParentDevice,
    BC250_MCDM_HANDLE OwnerProcess,
    uint32_t NodeOrdinal,
    uint32_t EngineOrdinal,
    BC250_MCDM_HANDLE *Handle);

BC250_MCDM_OBJECT_STATUS Bc250McdmContextAcquireSubmissionView(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_HANDLE ContextHandle,
    const void *ExpectedAdapter,
    uint32_t ExpectedNodeOrdinal,
    BC250_CONTEXT_SUBMISSION_VIEW *View);

BC250_MCDM_OBJECT_STATUS Bc250McdmContextReleaseSubmissionView(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_HANDLE ContextHandle);

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectCreateAdapterAllocation(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    const void *ExternalOwner,
    uint64_t AllocationToken,
    uint64_t AllocationBytes,
    uint64_t AllocationAlignment,
    BC250_MCDM_HANDLE *Handle);

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectOpenAllocation(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_HANDLE Device,
    BC250_MCDM_HANDLE Allocation,
    const void *ExternalOwner,
    BC250_MCDM_HANDLE *Handle);

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectFindAllocationByToken(
    const BC250_MCDM_OBJECT_REGISTRY *Registry,
    uint64_t AllocationToken,
    BC250_MCDM_HANDLE *Handle);

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectSetRootPageTable(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_HANDLE Context,
    uint32_t SegmentId,
    uint64_t Address,
    uint32_t NumEntries,
    uint32_t MaxEntries);

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectLookup(
    const BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_HANDLE Handle,
    BC250_MCDM_OBJECT_TYPE ExpectedType,
    BC250_MCDM_HANDLE ExpectedOwnerProcess,
    BC250_MCDM_OBJECT_RECORD *Record);

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectDestroy(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_HANDLE Handle,
    BC250_MCDM_OBJECT_TYPE ExpectedType,
    BC250_MCDM_HANDLE ExpectedOwnerProcess);

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectRegistryTeardown(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    uint32_t *DestroyedCount);

const char *Bc250McdmObjectStatusName(BC250_MCDM_OBJECT_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
