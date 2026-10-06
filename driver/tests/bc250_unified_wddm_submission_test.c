#include "../wdk/bc250_active_miniport.h"
#include "../wdk/bc250_submission_engine.h"
#include "../core/bc250_gpuva_space_contract.h"
#include "../core/bc250_mcdm_object_contract.h"

/* P1b.2: o helper Bc250VramCpuMapMemory referencia o mapeador da DRAM. Nesta
 * suite a P1b nunca e' armada (traducao identidade), entao o ramo da DRAM nao
 * roda; o stub falha fechado se um dia rodar. */
static PVOID Bc250P1bTestMapIoSpaceEx(PHYSICAL_ADDRESS PhysicalAddress, SIZE_T NumberOfBytes, ULONG Protect)
{
    UNREFERENCED_PARAMETER(PhysicalAddress);
    UNREFERENCED_PARAMETER(NumberOfBytes);
    UNREFERENCED_PARAMETER(Protect);
    return NULL;
}
static VOID Bc250P1bTestUnmapIoSpace(PVOID BaseAddress, SIZE_T NumberOfBytes)
{
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(NumberOfBytes);
}
PVOID (*__imp_MmMapIoSpaceEx)(PHYSICAL_ADDRESS, SIZE_T, ULONG) = Bc250P1bTestMapIoSpaceEx;
VOID (*__imp_MmUnmapIoSpace)(PVOID, SIZE_T) = Bc250P1bTestUnmapIoSpace;

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

#pragma function(memcpy)
void * __cdecl memcpy(void *Destination, const void *Source, size_t Length)
{
    volatile unsigned char *destination = (volatile unsigned char *)Destination;
    const volatile unsigned char *source = (const volatile unsigned char *)Source;
    size_t index;
    for (index = 0; index < Length; ++index) destination[index] = source[index];
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return __LINE__; } while (0)
#else
#include <stdio.h>

#define CHECK(Expression) do { if (!(Expression)) { printf("FAIL line %d: %s\n", __LINE__, #Expression); return __LINE__; } } while (0)
#endif

/* Stubs de SpinLock e Timer para ambiente de teste offline */
static VOID Bc250TestInitializeSpinLock(PKSPIN_LOCK SpinLock)
{
    if (SpinLock != NULL) *SpinLock = 0;
}
static KIRQL Bc250TestAcquireSpinLockRaiseToDpc(PKSPIN_LOCK SpinLock)
{
    UNREFERENCED_PARAMETER(SpinLock);
    return (KIRQL)0;
}
static VOID Bc250TestReleaseSpinLock(PKSPIN_LOCK SpinLock, KIRQL NewIrql)
{
    UNREFERENCED_PARAMETER(SpinLock);
    UNREFERENCED_PARAMETER(NewIrql);
}

VOID (*__imp_KeInitializeSpinLock)(PKSPIN_LOCK) = Bc250TestInitializeSpinLock;
KIRQL (*__imp_KeAcquireSpinLockRaiseToDpc)(PKSPIN_LOCK) = Bc250TestAcquireSpinLockRaiseToDpc;
VOID (*__imp_KeReleaseSpinLock)(PKSPIN_LOCK, KIRQL) = Bc250TestReleaseSpinLock;

static VOID Bc250TestInitializeTimer(PKTIMER Timer)
{
    if (Timer != NULL) RtlZeroMemory(Timer, sizeof(*Timer));
}
static VOID Bc250TestInitializeDpc(PKDPC Dpc, PKDEFERRED_ROUTINE Routine, PVOID DeferredContext)
{
    UNREFERENCED_PARAMETER(Routine);
    UNREFERENCED_PARAMETER(DeferredContext);
    if (Dpc != NULL) RtlZeroMemory(Dpc, sizeof(*Dpc));
}
static BOOLEAN Bc250TestSetTimerEx(PKTIMER Timer, LARGE_INTEGER DueTime, LONG Period, PKDPC Dpc)
{
    UNREFERENCED_PARAMETER(Timer);
    UNREFERENCED_PARAMETER(DueTime);
    UNREFERENCED_PARAMETER(Period);
    UNREFERENCED_PARAMETER(Dpc);
    return TRUE;
}
static BOOLEAN Bc250TestCancelTimer(PKTIMER Timer)
{
    UNREFERENCED_PARAMETER(Timer);
    return TRUE;
}

