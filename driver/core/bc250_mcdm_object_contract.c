#include "bc250_mcdm_object_contract.h"

#include <string.h>

static uint32_t Bc250McdmNextGeneration(BC250_MCDM_OBJECT_REGISTRY *Registry)
{
    ++Registry->NextGeneration;
    if (Registry->NextGeneration == 0u) ++Registry->NextGeneration;
    return Registry->NextGeneration;
}

static BC250_MCDM_OBJECT_RECORD *Bc250McdmFindObject(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_HANDLE Handle)
{
    uint32_t index;

    if (Registry == NULL || Handle == NULL) return NULL;
    for (index = 0u; index < Registry->NextSlot; ++index) {
        if (Handle == (BC250_MCDM_HANDLE)&Registry->Objects[index]) {
            return &Registry->Objects[index];
        }
    }
    return NULL;
}

static const BC250_MCDM_OBJECT_RECORD *Bc250McdmFindObjectConst(
    const BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_HANDLE Handle)
{
    uint32_t index;

    if (Registry == NULL || Handle == NULL) return NULL;
    for (index = 0u; index < Registry->NextSlot; ++index) {
        if (Handle == (BC250_MCDM_HANDLE)&Registry->Objects[index]) {
            return &Registry->Objects[index];
        }
    }
    return NULL;
}

static int Bc250McdmAcquireSubmissionRundown(
    BC250_MCDM_OBJECT_RECORD *Record)
{
    long observed;
    long desired;

    if (Record == NULL) return 0;
    for (;;) {
        observed = InterlockedCompareExchange(&Record->SubmissionRundown, 0, 0);
        if ((observed & BC250_MCDM_SUBMISSION_RUNDOWN_CLOSING) != 0 ||
            (observed & BC250_MCDM_SUBMISSION_RUNDOWN_COUNT_MASK) ==
                BC250_MCDM_SUBMISSION_RUNDOWN_COUNT_MASK) {
            return 0;
        }
        desired = observed + 1;
        if (InterlockedCompareExchange(
                &Record->SubmissionRundown, desired, observed) == observed) {
            return 1;
        }
    }
}

static long Bc250McdmCloseSubmissionRundown(
    BC250_MCDM_OBJECT_RECORD *Record)
{
    long observed;
    long desired;

    if (Record == NULL) return BC250_MCDM_SUBMISSION_RUNDOWN_CLOSING;
    for (;;) {
        observed = InterlockedCompareExchange(&Record->SubmissionRundown, 0, 0);
        desired = observed | BC250_MCDM_SUBMISSION_RUNDOWN_CLOSING;
        if (InterlockedCompareExchange(
                &Record->SubmissionRundown, desired, observed) == observed) {
            return desired;
        }
    }
}

static BC250_MCDM_OBJECT_STATUS Bc250McdmValidateRegistry(
    const BC250_MCDM_OBJECT_REGISTRY *Registry)
{
    if (Registry == NULL) return BC250_MCDM_OBJECT_BAD_ARGUMENT;
    if (Registry->Signature != BC250_MCDM_OBJECT_REGISTRY_SIGNATURE ||
        Registry->NextSlot > BC250_MCDM_MAX_OBJECTS ||
        Registry->LiveCount > Registry->NextSlot) {
        return BC250_MCDM_OBJECT_BAD_REGISTRY;
    }
    return BC250_MCDM_OBJECT_OK;
}

