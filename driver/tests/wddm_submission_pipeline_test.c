#include "../wdk/bc250_active_miniport.h"
#include "../wdk/bc250_submission_engine.h"
#include "../core/bc250_gpuva_space_contract.h"
#include "../core/bc250_mcdm_object_contract.h"

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

VOID (*__imp_KeInitializeSpinLock)(PKSPIN_LOCK) =
    Bc250TestInitializeSpinLock;
KIRQL (*__imp_KeAcquireSpinLockRaiseToDpc)(PKSPIN_LOCK) =
    Bc250TestAcquireSpinLockRaiseToDpc;
VOID (*__imp_KeReleaseSpinLock)(PKSPIN_LOCK, KIRQL) =
    Bc250TestReleaseSpinLock;

static VOID Bc250TestInitializeTimer(PKTIMER Timer)
{
    if (Timer != NULL) RtlZeroMemory(Timer, sizeof(*Timer));
}

static VOID Bc250TestInitializeDpc(PKDPC Dpc, PKDEFERRED_ROUTINE Routine,
    PVOID DeferredContext)
{
    UNREFERENCED_PARAMETER(Routine);
    UNREFERENCED_PARAMETER(DeferredContext);
    if (Dpc != NULL) RtlZeroMemory(Dpc, sizeof(*Dpc));
}

static BOOLEAN Bc250TestSetTimerEx(PKTIMER Timer, LARGE_INTEGER DueTime,
    LONG Period, PKDPC Dpc)
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
VOID (*__imp_KeInitializeDpc)(PKDPC, PKDEFERRED_ROUTINE, PVOID) =
    Bc250TestInitializeDpc;
BOOLEAN (*__imp_KeSetTimerEx)(PKTIMER, LARGE_INTEGER, LONG, PKDPC) =
    Bc250TestSetTimerEx;
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