VOID (*__imp_KeInitializeTimer)(PKTIMER) = Bc250TestInitializeTimer;
VOID (*__imp_KeInitializeDpc)(PKDPC, PKDEFERRED_ROUTINE, PVOID) = Bc250TestInitializeDpc;
BOOLEAN (*__imp_KeSetTimerEx)(PKTIMER, LARGE_INTEGER, LONG, PKDPC) = Bc250TestSetTimerEx;
BOOLEAN (*__imp_KeCancelTimer)(PKTIMER) = Bc250TestCancelTimer;

/* FPS.3b: timer de alta resolucao. NULL -> o miniport usa o KTIMER. */
static PEX_TIMER Bc250TestExAllocateTimer(PEXT_CALLBACK Callback, PVOID Context, ULONG Attributes)
{
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Attributes);
    return NULL;
}
static BOOLEAN Bc250TestExSetTimer(PEX_TIMER Timer, LONGLONG DueTime, LONGLONG Period, PEXT_SET_PARAMETERS Parameters)
{
    UNREFERENCED_PARAMETER(Timer);
    UNREFERENCED_PARAMETER(DueTime);
    UNREFERENCED_PARAMETER(Period);
    UNREFERENCED_PARAMETER(Parameters);
    return FALSE;
}
static BOOLEAN Bc250TestExCancelTimer(PEX_TIMER Timer, PEXT_CANCEL_PARAMETERS Parameters)
{
    UNREFERENCED_PARAMETER(Timer);
    UNREFERENCED_PARAMETER(Parameters);
    return FALSE;
}
static BOOLEAN Bc250TestExDeleteTimer(PEX_TIMER Timer, BOOLEAN Cancel, BOOLEAN Wait, PEXT_DELETE_PARAMETERS Parameters)
{
    UNREFERENCED_PARAMETER(Timer);
    UNREFERENCED_PARAMETER(Cancel);
    UNREFERENCED_PARAMETER(Wait);
    UNREFERENCED_PARAMETER(Parameters);
    return TRUE;
}
PEX_TIMER (*__imp_ExAllocateTimer)(PEXT_CALLBACK, PVOID, ULONG) = Bc250TestExAllocateTimer;
BOOLEAN (*__imp_ExSetTimer)(PEX_TIMER, LONGLONG, LONGLONG, PEXT_SET_PARAMETERS) = Bc250TestExSetTimer;
BOOLEAN (*__imp_ExCancelTimer)(PEX_TIMER, PEXT_CANCEL_PARAMETERS) = Bc250TestExCancelTimer;
BOOLEAN (*__imp_ExDeleteTimer)(PEX_TIMER, BOOLEAN, BOOLEAN, PEXT_DELETE_PARAMETERS) = Bc250TestExDeleteTimer;

/* Estruturas de teste alocadas estaticamente para não exceder a pilha nem invocar __chkstk */
static BC250_MCDM_OBJECT_REGISTRY g_objects;
static BC250_GPUVA_SPACE g_gpuvaSpace;
static BC250_WDDM_FENCE_TRACKER g_tracker;

