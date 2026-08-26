#include "../wdk/bc250_active_miniport.h"
#include "../wdk/bc250_mcdm_callbacks.h"
#include "../wdk/bc250_probe_telemetry.h"
#include "../core/bc250_allocation_abi_contract.h"
#include "../core/bc250_mcdm_debug_contract.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);
__declspec(dllimport) HANDLE __stdcall CreateThread(
    PVOID ThreadAttributes,
    SIZE_T StackSize,
    ULONG (__stdcall *StartAddress)(PVOID),
    PVOID Parameter,
    ULONG CreationFlags,
    PULONG ThreadId);
__declspec(dllimport) ULONG __stdcall WaitForSingleObject(
    HANDLE Object,
    ULONG Milliseconds);
__declspec(dllimport) int __stdcall CloseHandle(HANDLE Object);
__declspec(dllimport) int __stdcall SwitchToThread(void);

#define BC250_TEST_WAIT_OBJECT_0 0u
#define BC250_TEST_INFINITE 0xFFFFFFFFu
#define BC250_TEST_RACE_ROUNDS 16u
#define BC250_TEST_RACE_ATTEMPTS BC250_MCDM_MAX_OBJECTS

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#pragma function(memcpy)
void * __cdecl memcpy(void *Destination, const void *Source, size_t Length)
{
    unsigned char *destination = (unsigned char *)Destination;
    const unsigned char *source = (const unsigned char *)Source;
    size_t index;
    for (index = 0; index < Length; ++index) destination[index] = source[index];
    return Destination;
}

static __declspec(align(16)) unsigned char g_pool[32768];
static int g_poolAllocated;

static PVOID Bc250TestAllocatePool2(
    POOL_FLAGS Flags,
    SIZE_T NumberOfBytes,
    ULONG Tag)
{
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Tag);
    if (g_poolAllocated || NumberOfBytes > sizeof(g_pool)) return NULL;
    g_poolAllocated = 1;
    memset(g_pool, 0, sizeof(g_pool));
    return g_pool;
}

static VOID Bc250TestFreePoolWithTag(PVOID P, ULONG Tag)
{
    UNREFERENCED_PARAMETER(Tag);
    if (P == g_pool) g_poolAllocated = 0;
}

PVOID (*__imp_ExAllocatePool2)(POOL_FLAGS, SIZE_T, ULONG) =
    Bc250TestAllocatePool2;
VOID (*__imp_ExFreePoolWithTag)(PVOID, ULONG) = Bc250TestFreePoolWithTag;

VOID ExInitializePushLock(PEX_PUSH_LOCK PushLock)
{
    *PushLock = 0u;
}

static VOID Bc250TestEnterCriticalRegion(VOID) { }
static VOID Bc250TestLeaveCriticalRegion(VOID) { }
static VOID FASTCALL Bc250TestAcquirePushLockExclusiveEx(
    PEX_PUSH_LOCK PushLock, ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);
    while (InterlockedCompareExchange64(
            (volatile LONG64 *)PushLock, 1, 0) != 0) {
        (void)SwitchToThread();
    }
}
static VOID FASTCALL Bc250TestReleasePushLockExclusiveEx(
    PEX_PUSH_LOCK PushLock, ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);
    (void)InterlockedExchange64((volatile LONG64 *)PushLock, 0);
}

VOID (*__imp_KeEnterCriticalRegion)(VOID) = Bc250TestEnterCriticalRegion;
VOID (*__imp_KeLeaveCriticalRegion)(VOID) = Bc250TestLeaveCriticalRegion;
VOID (FASTCALL *__imp_ExAcquirePushLockExclusiveEx)(PEX_PUSH_LOCK, ULONG) =
    Bc250TestAcquirePushLockExclusiveEx;
VOID (FASTCALL *__imp_ExReleasePushLockExclusiveEx)(PEX_PUSH_LOCK, ULONG) =
    Bc250TestReleasePushLockExclusiveEx;