static BC250_MCDM_OBJECT_STATUS Bc250McdmAllocateRecord(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_OBJECT_TYPE Type,
    const void *ExternalOwner,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE Parent,
    BC250_MCDM_HANDLE *Handle)
{
    BC250_MCDM_OBJECT_RECORD *record;

    if (Handle != NULL) *Handle = NULL;
    if (Bc250McdmValidateRegistry(Registry) != BC250_MCDM_OBJECT_OK ||
        Handle == NULL || Type <= BC250_MCDM_OBJECT_NONE ||
        Type > BC250_MCDM_OBJECT_OPEN_ALLOCATION) {
        return BC250_MCDM_OBJECT_BAD_ARGUMENT;
    }
    if (Registry->Teardown != 0u) {
        return BC250_MCDM_OBJECT_IN_TEARDOWN;
    }
    if (Registry->NextSlot == BC250_MCDM_MAX_OBJECTS) {
        return BC250_MCDM_OBJECT_NO_SPACE;
    }

    record = &Registry->Objects[Registry->NextSlot++];
    memset(record, 0, sizeof(*record));
    record->Signature = BC250_MCDM_OBJECT_LIVE_SIGNATURE;
    record->Generation = Bc250McdmNextGeneration(Registry);
    record->Type = (uint32_t)Type;
    record->State = (uint32_t)BC250_MCDM_OBJECT_STATE_LIVE;
    record->ExternalOwner = ExternalOwner;
    record->OwnerProcess = OwnerProcess;
    record->Parent = Parent;
    ++Registry->LiveCount;
    *Handle = (BC250_MCDM_HANDLE)record;
    return BC250_MCDM_OBJECT_OK;
}

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectRegistryInitialize(
    BC250_MCDM_OBJECT_REGISTRY *Registry)
{
    if (Registry == NULL) return BC250_MCDM_OBJECT_BAD_ARGUMENT;
    memset(Registry, 0, sizeof(*Registry));
    Registry->Signature = BC250_MCDM_OBJECT_REGISTRY_SIGNATURE;
    return BC250_MCDM_OBJECT_OK;
}

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectCreateProcess(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    const void *ExternalOwner,
    BC250_MCDM_HANDLE *Handle)
{
    uint32_t index;

    if (Handle != NULL) *Handle = NULL;
    if (Bc250McdmValidateRegistry(Registry) != BC250_MCDM_OBJECT_OK ||
        ExternalOwner == NULL || Handle == NULL) {
        return BC250_MCDM_OBJECT_BAD_ARGUMENT;
    }
    if (Registry->Teardown != 0u) {
        return BC250_MCDM_OBJECT_IN_TEARDOWN;
    }
    for (index = 0u; index < Registry->NextSlot; ++index) {
        const BC250_MCDM_OBJECT_RECORD *record = &Registry->Objects[index];
        if (record->Signature == BC250_MCDM_OBJECT_LIVE_SIGNATURE &&
            record->Type == BC250_MCDM_OBJECT_PROCESS &&
            record->ExternalOwner == ExternalOwner) {
            return BC250_MCDM_OBJECT_DUPLICATE_OWNER;
        }
    }
    return Bc250McdmAllocateRecord(Registry, BC250_MCDM_OBJECT_PROCESS,
        ExternalOwner, NULL, NULL, Handle);
}

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectLookup(
    const BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_HANDLE Handle,
    BC250_MCDM_OBJECT_TYPE ExpectedType,
    BC250_MCDM_HANDLE ExpectedOwnerProcess,
    BC250_MCDM_OBJECT_RECORD *Record)
{
    const BC250_MCDM_OBJECT_RECORD *found;

    if (Record != NULL) memset(Record, 0, sizeof(*Record));
    if (Bc250McdmValidateRegistry(Registry) != BC250_MCDM_OBJECT_OK ||
        Handle == NULL || Record == NULL) {
        return BC250_MCDM_OBJECT_BAD_ARGUMENT;
    }
    found = Bc250McdmFindObjectConst(Registry, Handle);
    if (found == NULL) return BC250_MCDM_OBJECT_INVALID_HANDLE;
    if (found->Signature != BC250_MCDM_OBJECT_LIVE_SIGNATURE ||
        found->State == (uint32_t)BC250_MCDM_OBJECT_STATE_DEAD) {
        return BC250_MCDM_OBJECT_STALE_HANDLE;
    }
    if (ExpectedType != BC250_MCDM_OBJECT_NONE &&
        found->Type != (uint32_t)ExpectedType) {
        return BC250_MCDM_OBJECT_TYPE_MISMATCH;
    }
    if (ExpectedOwnerProcess != NULL &&
        found->OwnerProcess != ExpectedOwnerProcess &&
        Handle != ExpectedOwnerProcess) {
        return BC250_MCDM_OBJECT_OWNER_MISMATCH;
    }
    *Record = *found;
    return BC250_MCDM_OBJECT_OK;
}

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectCreateChild(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_OBJECT_TYPE Type,
    BC250_MCDM_HANDLE Parent,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE *Handle)
{
    BC250_MCDM_OBJECT_RECORD ownerRecord;
    BC250_MCDM_OBJECT_RECORD parentRecord;
    BC250_MCDM_OBJECT_RECORD *parent;
    BC250_MCDM_OBJECT_STATUS status;
    BC250_MCDM_OBJECT_TYPE expectedParentType;

    if (Handle != NULL) *Handle = NULL;
    if (Type == BC250_MCDM_OBJECT_DEVICE) {
        expectedParentType = BC250_MCDM_OBJECT_PROCESS;
    } else if (Type == BC250_MCDM_OBJECT_CONTEXT) {
        expectedParentType = BC250_MCDM_OBJECT_DEVICE;
    } else {
        return BC250_MCDM_OBJECT_BAD_ARGUMENT;
    }

    status = Bc250McdmObjectLookup(Registry, OwnerProcess,
        BC250_MCDM_OBJECT_PROCESS, OwnerProcess, &ownerRecord);
    if (status != BC250_MCDM_OBJECT_OK) return status;
    status = Bc250McdmObjectLookup(Registry, Parent, expectedParentType,
        OwnerProcess, &parentRecord);
    if (status != BC250_MCDM_OBJECT_OK) return status;
    if (Type == BC250_MCDM_OBJECT_DEVICE && Parent != OwnerProcess) {
        return BC250_MCDM_OBJECT_PARENT_MISMATCH;
    }
    parent = Bc250McdmFindObject(Registry, Parent);
    if (parent == NULL || parent->ChildCount == (uint32_t)~0u) {
        return BC250_MCDM_OBJECT_BAD_REGISTRY;
    }

    status = Bc250McdmAllocateRecord(Registry, Type,
        ownerRecord.ExternalOwner, OwnerProcess, Parent, Handle);
    if (status != BC250_MCDM_OBJECT_OK) return status;
    ++parent->ChildCount;
    return BC250_MCDM_OBJECT_OK;
}

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectCreateContext(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    const void *OwnerAdapter,
    BC250_MCDM_HANDLE ParentDevice,
    BC250_MCDM_HANDLE OwnerProcess,
    uint32_t NodeOrdinal,
    uint32_t EngineOrdinal,
    BC250_MCDM_HANDLE *Handle)
{
    BC250_MCDM_OBJECT_STATUS status;
    BC250_MCDM_OBJECT_RECORD *contextRec;

    if (Handle != NULL) *Handle = NULL;
    if (OwnerAdapter == NULL || NodeOrdinal >= 2u) {
        return BC250_MCDM_OBJECT_BAD_ARGUMENT;
    }

    status = Bc250McdmObjectCreateChild(Registry,
        BC250_MCDM_OBJECT_CONTEXT, ParentDevice, OwnerProcess, Handle);
    if (status != BC250_MCDM_OBJECT_OK) return status;

    contextRec = Bc250McdmFindObject(Registry, *Handle);
    if (contextRec == NULL) return BC250_MCDM_OBJECT_BAD_REGISTRY;

    contextRec->OwnerAdapter = OwnerAdapter;
    contextRec->NodeOrdinal = NodeOrdinal;
    contextRec->EngineOrdinal = EngineOrdinal;
    contextRec->ApplicableSegmentId = 1u;
    return BC250_MCDM_OBJECT_OK;
}

