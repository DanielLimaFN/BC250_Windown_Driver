#include "../wdk/bc250_readonly_miniport.h"
#include "../wdk/bc250_probe_telemetry.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

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
static int g_failUnmap;
static int g_unmapCalls;

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

static NTSTATUS APIENTRY Bc250TestUnmapMemory(
    HANDLE DeviceHandle, PVOID VirtualAddress)
{
    ++g_unmapCalls;
    if (DeviceHandle != (HANDLE)(ULONG_PTR)0x250u ||
        VirtualAddress != (PVOID)(ULONG_PTR)0x5000u) {
        return STATUS_INVALID_PARAMETER;
    }
    return g_failUnmap ? STATUS_DEVICE_NOT_READY : STATUS_SUCCESS;
}

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)

static int Bc250RunReadonlyMiniportTest(void)
{
    DRIVER_INITIALIZATION_DATA data;
    PVOID context = NULL;
    DRIVER_OBJECT driverObject;
    UNICODE_STRING registryPath;
    BC250_PROBE_FIELD lastProbeField;
    ULONG lastProbeValue;

    Bc250ProbeResetOffline();
    memset(&data, 0xA5, sizeof(data));
    CHECK(Bc250BuildReadonlyInitializationData(&data) == STATUS_SUCCESS);
    CHECK(data.Version == BC250_DXGK_TARGET_INTERFACE_VERSION);
    CHECK(data.Version == DXGKDDI_INTERFACE_VERSION_WDDM2_7);
    CHECK(data.DxgkDdiAddDevice == Bc250DxgkDdiAddDeviceReadonly);
    CHECK(data.DxgkDdiStartDevice == Bc250DxgkDdiStartDeviceReadonly);
    CHECK(data.DxgkDdiStopDevice == Bc250DxgkDdiStopDeviceReadonly);
    CHECK(data.DxgkDdiRemoveDevice == Bc250DxgkDdiRemoveDeviceReadonly);
    CHECK(data.DxgkDdiBuildPagingBuffer == Bc250McdmBuildPagingBuffer);
    CHECK(data.DxgkDdiCalibrateGpuClock == Bc250McdmCalibrateGpuClock);
    CHECK(data.DxgkDdiCloseAllocation == Bc250McdmCloseAllocation);
    CHECK(data.DxgkDdiCollectDbgInfo == Bc250McdmCollectDbgInfo);
    CHECK(data.DxgkDdiCreateAllocation == Bc250McdmCreateAllocation);
    CHECK(data.DxgkDdiCreateContext == Bc250McdmCreateContext);
    CHECK(data.DxgkDdiCreateDevice == Bc250McdmCreateDevice);
    CHECK(data.DxgkDdiDescribeAllocation == Bc250McdmDescribeAllocation);
    CHECK(data.DxgkDdiDestroyAllocation == Bc250McdmDestroyAllocation);
    CHECK(data.DxgkDdiDestroyContext == Bc250McdmDestroyContext);
    CHECK(data.DxgkDdiDestroyDevice == Bc250McdmDestroyDevice);
    CHECK(data.DxgkDdiDispatchIoRequest == Bc250McdmDispatchIoRequest);
    CHECK(data.DxgkDdiDpcRoutine == Bc250McdmDpcRoutine);
    CHECK(data.DxgkDdiFormatHistoryBuffer == Bc250McdmFormatHistoryBuffer);
    CHECK(data.DxgkDdiGetNodeMetadata == Bc250McdmGetNodeMetadata);
    CHECK(data.DxgkDdiGetStandardAllocationDriverData ==
        Bc250McdmGetStandardAllocationDriverData);
    CHECK(data.DxgkDdiInterruptRoutine == Bc250McdmInterruptRoutine);
    CHECK(data.DxgkDdiOpenAllocation == Bc250McdmOpenAllocation);
    CHECK(data.DxgkDdiPreemptCommand == Bc250McdmPreemptCommand);
    CHECK(data.DxgkDdiQueryAdapterInfo == Bc250McdmQueryAdapterInfo);
    CHECK(data.DxgkDdiQueryDependentEngineGroup ==
        Bc250McdmQueryDependentEngineGroup);
    CHECK(data.DxgkDdiQueryDeviceDescriptor ==
        Bc250McdmQueryDeviceDescriptor);
    CHECK(data.DxgkDdiQueryEngineStatus == Bc250McdmQueryEngineStatus);
    CHECK(data.DxgkDdiResetDevice == Bc250McdmResetDevice);
    CHECK(data.DxgkDdiResetEngine == Bc250McdmResetEngine);
    CHECK(data.DxgkDdiResetFromTimeout == Bc250McdmResetFromTimeout);
    CHECK(data.DxgkDdiRestartFromTimeout == Bc250McdmRestartFromTimeout);
    CHECK(data.DxgkDdiSetPowerState == Bc250McdmSetPowerState);
    CHECK(data.DxgkDdiSetStablePowerState == Bc250McdmSetStablePowerState);
    CHECK(data.DxgkDdiSetVirtualMachineData ==
        Bc250McdmSetVirtualMachineData);
    CHECK(data.DxgkDdiQueryChildStatus == Bc250McdmQueryChildStatus);
    CHECK(data.DxgkDdiQueryChildRelations == Bc250McdmQueryChildRelations);
    CHECK(data.DxgkDdiUnload == Bc250McdmUnload);
    CHECK(data.DxgkDdiSubmitCommand == NULL);
    CHECK(data.DxgkDdiQueryConnectionChange ==
        Bc250McdmQueryConnectionChange);
    CHECK(data.DxgkDdiCommitVidPn == NULL);
    CHECK(Bc250McdmBuildPagingBuffer(NULL, NULL) ==
        STATUS_INVALID_PARAMETER);
    CHECK(Bc250McdmQueryAdapterInfo(NULL, NULL) ==
        STATUS_INVALID_PARAMETER);
    CHECK(Bc250McdmDispatchIoRequest(NULL, 0, NULL) ==
        STATUS_NOT_SUPPORTED);
    CHECK(Bc250McdmInterruptRoutine(NULL, 0) == FALSE);
    CHECK(Bc250McdmQueryChildRelations(NULL, NULL, 0) == STATUS_SUCCESS);
    CHECK(Bc250McdmQueryChildRelations(NULL, NULL, 1) ==
        STATUS_INVALID_PARAMETER);

    CHECK(Bc250DxgkDdiAddDeviceReadonly(
        (PDEVICE_OBJECT)(ULONG_PTR)0x250u, &context) == STATUS_SUCCESS);
    CHECK(context != NULL && g_poolAllocated == 1);
    CHECK(((BC250_DXGK_READONLY_CONTEXT *)context)->Signature ==
        BC250_DXGK_READONLY_SIGNATURE);
    CHECK(((BC250_DXGK_READONLY_CONTEXT *)context)->PhysicalDeviceObject ==
        (PDEVICE_OBJECT)(ULONG_PTR)0x250u);
    ((BC250_DXGK_READONLY_CONTEXT *)context)->MmioTransport.State =
        BC250_MMIO_TOPOLOGY_MAPPED;
    ((BC250_DXGK_READONLY_CONTEXT *)context)->MmioTransport.DeviceHandle =
        (HANDLE)(ULONG_PTR)0x250u;
    ((BC250_DXGK_READONLY_CONTEXT *)context)->MmioTransport.UnmapMemory =
        Bc250TestUnmapMemory;
    ((BC250_DXGK_READONLY_CONTEXT *)context)->MmioTransport.VirtualBase =
        (PVOID)(ULONG_PTR)0x5000u;
    g_failUnmap = 1;
    g_unmapCalls = 0;
    CHECK(Bc250DxgkDdiRemoveDeviceReadonly(context) ==
        STATUS_DEVICE_CONFIGURATION_ERROR);
    CHECK(g_poolAllocated == 1 && g_unmapCalls == 1);
    CHECK(((BC250_DXGK_READONLY_CONTEXT *)context)->Signature ==
        BC250_DXGK_READONLY_SIGNATURE);
    g_failUnmap = 0;
    CHECK(Bc250DxgkDdiRemoveDeviceReadonly(context) == STATUS_SUCCESS);
    CHECK(g_poolAllocated == 0 && g_unmapCalls == 2);

    memset(&driverObject, 0, sizeof(driverObject));
    memset(&registryPath, 0, sizeof(registryPath));
    CHECK(DriverEntry(&driverObject, &registryPath) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250ProbeGetOffline(&lastProbeField, &lastProbeValue) > 0);
    CHECK(lastProbeField == BC250_PROBE_DXGK_INITIALIZE_STATUS);
    CHECK(lastProbeValue == (ULONG)STATUS_NOT_SUPPORTED);
    CHECK(g_Bc250ReadonlyInitializationData.DxgkDdiAddDevice ==
        Bc250DxgkDdiAddDeviceReadonly);
    CHECK(g_Bc250ReadonlyInitializationData.DxgkDdiSubmitCommand == NULL);
    return 1;
}

void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunReadonlyMiniportTest() ? 0ul : 1ul);
}
