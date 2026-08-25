#include "../wdk/bc250_mcdm_callbacks.h"
#include "../core/bc250_adapter_info_contract.h"
#include "../core/bc250_segment_contract.h"

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) return 1; } while (0)
#endif

static DRIVER_INITIALIZATION_DATA g_initData;
static DXGKARG_QUERYADAPTERINFO g_queryInfo;
static DXGKARG_GETNODEMETADATA g_nodeMetadata;
static DXGKARG_QUERYENGINESTATUS g_engineStatus;
static DXGKARG_RESETENGINE g_resetEngine;
static DXGKARG_CREATECONTEXT g_createContext;
static DXGKARG_CREATEPROCESS g_createProcess;
static DXGKARG_GETROOTPAGETABLESIZE g_rootPageTableSize;
static DXGKARG_SUBMITCOMMANDVIRTUAL g_submitVirtual;
static DXGK_QUERYSEGMENTOUT g_segmentOut;
static UINT g_numNodesOut;

static int Bc250RunMcdmCallbacksTest(void)
{
    /* 1. Test Callback Population */
    memset(&g_initData, 0, sizeof(g_initData));
    CHECK(Bc250McdmPopulateRequiredCallbacks(&g_initData) == STATUS_SUCCESS);
    CHECK(g_initData.DxgkDdiBuildPagingBuffer != NULL);
    CHECK(g_initData.DxgkDdiCreateAllocation != NULL);
    CHECK(g_initData.DxgkDdiCreateContext != NULL);
    CHECK(g_initData.DxgkDdiGetNodeMetadata != NULL);
    CHECK(g_initData.DxgkDdiQueryAdapterInfo != NULL);
    CHECK(g_initData.DxgkDdiQueryEngineStatus != NULL);
    CHECK(g_initData.DxgkDdiResetEngine != NULL);
    CHECK(g_initData.DxgkDdiResetFromTimeout != NULL);
    CHECK(g_initData.DxgkDdiRestartFromTimeout != NULL);
    CHECK(g_initData.DxgkDdiInterruptRoutine != NULL);
    CHECK(g_initData.DxgkDdiDpcRoutine != NULL);
    CHECK(g_initData.DxgkDdiCreateProcess != NULL);
    CHECK(g_initData.DxgkDdiDestroyProcess != NULL);
    CHECK(g_initData.DxgkDdiGetRootPageTableSize != NULL);
    CHECK(g_initData.DxgkDdiSetRootPageTable != NULL);
    CHECK(g_initData.DxgkDdiSubmitCommandVirtual != NULL);
    CHECK(g_initData.DxgkDdiQueryConnectionChange == NULL);

    static DXGK_DRIVERCAPS driverCaps;
    static DXGK_PHYSICALADAPTERCAPS physCaps;

    /* 2. Test QueryAdapterInfo - DRIVERCAPS & PHYSICALADAPTERCAPS */
    memset(&driverCaps, 0, sizeof(driverCaps));
    memset(&g_queryInfo, 0, sizeof(g_queryInfo));
    g_queryInfo.Type = DXGKQAITYPE_DRIVERCAPS;
    g_queryInfo.pOutputData = &driverCaps;
    g_queryInfo.OutputDataSize = sizeof(driverCaps);

    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) == STATUS_NOT_SUPPORTED);

    memset(&physCaps, 0, sizeof(physCaps));
    memset(&g_queryInfo, 0, sizeof(g_queryInfo));
    g_queryInfo.Type = DXGKQAITYPE_PHYSICALADAPTERCAPS;
    g_queryInfo.pOutputData = &physCaps;
    g_queryInfo.OutputDataSize = sizeof(physCaps);

    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) == STATUS_NOT_SUPPORTED);

    /* 3. Test QueryAdapterInfo - QUERYSEGMENT */
    memset(&g_segmentOut, 0, sizeof(g_segmentOut));
    memset(&g_queryInfo, 0, sizeof(g_queryInfo));
    g_queryInfo.Type = DXGKQAITYPE_QUERYSEGMENT;
    g_queryInfo.pOutputData = &g_segmentOut;
    g_queryInfo.OutputDataSize = sizeof(g_segmentOut);

    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) == STATUS_NOT_SUPPORTED);

    /* 4. Test GetNodeMetadata - Node 0 (SDMA) */
    memset(&g_nodeMetadata, 0, sizeof(g_nodeMetadata));
    CHECK(Bc250McdmGetNodeMetadata((HANDLE)0x250, 0u, &g_nodeMetadata) == STATUS_NOT_SUPPORTED);

    /* 5. Test GetNodeMetadata - Node 1 (Compute) */
    memset(&g_nodeMetadata, 0, sizeof(g_nodeMetadata));
    CHECK(Bc250McdmGetNodeMetadata((HANDLE)0x250, 1u, &g_nodeMetadata) == STATUS_NOT_SUPPORTED);

    /* 6. Test QueryEngineStatus */
    memset(&g_engineStatus, 0, sizeof(g_engineStatus));
    g_engineStatus.NodeOrdinal = 0u;
    CHECK(Bc250McdmQueryEngineStatus((HANDLE)0x250, &g_engineStatus) == STATUS_NOT_SUPPORTED);
    CHECK(g_engineStatus.EngineStatus.Responsive == 0);

    /* 7. Test ResetEngine */
    memset(&g_resetEngine, 0, sizeof(g_resetEngine));
    g_resetEngine.NodeOrdinal = 0u;
    CHECK(Bc250McdmResetEngine((HANDLE)0x250, &g_resetEngine) == STATUS_NOT_SUPPORTED);

    memset(&g_resetEngine, 0, sizeof(g_resetEngine));
    g_resetEngine.NodeOrdinal = 1u;
    CHECK(Bc250McdmResetEngine((HANDLE)0x250, &g_resetEngine) == STATUS_NOT_SUPPORTED);

    /* 8. Test CreateContext / DestroyContext */
    memset(&g_createContext, 0, sizeof(g_createContext));
    g_createContext.hContext = (HANDLE)0x254;
    g_createContext.NodeOrdinal = 0u;
    CHECK(Bc250McdmCreateContext((HANDLE)0x250, &g_createContext) == STATUS_NOT_SUPPORTED);

    /* 9. Test Timeout Recovery Callbacks & Preemption */
    CHECK(Bc250McdmResetFromTimeout((HANDLE)0x250) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250McdmRestartFromTimeout((HANDLE)0x250) == STATUS_NOT_SUPPORTED);

    {
        static DXGKARG_PREEMPTCOMMAND preemptCmd;
        memset(&preemptCmd, 0, sizeof(preemptCmd));
        preemptCmd.NodeOrdinal = 0u;
        preemptCmd.PreemptionFenceId = 777u;
        CHECK(Bc250McdmPreemptCommand((HANDLE)0x250, &preemptCmd) == STATUS_NOT_SUPPORTED);

        preemptCmd.NodeOrdinal = 1u;
        CHECK(Bc250McdmPreemptCommand((HANDLE)0x250, &preemptCmd) == STATUS_NOT_SUPPORTED);

        preemptCmd.NodeOrdinal = 2u;
        CHECK(Bc250McdmPreemptCommand((HANDLE)0x250, &preemptCmd) == STATUS_INVALID_PARAMETER);
    }

    /* 10. Test Calibrate GPU Clock */
    {
        static DXGKARG_CALIBRATEGPUCLOCK clockCalib;
        memset(&clockCalib, 0, sizeof(clockCalib));
        CHECK(Bc250McdmCalibrateGpuClock((HANDLE)0x250, 0u, 0u, &clockCalib) == STATUS_NOT_SUPPORTED);

        CHECK(Bc250McdmCalibrateGpuClock((HANDLE)0x250, 1u, 0u, &clockCalib) == STATUS_NOT_SUPPORTED);
        CHECK(Bc250McdmCalibrateGpuClock((HANDLE)0x250, 2u, 0u, &clockCalib) == STATUS_INVALID_PARAMETER);
        CHECK(Bc250McdmCalibrateGpuClock((HANDLE)0x250, 0u, 0u, NULL) == STATUS_INVALID_PARAMETER);
    }

    /* 11. Test Power Management Callbacks */
    {
        static DXGKARG_SETSTABLEPOWERSTATE stablePower;
        CHECK(Bc250McdmSetPowerState(NULL, 0u, PowerDeviceD3, PowerActionNone) == STATUS_NOT_SUPPORTED);
        CHECK(Bc250McdmSetPowerState(NULL, 0u, PowerDeviceD0, PowerActionNone) == STATUS_NOT_SUPPORTED);
        CHECK(Bc250McdmSetPowerState(NULL, 0u, PowerDeviceUnspecified, PowerActionNone) == STATUS_INVALID_PARAMETER);

        memset(&stablePower, 0, sizeof(stablePower));
        stablePower.Enabled = TRUE;
        Bc250McdmSetStablePowerState((HANDLE)0x250, &stablePower);

        stablePower.Enabled = FALSE;
        Bc250McdmSetStablePowerState((HANDLE)0x250, &stablePower);
    }

    /* 12. Test Error Handling */
    CHECK(Bc250McdmQueryAdapterInfo(NULL, NULL) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250McdmGetNodeMetadata((HANDLE)0x250, 2u, &g_nodeMetadata) == STATUS_INVALID_PARAMETER);
    g_createContext.NodeOrdinal = 2u;
    CHECK(Bc250McdmCreateContext((HANDLE)0x250, &g_createContext) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250McdmDestroyContext(NULL) == STATUS_INVALID_PARAMETER);

    /* 13. GPUVA ABI is present but remains fail-closed. */
    memset(&g_createProcess, 0, sizeof(g_createProcess));
    g_createProcess.hDxgkProcess = (HANDLE)0x251;
    g_createProcess.hKmdProcess = (HANDLE)0xBAD;
    CHECK(Bc250McdmCreateProcess((HANDLE)0x250, &g_createProcess) == STATUS_NOT_SUPPORTED);
    CHECK(g_createProcess.hKmdProcess == NULL);
    CHECK(Bc250McdmCreateProcess((HANDLE)0x250, NULL) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250McdmDestroyProcess((HANDLE)0x250, (HANDLE)0x252) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250McdmDestroyProcess((HANDLE)0x250, NULL) == STATUS_INVALID_PARAMETER);

    memset(&g_rootPageTableSize, 0xFF, sizeof(g_rootPageTableSize));
    CHECK(Bc250McdmGetRootPageTableSize((HANDLE)0x250, &g_rootPageTableSize) == 0u);
    CHECK(g_rootPageTableSize.NumberOfPte == 0u);

    memset(&g_submitVirtual, 0, sizeof(g_submitVirtual));
    g_submitVirtual.hContext = (HANDLE)0x253;
    g_submitVirtual.DmaBufferSize = 64u;
    g_submitVirtual.NodeOrdinal = 0u;
    CHECK(Bc250McdmSubmitCommandVirtual((HANDLE)0x250, &g_submitVirtual) == STATUS_NOT_SUPPORTED);
    g_submitVirtual.NodeOrdinal = BC250_ADAPTER_NODE_COUNT;
    CHECK(Bc250McdmSubmitCommandVirtual((HANDLE)0x250, &g_submitVirtual) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250McdmSubmitCommandVirtual((HANDLE)0x250, NULL) == STATUS_INVALID_PARAMETER);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunMcdmCallbacksTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunMcdmCallbacksTest() ? 0 : 1;
}
#endif
