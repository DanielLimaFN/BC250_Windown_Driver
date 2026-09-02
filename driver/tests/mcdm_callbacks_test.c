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

#define CHECK(Expression) do { if (!(Expression)) { ExitProcess((unsigned long)__LINE__); return 0; } } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) return 1; } while (0)
#endif

void Bc250ProbeRecord(int Field, unsigned long Value)
{
    (void)Field;
    (void)Value;
}

/*
 * Fase V5: o rastro de DDI e' instrumentacao pura, sem efeito sobre a
 * semantica dos callbacks. Fora do kernel vira no-op, mas o contador fica
 * disponivel caso algum contrato futuro queira afirmar que uma DDI foi de
 * fato alcancada.
 */
unsigned long g_Bc250TestDdiMarks;
unsigned long g_Bc250TestLastDdi;

void Bc250ProbeMarkDdi(unsigned long DdiId)
{
    g_Bc250TestLastDdi = DdiId;
    ++g_Bc250TestDdiMarks;
}

/*
 * Schema 134: o bitmap de DXGKQAITYPE tambem e' instrumentacao pura. Aqui ele
 * fica observavel para que um contrato possa afirmar que o tipo perguntado
 * chegou ao publicador.
 */
unsigned long g_Bc250TestQaiTypeSeenLo;
unsigned long g_Bc250TestQaiTypeSeenHi;

void Bc250ProbeMarkQaiType(unsigned long Type)
{
    if (Type >= 64u) {
        return;
    }
    if (Type < 32u) {
        g_Bc250TestQaiTypeSeenLo |= (1u << Type);
    } else {
        g_Bc250TestQaiTypeSeenHi |= (1u << (Type - 32u));
    }
}

static DRIVER_INITIALIZATION_DATA g_initData;
static DXGKARG_QUERYADAPTERINFO g_queryInfo;
static DXGKARG_GETNODEMETADATA g_nodeMetadata;
static DXGKARG_QUERYENGINESTATUS g_engineStatus;
static DXGKARG_RESETENGINE g_resetEngine;
static DXGKARG_CREATECONTEXT g_createContext;
static DXGKARG_CREATEPROCESS g_createProcess;
static DXGKARG_GETROOTPAGETABLESIZE g_rootPageTableSize;
static DXGKARG_SUBMITCOMMANDVIRTUAL g_submitVirtual;
static DXGK_QUERYSEGMENTIN4 g_segmentIn;
static DXGK_QUERYSEGMENTOUT4 g_segmentOut;
static DXGK_SEGMENTDESCRIPTOR4 g_segmentDescriptor;
static DXGK_QUERYGPUMMUCAPSIN g_gpuMmuIn;
static DXGK_GPUMMUCAPS g_gpuMmuCaps;
static DXGK_QUERYPAGETABLELEVELDESCIN g_pageLevelIn;
static DXGK_PAGE_TABLE_LEVEL_DESC g_pageLevelDesc;
static UINT g_numNodesOut;
static UINT g_fullMcdmReady;
static UINT64 g_rootAddress;
static UINT32 g_rootEntries;
static DXGKARG_BUILDPAGINGBUFFER g_paging;
static DXGK_PTE g_pagingEntries[2];
static UINT64 g_pageTable[512];
static UINT32 g_updateHandlerCalls;
static UINT32 g_updateHandlerLastLevel;