static int Bc250RunUnifiedWddmSubmissionTest(void)
{
    BC250_CONTEXT_SUBMISSION_VIEW view;
    BC250_MCDM_HANDLE procA = NULL, procB = NULL;
    BC250_MCDM_HANDLE devA = NULL, devB = NULL;
    BC250_MCDM_HANDLE ctxA_Compute = NULL; /* Nó 1 (Compute MEC) */
    BC250_MCDM_HANDLE ctxA_Sdma = NULL;    /* Nó 0 (SDMA) */
    BC250_MCDM_HANDLE ctxB_Compute = NULL; /* Nó 1 de outro processo */
    const void *adapterA = (const void *)(ULONG_PTR)0xAAAA1000u;
    const void *adapterB = (const void *)(ULONG_PTR)0xBBBB2000u;
    uint32_t destroyed = 0u;

    BC250_GPUVA_RANGE_RECORD r1, r2, r3, r4;
    uint64_t rId1 = 0, rId2 = 0, rId3 = 0, rId4 = 0;

    /* =========================================================================
     * INICIALIZAÇÃO DOS CONTRATOS OFFLINE
     * ========================================================================= */
    CHECK(Bc250McdmObjectRegistryInitialize(&g_objects) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250GpuvaSpaceInitialize(&g_gpuvaSpace, 48u) == BC250_GPUVA_OK);
    Bc250WddmFenceTrackerReset(&g_tracker);

    CHECK(Bc250McdmObjectCreateProcess(&g_objects, (void *)(ULONG_PTR)0x1001u, &procA) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateProcess(&g_objects, (void *)(ULONG_PTR)0x1002u, &procB) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateChild(&g_objects, BC250_MCDM_OBJECT_DEVICE, procA, procA, &devA) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateChild(&g_objects, BC250_MCDM_OBJECT_DEVICE, procB, procB, &devB) == BC250_MCDM_OBJECT_OK);

    CHECK(Bc250McdmObjectCreateContext(&g_objects, adapterA, devA, procA, 1u, 0u, &ctxA_Compute) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateContext(&g_objects, adapterA, devA, procA, 0u, 0u, &ctxA_Sdma) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateContext(&g_objects, adapterA, devB, procB, 1u, 0u, &ctxB_Compute) == BC250_MCDM_OBJECT_OK);

    /* =========================================================================
     * SEÇÃO 1: TESTES DE CONTEXTO WDDM SEGURO (14 CASOS)
     * ========================================================================= */
    /* 1.1 Contexto sem Root Page Table configurada -> Rejeitado fail-closed */
    CHECK(Bc250McdmContextAcquireSubmissionView(&g_objects, ctxA_Compute, adapterA, 1u, &view) ==
        BC250_MCDM_OBJECT_NO_ROOT_PAGE_TABLE);

    /* Configurar Root Page Table válida para procA */
    CHECK(Bc250McdmObjectSetRootPageTable(&g_objects, ctxA_Compute, 1u, 0x0000000030000000ull, 512u, 512u) ==
        BC250_MCDM_OBJECT_OK);

    /* 1.2 Contexto correto e vivo no Nó 1 (Compute) -> Sucesso com snapshot imutável */
    CHECK(Bc250McdmContextAcquireSubmissionView(&g_objects, ctxA_Compute, adapterA, 1u, &view) ==
        BC250_MCDM_OBJECT_OK);
    CHECK(view.ContextHandle == ctxA_Compute);
    CHECK(view.ParentDevice == devA);
    CHECK(view.OwnerProcess == procA);
    CHECK(view.OwnerAdapter == adapterA);
    CHECK(view.NodeOrdinal == 1u);
    CHECK(view.RootPageTableAddress == 0x0000000030000000ull);
    CHECK(view.RootPageTableEntries == 512u);

    /* 1.3 Rejeição por NodeOrdinal incorreto (Nó 0 solicitado para contexto do Nó 1) */
    CHECK(Bc250McdmContextAcquireSubmissionView(&g_objects, ctxA_Compute, adapterA, 0u, &view) ==
        BC250_MCDM_OBJECT_NODE_MISMATCH);

    /* 1.4 Sucesso no Nó 0 (SDMA) para o contexto de SDMA */
    CHECK(Bc250McdmContextAcquireSubmissionView(&g_objects, ctxA_Sdma, adapterA, 0u, &view) ==
        BC250_MCDM_OBJECT_OK);
    CHECK(view.ContextHandle == ctxA_Sdma);
    CHECK(view.NodeOrdinal == 0u);
    CHECK(Bc250McdmContextReleaseSubmissionView(&g_objects, ctxA_Sdma) == BC250_MCDM_OBJECT_OK);

    /* 1.5 Rejeição por adaptador incorreto */
    CHECK(Bc250McdmContextAcquireSubmissionView(&g_objects, ctxA_Compute, adapterB, 1u, &view) ==
        BC250_MCDM_OBJECT_OWNER_MISMATCH);

    /* 1.6 Rejeição por handle nulo */
    CHECK(Bc250McdmContextAcquireSubmissionView(&g_objects, NULL, adapterA, 1u, &view) ==
        BC250_MCDM_OBJECT_BAD_ARGUMENT);

    /* 1.7 Rejeição por handle aleatório */
    CHECK(Bc250McdmContextAcquireSubmissionView(&g_objects, (void *)(ULONG_PTR)0xDEADBEEFu, adapterA, 1u, &view) ==
        BC250_MCDM_OBJECT_INVALID_HANDLE);

    /* 1.8 Rejeição por tipo de objeto incompatível (passando handle de Device como se fosse Context) */
    CHECK(Bc250McdmContextAcquireSubmissionView(&g_objects, devA, adapterA, 1u, &view) ==
        BC250_MCDM_OBJECT_STALE_HANDLE);

    /* 1.9 Proteção contra destruição com submissões em voo (InFlightSubmissions > 0) */
    CHECK(Bc250McdmObjectDestroy(&g_objects, ctxA_Compute, BC250_MCDM_OBJECT_CONTEXT, procA) ==
        BC250_MCDM_OBJECT_IN_FLIGHT_SUBMISSIONS);

    /* 1.10 Liberação atômica da visão de submissão (InFlightSubmissions--) */
    CHECK(Bc250McdmContextReleaseSubmissionView(&g_objects, ctxA_Compute) == BC250_MCDM_OBJECT_OK);

    /* 1.11 Contexto procB sem root page table rejeitado */
    CHECK(Bc250McdmContextAcquireSubmissionView(&g_objects, ctxB_Compute, adapterA, 1u, &view) ==
        BC250_MCDM_OBJECT_NO_ROOT_PAGE_TABLE);

    /* 1.12 Destruição do contexto após término das submissões em voo */
    CHECK(Bc250McdmObjectDestroy(&g_objects, ctxB_Compute, BC250_MCDM_OBJECT_CONTEXT, procB) ==
        BC250_MCDM_OBJECT_OK);

    /* 1.13 Rejeição de handle stale após destruição */
    CHECK(Bc250McdmContextAcquireSubmissionView(&g_objects, ctxB_Compute, adapterA, 1u, &view) ==
        BC250_MCDM_OBJECT_STALE_HANDLE);

    /* 1.14 Teardown geral invalida novas aquisições */
    {
        static BC250_MCDM_OBJECT_REGISTRY tempReg;
        BC250_MCDM_HANDLE tProc, tDev, tCtx;
        CHECK(Bc250McdmObjectRegistryInitialize(&tempReg) == BC250_MCDM_OBJECT_OK);
        CHECK(Bc250McdmObjectCreateProcess(&tempReg, (void *)(ULONG_PTR)0x2001u, &tProc) == BC250_MCDM_OBJECT_OK);
        CHECK(Bc250McdmObjectCreateChild(&tempReg, BC250_MCDM_OBJECT_DEVICE, tProc, tProc, &tDev) == BC250_MCDM_OBJECT_OK);
        CHECK(Bc250McdmObjectCreateContext(&tempReg, adapterA, tDev, tProc, 1u, 0u, &tCtx) == BC250_MCDM_OBJECT_OK);
        CHECK(Bc250McdmObjectSetRootPageTable(&tempReg, tCtx, 1u, 0x0000000040000000ull, 512u, 512u) == BC250_MCDM_OBJECT_OK);
        CHECK(Bc250McdmObjectRegistryTeardown(&tempReg, &destroyed) == BC250_MCDM_OBJECT_OK);
        CHECK(Bc250McdmContextAcquireSubmissionView(&tempReg, tCtx, adapterA, 1u, &view) ==
            BC250_MCDM_OBJECT_IN_TEARDOWN);
    }

    /* =========================================================================
     * SEÇÃO 2: TESTES DE RASTREAMENTO DE GPUVA (17 CASOS)
     * ========================================================================= */
    /* Inserir faixa r1 de 16 KiB para procA: [0x000010000000, 0x000010004000) */
    memset(&r1, 0, sizeof(r1));
    r1.BaseGpuVa = 0x000010000000ull;
    r1.Size = 0x4000u;
    r1.OwnerProcess = procA;
    r1.OwnerContext = ctxA_Compute;
    r1.Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE | BC250_PTE_EXECUTABLE;
    CHECK(Bc250GpuvaSpaceInsertRange(&g_gpuvaSpace, &r1, &rId1) == BC250_GPUVA_OK);

    /* Inserir faixa r2 contígua de 16 KiB para procA: [0x000010004000, 0x000010008000) */
    memset(&r2, 0, sizeof(r2));
    r2.BaseGpuVa = 0x000010004000ull;
    r2.Size = 0x4000u;
    r2.OwnerProcess = procA;
    r2.OwnerContext = ctxA_Compute;
    r2.Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE | BC250_PTE_EXECUTABLE;
    CHECK(Bc250GpuvaSpaceInsertRange(&g_gpuvaSpace, &r2, &rId2) == BC250_GPUVA_OK);

    /* Inserir faixa r3 NÃO contígua (GAP de 4 KiB): [0x000010009000, 0x00001000D000) */
    memset(&r3, 0, sizeof(r3));
    r3.BaseGpuVa = 0x000010009000ull;
    r3.Size = 0x4000u;
    r3.OwnerProcess = procA;
    r3.OwnerContext = ctxA_Compute;
    r3.Permissions = BC250_PTE_READABLE; /* Sem WRITEABLE e sem EXECUTABLE */
    CHECK(Bc250GpuvaSpaceInsertRange(&g_gpuvaSpace, &r3, &rId3) == BC250_GPUVA_OK);

    /* Inserir faixa r4 para procB: [0x000020000000, 0x000020004000) */
    memset(&r4, 0, sizeof(r4));
    r4.BaseGpuVa = 0x000020000000ull;
    r4.Size = 0x4000u;
    r4.OwnerProcess = procB;
    r4.Permissions = BC250_PTE_READABLE;
    CHECK(Bc250GpuvaSpaceInsertRange(&g_gpuvaSpace, &r4, &rId4) == BC250_GPUVA_OK);

    /* 2.1 Faixa válida em único mapeamento */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&g_gpuvaSpace, procA, ctxA_Compute,
        0x000010000100ull, 128u, BC250_PTE_READABLE, g_gpuvaSpace.PageTableEpoch) == BC250_GPUVA_OK);

    /* 2.2 Faixa válida contígua cruzando fronteira entre r1 e r2 */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&g_gpuvaSpace, procA, ctxA_Compute,
        0x000010002000ull, 0x4000u, BC250_PTE_READABLE, g_gpuvaSpace.PageTableEpoch) == BC250_GPUVA_OK);

    /* 2.3 Endereço zero */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&g_gpuvaSpace, procA, ctxA_Compute,
        0ull, 128u, BC250_PTE_READABLE, g_gpuvaSpace.PageTableEpoch) == BC250_GPUVA_BAD_ARGUMENT);

    /* 2.4 Tamanho zero */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&g_gpuvaSpace, procA, ctxA_Compute,
        0x000010000100ull, 0u, BC250_PTE_READABLE, g_gpuvaSpace.PageTableEpoch) == BC250_GPUVA_BAD_ARGUMENT);

    /* 2.5 Tamanho não múltiplo de 4 bytes (DWORD) */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&g_gpuvaSpace, procA, ctxA_Compute,
        0x000010000100ull, 127u, BC250_PTE_READABLE, g_gpuvaSpace.PageTableEpoch) == BC250_GPUVA_MISALIGNED);

    /* 2.6 Endereço desalinhado (não múltiplo de 4 bytes) */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&g_gpuvaSpace, procA, ctxA_Compute,
        0x000010000101ull, 128u, BC250_PTE_READABLE, g_gpuvaSpace.PageTableEpoch) == BC250_GPUVA_MISALIGNED);

    /* 2.7 Overflow em Base + Size */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&g_gpuvaSpace, procA, ctxA_Compute,
        0xFFFFFFFFFFFFFFF0ull, 32u, BC250_PTE_READABLE, g_gpuvaSpace.PageTableEpoch) == BC250_GPUVA_RANGE_OVERFLOW);

    /* 2.8 Faixa acima do limite de 48 bits */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&g_gpuvaSpace, procA, ctxA_Compute,
        0x0001000000000000ull, 128u, BC250_PTE_READABLE, g_gpuvaSpace.PageTableEpoch) == BC250_GPUVA_OUT_OF_BOUNDS);

    /* 2.9 Detecção estrita de GAP entre r2 e r3 */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&g_gpuvaSpace, procA, ctxA_Compute,
        0x000010007000ull, 0x3000u, BC250_PTE_READABLE, g_gpuvaSpace.PageTableEpoch) == BC250_GPUVA_GAP_DETECTED);

    /* 2.10 Isolamento de processo (procA consultando faixa de procB) */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&g_gpuvaSpace, procA, ctxA_Compute,
        0x000020000000ull, 128u, BC250_PTE_READABLE, g_gpuvaSpace.PageTableEpoch) == BC250_GPUVA_OWNER_MISMATCH);

    /* 2.11 Permissão insuficiente (solicitando EXECUTABLE em r3 que é só READABLE) */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&g_gpuvaSpace, procA, ctxA_Compute,
        0x000010009000ull, 128u, BC250_PTE_EXECUTABLE, g_gpuvaSpace.PageTableEpoch) == BC250_GPUVA_PERMISSION_DENIED);

    /* 2.12 Page-table epoch antiga */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&g_gpuvaSpace, procA, ctxA_Compute,
        0x000010000100ull, 128u, BC250_PTE_READABLE, 0u /* stale epoch 0 */) == BC250_GPUVA_OK);

    /* 2.13 Remoção de mapeamento (Unmap de r1) */
    CHECK(Bc250GpuvaSpaceRemoveRange(&g_gpuvaSpace, procA, 0x000010000000ull, 0x4000u) == BC250_GPUVA_OK);

    /* 2.14 Proteção contra use-after-unmap -> NOT_RESIDENT */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&g_gpuvaSpace, procA, ctxA_Compute,
        0x000010000000ull, 128u, BC250_PTE_READABLE, g_gpuvaSpace.PageTableEpoch) == BC250_GPUVA_NOT_RESIDENT);

    /* 2.15 Consulta em endereço completamente não mapeado -> NOT_FOUND */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&g_gpuvaSpace, procA, ctxA_Compute,
        0x000070000000ull, 128u, BC250_PTE_READABLE, g_gpuvaSpace.PageTableEpoch) == BC250_GPUVA_NOT_FOUND);

    /* 2.16 Invalidação de faixas no teardown do contexto */
    CHECK(Bc250GpuvaSpaceInvalidateForObject(&g_gpuvaSpace, procA, ctxA_Compute, NULL) == BC250_GPUVA_OK);

    /* 2.17 Tentativa de consulta em faixa invalidada por teardown -> IN_TEARDOWN */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&g_gpuvaSpace, procA, ctxA_Compute,
        0x000010004000ull, 128u, BC250_PTE_READABLE, g_gpuvaSpace.PageTableEpoch) == BC250_GPUVA_IN_TEARDOWN);

    /* =========================================================================
     * SEÇÃO 3: TESTES DE TRANSAÇÃO E RASTREADOR DE FENCES WDDM (10 CASOS)
     * ========================================================================= */
    {
        uint32_t token1 = 0u, token2 = 0u, token3 = 0u;
        BC250_WDDM_FENCE_ENTRY drained[8];
        uint32_t drainedCount = 0u;
        uint32_t i;

        /* 3.1 Reserva prévia de capacidade */
        CHECK(Bc250WddmFenceTrackerReserve(&g_tracker, 201u, 1u, 0u, &token1) == TRUE);
        CHECK(token1 != 0u);
        CHECK(g_tracker.ReservedCount == 1u);
        CHECK(g_tracker.Count == 0u);

        /* 3.2 Rollback / Cancelamento em caso de falha de publicação no anel */
        CHECK(Bc250WddmFenceTrackerCancel(&g_tracker, token1) == TRUE);
        CHECK(g_tracker.ReservedCount == 0u);
        CHECK(g_tracker.Count == 0u);

        /* 3.3 Nova reserva e Commit atômico da fence física */
        CHECK(Bc250WddmFenceTrackerReserve(&g_tracker, 202u, 1u, 0u, &token2) == TRUE);
        CHECK(Bc250WddmFenceTrackerCommit(&g_tracker, token2, 5001ull) == TRUE);
        CHECK(g_tracker.ReservedCount == 0u);
        CHECK(g_tracker.Count == 1u);
        CHECK(g_tracker.LastPushedRingFence == 5001ull);

        /* 3.4 Rejeição de commit com fence física não-monotônica (fora de ordem) */
        CHECK(Bc250WddmFenceTrackerReserve(&g_tracker, 203u, 1u, 0u, &token3) == TRUE);
        CHECK(Bc250WddmFenceTrackerCommit(&g_tracker, token3, 5000ull) == FALSE);
        CHECK(g_tracker.OutOfOrderCount == 1u);
        CHECK(g_tracker.ReservedCount == 0u);

        /* 3.5 Dreno de fences concluídas pela GPU */
        drainedCount = Bc250WddmFenceTrackerDrain(&g_tracker, 5001ull, drained, 8);
        CHECK(drainedCount == 1u);
        CHECK(drained[0].SubmissionFenceId == 202u);
        CHECK(g_tracker.Count == 0u);

        /* 3.6 Contrapressão quando o rastreador de fences atinge a profundidade máxima */
        for (i = 0u; i < BC250_WDDM_FENCE_TRACK_DEPTH; ++i) {
            uint32_t t = 0u;
            CHECK(Bc250WddmFenceTrackerReserve(&g_tracker, 1000u + i, 1u, 0u, &t) == TRUE);
            CHECK(Bc250WddmFenceTrackerCommit(&g_tracker, t, 6000ull + (uint64_t)i) == TRUE);
        }
        CHECK(g_tracker.Count == BC250_WDDM_FENCE_TRACK_DEPTH);

        /* Tentativa de submissão adicional deve falhar por falta de capacidade (contrapressão) */
        {
            uint32_t tOverflow = 0u;
            CHECK(Bc250WddmFenceTrackerReserve(&g_tracker, 9999u, 1u, 0u, &tOverflow) == FALSE);
        }

        /* 3.7 Dreno de todo o lote para restaurar estado limpo */
        drainedCount = Bc250WddmFenceTrackerDrain(&g_tracker, 6000ull + (uint64_t)BC250_WDDM_FENCE_TRACK_DEPTH, drained, 8);
        CHECK(drainedCount == 8u);
        CHECK(g_tracker.Count == (BC250_WDDM_FENCE_TRACK_DEPTH - 8u));

        Bc250WddmFenceTrackerReset(&g_tracker);
        CHECK(g_tracker.Count == 0u && g_tracker.ReservedCount == 0u);
    }

    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess((unsigned long)Bc250RunUnifiedWddmSubmissionTest());
}
#else
int main(void)
{
    return Bc250RunUnifiedWddmSubmissionTest();
}
#endif