BC250_MCDM_OBJECT_STATUS Bc250McdmContextAcquireSubmissionView(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_HANDLE ContextHandle,
    const void *ExpectedAdapter,
    uint32_t ExpectedNodeOrdinal,
    BC250_CONTEXT_SUBMISSION_VIEW *View)
{
    BC250_MCDM_OBJECT_RECORD *contextRec;
    BC250_MCDM_OBJECT_RECORD *processRec;

    if (View != NULL) memset(View, 0, sizeof(*View));
    if (Registry == NULL || ContextHandle == NULL || View == NULL) {
        return BC250_MCDM_OBJECT_BAD_ARGUMENT;
    }
    if (Registry->Signature != BC250_MCDM_OBJECT_REGISTRY_SIGNATURE) {
        return BC250_MCDM_OBJECT_BAD_REGISTRY;
    }
    if (Registry->Teardown != 0u) {
        return BC250_MCDM_OBJECT_IN_TEARDOWN;
    }

    contextRec = Bc250McdmFindObject(Registry, ContextHandle);
    if (contextRec == NULL) {
        return BC250_MCDM_OBJECT_INVALID_HANDLE;
    }
    if (contextRec->Signature != BC250_MCDM_OBJECT_LIVE_SIGNATURE ||
        contextRec->Type != BC250_MCDM_OBJECT_CONTEXT) {
        return BC250_MCDM_OBJECT_STALE_HANDLE;
    }
    if (contextRec->State != (uint32_t)BC250_MCDM_OBJECT_STATE_LIVE) {
        return BC250_MCDM_OBJECT_STALE_HANDLE;
    }
    if (ExpectedAdapter != NULL && contextRec->OwnerAdapter != NULL &&
        contextRec->OwnerAdapter != ExpectedAdapter) {
        return BC250_MCDM_OBJECT_OWNER_MISMATCH;
    }
    if (contextRec->NodeOrdinal != ExpectedNodeOrdinal) {
        return BC250_MCDM_OBJECT_NODE_MISMATCH;
    }

    processRec = Bc250McdmFindObject(Registry, contextRec->OwnerProcess);
    if (processRec == NULL ||
        processRec->Signature != BC250_MCDM_OBJECT_LIVE_SIGNATURE ||
        processRec->State != (uint32_t)BC250_MCDM_OBJECT_STATE_LIVE) {
        return BC250_MCDM_OBJECT_OWNER_MISMATCH;
    }
    if (processRec->RootPageTableAddress == 0u ||
        processRec->RootPageTableEntries == 0u) {
        return BC250_MCDM_OBJECT_NO_ROOT_PAGE_TABLE;
    }

    if (!Bc250McdmAcquireSubmissionRundown(contextRec)) {
        return BC250_MCDM_OBJECT_IN_TEARDOWN;
    }
    InterlockedIncrement(&contextRec->InFlightSubmissions);

    /* O rundown fecha atomicamente a janela LIVE -> DESTROYING. Depois da
     * aquisição, repetir as invariantes que podem mudar durante teardown. */
    if (Registry->Teardown != 0u ||
        contextRec->Signature != BC250_MCDM_OBJECT_LIVE_SIGNATURE ||
        contextRec->State != (uint32_t)BC250_MCDM_OBJECT_STATE_LIVE ||
        processRec->Signature != BC250_MCDM_OBJECT_LIVE_SIGNATURE ||
        processRec->State != (uint32_t)BC250_MCDM_OBJECT_STATE_LIVE) {
        InterlockedDecrement(&contextRec->InFlightSubmissions);
        InterlockedDecrement(&contextRec->SubmissionRundown);
        return BC250_MCDM_OBJECT_IN_TEARDOWN;
    }

    View->ContextHandle = ContextHandle;
    View->ParentDevice = contextRec->Parent;
    View->OwnerProcess = contextRec->OwnerProcess;
    View->OwnerAdapter = contextRec->OwnerAdapter;
    View->Generation = contextRec->Generation;
    View->NodeOrdinal = contextRec->NodeOrdinal;
    View->EngineOrdinal = contextRec->EngineOrdinal;
    View->ApplicableSegmentId = contextRec->ApplicableSegmentId;
    View->RootPageTableAddress = processRec->RootPageTableAddress;
    View->RootPageTableSegmentId = processRec->RootPageTableSegmentId;
    View->RootPageTableEntries = processRec->RootPageTableEntries;
    View->PageTableEpoch = processRec->PageTableEpoch;
    View->TeardownEpoch = Registry->TeardownEpoch;

    return BC250_MCDM_OBJECT_OK;
}

