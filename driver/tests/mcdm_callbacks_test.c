#include "../wdk/bc250_mcdm_callbacks.h"
#include "../core/bc250_gcvm_contract.h"
#include "../core/bc250_adapter_info_contract.h"
#include "../core/bc250_segment_contract.h"
#include "../core/bc250_umd_private_contract.h"

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
/* Falha devolve 0 porque main() faz `return Run() ? 0 : 1`: devolver 1
 * aqui fazia a falha virar SUCESSO no build offline. Medido em 14/09
 * quebrando o emissor de proposito -- suite offline saiu EXIT=0 e a
 * kernel EXIT=1. Travado por audit_offline_check_is_not_inverted. */
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
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
static BC250_UMD_PRIVATE g_umdPrivate;
static DXGK_QUERYPAGETABLELEVELDESCIN g_pageLevelIn;
static DXGK_PAGE_TABLE_LEVEL_DESC g_pageLevelDesc;
static UINT g_numNodesOut;
static UINT g_fullMcdmReady;
static UINT64 g_rootAddress;
static UINT32 g_rootEntries;
static UINT32 g_rootMaxEntries;
static UINT32 g_testRootIndexBits;
static DXGKARG_BUILDPAGINGBUFFER g_paging;
static DXGK_PTE g_pagingEntries[2];
static UINT64 g_pageTable[512];
static UINT32 g_pagingDmaBuffer[64];
static UINT32 g_pagingBigDmaBuffer[16384]; /* X2b: paging buffer real de 64 KiB */
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
        ContextHandle != (HANDLE)0x253 || SegmentId != 1u) {
        return STATUS_INVALID_PARAMETER;
    }
    g_rootAddress = Address;
    g_rootEntries = NumEntries;
    g_rootMaxEntries = MaxEntries;
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
    Caps->VramMcBase = BC250_GCVM_VRAM_MC_BASE_FALLBACK;
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
        Caps->RootPageTableIndexBitCount = g_testRootIndexBits;
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

static BOOLEAN TestProbeHung(UINT32 NodeOrdinal, UINT32 *IsResponsive)
{
    (void)NodeOrdinal;
    *IsResponsive = 0u;
    return TRUE;
}

static BOOLEAN TestProbeAlive(UINT32 NodeOrdinal, UINT32 *IsResponsive)
{
    (void)NodeOrdinal;
    *IsResponsive = 1u;
    return TRUE;
}

/* Nao conseguiu medir: "nao sei" nao pode virar "esta viva". */
static BOOLEAN TestProbeCannotMeasure(UINT32 NodeOrdinal, UINT32 *IsResponsive)
{
    (void)NodeOrdinal;
    *IsResponsive = 1u;   /* mesmo escrevendo 1, a DDI tem de zerar */
    return FALSE;
}

static unsigned g_testTdrQuiesceCalls;
static unsigned g_testTdrRestartCalls;

static BOOLEAN TestTdrQuiesceOk(void)
{
    ++g_testTdrQuiesceCalls;
    return TRUE;
}

static BOOLEAN TestTdrRestartOk(void)
{
    ++g_testTdrRestartCalls;
    return TRUE;
}

/* Um unico stub de falha serve aos dois lados; os contadores separam quem foi. */
static BOOLEAN TestTdrExecutorFails(void)
{
    if (g_testTdrQuiesceCalls == g_testTdrRestartCalls) {
        ++g_testTdrQuiesceCalls;
    } else {
        ++g_testTdrRestartCalls;
    }
    return FALSE;
}