static unsigned long g_failureLine;
#define CHECK(Expression) do { \
    if (!(Expression)) { g_failureLine = __LINE__; return 0; } \
} while (0)

typedef struct BC250_STOP_RACE_FIXTURE {
    DRIVER_INITIALIZATION_DATA *InitializationData;
    BC250_DXGK_ACTIVE_CONTEXT *Context;
    volatile LONG Start;
    ULONG Round;
    ULONG SuccessCount;
    ULONG NotReadyCount;
    ULONG NoSpaceCount;
    ULONG UnexpectedCount;
} BC250_STOP_RACE_FIXTURE;

static ULONG __stdcall Bc250CreateProcessRaceThread(PVOID Parameter)
{
    BC250_STOP_RACE_FIXTURE *fixture =
        (BC250_STOP_RACE_FIXTURE *)Parameter;
    ULONG index;

    while (InterlockedCompareExchange(&fixture->Start, 1, 1) != 1) {
        (void)SwitchToThread();
    }
    for (index = 0u; index < BC250_TEST_RACE_ATTEMPTS; ++index) {
        DXGKARG_CREATEPROCESS createProcess;
        NTSTATUS status;
        memset(&createProcess, 0, sizeof(createProcess));
        createProcess.hDxgkProcess = (HANDLE)(ULONG_PTR)(
            0x200000u + (fixture->Round * BC250_TEST_RACE_ATTEMPTS) + index);
        status = fixture->InitializationData->DxgkDdiCreateProcess(
            fixture->Context, &createProcess);
        if (status == STATUS_SUCCESS) {
            ++fixture->SuccessCount;
        } else if (status == STATUS_DEVICE_NOT_READY) {
            ++fixture->NotReadyCount;
        } else if (status == STATUS_INSUFFICIENT_RESOURCES) {
            ++fixture->NoSpaceCount;
        } else {
            ++fixture->UnexpectedCount;
        }
        if ((index & 7u) == 0u) (void)SwitchToThread();
    }
    return 0u;
}

static int Bc250RunStopRaceTest(
    DRIVER_INITIALIZATION_DATA *InitializationData,
    DEVICE_OBJECT *FakeDeviceObject)
{
    ULONG round;

    for (round = 0u; round < BC250_TEST_RACE_ROUNDS; ++round) {
        PVOID deviceContext = NULL;
        BC250_DXGK_ACTIVE_CONTEXT *activeContext;
        BC250_STOP_RACE_FIXTURE fixture;
        HANDLE thread;
        ULONG yieldCount;

        if (Bc250DxgkDdiAddDeviceActive(
                FakeDeviceObject, &deviceContext) != STATUS_SUCCESS ||
            deviceContext == NULL) {
            return 0;
        }
        activeContext = (BC250_DXGK_ACTIVE_CONTEXT *)deviceContext;
        InterlockedExchange(&activeContext->IsActive, 1);
        memset(&fixture, 0, sizeof(fixture));
        fixture.InitializationData = InitializationData;
        fixture.Context = activeContext;
        fixture.Round = round;
        thread = CreateThread(NULL, 0u, Bc250CreateProcessRaceThread,
            &fixture, 0u, NULL);
        if (thread == NULL) return 0;
        InterlockedExchange(&fixture.Start, 1);
        for (yieldCount = 0u; yieldCount < (round & 3u); ++yieldCount) {
            (void)SwitchToThread();
        }
        if (Bc250DxgkDdiStopDeviceActive(activeContext) != STATUS_SUCCESS) {
            (void)WaitForSingleObject(thread, BC250_TEST_INFINITE);
            (void)CloseHandle(thread);
            return 0;
        }
        if (WaitForSingleObject(thread, BC250_TEST_INFINITE) !=
                BC250_TEST_WAIT_OBJECT_0 ||
            !CloseHandle(thread)) {
            return 0;
        }
        if (fixture.UnexpectedCount != 0u ||
            fixture.SuccessCount + fixture.NotReadyCount +
                fixture.NoSpaceCount != BC250_TEST_RACE_ATTEMPTS ||
            activeContext->IsActive != 0 ||
            activeContext->Objects.LiveCount != 0u) {
            return 0;
        }
        if (Bc250DxgkDdiRemoveDeviceActive(activeContext) != STATUS_SUCCESS ||
            g_poolAllocated != 0) {
            return 0;
        }
    }
    return 1;
}