BC250_MCDM_OBJECT_STATUS Bc250McdmContextReleaseSubmissionView(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_HANDLE ContextHandle)
{
    BC250_MCDM_OBJECT_RECORD *contextRec;

    if (Registry == NULL || ContextHandle == NULL) {
        return BC250_MCDM_OBJECT_BAD_ARGUMENT;
    }
    contextRec = Bc250McdmFindObject(Registry, ContextHandle);
    if (contextRec == NULL) {
        return BC250_MCDM_OBJECT_INVALID_HANDLE;
    }
    if (contextRec->InFlightSubmissions > 0) {
        InterlockedDecrement(&contextRec->InFlightSubmissions);
        InterlockedDecrement(&contextRec->SubmissionRundown);
    }
    return BC250_MCDM_OBJECT_OK;
}

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectCreateAdapterAllocation(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    const void *ExternalOwner,
    uint64_t AllocationToken,
    uint64_t AllocationBytes,
    uint64_t AllocationAlignment,
    BC250_MCDM_HANDLE *Handle)
{
    BC250_MCDM_OBJECT_STATUS status;
    BC250_MCDM_OBJECT_RECORD *record;
    uint32_t index;

    if (Handle != NULL) *Handle = NULL;

    if (Bc250McdmValidateRegistry(Registry) != BC250_MCDM_OBJECT_OK ||
        ExternalOwner == NULL || AllocationToken == 0u || AllocationBytes == 0u ||
        AllocationAlignment == 0u ||
        (AllocationAlignment & (AllocationAlignment - 1u)) != 0u) {
        return BC250_MCDM_OBJECT_BAD_ARGUMENT;
    }
    if (Registry->Teardown != 0u) {
        return BC250_MCDM_OBJECT_IN_TEARDOWN;
    }
    for (index = 0u; index < Registry->NextSlot; ++index) {
        const BC250_MCDM_OBJECT_RECORD *candidate = &Registry->Objects[index];
        if (candidate->Signature == BC250_MCDM_OBJECT_LIVE_SIGNATURE &&
            candidate->Type == BC250_MCDM_OBJECT_ALLOCATION &&
            candidate->AllocationToken == AllocationToken) {
            return BC250_MCDM_OBJECT_DUPLICATE_OWNER;
        }
    }
    status = Bc250McdmAllocateRecord(Registry,
        BC250_MCDM_OBJECT_ALLOCATION, ExternalOwner, NULL, NULL, Handle);
    if (status != BC250_MCDM_OBJECT_OK) return status;
    record = Bc250McdmFindObject(Registry, *Handle);
    if (record == NULL) return BC250_MCDM_OBJECT_BAD_REGISTRY;
    record->AllocationToken = AllocationToken;
    record->AllocationBytes = AllocationBytes;
    record->AllocationAlignment = AllocationAlignment;
    return BC250_MCDM_OBJECT_OK;
}

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectOpenAllocation(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_HANDLE Device,
    BC250_MCDM_HANDLE Allocation,
    const void *ExternalOwner,
    BC250_MCDM_HANDLE *Handle)
{
    BC250_MCDM_OBJECT_RECORD deviceRecord, allocationRecord;
    BC250_MCDM_OBJECT_RECORD *device, *allocation, *opened;
    BC250_MCDM_OBJECT_STATUS status;
    uint32_t index;

    if (Handle != NULL) *Handle = NULL;
    if (ExternalOwner == NULL || Handle == NULL) {
        return BC250_MCDM_OBJECT_BAD_ARGUMENT;
    }
    status = Bc250McdmObjectLookup(Registry, Device,
        BC250_MCDM_OBJECT_DEVICE, NULL, &deviceRecord);
    if (status != BC250_MCDM_OBJECT_OK) return status;
    status = Bc250McdmObjectLookup(Registry, Allocation,
        BC250_MCDM_OBJECT_ALLOCATION, NULL, &allocationRecord);
    if (status != BC250_MCDM_OBJECT_OK) return status;
    for (index = 0u; index < Registry->NextSlot; ++index) {
        const BC250_MCDM_OBJECT_RECORD *candidate = &Registry->Objects[index];
        if (candidate->Signature == BC250_MCDM_OBJECT_LIVE_SIGNATURE &&
            candidate->Type == BC250_MCDM_OBJECT_OPEN_ALLOCATION &&
            candidate->Parent == Device &&
            (candidate->ExternalOwner == ExternalOwner ||
             candidate->ReferencedObject == Allocation)) {
            return BC250_MCDM_OBJECT_DUPLICATE_OWNER;
        }
    }
    device = Bc250McdmFindObject(Registry, Device);
    allocation = Bc250McdmFindObject(Registry, Allocation);
    if (device == NULL || allocation == NULL ||
        device->ChildCount == (uint32_t)~0u ||
        allocation->ChildCount == (uint32_t)~0u) {
        return BC250_MCDM_OBJECT_BAD_REGISTRY;
    }
    status = Bc250McdmAllocateRecord(Registry,
        BC250_MCDM_OBJECT_OPEN_ALLOCATION, ExternalOwner,
        deviceRecord.OwnerProcess, Device, Handle);
    if (status != BC250_MCDM_OBJECT_OK) return status;
    opened = Bc250McdmFindObject(Registry, *Handle);
    if (opened == NULL) return BC250_MCDM_OBJECT_BAD_REGISTRY;
    opened->ReferencedObject = Allocation;
    opened->AllocationToken = allocationRecord.AllocationToken;
    opened->AllocationBytes = allocationRecord.AllocationBytes;
    opened->AllocationAlignment = allocationRecord.AllocationAlignment;
    ++device->ChildCount;
    ++allocation->ChildCount;
    return BC250_MCDM_OBJECT_OK;
}

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectFindAllocationByToken(
    const BC250_MCDM_OBJECT_REGISTRY *Registry,
    uint64_t AllocationToken,
    BC250_MCDM_HANDLE *Handle)
{
    uint32_t index;

    if (Handle != NULL) *Handle = NULL;
    if (Bc250McdmValidateRegistry(Registry) != BC250_MCDM_OBJECT_OK ||
        AllocationToken == 0u || Handle == NULL) {
        return BC250_MCDM_OBJECT_BAD_ARGUMENT;
    }
    for (index = 0u; index < Registry->NextSlot; ++index) {
        const BC250_MCDM_OBJECT_RECORD *record = &Registry->Objects[index];
        if (record->Signature == BC250_MCDM_OBJECT_LIVE_SIGNATURE &&
            record->Type == BC250_MCDM_OBJECT_ALLOCATION &&
            record->AllocationToken == AllocationToken) {
            *Handle = (BC250_MCDM_HANDLE)record;
            return BC250_MCDM_OBJECT_OK;
        }
    }
    return BC250_MCDM_OBJECT_INVALID_HANDLE;
}

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectSetRootPageTable(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_HANDLE Context,
    uint32_t SegmentId,
    uint64_t Address,
    uint32_t NumEntries,
    uint32_t MaxEntries)
{
    BC250_MCDM_OBJECT_RECORD contextRecord;
    BC250_MCDM_OBJECT_RECORD *process;
    BC250_MCDM_OBJECT_STATUS status;

    if (MaxEntries == 0u || (SegmentId != 0u && SegmentId != 1u) ||
        ((Address == 0u) != (NumEntries == 0u)) ||
        ((Address == 0u) != (SegmentId == 0u)) ||
        (Address != 0u && ((Address & 0xFFFull) != 0u ||
         (Address & ~0x0000FFFFFFFFF000ull) != 0u)) ||
        NumEntries > MaxEntries) {
        return BC250_MCDM_OBJECT_BAD_ARGUMENT;
    }
    status = Bc250McdmObjectLookup(Registry, Context,
        BC250_MCDM_OBJECT_CONTEXT, NULL, &contextRecord);
    if (status != BC250_MCDM_OBJECT_OK) return status;
    process = Bc250McdmFindObject(Registry, contextRecord.OwnerProcess);
    if (process == NULL ||
        process->Signature != BC250_MCDM_OBJECT_LIVE_SIGNATURE ||
        process->Type != BC250_MCDM_OBJECT_PROCESS) {
        return BC250_MCDM_OBJECT_BAD_REGISTRY;
    }
    process->RootPageTableAddress = Address;
    process->RootPageTableSegmentId = SegmentId;
    process->RootPageTableEntries = NumEntries;
    ++process->RootPageTableSetCount;
    if (process->RootPageTableSetCount == 0u) {
        process->RootPageTableSetCount = 1u;
    }
    ++process->PageTableEpoch;
    return BC250_MCDM_OBJECT_OK;
}

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectDestroy(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    BC250_MCDM_HANDLE Handle,
    BC250_MCDM_OBJECT_TYPE ExpectedType,
    BC250_MCDM_HANDLE ExpectedOwnerProcess)
{
    BC250_MCDM_OBJECT_RECORD snapshot;
    BC250_MCDM_OBJECT_RECORD *record;
    BC250_MCDM_OBJECT_RECORD *parent;
    BC250_MCDM_OBJECT_RECORD *referenced;
    BC250_MCDM_OBJECT_STATUS status;

    status = Bc250McdmObjectLookup(Registry, Handle, ExpectedType,
        ExpectedOwnerProcess, &snapshot);
    if (status != BC250_MCDM_OBJECT_OK) return status;
    if (snapshot.ChildCount != 0u) return BC250_MCDM_OBJECT_CHILDREN_LIVE;

    record = Bc250McdmFindObject(Registry, Handle);
    if (record == NULL) return BC250_MCDM_OBJECT_BAD_REGISTRY;

    if (record->Type == BC250_MCDM_OBJECT_CONTEXT) {
        long rundown = Bc250McdmCloseSubmissionRundown(record);
        record->State = (uint32_t)BC250_MCDM_OBJECT_STATE_DESTROYING;
        if ((rundown & BC250_MCDM_SUBMISSION_RUNDOWN_COUNT_MASK) != 0) {
            return BC250_MCDM_OBJECT_IN_FLIGHT_SUBMISSIONS;
        }
    }

    parent = NULL;
    referenced = NULL;
    if (record->Parent != NULL) {
        parent = Bc250McdmFindObject(Registry, record->Parent);
        if (parent == NULL || parent->Signature != BC250_MCDM_OBJECT_LIVE_SIGNATURE ||
            parent->ChildCount == 0u) {
            return BC250_MCDM_OBJECT_BAD_REGISTRY;
        }
    }
    if (record->ReferencedObject != NULL) {
        referenced = Bc250McdmFindObject(Registry, record->ReferencedObject);
        if (referenced == NULL ||
            referenced->Signature != BC250_MCDM_OBJECT_LIVE_SIGNATURE ||
            referenced->ChildCount == 0u) {
            return BC250_MCDM_OBJECT_BAD_REGISTRY;
        }
    }
    if (parent != NULL) --parent->ChildCount;
    if (referenced != NULL) --referenced->ChildCount;
    record->Signature = BC250_MCDM_OBJECT_DEAD_SIGNATURE;
    record->State = (uint32_t)BC250_MCDM_OBJECT_STATE_DEAD;
    --Registry->LiveCount;
    return BC250_MCDM_OBJECT_OK;
}