static int Bc250RunMcdmCallbacksTest(void)
{
    /* 0. I.PLAT: conversao do Tctl igual a do k10temp (P1), nos dois ramos. */
    CHECK(Bc250PlatformTctlRawToMilliC(440u << 21) == 55000);
    CHECK(Bc250PlatformTctlRawToMilliC((832u << 21) | 0x00080000u) == 55000);
    CHECK(Bc250PlatformTctlRawToMilliC((832u << 21) | 0x00030000u) == 55000);
    /* TJ_SEL so' com os dois bits: um bit sozinho NAO desloca. */
    CHECK(Bc250PlatformTctlRawToMilliC((440u << 21) | 0x00010000u) == 55000);
    CHECK(Bc250PlatformTctlRawToMilliC(0u) == 0);

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
    /*
     * A1 (06/09): esta assercao dizia FALSE e com isso CONGELAVA o defeito.
     * wddm-driver-and-feature-caps.md marca SupportNonVGA como M para Full
     * Graphics e para Display Only, e o handler KMDOD do mesmo binario ja
     * declarava TRUE -- os dois caminhos divergiam e o teste protegia a
     * divergencia. O valor agora vem de Bc250GetDriverCapsPolicy.
     */
    CHECK(driverCaps.SupportNonVGA == TRUE);
    /*
     * Dispensado por nao existir modo rotacionado
     * (optimized-screen-rotation-support.md condiciona a exigencia).
     */
    CHECK(driverCaps.SupportSmoothRotation == FALSE);
    /*
     * DIVIDA DECLARADA: direct-flip-of-video-memory.md marca Mandatory para
     * Full Graphics, mas exigiria reprogramar o endereco de scanout (escrita
     * em DCN, proibida fora da P9). Declarar TRUE seria cap sem backend.
     */
    CHECK(driverCaps.SupportDirectFlip == FALSE);
    CHECK(driverCaps.SupportPerEngineTDR == TRUE);
    CHECK(driverCaps.FlipCaps.FlipOnVSyncMmIo == 1u);
    CHECK(driverCaps.SchedulingCaps.MultiEngineAware == 1u);
    CHECK(driverCaps.SchedulingCaps.PreemptionAware == 1u);
    CHECK(driverCaps.SchedulingCaps.LowIrqlPreemptCommand == 1u);
    CHECK(driverCaps.MemoryManagementCaps.VirtualAddressingSupported == 1u);
    CHECK(driverCaps.MemoryManagementCaps.GpuMmuSupported == 1u);
    CHECK(driverCaps.MemoryManagementCaps.ParavirtualizationSupported == 0u);

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

    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) == STATUS_NOT_SUPPORTED);

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

    /*
     * FONTE UNICA DA GEOMETRIA DE GPUVA (regressao de 07/09).
     *
     * O mock acima devolve 48/4 de proposito -- numeros que NAO sao os do
     * silicio (34/2, ver BC250_GPUVA_PT_VA_BITS e docs/03). Isso torna este
     * teste capaz de distinguir repasse de hardcode: se um handler cravar um
     * numero proprio, ele aparece aqui divergindo do mock.
     *
     * A primeira versao do blob UMDRIVERPRIVATE cravava
     * BC250_ADAPTER_GPUVA_BITS (= 48) e PageTableLevels = 2, enquanto o mesmo
     * driver declarava VaBits=34 ao WDDM (log da corrida 11:08). Era uma
     * segunda fonte de verdade -- o defeito do A1 -- e faria a UMD (e, na rota
     * B, o winsys do RADV) dispor enderecos fora do que o VMID8 traduz.
     *
     * Este bloco trava as DUAS respostas na MESMA fonte.
     */
    memset(&g_queryInfo, 0, sizeof(g_queryInfo));
    memset(&g_umdPrivate, 0, sizeof(g_umdPrivate));
    g_queryInfo.Type = DXGKQAITYPE_UMDRIVERPRIVATE;
    g_queryInfo.pOutputData = &g_umdPrivate;
    g_queryInfo.OutputDataSize = sizeof(g_umdPrivate);
    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) ==
        STATUS_SUCCESS);
    CHECK(Bc250UmdPrivateValidate(&g_umdPrivate, (uint32_t)sizeof(g_umdPrivate)) ==
        BC250_UMD_PRIVATE_OK);
    CHECK(g_umdPrivate.GpuVaBits == g_gpuMmuCaps.VirtualAddressBitCount);
    CHECK(g_umdPrivate.PageTableLevels == g_gpuMmuCaps.PageTableLevelCount);

    /*
     * E sem a geometria o blob nao sai: melhor a UMD nao receber nada do que
     * receber numero inventado.
     */
    g_fullMcdmReady = 0u;
    memset(&g_queryInfo, 0, sizeof(g_queryInfo));
    memset(&g_umdPrivate, 0, sizeof(g_umdPrivate));
    g_queryInfo.Type = DXGKQAITYPE_UMDRIVERPRIVATE;
    g_queryInfo.pOutputData = &g_umdPrivate;
    g_queryInfo.OutputDataSize = sizeof(g_umdPrivate);
    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) ==
        STATUS_DEVICE_NOT_READY);
    CHECK(g_umdPrivate.Magic == 0ull);
    g_fullMcdmReady = 1u;

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
    g_pageLevelIn.LevelIndex = 1u;
    CHECK(Bc250McdmQueryAdapterInfo((HANDLE)0x250, &g_queryInfo) ==
        STATUS_SUCCESS);
    CHECK(g_pageLevelDesc.PageTableIndexBitCount == 13u);
    CHECK(g_pageLevelDesc.PageTableSizeInBytes == 65536u);
    g_pageLevelIn.LevelIndex = 0u;
    {
        DXGKARG_SETROOTPAGETABLE setRoot;
        memset(&setRoot, 0, sizeof(setRoot));
        setRoot.hContext = (HANDLE)0x253;
        setRoot.Address.SegmentId = 1u;
        setRoot.Address.SegmentOffset = 0x0000000000345000ull;
        setRoot.NumEntries = 512u;
        Bc250McdmInstallGpuVaLifecycle(Bc250TestSetRootPageTable);
        /* Fallback: sem RootPageTableIndexBitCount usa PageTableIndexBitCount (9 bits -> 512) */
        g_testRootIndexBits = 0u;
        Bc250McdmSetRootPageTable((HANDLE)0x250, &setRoot);
        CHECK(g_rootAddress == 0x0000000000345000ull);
        CHECK(g_rootEntries == 512u);
        CHECK(g_rootMaxEntries == 512u);

        /* Com RootPageTableIndexBitCount = 13 (espaço 34-bit / 2 níveis), maxEntries = 8192 */
        g_testRootIndexBits = 13u;
        setRoot.NumEntries = 8192u;
        Bc250McdmSetRootPageTable((HANDLE)0x250, &setRoot);
        CHECK(g_rootEntries == 8192u);
        CHECK(g_rootMaxEntries == 8192u);
        g_testRootIndexBits = 0u;
    }
    {
        NTSTATUS badStatus;
        memset(&g_paging, 0, sizeof(g_paging));
        memset(g_pageTable, 0xCC, sizeof(g_pageTable));
        memset(g_pagingEntries, 0, sizeof(g_pagingEntries));
        g_pagingEntries[0].Valid = 1u;
        g_pagingEntries[0].CacheCoherent = 1u;
        g_pagingEntries[0].ReadOnly = 1u;
        g_pagingEntries[0].NoExecute = 1u;
        g_pagingEntries[0].Segment = 1u;
        g_pagingEntries[0].PageTablePageSize =
            DXGK_PTE_PAGE_TABLE_PAGE_4KB;
        g_pagingEntries[0].PageAddress = 2u; /* PFN 2 -> 0x2000 offset */
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
        /*
         * PTE folha em VRAM: leva a base PA (GCMC_VM_FB_OFFSET << 24) mais o
         * inicio do segmento, e mantem as flags de folha.
         *   0x450000000 + 0xA8C000 + 0x2000 | 0x25
         * Historico: 0x0000000000A8E025 era o offset cru (sem base nenhuma);
         * 0x000000F400A8E025 usava a base MC, que e o dominio do anel e nao o
         * do walker do GCVM.
         */
        CHECK(g_pageTable[3] == 0x0000000450A8E025ull);
        CHECK(g_pageTable[4] == 0u);
        /* Teste de Segmento 0 (Memoria do Sistema) */
        g_pagingEntries[0].Segment = 0u;
        g_pagingEntries[0].PageAddress = 0x442C08ull;
        CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) == STATUS_SUCCESS);
        CHECK((g_pageTable[3] & (1ull << 1)) != 0); /* BC250_PTE_SYSTEM bit set */

        /* Teste de segmento invalido (Segmento 3) */
        g_pagingEntries[0].Segment = 3u;
        g_pageTable[3] = 0x1122334455667788ull;
        badStatus = Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging);
        CHECK(badStatus == STATUS_INVALID_PARAMETER || badStatus == STATUS_NOT_SUPPORTED);
        CHECK(g_pageTable[3] == 0x1122334455667788ull);

        /* Teste de repeticao (Flags.Repeat = 1) com lote grande (512 PTEs) */
        memset(g_pagingEntries, 0, sizeof(g_pagingEntries));
        g_pagingEntries[0].Valid = 0u;
        g_paging.Operation = DXGK_OPERATION_UPDATE_PAGE_TABLE;
        g_paging.UpdatePageTable.PageTableLevel = 0u;
        g_paging.UpdatePageTable.PageTableAddress.CpuVirtual = g_pageTable;
        g_paging.UpdatePageTable.pPageTableEntries = g_pagingEntries;
        g_paging.UpdatePageTable.StartIndex = 0u;
        g_paging.UpdatePageTable.NumPageTableEntries = 512u;
        g_paging.UpdatePageTable.Flags.Repeat = 1u;
        g_paging.UpdatePageTable.UpdateMode = DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
        CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) == STATUS_SUCCESS);
        CHECK(g_pageTable[0] == 0u);
        CHECK(g_pageTable[511] == 0u);

        /* Diretórios não-leaf também passam pelo handler transacional ativo. */
        g_pagingEntries[0].Segment = 1u;
        g_paging.UpdatePageTable.Flags.Repeat = 0u;
        g_paging.UpdatePageTable.PageTableLevel = 2u;
        g_paging.UpdatePageTable.StartIndex = 3u;
        g_paging.UpdatePageTable.NumPageTableEntries = 2u;
        g_updateHandlerCalls = 0u;
        Bc250McdmInstallUpdatePageTableHandler(
            Bc250TestUpdatePageTableHandler);
        CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) ==
            STATUS_SUCCESS);
        CHECK(g_updateHandlerCalls == 1u);
        CHECK(g_updateHandlerLastLevel == 2u);
        Bc250McdmInstallUpdatePageTableHandler(NULL);

        /*
         * P6: o VidMm reutiliza a area logo apos pDmaBuffer. Cada IB SDMA
         * precisa consumir multiplo de 8 DW para que a proxima GPUVA continue
         * alinhada a 32 bytes.
         */
        memset(&g_paging, 0, sizeof(g_paging));
        memset(g_pagingDmaBuffer, 0xCC, sizeof(g_pagingDmaBuffer));
        g_paging.Operation = DXGK_OPERATION_VIRTUAL_FILL;
        g_paging.pDmaBuffer = g_pagingDmaBuffer;
        g_paging.DmaSize = sizeof(g_pagingDmaBuffer);
        g_paging.FillVirtual.DestinationVirtualAddress = 0x260000ull;
        g_paging.FillVirtual.FillSizeInBytes = 11075584ull;
        /* X2b: padrao 0 vai por copia; o fatiamento do CONST_FILL e' testado com padrao != 0. */
        g_paging.FillVirtual.FillPattern = 0xA5A5A5A5u;
        /*
         * X2a (doc 523): o FILL de paginacao e' fatiado em 256 KiB. A
         * primaria de 11.075.584 bytes (medida nas travas) vira 43 pacotes e
         * NAO cabe num buffer de 64 DW: o primeiro passo enche o buffer com 12
         * pacotes (60 DW + 4 NOPs de alinhamento) e pede outro passo pelo
         * mecanismo legal (INSUFFICIENT_DMA_BUFFER + MultipassOffset).
         */
        {
            UINT32 packet;
            CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) ==
                STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER);
            CHECK((UINT32 *)g_paging.pDmaBuffer == g_pagingDmaBuffer + 64u);
            CHECK(g_paging.MultipassOffset == 12u * 262144u);
            for (packet = 0u; packet < 12u; ++packet) {
                CHECK((g_pagingDmaBuffer[packet * 5u] & 0xFFu) == BC250_SDMA_OP_CONST_FILL);
                /* contagem = bytes - 1: nenhuma fatia passa de 256 KiB */
                CHECK(g_pagingDmaBuffer[packet * 5u + 4u] == 262144u - 1u);
            }
            CHECK(g_pagingDmaBuffer[60] == BC250_SDMA_OP_NOP);
            CHECK(g_pagingDmaBuffer[63] == BC250_SDMA_OP_NOP);
        }

        /*
         * P6_PAGING_IB_SHAPE_PUBLICATION_OFFLINE
         *
         * O IB_SIZE do pacote INDIRECT precisa poder excluir os NOPs de cauda:
         * eles existem so para alinhar a GPUVA do proximo buffer e nao
         * precisam ser buscados pela engine. A publicacao e autovalidada
         * contra o tamanho que o VidMm entregou e e destrutiva.
         */
        /*
         * P6_PAGING_IB_DEST_RANGE_OFFLINE
         *
         * A faixa que o IB vai ESCREVER precisa chegar a submissao para ser
         * caminhada. Na corrida de 05/09 01:12 so a GPUVA do IB era validada;
         * a engine resolveu o IB, entrou nele e parou, com o UTCL1 acordando
         * do lado de escrita. Leitura nao destrutiva.
         */
        {
            UINT64 destStart = 0ull;
            UINT64 destEnd = 0ull;
            CHECK(Bc250PagingPeekIbDestRange(&destStart, &destEnd));
            CHECK(destStart == 0x260000ull);
            /* So' a faixa DESTE passo: 12 fatias de 256 KiB. */
            CHECK(destEnd == 0x260000ull + 12ull * 262144ull);
            /* Nao destrutiva: a segunda leitura devolve o mesmo. */
            CHECK(Bc250PagingPeekIbDestRange(&destStart, &destEnd));
            CHECK(destStart == 0x260000ull);
        }
        CHECK(Bc250PagingConsumeIbUsefulDwords(64u) == 60u);
        /* Consumo destrutivo: a segunda leitura nao reaproveita. */
        CHECK(Bc250PagingConsumeIbUsefulDwords(64u) == 0u);

        /*
         * X2a: os passos seguintes continuam do MultipassOffset e cobrem o
         * pedido inteiro, sem buraco e sem fatia maior que 256 KiB.
         */
        {
            UINT64 covered = (UINT64)g_paging.MultipassOffset;
            UINT32 passes = 1u;
            NTSTATUS passStatus;
            do {
                UINT32 before = g_paging.MultipassOffset;
                UINT32 used;
                UINT32 packet;
                memset(g_pagingDmaBuffer, 0xCC, sizeof(g_pagingDmaBuffer));
                g_paging.pDmaBuffer = g_pagingDmaBuffer;
                g_paging.DmaSize = sizeof(g_pagingDmaBuffer);
                passStatus = Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging);
                CHECK(passStatus == STATUS_SUCCESS ||
                    passStatus == STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER);
                used = (UINT32)((UINT32 *)g_paging.pDmaBuffer - g_pagingDmaBuffer);
                CHECK((used & 7u) == 0u);
                for (packet = 0u; packet * 5u + 4u < used &&
                        (g_pagingDmaBuffer[packet * 5u] & 0xFFu) == BC250_SDMA_OP_CONST_FILL; ++packet) {
                    CHECK(g_pagingDmaBuffer[packet * 5u + 4u] <= 262144u - 1u);
                    covered += (UINT64)g_pagingDmaBuffer[packet * 5u + 4u] + 1ull;
                }
                CHECK(packet != 0u);
                if (passStatus == STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER) {
                    CHECK(g_paging.MultipassOffset > before);
                }
                ++passes;
            } while (passStatus == STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER && passes < 16u);
            CHECK(passStatus == STATUS_SUCCESS);
            CHECK(covered == 11075584ull);
            CHECK(passes == 4u); /* 43 fatias: 12 + 12 + 12 + 7 */
            (void)Bc250PagingConsumeIbUsefulDwords(0u);
        }

        /*
         * X2b (doc 561): padrao 0 zera por COPIA a partir de uma regiao de
         * zeros na frente do proprio pedaco do paging buffer. A primaria
         * medida (11.075.584 B) num buffer de 64 KiB: 32 KiB de zeros + 338
         * copias de 32 KiB com FONTE CONSTANTE = DmaBufferGpuVirtualAddress +
         * DmaBufferWriteOffset, destino contiguo, um passo so'.
         */
        {
            UINT32 packet;
            UINT64 zeroVa = 0x229000ull + 0x80ull;
            memset(&g_paging, 0, sizeof(g_paging));
            memset(g_pagingBigDmaBuffer, 0xCC, sizeof(g_pagingBigDmaBuffer));
            g_paging.Operation = DXGK_OPERATION_VIRTUAL_FILL;
            g_paging.pDmaBuffer = g_pagingBigDmaBuffer;
            g_paging.DmaSize = sizeof(g_pagingBigDmaBuffer);
            g_paging.DmaBufferGpuVirtualAddress = 0x229000ull;
            g_paging.DmaBufferWriteOffset = 0x80u;
            g_paging.FillVirtual.DestinationVirtualAddress = 0x290000ull;
            g_paging.FillVirtual.FillSizeInBytes = 11075584ull;
            g_paging.FillVirtual.FillPattern = 0u;
            CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) == STATUS_SUCCESS);
            CHECK(g_paging.MultipassOffset == 0u); /* BC250_PAGING_MULTIPASS_DONE */
            /* 8192 DW de zeros + 338 * 7 = 10558, alinhado a 10560. */
            CHECK((UINT32 *)g_paging.pDmaBuffer == g_pagingBigDmaBuffer + 10560u);
            for (packet = 0u; packet < 8192u; ++packet) {
                if (g_pagingBigDmaBuffer[packet] != 0u) break;
            }
            CHECK(packet == 8192u);
            for (packet = 0u; packet < 338u; ++packet) {
                const UINT32 *c = g_pagingBigDmaBuffer + 8192u + packet * 7u;
                CHECK(c[0] == (BC250_SDMA_OP_COPY | (BC250_SDMA_SUBOP_COPY_LINEAR << 8)));
                CHECK(c[1] == 32768u - 1u);
                CHECK(c[3] == (UINT32)zeroVa && c[4] == 0u);
                CHECK(c[5] == (UINT32)(0x290000ull + (UINT64)packet * 32768ull) && c[6] == 0u);
            }
            CHECK(g_pagingBigDmaBuffer[10558] == BC250_SDMA_OP_NOP);
            CHECK(g_pagingBigDmaBuffer[10559] == BC250_SDMA_OP_NOP);
            {
                BC250_PAGING_PROOF proof;
                CHECK(Bc250PagingDecodeProof(g_pagingBigDmaBuffer, 10560u, 0x3AC510000ull, &proof));
                CHECK(proof.Opcode == BC250_PAGING_PROOF_OP_FILL_BY_COPY);
                CHECK(proof.Source == zeroVa);
                CHECK(proof.SourceBytes == 32768u);
                CHECK(proof.Destination == 0x290000ull);
                CHECK(proof.Bytes == 11075584u);
            }
            {
                UINT64 destStart = 0ull, destEnd = 0ull;
                CHECK(Bc250PagingPeekIbDestRange(&destStart, &destEnd));
                CHECK(destStart == 0x290000ull && destEnd == 0x290000ull + 11075584ull);
            }
            (void)Bc250PagingConsumeIbUsefulDwords(0u);

            /* Sem GPUVA do DMA buffer nao ha fonte: fica em CONST_FILL e fala. */
            memset(&g_paging, 0, sizeof(g_paging));
            memset(g_pagingBigDmaBuffer, 0xCC, sizeof(g_pagingBigDmaBuffer));
            g_paging.Operation = DXGK_OPERATION_VIRTUAL_FILL;
            g_paging.pDmaBuffer = g_pagingBigDmaBuffer;
            g_paging.DmaSize = sizeof(g_pagingBigDmaBuffer);
            g_paging.FillVirtual.DestinationVirtualAddress = 0x290000ull;
            g_paging.FillVirtual.FillSizeInBytes = 65536ull;
            g_paging.FillVirtual.FillPattern = 0u;
            CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) == STATUS_SUCCESS);
            CHECK((g_pagingBigDmaBuffer[0] & 0xFFu) == BC250_SDMA_OP_CONST_FILL);
            (void)Bc250PagingConsumeIbUsefulDwords(0u);

            /* Buffer sem lugar nem para 4 KiB de zeros: multipasse sem progresso. */
            memset(&g_paging, 0, sizeof(g_paging));
            g_paging.Operation = DXGK_OPERATION_VIRTUAL_FILL;
            g_paging.pDmaBuffer = g_pagingDmaBuffer;
            g_paging.DmaSize = sizeof(g_pagingDmaBuffer);
            g_paging.DmaBufferGpuVirtualAddress = 0x229000ull;
            g_paging.FillVirtual.DestinationVirtualAddress = 0x290000ull;
            g_paging.FillVirtual.FillSizeInBytes = 65536ull;
            g_paging.FillVirtual.FillPattern = 0u;
            CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) ==
                STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER);
            CHECK(g_paging.MultipassOffset == 0u);
            CHECK((UINT32 *)g_paging.pDmaBuffer == g_pagingDmaBuffer);
        }

        memset(&g_paging, 0, sizeof(g_paging));
        memset(g_pagingDmaBuffer, 0xCC, sizeof(g_pagingDmaBuffer));
        g_paging.Operation = DXGK_OPERATION_VIRTUAL_TRANSFER;
        g_paging.pDmaBuffer = g_pagingDmaBuffer;
        g_paging.DmaSize = sizeof(g_pagingDmaBuffer);
        g_paging.TransferVirtual.SourceVirtualAddress = 0x200000ull;
        g_paging.TransferVirtual.DestinationVirtualAddress = 0x400000ull;
        g_paging.TransferVirtual.TransferSizeInBytes = 786432ull;
        CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) ==
            STATUS_SUCCESS);
        CHECK((UINT32 *)g_paging.pDmaBuffer == g_pagingDmaBuffer + 24u);
        CHECK(g_pagingDmaBuffer[21] == BC250_SDMA_OP_NOP);
        CHECK(g_pagingDmaBuffer[22] == BC250_SDMA_OP_NOP);
        CHECK(g_pagingDmaBuffer[23] == BC250_SDMA_OP_NOP);

        /* 21 DW uteis + 3 NOPs; o total publicado e 24 DW / 96 bytes. */
        {
            UINT64 destStart = 0ull;
            UINT64 destEnd = 0ull;
            CHECK(Bc250PagingPeekIbDestRange(&destStart, &destEnd));
            CHECK(destStart == 0x400000ull);
            CHECK(destEnd == 0x400000ull + 786432ull);
        }
        CHECK(Bc250PagingConsumeIbUsefulDwords(24u) == 21u);

        /*
         * WDDM 2.2+ Monitored Fence Signaling (Op=16).
         */
        memset(&g_paging, 0, sizeof(g_paging));
        memset(g_pagingDmaBuffer, 0xCC, sizeof(g_pagingDmaBuffer));
        g_paging.Operation = DXGK_OPERATION_SIGNAL_MONITORED_FENCE;
        g_paging.pDmaBuffer = g_pagingDmaBuffer;
        g_paging.DmaSize = sizeof(g_pagingDmaBuffer);
        g_paging.SignalMonitoredFence.MonitoredFenceGpuVa = 0x20001000ull;
        g_paging.SignalMonitoredFence.MonitoredFenceValue = 0x0123456789ABCDEFull;

        CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) ==
            STATUS_SUCCESS);
        CHECK((UINT32 *)g_paging.pDmaBuffer == g_pagingDmaBuffer + 16u);
        /* Packet 0: FENCE64 header (OP=5, MTYPE_UC=3, WRITE64=1) -> 0x80030005 */
        CHECK(g_pagingDmaBuffer[0] == 0x80030005u);
        /* Packet 0: DstAddress = 0x20001000 */
        CHECK(g_pagingDmaBuffer[1] == 0x20001000u);
        CHECK(g_pagingDmaBuffer[2] == 0u);
        /* Packet 0: Low 32 bits = 0x89ABCDEF */
        CHECK(g_pagingDmaBuffer[3] == 0x89ABCDEFu);
        /* Packet 0: High 32 bits = 0x01234567 */
        CHECK(g_pagingDmaBuffer[4] == 0x01234567u);
        /* Packet 1: GCR REQ (OP=17) */
        CHECK((g_pagingDmaBuffer[5] & 0xFFu) == BC250_SDMA_OP_GCR_REQ);
        /* Cauda: 6 NOPs de alinhamento */
        CHECK(g_pagingDmaBuffer[10] == BC250_SDMA_OP_NOP);
        CHECK(g_pagingDmaBuffer[15] == BC250_SDMA_OP_NOP);

        /* Casos de erro: endereco desalinhado e buffer insuficiente */
        g_paging.SignalMonitoredFence.MonitoredFenceGpuVa = 0x20001001ull;
        CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) ==
            STATUS_INVALID_PARAMETER);
        g_paging.SignalMonitoredFence.MonitoredFenceGpuVa = 0x20001000ull;
        g_paging.DmaSize = 16u;
        CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) ==
            STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER);

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
        /* DxgkDdiSignalMonitoredFence DDI */
        {
            DXGKARG_SIGNALMONITOREDFENCE ddiSignal;
            memset(&ddiSignal, 0, sizeof(ddiSignal));
            memset(g_pagingDmaBuffer, 0xCC, sizeof(g_pagingDmaBuffer));
            ddiSignal.pDmaBuffer = g_pagingDmaBuffer;
            ddiSignal.DmaSize = sizeof(g_pagingDmaBuffer);
            ddiSignal.MonitoredFenceGpuVa = 0x20002000ull;
            ddiSignal.MonitoredFenceValue = 0xFEDCBA9876543210ull;
            CHECK(Bc250McdmSignalMonitoredFence((HANDLE)0x250, &ddiSignal) ==
                STATUS_SUCCESS);
            CHECK(g_pagingDmaBuffer[0] == 0x80030005u);
            CHECK(g_pagingDmaBuffer[1] == 0x20002000u);
            CHECK(g_pagingDmaBuffer[2] == 0u);
            CHECK(g_pagingDmaBuffer[3] == 0x76543210u);
            CHECK(g_pagingDmaBuffer[4] == 0xFEDCBA98u);
            CHECK(ddiSignal.DmaSize == sizeof(g_pagingDmaBuffer) - (16u * sizeof(UINT32)));
        }