static NTSTATUS Bc250TestUpdatePageTableHandler(
    HANDLE AdapterContext,
    CONST BC250_MCDM_RUNTIME_CAPS *Caps,
    DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *Update)
{
    UNREFERENCED_PARAMETER(Caps);
    if (AdapterContext != (HANDLE)0x250 || Update == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    ++g_updateHandlerCalls;
    g_updateHandlerLastLevel = Update->PageTableLevel;
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250TestSetRootPageTable(
    HANDLE AdapterContext,
    HANDLE ContextHandle,
    UINT32 SegmentId,
    UINT64 Address,
    UINT32 NumEntries,
    UINT32 MaxEntries)
{
    if (AdapterContext != (HANDLE)0x250 ||
        ContextHandle != (HANDLE)0x253 || SegmentId != 1u ||
        MaxEntries != 512u) {
        return STATUS_INVALID_PARAMETER;
    }
    g_rootAddress = Address;
    g_rootEntries = NumEntries;
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250TestRuntimeCaps(
    HANDLE AdapterContext, BC250_MCDM_RUNTIME_CAPS *Caps)
{
    if (AdapterContext != (HANDLE)0x250 || Caps == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    memset(Caps, 0, sizeof(*Caps));
    Caps->LocalSegmentBaseOffset = 0x00A8C000ull;
    Caps->LocalSegmentBytes = 0x0F132000ull;
    Caps->CpuPhysicalStart = 0xC0A8C000ull;
    Caps->NodeCount = 2u;
    Caps->Ready = 1u;
    Caps->FullMcdmReady = g_fullMcdmReady;
    Caps->GpuVaReady = g_fullMcdmReady;
    Caps->PagingReady = g_fullMcdmReady;
    Caps->TdrReady = g_fullMcdmReady;
    Caps->PreemptionReady = g_fullMcdmReady;
    Caps->SubmitReady = g_fullMcdmReady;
    Caps->HistoryBufferReady = g_fullMcdmReady;
    if (g_fullMcdmReady != 0u) {
        Caps->GpuVaBitCount = 48u;
        Caps->PageTableLevelCount = 4u;
        Caps->PageTableIndexBitCount = 9u;
        Caps->PageTableSizeBytes = 4096u;
        Caps->PageTableAlignmentBytes = 4096u;
    }
    return STATUS_SUCCESS;
}

static UINT32 g_submissionExecutionCount;
static UINT32 g_interruptExecutionCount;
static UINT32 g_dpcExecutionCount;

static NTSTATUS Bc250TestSubmitExecutor(
    HANDLE AdapterContext,
    CONST DXGKARG_SUBMITCOMMANDVIRTUAL *pSubmitCommand)
{
    if (AdapterContext != (HANDLE)0x250 || pSubmitCommand == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    g_submissionExecutionCount++;
    return STATUS_SUCCESS;
}

static BOOLEAN Bc250TestInterruptHandler(
    CONST PVOID MiniportDeviceContext,
    ULONG MessageNumber)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(MessageNumber);
    g_interruptExecutionCount++;
    return TRUE;
}

static VOID Bc250TestDpcHandler(
    CONST PVOID MiniportDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    g_dpcExecutionCount++;
}

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
    CHECK(g_initData.DxgkDdiQueryConnectionChange ==
        Bc250McdmQueryConnectionChange);
    {
        DXGKARG_QUERYCONNECTIONCHANGE change;
        memset(&change, 0xA5, sizeof(change));
        CHECK(Bc250McdmQueryConnectionChange(NULL, &change) ==
            STATUS_INVALID_PARAMETER);
        CHECK(Bc250McdmQueryConnectionChange((HANDLE)0x250, NULL) ==
            STATUS_INVALID_PARAMETER);
        CHECK(Bc250McdmQueryConnectionChange((HANDLE)0x250, &change) ==
            STATUS_ALREADY_COMPLETE);
    }

    static DXGK_DRIVERCAPS driverCaps;
    static DXGK_PHYSICALADAPTERCAPS physCaps;

    /* 2. Test QueryAdapterInfo - DRIVERCAPS & PHYSICALADAPTERCAPS */
    memset(&driverCaps, 0, sizeof(driverCaps));
    memset(&g_queryInfo, 0, sizeof(g_queryInfo));
    g_queryInfo.Type = DXGKQAITYPE_DRIVERCAPS;
    g_queryInfo.pOutputData = &driverCaps;
    g_queryInfo.OutputDataSize = sizeof(driverCaps);

    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) == STATUS_DEVICE_NOT_READY);
    g_fullMcdmReady = 0u;
    Bc250McdmInstallRuntimeCapsProvider(Bc250TestRuntimeCaps);
    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) == STATUS_DEVICE_NOT_READY);
    g_fullMcdmReady = 1u;
    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) == STATUS_SUCCESS);
    CHECK(driverCaps.WDDMVersion == DXGKDDI_WDDMv2_7);
    /*
     * Alvo do driver: WDDM display+render (jogos). ComputeOnly deve ser 0.
     *
     * Era 1, herdado do alvo MCDM original, e contradizia Class=Display no
     * INF, a fonte de video reportada pelo StartDevice e a tabela de VidPn
     * completa. O contrato agora afirma a coerencia em vez de congelar a
     * contradicao.
     */
    CHECK(driverCaps.MiscCaps.ComputeOnly == 0u);
    CHECK(driverCaps.SupportNonVGA == FALSE);
    CHECK(driverCaps.SupportPerEngineTDR == TRUE);
    CHECK(driverCaps.SchedulingCaps.MultiEngineAware == 1u);
    CHECK(driverCaps.SchedulingCaps.PreemptionAware == 1u);
    CHECK(driverCaps.SchedulingCaps.LowIrqlPreemptCommand == 1u);
    CHECK(driverCaps.MemoryManagementCaps.VirtualAddressingSupported == 1u);
    CHECK(driverCaps.MemoryManagementCaps.GpuMmuSupported == 1u);
    CHECK(driverCaps.MemoryManagementCaps.ParavirtualizationSupported == 1u);

    /* A/B usa o mesmo binario: ambas as respostas devem mudar juntas. */
    CHECK(Bc250McdmSetWddmContractVersion(DXGKDDI_WDDMv2) ==
        STATUS_SUCCESS);
    memset(&driverCaps, 0, sizeof(driverCaps));
    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) ==
        STATUS_SUCCESS);
    CHECK(driverCaps.WDDMVersion == DXGKDDI_WDDMv2);
    {
        DXGK_WDDMDEVICECAPS deviceCaps;
        memset(&deviceCaps, 0, sizeof(deviceCaps));
        g_queryInfo.Type = DXGKQAITYPE_WDDMDEVICECAPS;
        g_queryInfo.pOutputData = &deviceCaps;
        g_queryInfo.OutputDataSize = sizeof(deviceCaps);
        CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) ==
            STATUS_SUCCESS);
        CHECK(deviceCaps.WDDMVersion == DXGKDDI_WDDMv2);
    }
    CHECK(Bc250McdmSetWddmContractVersion(DXGKDDI_WDDMv2_7) ==
        STATUS_SUCCESS);

    memset(&physCaps, 0, sizeof(physCaps));
    memset(&g_queryInfo, 0, sizeof(g_queryInfo));
    g_queryInfo.Type = DXGKQAITYPE_PHYSICALADAPTERCAPS;
    g_queryInfo.pOutputData = &physCaps;
    g_queryInfo.OutputDataSize = sizeof(physCaps);

    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) == STATUS_SUCCESS);
    CHECK(physCaps.NumExecutionNodes == 2u);

    /* GPUVA descriptors exist only behind the full runtime gate. */
    memset(&g_gpuMmuIn, 0, sizeof(g_gpuMmuIn));
    memset(&g_gpuMmuCaps, 0, sizeof(g_gpuMmuCaps));
    memset(&g_queryInfo, 0, sizeof(g_queryInfo));
    g_queryInfo.Type = DXGKQAITYPE_GPUMMUCAPS;
    g_queryInfo.pInputData = &g_gpuMmuIn;
    g_queryInfo.InputDataSize = sizeof(g_gpuMmuIn);
    g_queryInfo.pOutputData = &g_gpuMmuCaps;
    g_queryInfo.OutputDataSize = sizeof(g_gpuMmuCaps);
    g_fullMcdmReady = 0u;
    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) ==
        STATUS_DEVICE_NOT_READY);
    g_fullMcdmReady = 1u;
    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) ==
        STATUS_SUCCESS);
    CHECK(g_gpuMmuCaps.PageTableUpdateMode ==
        DXGK_PAGETABLEUPDATE_CPU_VIRTUAL);
    CHECK(g_gpuMmuCaps.VirtualAddressBitCount == 48u);
    CHECK(g_gpuMmuCaps.PageTableLevelCount == 4u);

    memset(&g_pageLevelIn, 0, sizeof(g_pageLevelIn));
    memset(&g_pageLevelDesc, 0, sizeof(g_pageLevelDesc));
    memset(&g_queryInfo, 0, sizeof(g_queryInfo));
    g_queryInfo.Type = DXGKQAITYPE_PAGETABLELEVELDESC;
    g_queryInfo.pInputData = &g_pageLevelIn;
    g_queryInfo.InputDataSize = sizeof(g_pageLevelIn);
    g_queryInfo.pOutputData = &g_pageLevelDesc;
    g_queryInfo.OutputDataSize = sizeof(g_pageLevelDesc);
    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) ==
        STATUS_SUCCESS);
    CHECK(g_pageLevelDesc.PageTableIndexBitCount == 9u);
    CHECK(g_pageLevelDesc.PageTableSegmentId == 1u);
    CHECK(g_pageLevelDesc.PagingProcessPageTableSegmentId == 1u);
    CHECK(g_pageLevelDesc.PageTableSizeInBytes == 4096u);
    CHECK(g_pageLevelDesc.PageTableAlignmentInBytes == 4096u);
    g_pageLevelIn.LevelIndex = 4u;
    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) ==
        STATUS_INVALID_PARAMETER);
    g_pageLevelIn.LevelIndex = 0u;
    {
        DXGKARG_SETROOTPAGETABLE setRoot;
        memset(&setRoot, 0, sizeof(setRoot));
        setRoot.hContext = (HANDLE)0x253;
        setRoot.Address.SegmentId = 1u;
        setRoot.Address.SegmentOffset = 0x0000000000345000ull;
        setRoot.NumEntries = 512u;
        Bc250McdmInstallGpuVaLifecycle(Bc250TestSetRootPageTable);
        Bc250McdmSetRootPageTable((HANDLE)0x250, &setRoot);
        CHECK(g_rootAddress == 0x0000000000345000ull);
        CHECK(g_rootEntries == 512u);
    }
    {
        memset(&g_paging, 0, sizeof(g_paging));
        memset(g_pagingEntries, 0, sizeof(g_pagingEntries));
        memset(g_pageTable, 0xCC, sizeof(g_pageTable));
        g_pagingEntries[0].Valid = 1u;
        g_pagingEntries[0].CacheCoherent = 1u;
        g_pagingEntries[0].ReadOnly = 1u;
        g_pagingEntries[0].NoExecute = 1u;
        g_pagingEntries[0].Segment = 1u;
        g_pagingEntries[0].PageTablePageSize =
            DXGK_PTE_PAGE_TABLE_PAGE_4KB;
        g_pagingEntries[0].PageAddress = 0x2000u;
        g_paging.Operation = DXGK_OPERATION_UPDATE_PAGE_TABLE;
        g_paging.UpdatePageTable.PageTableLevel = 0u;
        g_paging.UpdatePageTable.PageTableAddress.CpuVirtual = g_pageTable;
        g_paging.UpdatePageTable.pPageTableEntries = g_pagingEntries;
        g_paging.UpdatePageTable.StartIndex = 3u;
        g_paging.UpdatePageTable.NumPageTableEntries = 2u;
        g_paging.UpdatePageTable.UpdateMode =
            DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
        CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) ==
            STATUS_SUCCESS);
        CHECK(g_pageTable[2] == 0xCCCCCCCCCCCCCCCCull);
        CHECK(g_pageTable[3] == 0x0000000000A8E025ull);
        CHECK(g_pageTable[4] == 0u);
        g_pagingEntries[0].Segment = 0u;
        g_pageTable[3] = 0x1122334455667788ull;
        CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) ==
            STATUS_NOT_SUPPORTED);
        CHECK(g_pageTable[3] == 0x1122334455667788ull);

        /* Diretórios não-leaf também passam pelo handler transacional ativo. */
        g_pagingEntries[0].Segment = 1u;
        g_paging.UpdatePageTable.PageTableLevel = 2u;
        g_updateHandlerCalls = 0u;
        Bc250McdmInstallUpdatePageTableHandler(
            Bc250TestUpdatePageTableHandler);
        CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) ==
            STATUS_SUCCESS);
        CHECK(g_updateHandlerCalls == 1u);
        CHECK(g_updateHandlerLastLevel == 2u);
        Bc250McdmInstallUpdatePageTableHandler(NULL);

        memset(&g_paging, 0, sizeof(g_paging));
        g_paging.Operation = DXGK_OPERATION_FLUSH_TLB;
        CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) == STATUS_NOT_SUPPORTED);
    }

    /* 3. MCDM requires QUERYSEGMENT4 and prohibits legacy segment queries. */
    memset(&g_segmentOut, 0, sizeof(g_segmentOut));
    memset(&g_segmentDescriptor, 0, sizeof(g_segmentDescriptor));
    memset(&g_segmentIn, 0, sizeof(g_segmentIn));
    memset(&g_queryInfo, 0, sizeof(g_queryInfo));
    g_queryInfo.Type = DXGKQAITYPE_QUERYSEGMENT4;
    g_queryInfo.pInputData = &g_segmentIn;
    g_queryInfo.InputDataSize = sizeof(g_segmentIn);
    g_queryInfo.pOutputData = &g_segmentOut;
    g_queryInfo.OutputDataSize = sizeof(g_segmentOut);

    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) == STATUS_SUCCESS);
    CHECK(g_segmentOut.NbSegment == 1u);
    g_segmentOut.pSegmentDescriptor = (BYTE *)&g_segmentDescriptor;
    g_segmentOut.SegmentDescriptorStride = sizeof(g_segmentDescriptor) - 1u;
    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) == STATUS_INVALID_PARAMETER);
    g_segmentOut.SegmentDescriptorStride = sizeof(g_segmentDescriptor);
    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) == STATUS_SUCCESS);
    CHECK(g_segmentDescriptor.BaseAddress.QuadPart == 0x00A8C000ll);
    CHECK(g_segmentDescriptor.CpuTranslatedAddress.QuadPart == 0xC0A8C000ll);
    CHECK(g_segmentDescriptor.Size == 0x0F132000ull);
    CHECK(g_segmentDescriptor.CommitLimit == 0u);
    CHECK(g_segmentDescriptor.Flags.CpuVisible == 1u);
    g_queryInfo.Type = DXGKQAITYPE_QUERYSEGMENT;
    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) == STATUS_NOT_SUPPORTED);
    g_queryInfo.Type = DXGKQAITYPE_QUERYSEGMENT3;
    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) == STATUS_NOT_SUPPORTED);

    {
        UINT powerComponents = 99u;
        DXGKARG_HISTORYBUFFERPRECISION precision[2];
        memset(&g_queryInfo, 0, sizeof(g_queryInfo));
        g_queryInfo.Type = DXGKQAITYPE_NUMPOWERCOMPONENTS;
        g_queryInfo.pOutputData = &powerComponents;
        g_queryInfo.OutputDataSize = sizeof(powerComponents);
        CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) == STATUS_SUCCESS);
        CHECK(powerComponents == 0u);
        memset(precision, 0, sizeof(precision));
        g_queryInfo.Type = DXGKQAITYPE_HISTORYBUFFERPRECISION;
        g_queryInfo.pOutputData = precision;
        g_queryInfo.OutputDataSize = sizeof(precision);
        CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) == STATUS_SUCCESS);
        CHECK(precision[0].PrecisionBits == 32u);
        CHECK(precision[1].PrecisionBits == 32u);
    }

    /* 4. Test GetNodeMetadata - Node 0 (SDMA) */
    memset(&g_nodeMetadata, 0, sizeof(g_nodeMetadata));
    CHECK(Bc250McdmGetNodeMetadata((HANDLE)0x250, 0u, &g_nodeMetadata) == STATUS_SUCCESS);
    CHECK(g_nodeMetadata.EngineType == DXGK_ENGINE_TYPE_COPY);
    CHECK(g_nodeMetadata.GpuMmuSupported == TRUE);

    /* 5. Test GetNodeMetadata - Node 1 (Compute) */
    memset(&g_nodeMetadata, 0, sizeof(g_nodeMetadata));
    CHECK(Bc250McdmGetNodeMetadata((HANDLE)0x250, 1u, &g_nodeMetadata) == STATUS_SUCCESS);
    CHECK(g_nodeMetadata.EngineType == DXGK_ENGINE_TYPE_OTHER);

    /* 6. Test QueryEngineStatus */
    memset(&g_engineStatus, 0, sizeof(g_engineStatus));
    g_engineStatus.NodeOrdinal = 0u;
    CHECK(Bc250McdmQueryEngineStatus((HANDLE)0x250, &g_engineStatus) == STATUS_SUCCESS);
    CHECK(g_engineStatus.EngineStatus.Responsive == 1);

    /* 7. Test ResetEngine */
    memset(&g_resetEngine, 0, sizeof(g_resetEngine));
    g_resetEngine.NodeOrdinal = 0u;
    CHECK(Bc250McdmResetEngine((HANDLE)0x250, &g_resetEngine) == STATUS_NOT_SUPPORTED);
    CHECK(g_resetEngine.LastAbortedFenceId == 0u);

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
        CHECK(Bc250McdmPreemptCommand((HANDLE)0x250, &preemptCmd) == STATUS_SUCCESS);

        preemptCmd.NodeOrdinal = 1u;
        CHECK(Bc250McdmPreemptCommand((HANDLE)0x250, &preemptCmd) == STATUS_SUCCESS);

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
    g_fullMcdmReady = 0u;
    CHECK(Bc250McdmSubmitCommandVirtual((HANDLE)0x250, &g_submitVirtual) == STATUS_NOT_SUPPORTED);
    g_fullMcdmReady = 1u;
    CHECK(Bc250McdmSubmitCommandVirtual((HANDLE)0x250, &g_submitVirtual) == STATUS_INVALID_PARAMETER);
    g_submitVirtual.DmaBufferVirtualAddress = 0xF400001000ull;
    CHECK(Bc250McdmSubmitCommandVirtual((HANDLE)0x250, &g_submitVirtual) == STATUS_NOT_SUPPORTED);
    g_submitVirtual.DmaBufferSize = 66u;
    CHECK(Bc250McdmSubmitCommandVirtual((HANDLE)0x250, &g_submitVirtual) == STATUS_INVALID_PARAMETER);
    g_submitVirtual.DmaBufferSize = 64u;
    g_submitVirtual.NodeOrdinal = 1u;
    CHECK(Bc250McdmSubmitCommandVirtual((HANDLE)0x250, &g_submitVirtual) == STATUS_NOT_SUPPORTED);
    g_submitVirtual.NodeOrdinal = BC250_ADAPTER_NODE_COUNT;
    CHECK(Bc250McdmSubmitCommandVirtual((HANDLE)0x250, &g_submitVirtual) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250McdmSubmitCommandVirtual((HANDLE)0x250, NULL) == STATUS_INVALID_PARAMETER);

    /* 14. Test Custom Submission Executor Installation */
    Bc250McdmInstallSubmissionExecutor(Bc250TestSubmitExecutor);
    g_submissionExecutionCount = 0u;
    g_submitVirtual.NodeOrdinal = 1u;
    CHECK(Bc250McdmSubmitCommandVirtual((HANDLE)0x250, &g_submitVirtual) == STATUS_SUCCESS);
    CHECK(g_submissionExecutionCount == 1u);
    Bc250McdmInstallSubmissionExecutor(NULL);

    /* 15. Test Interrupt & DPC Lifecycle Installation */
    CHECK(Bc250McdmInterruptRoutine((PVOID)0x250, 0u) == FALSE);
    Bc250McdmDpcRoutine((PVOID)0x250);
    CHECK(g_interruptExecutionCount == 0u);
    CHECK(g_dpcExecutionCount == 0u);

    Bc250McdmInstallInterruptLifecycle(Bc250TestInterruptHandler, Bc250TestDpcHandler);
    CHECK(Bc250McdmInterruptRoutine((PVOID)0x250, 0u) == TRUE);
    CHECK(g_interruptExecutionCount == 1u);
    Bc250McdmDpcRoutine((PVOID)0x250);
    CHECK(g_dpcExecutionCount == 1u);

    Bc250McdmInstallInterruptLifecycle(NULL, NULL);
    CHECK(Bc250McdmInterruptRoutine((PVOID)0x250, 0u) == FALSE);

    /* 16. Test Scheduler & Preemption Callbacks when FullMcdmReady */
    {
        static DXGKARG_PREEMPTCOMMAND preemptActive;
        memset(&preemptActive, 0, sizeof(preemptActive));
        preemptActive.NodeOrdinal = 0u;
        CHECK(Bc250McdmPreemptCommand((HANDLE)0x250, &preemptActive) == STATUS_SUCCESS);
        preemptActive.NodeOrdinal = 1u;
        CHECK(Bc250McdmPreemptCommand((HANDLE)0x250, &preemptActive) == STATUS_SUCCESS);

        memset(&g_engineStatus, 0, sizeof(g_engineStatus));
        g_engineStatus.NodeOrdinal = 0u;
        CHECK(Bc250McdmQueryEngineStatus((HANDLE)0x250, &g_engineStatus) == STATUS_SUCCESS);
        CHECK(g_engineStatus.EngineStatus.Responsive == 1);

        memset(&g_resetEngine, 0, sizeof(g_resetEngine));
        g_resetEngine.NodeOrdinal = 1u;
        CHECK(Bc250McdmResetEngine((HANDLE)0x250, &g_resetEngine) == STATUS_NOT_SUPPORTED);
        CHECK(g_resetEngine.LastAbortedFenceId == 0u);
    }

    g_fullMcdmReady = 0u;

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