BC250_MCDM_OBJECT_STATUS Bc250McdmObjectRegistryTeardown(
    BC250_MCDM_OBJECT_REGISTRY *Registry,
    uint32_t *DestroyedCount)
{
    uint32_t index;
    uint32_t destroyed = 0u;

    if (DestroyedCount != NULL) *DestroyedCount = 0u;
    if (Bc250McdmValidateRegistry(Registry) != BC250_MCDM_OBJECT_OK ||
        DestroyedCount == NULL) {
        return BC250_MCDM_OBJECT_BAD_ARGUMENT;
    }
    Registry->Teardown = 1u;
    ++Registry->TeardownEpoch;
    for (index = 0u; index < Registry->NextSlot; ++index) {
        BC250_MCDM_OBJECT_RECORD *record = &Registry->Objects[index];
        if (record->Signature == BC250_MCDM_OBJECT_LIVE_SIGNATURE &&
            record->Type == BC250_MCDM_OBJECT_CONTEXT) {
            long rundown = Bc250McdmCloseSubmissionRundown(record);
            record->State = (uint32_t)BC250_MCDM_OBJECT_STATE_DESTROYING;
            if ((rundown & BC250_MCDM_SUBMISSION_RUNDOWN_COUNT_MASK) != 0) {
                return BC250_MCDM_OBJECT_IN_FLIGHT_SUBMISSIONS;
            }
        }
    }
    index = Registry->NextSlot;
    while (index != 0u) {
        BC250_MCDM_OBJECT_RECORD *record = &Registry->Objects[--index];
        if (record->Signature != BC250_MCDM_OBJECT_LIVE_SIGNATURE) continue;
        record->Signature = BC250_MCDM_OBJECT_DEAD_SIGNATURE;
        record->State = (uint32_t)BC250_MCDM_OBJECT_STATE_DEAD;
        record->ChildCount = 0u;
        ++destroyed;
    }
    Registry->LiveCount = 0u;
    *DestroyedCount = destroyed;
    return BC250_MCDM_OBJECT_OK;
}