static int Bc250RunSubmissionPipelineTest(void)
{
    static BC250_MCDM_OBJECT_REGISTRY objects;
    static BC250_GPUVA_SPACE gpuvaSpace;
    static BC250_WDDM_FENCE_TRACKER tracker;
    BC250_CONTEXT_SUBMISSION_VIEW view;

    BC250_MCDM_HANDLE procA = NULL, procB = NULL;
    BC250_MCDM_HANDLE devA = NULL, devB = NULL;
    BC250_MCDM_HANDLE ctxA1 = NULL, ctxA2 = NULL;
    const void *adapterA = (const void *)(ULONG_PTR)0xAAAA1000u;
    const void *adapterB = (const void *)(ULONG_PTR)0xBBBB2000u;

    uint64_t rangeId1 = 0, rangeId2 = 0;
    BC250_GPUVA_RANGE_RECORD r1, r2;

    /* 1. Inicialização dos Contratos */
    CHECK(Bc250McdmObjectRegistryInitialize(&objects) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250GpuvaSpaceInitialize(&gpuvaSpace, 48u) == BC250_GPUVA_OK);
    Bc250WddmFenceTrackerReset(&tracker);

    /* 2. Criação da Hierarquia de Objetos WDDM */
    CHECK(Bc250McdmObjectCreateProcess(&objects, (void *)(ULONG_PTR)0x1001u, &procA) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateProcess(&objects, (void *)(ULONG_PTR)0x1002u, &procB) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateChild(&objects, BC250_MCDM_OBJECT_DEVICE, procA, procA, &devA) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateChild(&objects, BC250_MCDM_OBJECT_DEVICE, procB, procB, &devB) == BC250_MCDM_OBJECT_OK);
    CHECK(devB != NULL);

    /* Contexto 1 no Nó 1 (Compute MEC) pertencente ao Adapter A */
    CHECK(Bc250McdmObjectCreateContext(&objects, adapterA, devA, procA, 1u, 0u, &ctxA1) == BC250_MCDM_OBJECT_OK);
    /* Contexto 2 no Nó 0 (SDMA) pertencente ao Adapter A */
    CHECK(Bc250McdmObjectCreateContext(&objects, adapterA, devA, procA, 0u, 0u, &ctxA2) == BC250_MCDM_OBJECT_OK);

    /* =========================================================================
     * PARTE 1: TESTES DE CONTEXTO WDDM SEGURO
     * ========================================================================= */
    /* Contexto sem Root Page Table configurada deve falhar closed */
    CHECK(Bc250McdmContextAcquireSubmissionView(&objects, ctxA1, adapterA, 1u, &view) ==
        BC250_MCDM_OBJECT_NO_ROOT_PAGE_TABLE);

    /* Configurar Root Page Table válida para procA */
    CHECK(Bc250McdmObjectSetRootPageTable(&objects, ctxA1, 1u, 0x0000000030000000ull, 512u, 512u) ==
        BC250_MCDM_OBJECT_OK);

    /* Aquisição bem sucedida no Nó 1 */
    CHECK(Bc250McdmContextAcquireSubmissionView(&objects, ctxA1, adapterA, 1u, &view) ==
        BC250_MCDM_OBJECT_OK);
    CHECK(view.ContextHandle == ctxA1);
    CHECK(view.ParentDevice == devA);
    CHECK(view.OwnerProcess == procA);
    CHECK(view.OwnerAdapter == adapterA);
    CHECK(view.NodeOrdinal == 1u);
    CHECK(view.RootPageTableAddress == 0x0000000030000000ull);
    CHECK(view.RootPageTableEntries == 512u);

    /* Rejeição por NodeOrdinal incorreto (submissão enviada ao nó 0 com contexto do nó 1) */
    CHECK(Bc250McdmContextAcquireSubmissionView(&objects, ctxA1, adapterA, 0u, &view) ==
        BC250_MCDM_OBJECT_NODE_MISMATCH);

    /* Aquisição bem sucedida no Nó 0 com ctxA2 */
    CHECK(Bc250McdmContextAcquireSubmissionView(&objects, ctxA2, adapterA, 0u, &view) ==
        BC250_MCDM_OBJECT_OK);
    CHECK(view.ContextHandle == ctxA2);
    CHECK(view.NodeOrdinal == 0u);
    CHECK(Bc250McdmContextReleaseSubmissionView(&objects, ctxA2) == BC250_MCDM_OBJECT_OK);

    /* Rejeição por adaptador incorreto */
    CHECK(Bc250McdmContextAcquireSubmissionView(&objects, ctxA1, adapterB, 1u, &view) ==
        BC250_MCDM_OBJECT_OWNER_MISMATCH);

    /* Rejeição de handle nulo, aleatório ou stale */
    CHECK(Bc250McdmContextAcquireSubmissionView(&objects, NULL, adapterA, 1u, &view) ==
        BC250_MCDM_OBJECT_BAD_ARGUMENT);
    CHECK(Bc250McdmContextAcquireSubmissionView(&objects, (void *)(ULONG_PTR)0xDEADBEEFu, adapterA, 1u, &view) ==
        BC250_MCDM_OBJECT_INVALID_HANDLE);

    /* Destruição concorrente impedida enquanto InFlightSubmissions > 0 */
    CHECK(Bc250McdmObjectDestroy(&objects, ctxA1, BC250_MCDM_OBJECT_CONTEXT, procA) ==
        BC250_MCDM_OBJECT_IN_FLIGHT_SUBMISSIONS);

    /* Liberação da referência de submissão */
    CHECK(Bc250McdmContextReleaseSubmissionView(&objects, ctxA1) == BC250_MCDM_OBJECT_OK);

    /* =========================================================================
     * PARTE 2: TESTES DE RASTREAMENTO E COBERTURA DE GPUVA
     * ========================================================================= */
    /* Inserir faixa mapeada de 64 KiB: [0x000020000000, 0x000020010000) */
    memset(&r1, 0, sizeof(r1));
    r1.BaseGpuVa = 0x000020000000ull;
    r1.Size = 0x10000u; /* 64 KiB */
    r1.OwnerProcess = procA;
    r1.OwnerContext = ctxA1;
    r1.Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE | BC250_PTE_EXECUTABLE;
    CHECK(Bc250GpuvaSpaceInsertRange(&gpuvaSpace, &r1, &rangeId1) == BC250_GPUVA_OK);

    /* Validação positiva: IB de 256 bytes dentro da faixa mapeada */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&gpuvaSpace, procA, ctxA1,
        0x000020001000ull, 256u, BC250_PTE_READABLE, view.PageTableEpoch) == BC250_GPUVA_OK);

    /* Falha: Endereço zero */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&gpuvaSpace, procA, ctxA1,
        0ull, 256u, BC250_PTE_READABLE, view.PageTableEpoch) == BC250_GPUVA_BAD_ARGUMENT);

    /* Falha: Tamanho zero */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&gpuvaSpace, procA, ctxA1,
        0x000020001000ull, 0u, BC250_PTE_READABLE, view.PageTableEpoch) == BC250_GPUVA_BAD_ARGUMENT);

    /* Falha: Tamanho não múltiplo de DWORD (4 bytes) */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&gpuvaSpace, procA, ctxA1,
        0x000020001000ull, 255u, BC250_PTE_READABLE, view.PageTableEpoch) == BC250_GPUVA_MISALIGNED);

    /* Falha: Endereço não alinhado em DWORD */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&gpuvaSpace, procA, ctxA1,
        0x000020001001ull, 256u, BC250_PTE_READABLE, view.PageTableEpoch) == BC250_GPUVA_MISALIGNED);

    /* Falha: Base + Tamanho com overflow de 64 bits */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&gpuvaSpace, procA, ctxA1,
        0xFFFFFFFFFFFFFFF0ull, 32u, BC250_PTE_READABLE, view.PageTableEpoch) == BC250_GPUVA_RANGE_OVERFLOW);

    /* Falha: Endereço acima da largura anunciada de 48 bits */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&gpuvaSpace, procA, ctxA1,
        0x0001000000000000ull, 256u, BC250_PTE_READABLE, view.PageTableEpoch) == BC250_GPUVA_OUT_OF_BOUNDS);

    /* Inserir faixa com GAP: [0x000020020000, 0x000020030000) (GAP de 64 KiB) */
    memset(&r2, 0, sizeof(r2));
    r2.BaseGpuVa = 0x000020020000ull;
    r2.Size = 0x10000u;
    r2.OwnerProcess = procA;
    r2.OwnerContext = ctxA1;
    r2.Permissions = BC250_PTE_READABLE;
    CHECK(Bc250GpuvaSpaceInsertRange(&gpuvaSpace, &r2, &rangeId2) == BC250_GPUVA_OK);

    /* Tentativa de submeter IB que cruza o GAP */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&gpuvaSpace, procA, ctxA1,
        0x00002000F000ull, 0x15000u, BC250_PTE_READABLE, view.PageTableEpoch) == BC250_GPUVA_GAP_DETECTED);

    /* Falha de isolamento de processo: consulta por procB */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&gpuvaSpace, procB, NULL,
        0x000020001000ull, 256u, BC250_PTE_READABLE, view.PageTableEpoch) == BC250_GPUVA_OWNER_MISMATCH);

    /* Falha de permissão: solicitando WRITEABLE em r2 que é apenas READABLE */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&gpuvaSpace, procA, ctxA1,
        0x000020021000ull, 256u, BC250_PTE_WRITEABLE, view.PageTableEpoch) == BC250_GPUVA_PERMISSION_DENIED);

    /* =========================================================================
     * PARTE 3: TESTES DE TRANSAÇÃO DO RASTREADOR DE FENCES (RESERVE/COMMIT/CANCEL)
     * ========================================================================= */
    {
        uint32_t token1 = 0u, token2 = 0u;

        /* Reserva de slot */
        CHECK(Bc250WddmFenceTrackerReserve(&tracker, 101u, 1u, 0u, &token1) == TRUE);
        CHECK(token1 != 0u);
        CHECK(tracker.ReservedCount == 1u);
        CHECK(tracker.Count == 0u);

        /* Cancelamento da reserva (simulando falha de submissão no anel) */
        CHECK(Bc250WddmFenceTrackerCancel(&tracker, token1) == TRUE);
        CHECK(tracker.ReservedCount == 0u);
        CHECK(tracker.Count == 0u);

        /* Nova reserva e Commit bem sucedido */
        CHECK(Bc250WddmFenceTrackerReserve(&tracker, 102u, 1u, 0u, &token2) == TRUE);
        CHECK(Bc250WddmFenceTrackerCommit(&tracker, token2, 1001ull) == TRUE);
        CHECK(tracker.ReservedCount == 0u);
        CHECK(tracker.Count == 1u);
        CHECK(tracker.LastPushedRingFence == 1001ull);

        /* Commit com fence não-monotônica (fora de ordem) deve falhar */
        CHECK(Bc250WddmFenceTrackerReserve(&tracker, 103u, 1u, 0u, &token1) == TRUE);
        CHECK(Bc250WddmFenceTrackerCommit(&tracker, token1, 1000ull) == FALSE);
        CHECK(tracker.OutOfOrderCount == 1u);

        /* Drenar fence aposentada */
        {
            BC250_WDDM_FENCE_ENTRY drained[4];
            uint32_t count = Bc250WddmFenceTrackerDrain(&tracker, 1001ull, drained, 4);
            CHECK(count == 1u);
            CHECK(drained[0].SubmissionFenceId == 102u);
            CHECK(tracker.Count == 0u);
        }
    }

    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess((unsigned long)Bc250RunSubmissionPipelineTest());
}
#else
int main(void)
{
    return Bc250RunSubmissionPipelineTest();
}
#endif