#endif

        /*
         * P6_PTE_VRAM_MC_BASE_OFFLINE
         *
         * Medido na corrida de 05/09 00:55: o PDE continha 0x00A91061, um
         * offset CRU de VRAM. A GPU caminha em espaco MC e foi buscar a folha
         * no MC 0x0000A91000, que nao e VRAM. Endereco de VRAM numa entrada
         * PRECISA levar a base MC; endereco de memoria de sistema NAO pode
         * leva-la.
         */
        {
            BC250_MCDM_RUNTIME_CAPS pteCaps;
            DXGK_PTE pte;
            UINT64 encoded = 0ull;
            CONST UINT64 addrMask = 0x0000FFFFFFFFF000ull;
            CONST UINT64 base = BC250_GCVM_VRAM_MC_BASE_FALLBACK;
            CONST UINT64 segBase = 0x00A8C000ull;

            CHECK(Bc250TestRuntimeCaps((HANDLE)0x250, &pteCaps) ==
                STATUS_SUCCESS);
            CHECK(pteCaps.VramMcBase == base);

            /* PDE (nivel 1): tabela filha mora em VRAM -> leva base MC. */
            memset(&pte, 0, sizeof(pte));
            pte.Valid = 1u;
            pte.Segment = 1u;
            pte.PageAddress = 0x100ull;
            CHECK(Bc250McdmEncodeVidMmPteWithLevel(&pteCaps, &pte, 1u,
                &encoded) == STATUS_SUCCESS);
            CHECK((encoded & addrMask) == (base + segBase + 0x100000ull));

            /* PTE folha em VRAM (Segment 1) -> tambem leva base MC. */
            memset(&pte, 0, sizeof(pte));
            pte.Valid = 1u;
            pte.Segment = 1u;
            pte.PageAddress = 0x100ull;
            pte.PageTablePageSize = DXGK_PTE_PAGE_TABLE_PAGE_4KB;
            CHECK(Bc250McdmEncodeVidMmPteWithLevel(&pteCaps, &pte, 0u,
                &encoded) == STATUS_SUCCESS);
            CHECK((encoded & addrMask) == (base + segBase + 0x100000ull));
            CHECK((encoded & BC250_PTE_SYSTEM) == 0u);

            /* PTE folha em memoria de sistema (Segment 0) -> endereco cru. */
            memset(&pte, 0, sizeof(pte));
            pte.Valid = 1u;
            pte.Segment = 0u;
            pte.PageAddress = 0x10721Dull;
            pte.PageTablePageSize = DXGK_PTE_PAGE_TABLE_PAGE_4KB;
            CHECK(Bc250McdmEncodeVidMmPteWithLevel(&pteCaps, &pte, 0u,
                &encoded) == STATUS_SUCCESS);
            CHECK((encoded & addrMask) == 0x10721D000ull);
            CHECK((encoded & BC250_PTE_SYSTEM) != 0u);
            /*
             * P6 (doc 618): sem flag extra a folha sai NC (MTYPE 0); com
             * BC250_PTE_MTYPE_UC sai UC (3 << 48) com o mesmo endereco e
             * SYSTEM. Outro bit extra e ignorado, e no PDE nao tem efeito.
             */
            CHECK((encoded & 0x0007000000000000ull) == 0ull);
            CHECK(Bc250McdmEncodeVidMmPteWithLevelEx(&pteCaps, &pte, 0u,
                BC250_PTE_MTYPE_UC, &encoded) == STATUS_SUCCESS);
            CHECK((encoded & 0x0007000000000000ull) == 0x0003000000000000ull);
            CHECK((encoded & addrMask) == 0x10721D000ull);
            CHECK((encoded & BC250_PTE_SYSTEM) != 0u);
            CHECK(Bc250McdmEncodeVidMmPteWithLevelEx(&pteCaps, &pte, 0u,
                BC250_PTE_PRT, &encoded) == STATUS_SUCCESS);
            CHECK((encoded & ((1ull << 54) | 0x0007000000000000ull)) == 0ull);
            pte.Segment = 1u;
            pte.PageAddress = 0x100ull;
            CHECK(Bc250McdmEncodeVidMmPteWithLevelEx(&pteCaps, &pte, 1u,
                BC250_PTE_MTYPE_UC, &encoded) == STATUS_SUCCESS);
            CHECK((encoded & 0x0007000000000000ull) == 0ull);
            CHECK(Bc250McdmEncodeVidMmPteWithLevelEx(&pteCaps, &pte, 0u,
                BC250_PTE_MTYPE_UC, &encoded) == STATUS_SUCCESS);
            CHECK((encoded & 0x0007000000000000ull) == 0x0003000000000000ull);
            CHECK((encoded & BC250_PTE_SYSTEM) == 0u);

            /*
             * M2 (doc 548): folha no segmento 3 (VRAM sem CPU) leva a MESMA base
             * MC e a base PROPRIA do segmento 3; fora da faixa, com segmento 3
             * ausente, ou como PDE: recusa.
             */
            pteCaps.HiddenSegmentBaseOffset = 0x10000000ull;
            pteCaps.HiddenSegmentBytes = 240ull * 1024ull * 1024ull;
            memset(&pte, 0, sizeof(pte));
            pte.Valid = 1u;
            pte.Segment = 3u;
            pte.PageAddress = 0x100ull;
            pte.PageTablePageSize = DXGK_PTE_PAGE_TABLE_PAGE_4KB;
            CHECK(Bc250McdmEncodeVidMmPteWithLevel(&pteCaps, &pte, 0u,
                &encoded) == STATUS_SUCCESS);
            CHECK((encoded & addrMask) == (base + 0x10000000ull + 0x100000ull));
            CHECK((encoded & BC250_PTE_SYSTEM) == 0u);
            pte.PageAddress = (240ull * 1024ull * 1024ull) >> 12;
            CHECK(Bc250McdmEncodeVidMmPteWithLevel(&pteCaps, &pte, 0u,
                &encoded) == STATUS_NOT_SUPPORTED);
            pte.PageAddress = 0x100ull;
            CHECK(Bc250McdmEncodeVidMmPteWithLevel(&pteCaps, &pte, 1u,
                &encoded) == STATUS_NOT_SUPPORTED);
            pteCaps.HiddenSegmentBytes = 0ull;
            CHECK(Bc250McdmEncodeVidMmPteWithLevel(&pteCaps, &pte, 0u,
                &encoded) == STATUS_NOT_SUPPORTED);
        }
        /*
         * P6_PDE_FORMAT_OFFLINE
         *
         * Formato de PDE do Navi 10 (gmc_v10_0.c): endereco em [47:6],
         * [5:3] reservados, 2 C, 1 system, 0 valid. Nao existem READABLE
         * (bit 5) nem WRITEABLE (bit 6) num PDE -- o bit 6 e o LSB do
         * endereco. Gravar 0x61 deslocava a tabela folha em 64 bytes, e o
         * decodificador de PTE (mascara de 4 KiB) escondia isso da caminhada
         * durante seis corridas.
         */
        {
            UINT64 pde = 0ull;
            UINT64 phys = 0ull;
            UINT32 flags = 0u;
            UINT32 reserved = 0u;

            /* PDE valido: so bit 0, endereco preservado. */
            CHECK(Bc250GcvmEncodePde(0x450A91000ull, BC250_PTE_VALID, &pde) ==
                BC250_GCVM_OK);
            CHECK(pde == 0x0000000450A91001ull);

            /* Flags de folha sao RECUSADAS num PDE (fail-closed). */
            CHECK(Bc250GcvmEncodePde(0x450A91000ull,
                BC250_PTE_VALID | BC250_PTE_READABLE, &pde) ==
                BC250_GCVM_BAD_ARGUMENT);
            CHECK(Bc250GcvmEncodePde(0x450A91000ull,
                BC250_PTE_VALID | BC250_PTE_WRITEABLE, &pde) ==
                BC250_GCVM_BAD_ARGUMENT);

            /* Decode com mascara de 64 bytes enxerga o deslocamento. */
            CHECK(Bc250GcvmDecodePde(0x0000000450A91061ull, &phys, &flags,
                &reserved) == BC250_GCVM_OK);
            CHECK(phys == 0x450A91040ull);   /* +64 bytes: o bug real */
            CHECK(reserved == 0x20u);        /* bit 5 em campo reservado */

            /* Entrada limpa: endereco intacto e nenhum bit reservado. */
            CHECK(Bc250GcvmDecodePde(0x0000000450A91001ull, &phys, &flags,
                &reserved) == BC250_GCVM_OK);
            CHECK(phys == 0x450A91000ull);
            CHECK(reserved == 0u);
            CHECK((flags & BC250_PTE_VALID) != 0u);

            /*
             * Contraste com o decodificador de PTE, que mascara 4 KiB: ele
             * devolve o endereco "limpo" mesmo com a entrada corrompida. Foi
             * este falso positivo que a caminhada reportava.
             */
            CHECK(Bc250GcvmDecodePte(0x0000000450A91061ull, &phys, &flags) ==
                BC250_GCVM_OK);
            CHECK(phys == 0x450A91000ull);
        }

        memset(&g_paging, 0, sizeof(g_paging));
        memset(g_pagingDmaBuffer, 0xCC, sizeof(g_pagingDmaBuffer));
        g_paging.Operation = DXGK_OPERATION_VIRTUAL_FILL;
        g_paging.pDmaBuffer = g_pagingDmaBuffer;
        g_paging.DmaSize = sizeof(g_pagingDmaBuffer);
        g_paging.FillVirtual.DestinationVirtualAddress = 0x260000ull;
        g_paging.FillVirtual.FillSizeInBytes = 11075584ull;
        /* X2b: padrao 0 vai por copia; o fatiamento do CONST_FILL e' testado com padrao != 0. */
        g_paging.FillVirtual.FillPattern = 0xA5A5A5A5u;
        /* X2a: com fatias de 256 KiB este pedido pede multipasse. */
        CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) ==
            STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER);
        /*
         * Fail-closed: se o total publicado nao casar com o DmaBufferSize que
         * o VidMm entregou na submissao, a publicacao e descartada e o
         * executor deriva o IB_SIZE do tamanho -- nunca usa um valor de outro
         * buffer. Publicado = 64 DW; pedimos com 16.
         */
        CHECK(Bc250PagingConsumeIbUsefulDwords(16u) == 0u);

        memset(&g_paging, 0, sizeof(g_paging));
        g_paging.Operation = DXGK_OPERATION_FLUSH_TLB;
        CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) == STATUS_NOT_SUPPORTED);

        memset(&g_paging, 0, sizeof(g_paging));
        g_paging.Operation = (DXGK_BUILDPAGINGBUFFER_OPERATION)0x7FFFFFFFu;
        CHECK(Bc250McdmBuildPagingBuffer((HANDLE)0x250, &g_paging) ==
            STATUS_NOT_SUPPORTED);
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
    CHECK(g_nodeMetadata.EngineType == (Bc250McdmGetComputeOnly() ? DXGK_ENGINE_TYPE_OTHER : DXGK_ENGINE_TYPE_3D));

    /*
     * 6. Test QueryEngineStatus
     *
     * item 24 (07/09) -- ESTE BLOCO TAMBEM CONGELAVA UM FALSO SUCESSO: exigia
     * Responsive==1 sem probe instalada, isto e', exigia que a DDI afirmasse
     * engine viva sem medir registrador nenhum. Contrato novo:
     *   - sem probe             -> STATUS_NOT_SUPPORTED e Responsive=0
     *   - probe falha ao medir  -> STATUS_DEVICE_NOT_READY e Responsive=0
     *   - probe mede "viva"     -> STATUS_SUCCESS e Responsive=1
     *   - probe mede "travada"  -> STATUS_SUCCESS e Responsive=0
     */
    memset(&g_engineStatus, 0, sizeof(g_engineStatus));
    g_engineStatus.NodeOrdinal = 0u;
    Bc250McdmInstallEngineResponsiveProbe(NULL);
    CHECK(Bc250McdmQueryEngineStatus((HANDLE)0x250, &g_engineStatus) == STATUS_NOT_SUPPORTED);
    CHECK(g_engineStatus.EngineStatus.Responsive == 0);

    Bc250McdmInstallEngineResponsiveProbe(TestProbeCannotMeasure);
    g_engineStatus.EngineStatus.Responsive = 1;
    CHECK(Bc250McdmQueryEngineStatus((HANDLE)0x250, &g_engineStatus) == STATUS_DEVICE_NOT_READY);
    CHECK(g_engineStatus.EngineStatus.Responsive == 0);

    Bc250McdmInstallEngineResponsiveProbe(TestProbeHung);
    g_engineStatus.EngineStatus.Responsive = 1;
    CHECK(Bc250McdmQueryEngineStatus((HANDLE)0x250, &g_engineStatus) == STATUS_SUCCESS);
    CHECK(g_engineStatus.EngineStatus.Responsive == 0);

    Bc250McdmInstallEngineResponsiveProbe(TestProbeAlive);
    CHECK(Bc250McdmQueryEngineStatus((HANDLE)0x250, &g_engineStatus) == STATUS_SUCCESS);
    CHECK(g_engineStatus.EngineStatus.Responsive == 1);
    g_engineStatus.NodeOrdinal = 1u;
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

    /*
     * 9. Test Timeout Recovery Callbacks & Preemption
     *
     * B6 / item 25 (07/09) -- ESTE BLOCO CONGELAVA UM FALSO SUCESSO. Ele
     * exigia STATUS_SUCCESS de ResetFromTimeout/RestartFromTimeout SEM
     * nenhum executor instalado, isto e', exigia que as duas DDIs mentissem
     * ao dxgkrnl (Lei 4). Agora o contrato e':
     *   - sem executor           -> STATUS_DEVICE_NOT_READY
     *   - executor devolve FALSE -> STATUS_DEVICE_NOT_READY
     *   - executor devolve TRUE  -> STATUS_SUCCESS
     */
    CHECK(Bc250McdmResetFromTimeout(NULL) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250McdmRestartFromTimeout(NULL) == STATUS_NOT_SUPPORTED);

    Bc250McdmInstallTdrExecutors(NULL, NULL);
    CHECK(Bc250McdmResetFromTimeout((HANDLE)0x250) == STATUS_DEVICE_NOT_READY);
    CHECK(Bc250McdmRestartFromTimeout((HANDLE)0x250) == STATUS_DEVICE_NOT_READY);

    Bc250McdmInstallTdrExecutors(TestTdrExecutorFails, TestTdrExecutorFails);
    CHECK(Bc250McdmResetFromTimeout((HANDLE)0x250) == STATUS_DEVICE_NOT_READY);
    CHECK(Bc250McdmRestartFromTimeout((HANDLE)0x250) == STATUS_DEVICE_NOT_READY);
    CHECK(g_testTdrQuiesceCalls == 1u);
    CHECK(g_testTdrRestartCalls == 1u);

    Bc250McdmInstallTdrExecutors(TestTdrQuiesceOk, TestTdrRestartOk);
    CHECK(Bc250McdmResetFromTimeout((HANDLE)0x250) == STATUS_SUCCESS);
    CHECK(Bc250McdmRestartFromTimeout((HANDLE)0x250) == STATUS_SUCCESS);
    CHECK(g_testTdrQuiesceCalls == 2u);
    CHECK(g_testTdrRestartCalls == 2u);

    /* Desinstalar volta ao fail-closed: nada de sucesso residual. */
    Bc250McdmInstallTdrExecutors(NULL, NULL);
    CHECK(Bc250McdmResetFromTimeout((HANDLE)0x250) == STATUS_DEVICE_NOT_READY);
    CHECK(Bc250McdmRestartFromTimeout((HANDLE)0x250) == STATUS_DEVICE_NOT_READY);

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

    /*
     * 4c.4 corrigida (doc 576): pontos de divisao casam SO' com o IB que os
     * contem. Sequencia MEDIDA na Corrida 209: o VidMm monta o buffer
     * 0x21E340 (pontos em +8 e +56 dw) ANTES de enviar o IB 28 em 0x23BB80;
     * a regra antiga dava os dois ao 28 como "velhos" e o IB 30 rodava sem
     * invalidacao entre PTE e uso.
     */
    {
        BC250_SDMA_IB_SPLIT out[BC250_SDMA_IB_SPLIT_MAX];
        UINT32 staleMask = 0u, staleCount = 0u, n, k;

        /* esvazia restos de outros blocos */
        (void)Bc250PagingSplitTake(0ull, 0xFFFFFFFFu, out, BC250_SDMA_IB_SPLIT_MAX,
            &staleMask, &staleCount);
        CHECK(Bc250PagingSplitPendingCount() == 0u);

        Bc250PagingSplitRecordAt(0x21E340ull + 8u * 4u, 1u << 8);
        Bc250PagingSplitRecordAt(0x21E340ull + 56u * 4u, 1u << 8);
        /* envio 28: IB em 0x23BB80, 8128 bytes -- NAO e' dono dos pontos */
        n = Bc250PagingSplitTake(0x23BB80ull, 8128u, out, BC250_SDMA_IB_SPLIT_MAX,
            &staleMask, &staleCount);
        CHECK(n == 0u && staleCount == 0u && staleMask == 0u);
        CHECK(Bc250PagingSplitPendingCount() == 2u);
        /* envio 29: 0x21E000, 832 bytes, termina em 0x21E340 -- tambem nao */
        n = Bc250PagingSplitTake(0x21E000ull, 832u, out, BC250_SDMA_IB_SPLIT_MAX,
            &staleMask, &staleCount);
        CHECK(n == 0u && staleCount == 0u);
        /* envio 30: o dono -- os dois pontos, nos offsets exatos */
        n = Bc250PagingSplitTake(0x21E340ull, 36768u, out, BC250_SDMA_IB_SPLIT_MAX,
            &staleMask, &staleCount);
        CHECK(n == 2u && staleCount == 0u);
        CHECK(out[0].OffsetDwords == 8u && out[1].OffsetDwords == 56u);
        CHECK(out[0].VmidMask == (1u << 8));
        CHECK(Bc250PagingSplitPendingCount() == 0u);

        /* ponto no fim de um IB e no inicio do seguinte fica com o primeiro */
        Bc250PagingSplitRecordAt(0x21E340ull, 1u << 8);
        n = Bc250PagingSplitTake(0x21E000ull, 832u, out, BC250_SDMA_IB_SPLIT_MAX,
            &staleMask, &staleCount);
        CHECK(n == 1u && out[0].OffsetDwords == 832u / 4u);

        /* sem posicao: antes do proximo IB de paginacao */
        Bc250PagingSplitRecordAt(0ull, 1u << 8);
        n = Bc250PagingSplitTake(0x22E000ull, 4096u, out, BC250_SDMA_IB_SPLIT_MAX,
            &staleMask, &staleCount);
        CHECK(n == 0u && staleCount == 1u && staleMask == (1u << 8));

        /* orfao: so' depois de 64 envios sem casar, e entao antes do IB */
        Bc250PagingSplitRecordAt(0x300000ull, 1u << 7);
        for (k = 0u; k < 63u; ++k) {
            n = Bc250PagingSplitTake(0x22E000ull, 4096u, out, BC250_SDMA_IB_SPLIT_MAX,
                &staleMask, &staleCount);
            CHECK(n == 0u && staleCount == 0u);
        }
        n = Bc250PagingSplitTake(0x22E000ull, 4096u, out, BC250_SDMA_IB_SPLIT_MAX,
            &staleMask, &staleCount);
        CHECK(n == 0u && staleCount == 1u && staleMask == (1u << 7));
        CHECK(Bc250PagingSplitPendingCount() == 0u);

        /*
         * P4.2 (doc 622): faixas de PTE para o SDMA1. Sem a rota armada nada
         * e' registrado (as PTEs ficam no IB do SDMA0). Com a rota: faixa
         * desalinhada fica no SDMA0; faixas contiguas fundem; o FlushTlb no
         * fim da faixa NAO funde nela; a faixa sai so' no envio do buffer dela,
         * com inicio e fim exatos e sem invalidacao propria.
         */
        Bc250PagingSdma1RouteSet(FALSE);
        Bc250PagingSdma1RangeRecord(0x21E340ull + 32u, 0x21E340ull + 96u);
        CHECK(Bc250PagingSplitPendingCount() == 0u);
        Bc250PagingSdma1RouteSet(TRUE);
        Bc250PagingSdma1RangeRecord(0x21E340ull + 4u, 0x21E340ull + 96u);  /* desalinhada */
        CHECK(Bc250PagingSplitPendingCount() == 0u);
        Bc250PagingSdma1RangeRecord(0x21E340ull + 32u, 0x21E340ull + 96u);
        Bc250PagingSdma1RangeRecord(0x21E340ull + 96u, 0x21E340ull + 160u); /* contigua */
        CHECK(Bc250PagingSplitPendingCount() == 1u);
        Bc250PagingSplitRecordAt(0x21E340ull + 160u, 1u << 8);              /* flush no fim */
        CHECK(Bc250PagingSplitPendingCount() == 2u);
        n = Bc250PagingSplitTake(0x23BB80ull, 8128u, out, BC250_SDMA_IB_SPLIT_MAX,
            &staleMask, &staleCount);
        CHECK(n == 0u && Bc250PagingSplitPendingCount() == 2u);              /* outro IB */
        n = Bc250PagingSplitTake(0x21E340ull, 4096u, out, BC250_SDMA_IB_SPLIT_MAX,
            &staleMask, &staleCount);
        CHECK(n == 2u && staleCount == 0u);
        CHECK(out[0].OffsetDwords == 8u && out[0].Sdma1EndDwords == 40u &&
              out[0].VmidMask == 0u && out[0].Sdma1Token == 0u);
        CHECK(out[1].OffsetDwords == 40u && out[1].Sdma1EndDwords == 0u &&
              out[1].VmidMask == (1u << 8));
        CHECK(Bc250PagingSplitPendingCount() == 0u);
        /* faixa que sai do IB por um lado: descartada (roda no SDMA0) */
        Bc250PagingSdma1RangeRecord(0x21E340ull + 4064u, 0x21E340ull + 4160u);
        n = Bc250PagingSplitTake(0x21E340ull, 4096u, out, BC250_SDMA_IB_SPLIT_MAX,
            &staleMask, &staleCount);
        CHECK(n == 0u && Bc250PagingSplitPendingCount() == 0u);
        Bc250PagingSdma1RouteSet(FALSE);

        /*
         * LC.1 (Corrida 216, doc 589): 12 pontos de um buffer que NAO foi
         * enviado (0x21E000) nao podem recusar outro buffer. No rollback o
         * VidMm parou de enviar e a regra global recusou para sempre.
         */
        for (k = 0u; k < 12u; ++k) {
            Bc250PagingSplitRecordAt(0x21E000ull + 0x40ull + (UINT64)k * 0x40ull, 1u << 8);
        }
        CHECK(Bc250PagingSplitPendingCount() == 12u);
        /* buffer novo, vazio: progresso garantido */
        CHECK(!Bc250PagingSplitMustDefer(0x22E000ull, 0u, 12u));
        /* buffer novo ja escrito, mas sem pontos proprios */
        CHECK(!Bc250PagingSplitMustDefer(0x22E000ull, 0x800u, 12u));
        /* sem GPUVA: pontos sem posicao nao viram pedaco de IB */
        CHECK(!Bc250PagingSplitMustDefer(0ull, 0x800u, 12u));
        /* o proprio buffer cheio: ai sim, envia antes */
        CHECK(Bc250PagingSplitMustDefer(0x21E000ull, 0x340u, 12u));
        /* 11 proprios: ainda cabe */
        CHECK(!Bc250PagingSplitMustDefer(0x21E000ull, 0x2C0u, 12u));
        /* buffer vazio no MESMO endereco do cheio: nunca recusa */
        CHECK(!Bc250PagingSplitMustDefer(0x21E000ull, 0u, 12u));
        (void)Bc250PagingSplitTake(0x21E000ull, 0x10000u, out, BC250_SDMA_IB_SPLIT_MAX,
            &staleMask, &staleCount);
        CHECK(Bc250PagingSplitPendingCount() == 0u);
    }

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