const char *Bc250McdmObjectStatusName(BC250_MCDM_OBJECT_STATUS Status)
{
    switch (Status) {
    case BC250_MCDM_OBJECT_OK: return "ok";
    case BC250_MCDM_OBJECT_BAD_ARGUMENT: return "bad_argument";
    case BC250_MCDM_OBJECT_BAD_REGISTRY: return "bad_registry";
    case BC250_MCDM_OBJECT_NO_SPACE: return "no_space";
    case BC250_MCDM_OBJECT_DUPLICATE_OWNER: return "duplicate_owner";
    case BC250_MCDM_OBJECT_INVALID_HANDLE: return "invalid_handle";
    case BC250_MCDM_OBJECT_STALE_HANDLE: return "stale_handle";
    case BC250_MCDM_OBJECT_TYPE_MISMATCH: return "type_mismatch";
    case BC250_MCDM_OBJECT_OWNER_MISMATCH: return "owner_mismatch";
    case BC250_MCDM_OBJECT_PARENT_MISMATCH: return "parent_mismatch";
    case BC250_MCDM_OBJECT_CHILDREN_LIVE: return "children_live";
    case BC250_MCDM_OBJECT_NODE_MISMATCH: return "node_mismatch";
    case BC250_MCDM_OBJECT_NO_ROOT_PAGE_TABLE: return "no_root_page_table";
    case BC250_MCDM_OBJECT_IN_TEARDOWN: return "in_teardown";
    case BC250_MCDM_OBJECT_IN_FLIGHT_SUBMISSIONS: return "in_flight_submissions";
    default: return "unknown";
    }
}