static int Bc250RunActiveMiniportTest(void)
{
    DRIVER_INITIALIZATION_DATA initData;
    PVOID deviceContext = NULL;
    DEVICE_OBJECT fakeDeviceObject;

    Bc250ProbeResetOffline();
    memset(&initData, 0, sizeof(initData));
    memset(&fakeDeviceObject, 0, sizeof(fakeDeviceObject));
    g_poolAllocated = 0;

    /* 1. Test Active Initialization Data Structure */
    CHECK(Bc250BuildActiveInitializationData(&initData) == STATUS_SUCCESS);
    CHECK(initData.Version == BC250_DXGK_TARGET_INTERFACE_VERSION);
    CHECK(initData.Version == DXGKDDI_INTERFACE_VERSION_WDDM2_7);
    CHECK(initData.DxgkDdiAddDevice == Bc250DxgkDdiAddDeviceActive);
    CHECK(initData.DxgkDdiStartDevice == Bc250DxgkDdiStartDeviceActive);
    CHECK(initData.DxgkDdiStopDevice == Bc250DxgkDdiStopDeviceActive);
    CHECK(initData.DxgkDdiRemoveDevice == Bc250DxgkDdiRemoveDeviceActive);
    CHECK(initData.DxgkDdiPreemptCommand == Bc250McdmPreemptCommand);
    CHECK(initData.DxgkDdiCreateContext == Bc250McdmCreateContext);
    CHECK(initData.DxgkDdiQueryAdapterInfo == Bc250McdmQueryAdapterInfo);

    /* 2. Test AddDevice */
    CHECK(Bc250DxgkDdiAddDeviceActive(&fakeDeviceObject, &deviceContext) == STATUS_SUCCESS);
    CHECK(deviceContext != NULL);
    {
        PVOID rejectedContext = (PVOID)(ULONG_PTR)0xBAD250u;
        CHECK(Bc250DxgkDdiAddDeviceActive(
            &fakeDeviceObject, &rejectedContext) ==
            STATUS_INSUFFICIENT_RESOURCES);
        CHECK(rejectedContext == NULL);
    }

    {
        BC250_DXGK_ACTIVE_CONTEXT *activeCtx = (BC250_DXGK_ACTIVE_CONTEXT *)deviceContext;
        BC250_MCDM_DEBUG_SNAPSHOT_V1 debugSnapshot;
        DXGKARG_COLLECTDBGINFO collectDebug;
        DXGKARG_COLLECTDBGINFO_EXT debugExtension;
        DXGKARG_CREATEPROCESS createProcess;
        DXGKARG_CREATEDEVICE createDevice;
        DXGKARG_CREATECONTEXT createContext;
        HANDLE processHandle;
        HANDLE deviceHandle;
        HANDLE contextHandle;
        BC250_ALLOCATION_PRIVATE_V1 allocationPrivate[2];
        DXGK_ALLOCATIONINFO allocationInfo[2];
        DXGKARG_CREATEALLOCATION createAllocation;
        DXGKARG_DESTROYALLOCATION destroyAllocation;
        HANDLE allocationHandles[2];
        DXGK_OPENALLOCATIONINFO openInfo[2];
        DXGKARG_OPENALLOCATION openAllocation;
        DXGKARG_CLOSEALLOCATION closeAllocation;
        HANDLE openHandles[2];
        UINT allocationIndex;
        UINT capacityStartSlot;
        BC250_MCDM_OBJECT_RECORD processRecord;
        CHECK(activeCtx->Signature == BC250_ACTIVE_SIGNATURE);
        CHECK(activeCtx->PhysicalDeviceObject == &fakeDeviceObject);
        CHECK(activeCtx->Core.RuntimeStaged == 0u);
        CHECK(activeCtx->Core.ReadonlyGatePassed == 0u);
        CHECK(activeCtx->IsActive == 0u);
        CHECK(activeCtx->Objects.Signature ==
            BC250_MCDM_OBJECT_REGISTRY_SIGNATURE);
        CHECK(activeCtx->Objects.LiveCount == 0u);

        memset(&debugSnapshot, 0xCC, sizeof(debugSnapshot));
        memset(&collectDebug, 0, sizeof(collectDebug));
        memset(&debugExtension, 0xCC, sizeof(debugExtension));
        collectDebug.Reason = 0xBC250u;
        collectDebug.pBuffer = &debugSnapshot;
        collectDebug.BufferSize = sizeof(debugSnapshot) - 1u;
        collectDebug.pExtension = &debugExtension;
        CHECK(initData.DxgkDdiCollectDbgInfo(activeCtx, &collectDebug) ==
            STATUS_BUFFER_TOO_SMALL);
        Bc250ProbeRecordFirstFailure(STATUS_DEVICE_NOT_READY);
        Bc250ProbeRecordFirstFailure(STATUS_INVALID_PARAMETER);
        collectDebug.BufferSize = sizeof(debugSnapshot);
        CHECK(initData.DxgkDdiCollectDbgInfo(activeCtx, &collectDebug) ==
            STATUS_SUCCESS);
        CHECK(debugSnapshot.Magic == BC250_MCDM_DEBUG_MAGIC &&
            debugSnapshot.Version == BC250_MCDM_DEBUG_VERSION &&
            debugSnapshot.StructSize == sizeof(debugSnapshot));
        CHECK(debugSnapshot.Reason == 0xBC250u &&
            debugSnapshot.StateFlags == BC250_MCDM_DEBUG_ADAPTER_REGISTERED);
        CHECK(debugSnapshot.FirstFailureStatus ==
            (uint32_t)STATUS_DEVICE_NOT_READY);
        CHECK(debugSnapshot.ObjectLiveCount == 0u &&
            debugSnapshot.SubmissionsCount == 0u &&
            debugSnapshot.ResetsCount == 0u);
        CHECK(debugExtension.BucketingKey == BC250_MCDM_DEBUG_MAGIC &&
            debugExtension.CurrentDmaBufferOffset == 0u);

        /* Fixture offline: exercise only the synchronized software lifecycle. */
        memset(&createProcess, 0, sizeof(createProcess));
        createProcess.hDxgkProcess = (HANDLE)(ULONG_PTR)0xA250u;
        CHECK(initData.DxgkDdiCreateProcess(activeCtx, &createProcess) ==
            STATUS_DEVICE_NOT_READY);
        InterlockedExchange(&activeCtx->IsActive, 1);
        CHECK(initData.DxgkDdiCreateProcess(activeCtx, &createProcess) ==
            STATUS_SUCCESS);
        processHandle = createProcess.hKmdProcess;
        CHECK(processHandle != NULL);
        CHECK(activeCtx->Objects.LiveCount == 1u);
        createProcess.hKmdProcess = NULL;
        CHECK(initData.DxgkDdiCreateProcess(activeCtx, &createProcess) ==
            STATUS_OBJECT_NAME_COLLISION);
        CHECK(createProcess.hKmdProcess == NULL);

        memset(&createDevice, 0, sizeof(createDevice));
        createDevice.hDevice = (HANDLE)(ULONG_PTR)0xD250u;
        createDevice.hKmdProcess = processHandle;
        CHECK(initData.DxgkDdiCreateDevice(activeCtx, &createDevice) ==
            STATUS_SUCCESS);
        deviceHandle = createDevice.hDevice;
        CHECK(deviceHandle != NULL);
        CHECK(initData.DxgkDdiDestroyProcess(activeCtx, processHandle) ==
            STATUS_DEVICE_BUSY);

        memset(&createContext, 0, sizeof(createContext));
        createContext.hContext = (HANDLE)(ULONG_PTR)0xC250u;
        createContext.NodeOrdinal = 0u;
        CHECK(initData.DxgkDdiCreateContext(deviceHandle, &createContext) ==
            STATUS_DEVICE_NOT_READY);
        activeCtx->ContextAbi.DmaBufferSize = 4096u;
        InterlockedExchange(&activeCtx->ContextAbiReady, 1);
        createContext.hContext = (HANDLE)(ULONG_PTR)0xC250u;
        CHECK(initData.DxgkDdiCreateContext(deviceHandle, &createContext) ==
            STATUS_SUCCESS);
        contextHandle = createContext.hContext;
        CHECK(contextHandle != NULL);
        CHECK(createContext.ContextInfo.DmaBufferSize == 4096u);
        activeCtx->AllocationSegmentBytes = 0x0F132000ull;
        CHECK(Bc250ActiveSetRootPageTableObject(activeCtx, contextHandle,
            1u, 0x0000000000345000ull, 512u, 512u) ==
            STATUS_DEVICE_NOT_READY);
        activeCtx->RootPageTableMaxEntries = 512u;
        InterlockedExchange(&activeCtx->RootPageTableAbiReady, 1);
        CHECK(Bc250ActiveSetRootPageTableObject(activeCtx, contextHandle,
            1u, 0x0000000000345000ull, 512u, 512u) == STATUS_SUCCESS);
        CHECK(Bc250McdmObjectLookup(&activeCtx->Objects, processHandle,
            BC250_MCDM_OBJECT_PROCESS, processHandle, &processRecord) ==
            BC250_MCDM_OBJECT_OK);
        CHECK(processRecord.RootPageTableAddress ==
            0x0000000000345000ull &&
            processRecord.RootPageTableSegmentId == 1u &&
            processRecord.RootPageTableEntries == 512u);
        CHECK(initData.DxgkDdiDestroyDevice(deviceHandle) ==
            STATUS_DEVICE_BUSY);
        CHECK(initData.DxgkDdiDestroyContext(
            (HANDLE)(ULONG_PTR)0xBAD251u) == STATUS_INVALID_HANDLE);
        CHECK(initData.DxgkDdiDestroyContext(contextHandle) == STATUS_SUCCESS);
        CHECK(initData.DxgkDdiDestroyContext(contextHandle) ==
            STATUS_INVALID_HANDLE);

        memset(allocationPrivate, 0, sizeof(allocationPrivate));
        memset(allocationInfo, 0, sizeof(allocationInfo));
        memset(&createAllocation, 0, sizeof(createAllocation));
        for (allocationIndex = 0u; allocationIndex < 2u; ++allocationIndex) {
            allocationPrivate[allocationIndex].Magic =
                BC250_ALLOCATION_PRIVATE_MAGIC;
            allocationPrivate[allocationIndex].Version =
                BC250_ALLOCATION_PRIVATE_VERSION;
            allocationPrivate[allocationIndex].StructSize =
                sizeof(BC250_ALLOCATION_PRIVATE_V1);
            allocationPrivate[allocationIndex].AllocationToken =
                0xA110u + allocationIndex;
            allocationPrivate[allocationIndex].ByteSize = 65536u;
            allocationPrivate[allocationIndex].Alignment = 4096u;
            allocationInfo[allocationIndex].pPrivateDriverData =
                &allocationPrivate[allocationIndex];
            allocationInfo[allocationIndex].PrivateDriverDataSize =
                sizeof(BC250_ALLOCATION_PRIVATE_V1);
        }
        createAllocation.NumAllocations = 2u;
        createAllocation.pAllocationInfo = allocationInfo;
        CHECK(initData.DxgkDdiCreateAllocation(activeCtx,
            &createAllocation) == STATUS_DEVICE_NOT_READY);
        CHECK(allocationInfo[0].hAllocation == NULL &&
            allocationInfo[1].hAllocation == NULL);
        activeCtx->AllocationSegmentBytes = 0x0F132000ull;
        activeCtx->AllocationSegmentSet = 1u;
        InterlockedExchange(&activeCtx->AllocationAbiReady, 1);
        allocationPrivate[1].ByteSize = 0x0F133000ull;
        CHECK(initData.DxgkDdiCreateAllocation(activeCtx,
            &createAllocation) == STATUS_INVALID_PARAMETER);
        CHECK(allocationInfo[0].hAllocation == NULL &&
            allocationInfo[1].hAllocation == NULL);
        allocationPrivate[1].ByteSize = 65536u;
        CHECK(initData.DxgkDdiCreateAllocation(activeCtx,
            &createAllocation) == STATUS_SUCCESS);
        allocationHandles[0] = allocationInfo[0].hAllocation;
        allocationHandles[1] = allocationInfo[1].hAllocation;
        CHECK(allocationHandles[0] != NULL && allocationHandles[1] != NULL);
        CHECK(allocationInfo[0].Size == 65536u &&
            allocationInfo[0].SupportedReadSegmentSet == 1u &&
            allocationInfo[0].SupportedWriteSegmentSet == 1u &&
            allocationInfo[0].PreferredSegment.SegmentId0 == 1u &&
            allocationInfo[0].FlagsWddm2.CpuVisible == 1u);

        memset(openInfo, 0, sizeof(openInfo));
        memset(&openAllocation, 0, sizeof(openAllocation));
        for (allocationIndex = 0u; allocationIndex < 2u; ++allocationIndex) {
            openInfo[allocationIndex].hAllocation = 0xD310u + allocationIndex;
            openInfo[allocationIndex].pPrivateDriverData =
                &allocationPrivate[allocationIndex];
            openInfo[allocationIndex].PrivateDriverDataSize =
                sizeof(BC250_ALLOCATION_PRIVATE_V1);
        }
        openAllocation.NumAllocations = 2u;
        openAllocation.pOpenAllocation = openInfo;
        allocationPrivate[1].AllocationToken = 0xBAD110u;
        CHECK(initData.DxgkDdiOpenAllocation(deviceHandle,
            &openAllocation) == STATUS_INVALID_HANDLE);
        CHECK(openInfo[0].hDeviceSpecificAllocation == NULL &&
            openInfo[1].hDeviceSpecificAllocation == NULL);
        allocationPrivate[1].AllocationToken = 0xA111u;
        CHECK(initData.DxgkDdiOpenAllocation(deviceHandle,
            &openAllocation) == STATUS_SUCCESS);
        openHandles[0] = openInfo[0].hDeviceSpecificAllocation;
        openHandles[1] = openInfo[1].hDeviceSpecificAllocation;
        CHECK(openHandles[0] != NULL && openHandles[1] != NULL);

        memset(&destroyAllocation, 0, sizeof(destroyAllocation));
        destroyAllocation.NumAllocations = 2u;
        destroyAllocation.pAllocationList = allocationHandles;
        allocationHandles[1] = allocationHandles[0];
        CHECK(initData.DxgkDdiDestroyAllocation(activeCtx,
            &destroyAllocation) == STATUS_INVALID_PARAMETER);
        allocationHandles[1] = allocationInfo[1].hAllocation;
        CHECK(initData.DxgkDdiDestroyAllocation(activeCtx,
            &destroyAllocation) == STATUS_DEVICE_BUSY);
        CHECK(initData.DxgkDdiDestroyDevice(deviceHandle) ==
            STATUS_DEVICE_BUSY);

        memset(&closeAllocation, 0, sizeof(closeAllocation));
        closeAllocation.NumAllocations = 2u;
        closeAllocation.pOpenHandleList = openHandles;
        openHandles[1] = openHandles[0];
        CHECK(initData.DxgkDdiCloseAllocation(deviceHandle,
            &closeAllocation) == STATUS_INVALID_PARAMETER);
        openHandles[1] = openInfo[1].hDeviceSpecificAllocation;
        CHECK(initData.DxgkDdiCloseAllocation(deviceHandle,
            &closeAllocation) == STATUS_SUCCESS);
        CHECK(initData.DxgkDdiCloseAllocation(deviceHandle,
            &closeAllocation) == STATUS_INVALID_HANDLE);
        CHECK(initData.DxgkDdiDestroyAllocation(activeCtx,
            &destroyAllocation) == STATUS_SUCCESS);
        CHECK(initData.DxgkDdiDestroyAllocation(activeCtx,
            &destroyAllocation) == STATUS_INVALID_HANDLE);
        CHECK(initData.DxgkDdiDestroyDevice((HANDLE)(ULONG_PTR)0xBAD250u) ==
            STATUS_INVALID_HANDLE);
        CHECK(initData.DxgkDdiDestroyDevice(deviceHandle) == STATUS_SUCCESS);
        CHECK(initData.DxgkDdiDestroyDevice(deviceHandle) ==
            STATUS_INVALID_HANDLE);
        CHECK(initData.DxgkDdiDestroyProcess(activeCtx, processHandle) ==
            STATUS_SUCCESS);
        CHECK(initData.DxgkDdiDestroyProcess(activeCtx, processHandle) ==
            STATUS_INVALID_HANDLE);
        capacityStartSlot = activeCtx->Objects.NextSlot;
        for (allocationIndex = 0u;
             allocationIndex < BC250_MCDM_MAX_OBJECTS;
             ++allocationIndex) {
            DXGKARG_CREATEPROCESS capacityProcess;
            memset(&capacityProcess, 0, sizeof(capacityProcess));
            capacityProcess.hDxgkProcess =
                (HANDLE)(ULONG_PTR)(0x10000u + allocationIndex);
            if (allocationIndex <
                    BC250_MCDM_MAX_OBJECTS - capacityStartSlot) {
                CHECK(initData.DxgkDdiCreateProcess(activeCtx,
                    &capacityProcess) == STATUS_SUCCESS);
            } else {
                CHECK(initData.DxgkDdiCreateProcess(activeCtx,
                    &capacityProcess) == STATUS_INSUFFICIENT_RESOURCES);
                break;
            }
        }
        CHECK(activeCtx->Objects.NextSlot == BC250_MCDM_MAX_OBJECTS);
        CHECK(Bc250DxgkDdiStopDeviceActive(activeCtx) == STATUS_SUCCESS);
        CHECK(activeCtx->Objects.LiveCount == 0u);
        memset(&createProcess, 0, sizeof(createProcess));
        createProcess.hDxgkProcess = (HANDLE)(ULONG_PTR)0xA251u;
        CHECK(initData.DxgkDdiCreateProcess(activeCtx, &createProcess) ==
            STATUS_DEVICE_NOT_READY);
    }

    /* 3. Test RemoveDevice (which cleanly tears down and frees context) */
    CHECK(Bc250DxgkDdiRemoveDeviceActive(deviceContext) == STATUS_SUCCESS);
    CHECK(g_poolAllocated == 0);

    /* Real host threads contend on the same lock order used by StopDevice. */
    CHECK(Bc250RunStopRaceTest(&initData, &fakeDeviceObject));

    /* Unload is idempotent and disconnects every active-only interface. */
    Bc250McdmUnload();
    Bc250McdmUnload();
    {
        DXGKARG_CREATEPROCESS disconnectedProcess;
        memset(&disconnectedProcess, 0, sizeof(disconnectedProcess));
        disconnectedProcess.hDxgkProcess = (HANDLE)(ULONG_PTR)0xA252u;
        CHECK(Bc250McdmCreateProcess((HANDLE)(ULONG_PTR)0xA253u,
            &disconnectedProcess) == STATUS_NOT_SUPPORTED);
        CHECK(disconnectedProcess.hKmdProcess == NULL);
    }

    return 1;
}

void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunActiveMiniportTest() ? 0ul : g_failureLine);
}
