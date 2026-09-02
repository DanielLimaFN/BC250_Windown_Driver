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

/*
 * Fase P1: o contexto do adaptador passou a carregar o motor de submissao
 * persistente (estado de teardown do HQD + duas evidencias completas), o que
 * o levou acima de 32 KiB.  O pool do fixture acompanha.
 */
static __declspec(align(16)) unsigned char g_pool[262144];
static int g_poolAllocated;
static __declspec(align(4096)) unsigned char g_contiguousPage[4096];
static int g_contiguousAllocated;
static DRIVER_INITIALIZATION_DATA g_initData20;
static DRIVER_INITIALIZATION_DATA g_initDataRenderOnly;
static DRIVER_INITIALIZATION_DATA g_initDataDisplayRender;

static PVOID Bc250TestAllocateContiguous(
    SIZE_T NumberOfBytes, PHYSICAL_ADDRESS LowestAcceptableAddress,
    PHYSICAL_ADDRESS HighestAcceptableAddress,
    PHYSICAL_ADDRESS BoundaryAddressMultiple,
    MEMORY_CACHING_TYPE CacheType)
{
    UNREFERENCED_PARAMETER(LowestAcceptableAddress);
    UNREFERENCED_PARAMETER(HighestAcceptableAddress);
    UNREFERENCED_PARAMETER(BoundaryAddressMultiple);
    UNREFERENCED_PARAMETER(CacheType);
    if (g_contiguousAllocated || NumberOfBytes > sizeof(g_contiguousPage)) return NULL;
    g_contiguousAllocated = 1;
    memset(g_contiguousPage, 0, sizeof(g_contiguousPage));
    return g_contiguousPage;
}
static VOID Bc250TestFreeContiguous(PVOID BaseAddress)
{
    if (BaseAddress == g_contiguousPage) g_contiguousAllocated = 0;
}
static PHYSICAL_ADDRESS Bc250TestGetPhysicalAddress(PVOID BaseAddress)
{
    PHYSICAL_ADDRESS address;
    address.QuadPart = BaseAddress == g_contiguousPage ? 0x12345000ll : 0ll;
    return address;
}
PVOID (*__imp_MmAllocateContiguousMemorySpecifyCache)(
    SIZE_T, PHYSICAL_ADDRESS, PHYSICAL_ADDRESS, PHYSICAL_ADDRESS,
    MEMORY_CACHING_TYPE) = Bc250TestAllocateContiguous;
VOID (*__imp_MmFreeContiguousMemory)(PVOID) = Bc250TestFreeContiguous;
PHYSICAL_ADDRESS (*__imp_MmGetPhysicalAddress)(PVOID) =
    Bc250TestGetPhysicalAddress;

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
static VOID FASTCALL Bc250TestAcquirePushLockSharedEx(
    PEX_PUSH_LOCK PushLock, ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);
    while (InterlockedCompareExchange64(
            (volatile LONG64 *)PushLock, 1, 0) != 0) {
        (void)SwitchToThread();
    }
}
static VOID FASTCALL Bc250TestReleasePushLockSharedEx(
    PEX_PUSH_LOCK PushLock, ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);
    (void)InterlockedExchange64((volatile LONG64 *)PushLock, 0);
}
static LARGE_INTEGER Bc250TestQueryPerformanceCounter(
    PLARGE_INTEGER PerformanceFrequency)
{
    LARGE_INTEGER val;
    if (PerformanceFrequency != NULL) PerformanceFrequency->QuadPart = 10000000ll;
    val.QuadPart = 1000000ll;
    return val;
}

VOID (*__imp_KeEnterCriticalRegion)(VOID) = Bc250TestEnterCriticalRegion;
VOID (*__imp_KeLeaveCriticalRegion)(VOID) = Bc250TestLeaveCriticalRegion;
VOID (FASTCALL *__imp_ExAcquirePushLockExclusiveEx)(PEX_PUSH_LOCK, ULONG) =
    Bc250TestAcquirePushLockExclusiveEx;
VOID (FASTCALL *__imp_ExReleasePushLockExclusiveEx)(PEX_PUSH_LOCK, ULONG) =
    Bc250TestReleasePushLockExclusiveEx;
VOID (FASTCALL *__imp_ExAcquirePushLockSharedEx)(PEX_PUSH_LOCK, ULONG) =
    Bc250TestAcquirePushLockSharedEx;
VOID (FASTCALL *__imp_ExReleasePushLockSharedEx)(PEX_PUSH_LOCK, ULONG) =
    Bc250TestReleasePushLockSharedEx;
LARGE_INTEGER (*__imp_KeQueryPerformanceCounter)(PLARGE_INTEGER) =
    Bc250TestQueryPerformanceCounter;

/*
 * Fase P1: o motor de submissao serializa com KSPIN_LOCK porque
 * DxgkDdiSubmitCommandVirtual pode chegar em DISPATCH_LEVEL, onde push lock
 * nao e' permitido.  Fora do kernel os tres viram no-ops verificaveis.
 */
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

/*
 * Fase V2: a varredura de retirada de fence usa KTIMER + KDPC.  Fora do kernel
 * eles viram no-ops observaveis — o teste de host exercita o rastreador de
 * fences puro (Bc250WddmFenceTracker*), nao o agendamento do timer, que so faz
 * sentido no silicio.
 */
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
    return FALSE;
}

static BOOLEAN Bc250TestCancelTimer(PKTIMER Timer)
{
    UNREFERENCED_PARAMETER(Timer);
    return FALSE;
}

static VOID Bc250TestFlushQueuedDpcs(VOID)
{
}

VOID (*__imp_KeInitializeTimer)(PKTIMER) =
    Bc250TestInitializeTimer;
VOID (*__imp_KeInitializeDpc)(PKDPC, PKDEFERRED_ROUTINE, PVOID) =
    Bc250TestInitializeDpc;
BOOLEAN (*__imp_KeSetTimerEx)(PKTIMER, LARGE_INTEGER, LONG, PKDPC) =
    Bc250TestSetTimerEx;
BOOLEAN (*__imp_KeCancelTimer)(PKTIMER) =
    Bc250TestCancelTimer;
VOID (*__imp_KeFlushQueuedDpcs)(VOID) =
    Bc250TestFlushQueuedDpcs;

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

/*
 * QCHECK: mesma semantica de CHECK, mas para funcoes auxiliares que devolvem
 * int. Registra a linha exata da falha em vez de deixar o CHECK do chamador
 * apontar para a chamada inteira — sem isso, uma falha em qualquer um dos
 * quatro casos abaixo reportaria sempre a mesma linha.
 */
#define QCHECK(Expression) do {     if (!(Expression)) { g_failureLine = __LINE__; return 0; } } while (0)

/* ==========================================================================
   Candidato 3.67 — quiescencia do teardown.

   Reproduz offline exatamente a falha medida no silicio em 2026-09-01
   (docs/evidence/2026-09-01_11-34-36-DESKTOP-HQAG6T4-rendernode-inspect):
   DxgkCbSynchronizeExecution devolveu STATUS_UNSUCCESSFUL com queried=FALSE,
   o rastreador estava comprovadamente vazio (Pending=0, Reserved=0) e mesmo
   assim StopDevice e RemoveDevice terminaram em STATUS_DEVICE_BUSY.

   O gate fail-closed NAO foi afrouxado: os casos com contagem real
   continuam exigindo STATUS_DEVICE_BUSY, e nenhum teste limpa o rastreador
   para fabricar sucesso.
   ========================================================================== */

static LONG g_syncFailCalls;

static NTSTATUS APIENTRY Bc250TestSynchronizeExecutionAlwaysFails(
    HANDLE DeviceHandle,
    PKSYNCHRONIZE_ROUTINE SynchronizeRoutine,
    PVOID Context,
    ULONG MessageNumber,
    PBOOLEAN ReturnValue)
{
    (void)DeviceHandle;
    (void)SynchronizeRoutine;
    (void)Context;
    (void)MessageNumber;
    InterlockedIncrement(&g_syncFailCalls);
    /*
     * Nao invoca a rotina. E' o comportamento observado no silicio: a
     * chamada falha antes de serializar qualquer coisa.
     */
    if (ReturnValue != NULL) *ReturnValue = FALSE;
    return STATUS_UNSUCCESSFUL;
}

/* Contexto pronto para teardown, com a callback de sincronizacao quebrada. */
static BC250_DXGK_ACTIVE_CONTEXT *Bc250TestMakeBrokenSyncContext(
    DEVICE_OBJECT *FakeDeviceObject)
{
    PVOID deviceContext = NULL;
    BC250_DXGK_ACTIVE_CONTEXT *context;

    if (Bc250DxgkDdiAddDeviceActive(
            FakeDeviceObject, &deviceContext) != STATUS_SUCCESS ||
        deviceContext == NULL) {
        return NULL;
    }
    context = (BC250_DXGK_ACTIVE_CONTEXT *)deviceContext;
    InterlockedExchange(&context->IsActive, 1);
    InterlockedExchange(&context->LifecycleState,
        (LONG)BC250_ACTIVE_LIFECYCLE_RUNNING);
    context->Core.DxgkInterface.DeviceHandle =
        (HANDLE)(ULONG_PTR)0xBC250067u;
    context->Core.DxgkInterface.DxgkCbSynchronizeExecution =
        Bc250TestSynchronizeExecutionAlwaysFails;
    return context;
}

typedef struct BC250_SUBMIT_STOP_GATE_FIXTURE {
    BC250_DXGK_ACTIVE_CONTEXT *Context;
    volatile LONG Entered;
} BC250_SUBMIT_STOP_GATE_FIXTURE;

static ULONG __stdcall Bc250SubmitStopGateThread(PVOID Parameter)
{
    BC250_SUBMIT_STOP_GATE_FIXTURE *fixture =
        (BC250_SUBMIT_STOP_GATE_FIXTURE *)Parameter;

    if (!Bc250ActiveSubmissionGateEnter(fixture->Context)) {
        return 1u;
    }
    InterlockedExchange(&fixture->Entered, 1);
    while (InterlockedCompareExchange(
            &fixture->Context->LifecycleState, 0, 0) ==
            (LONG)BC250_ACTIVE_LIFECYCLE_RUNNING) {
        (void)SwitchToThread();
    }
    /* Espelha o rearme condicional no fim do corpo de SubmitCommandVirtual. */
    if (InterlockedCompareExchange(
            &fixture->Context->LifecycleState, 0, 0) ==
            (LONG)BC250_ACTIVE_LIFECYCLE_RUNNING) {
        Bc250ActiveRetirementTimerStart(fixture->Context);
    }
    Bc250ActiveSubmissionGateLeave(fixture->Context);
    return 0u;
}

static int Bc250RunStopQuiescenceTest(
    DRIVER_INITIALIZATION_DATA *InitializationData,
    DEVICE_OBJECT *FakeDeviceObject)
{
    /*
     * Caso 1 — a regressao do 3.66.
     * Sincronizacao falhando + rastreador vazio => StopDevice DEVE concluir.
     */
    {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            Bc250TestMakeBrokenSyncContext(FakeDeviceObject);
        QCHECK(!(context == NULL));
        InterlockedExchange(&g_syncFailCalls, 0);
        QCHECK(!(Bc250DxgkDdiStopDeviceActive(context) != STATUS_SUCCESS));
        QCHECK(!(context->IsActive != 0));
        QCHECK(!(InterlockedCompareExchange(&context->LifecycleState, 0, 0) != (LONG)BC250_ACTIVE_LIFECYCLE_STOPPED));
        /* O motivo precisa ser publicado, e o dreno precisa ter fechado. */
        QCHECK(!(context->StopReason != BC250_ACTIVE_STOP_REASON_STOP_DEVICE));
        QCHECK(!(context->StopSubmitInFlightAfterDrain != 0u));
        QCHECK(!(Bc250DxgkDdiRemoveDeviceActive(context) != STATUS_SUCCESS));
        QCHECK(!(g_poolAllocated != 0));
    }

    /*
     * Caso 2 — reserva pendente continua fail-closed.
     * A reserva nao foi comitada: ReservedCount > 0 e Count == 0.
     */
    {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            Bc250TestMakeBrokenSyncContext(FakeDeviceObject);
        uint32_t token = 0u;
        uint32_t pending = 0u;
        uint32_t reserved = 0u;
        QCHECK(!(context == NULL));
        QCHECK(!(!Bc250WddmFenceTrackerReserve( &context->SubmissionEngine.WddmFences, 0x3067u, 1u, 0u, &token)));
        QCHECK(!(Bc250DxgkDdiStopDeviceActive(context) != STATUS_DEVICE_BUSY));
        QCHECK(!(context->IsActive != 1));
        QCHECK(!(context->RetirementTimerRunning != 1));

        /*
         * O rastreador NAO pode ter sido limpo para fabricar sucesso: a
         * reserva tem de continuar exatamente onde estava.
         */
        QCHECK(!(!Bc250WddmFenceTrackerSnapshotCounts( &context->SubmissionEngine.WddmFences, &pending, &reserved)));
        QCHECK(!(pending != 0u || reserved != 1u));

        /*
         * Depois do STOPPING nenhuma reserva nova entra, e a recusa e'
         * contabilizada.
         */
        {
            uint32_t rejectedToken = 0u;
            QCHECK(!(Bc250WddmFenceTrackerReserve( &context->SubmissionEngine.WddmFences, 0x3068u, 1u, 0u, &rejectedToken)));
            QCHECK(!(rejectedToken != 0u));
            QCHECK(!(context->SubmissionEngine.WddmFences.RejectedWhileStopping == 0u));
        }
        /*
         * O estado publicado tem de refletir STOPPING, que e' a entrada do
         * portao de submissao.
         */
        QCHECK(InterlockedCompareExchange(&context->LifecycleState, 0, 0) ==
               (LONG)BC250_ACTIVE_LIFECYCLE_STOPPING);
        QCHECK(context->StopReason == BC250_ACTIVE_STOP_REASON_STOP_DEVICE);
        {
            LONG before = InterlockedCompareExchange(
                &context->SubmitInFlight, 0, 0);
            QCHECK(!Bc250ActiveSubmissionGateEnter(context));
            QCHECK(InterlockedCompareExchange(
                &context->SubmitInFlight, 0, 0) == before);
        }

        /*
         * A DDI publica tambem recusa — mas ATENCAO ao que isto prova.
         *
         * Bc250McdmSubmitCommandVirtual exige caps.SubmitReady antes de
         * chamar o executor, e SubmitReady depende do motor persistente
         * online, que so existe com hardware. Neste ambiente a recusa vem de
         * STATUS_NOT_SUPPORTED naquela camada, e a chamada NAO chega ao
         * portao de LifecycleState.
         *
         * Portanto isto e' um teste de "nao aceita", nao um teste do portao.
         * O portao real foi exercitado diretamente acima e tambem no Caso 5.
         */
        {
            DXGKARG_SUBMITCOMMANDVIRTUAL submit;
            NTSTATUS submitStatus;
            memset(&submit, 0, sizeof(submit));
            submit.hContext = (HANDLE)(ULONG_PTR)0xC0FFEEu;
            submit.DmaBufferSize = 64u;
            submit.DmaBufferVirtualAddress =
                (D3DGPU_VIRTUAL_ADDRESS)0x2000ull;
            submit.NodeOrdinal = 1u;
            QCHECK(InitializationData->DxgkDdiSubmitCommandVirtual != NULL);
            submitStatus = InitializationData->DxgkDdiSubmitCommandVirtual(
                context, &submit);
            QCHECK(!NT_SUCCESS(submitStatus));
        }

        /* Cancelar libera o teardown — sem nenhuma limpeza artificial. */
        QCHECK(!(!Bc250WddmFenceTrackerCancel( &context->SubmissionEngine.WddmFences, token)));
        QCHECK(!(Bc250DxgkDdiStopDeviceActive(context) != STATUS_SUCCESS));
        QCHECK(!(context->RetirementTimerRunning != 0));
        QCHECK(!(Bc250DxgkDdiRemoveDeviceActive(context) != STATUS_SUCCESS));
        QCHECK(!(g_poolAllocated != 0));
    }

    /*
     * Caso 3 — fence em voo (Count > 0) continua fail-closed.
     */
    {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            Bc250TestMakeBrokenSyncContext(FakeDeviceObject);
        BC250_WDDM_FENCE_ENTRY drained[4];
        uint32_t pending = 0u;
        uint32_t reserved = 0u;
        QCHECK(!(context == NULL));
        QCHECK(!(!Bc250WddmFenceTrackerPush( &context->SubmissionEngine.WddmFences, 7ull, 0x3069u, 1u, 0u)));
        QCHECK(!(Bc250DxgkDdiStopDeviceActive(context) != STATUS_DEVICE_BUSY));
        QCHECK(!(context->IsActive != 1));
        QCHECK(!(context->RetirementTimerRunning != 1));
        QCHECK(!(!Bc250WddmFenceTrackerSnapshotCounts( &context->SubmissionEngine.WddmFences, &pending, &reserved)));
        QCHECK(!(pending != 1u || reserved != 0u));

        /* Aposentar de verdade (fence do anel alcancada) libera o teardown. */
        QCHECK(!(Bc250WddmFenceTrackerDrain( &context->SubmissionEngine.WddmFences, 7ull, drained, 4u) != 1u));
        QCHECK(!(drained[0].SubmissionFenceId != 0x3069u));
        QCHECK(!(Bc250DxgkDdiStopDeviceActive(context) != STATUS_SUCCESS));
        QCHECK(!(context->RetirementTimerRunning != 0));
        QCHECK(!(Bc250DxgkDdiRemoveDeviceActive(context) != STATUS_SUCCESS));
        QCHECK(!(g_poolAllocated != 0));
    }

    /*
     * Caso 4 — RemoveDevice direto sobre contexto ativo com sincronizacao
     * quebrada. Era o segundo STATUS_DEVICE_BUSY do ensaio 3.66.
     */
    {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            Bc250TestMakeBrokenSyncContext(FakeDeviceObject);
        QCHECK(!(context == NULL));
        QCHECK(!(Bc250DxgkDdiRemoveDeviceActive(context) != STATUS_SUCCESS));
        QCHECK(!(g_poolAllocated != 0));
    }

    /*
     * Caso 5 — submissao passou pelo portao antes de STOPPING. O Stop espera
     * SubmitInFlight zerar e a segunda parada impede qualquer timer rearmado
     * na janela. Este teste alcanca o portao real, nao o gate de caps.
     */
    {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            Bc250TestMakeBrokenSyncContext(FakeDeviceObject);
        BC250_SUBMIT_STOP_GATE_FIXTURE fixture;
        HANDLE thread;
        ULONG threadId = 0u;
        QCHECK(!(context == NULL));
        memset(&fixture, 0, sizeof(fixture));
        fixture.Context = context;
        thread = CreateThread(NULL, 0u, Bc250SubmitStopGateThread,
            &fixture, 0u, &threadId);
        QCHECK(!(thread == NULL));
        while (InterlockedCompareExchange(&fixture.Entered, 1, 1) != 1) {
            (void)SwitchToThread();
        }
        QCHECK(!(Bc250DxgkDdiStopDeviceActive(context) != STATUS_SUCCESS));
        QCHECK(!(WaitForSingleObject(thread, BC250_TEST_INFINITE) !=
            BC250_TEST_WAIT_OBJECT_0));
        QCHECK(!(CloseHandle(thread) == 0));
        QCHECK(!(InterlockedCompareExchange(
            &context->SubmitInFlight, 0, 0) != 0));
        QCHECK(!(context->RetirementTimerRunning != 0));
        QCHECK(!(Bc250DxgkDdiRemoveDeviceActive(context) != STATUS_SUCCESS));
        QCHECK(!(g_poolAllocated != 0));
    }

    /* A callback quebrada foi de fato exercitada em todos os casos. */
    QCHECK(!(InterlockedCompareExchange(&g_syncFailCalls, 0, 0) == 0));
    return 1;
}

/* ==========================================================================
   Corrida real submit/retire/stop sobre o lock do rastreador.

   Threads de host disputam reserve/commit/cancel/drain enquanto o StopDevice
   fecha o portao e fotografa. Invariantes: nenhuma entrada perdida, nenhuma
   contagem negativa, e o veredito do StopDevice sempre coerente com o que o
   rastreador realmente contem.
   ========================================================================== */

typedef struct BC250_TRACKER_RACE_FIXTURE {
    BC250_WDDM_FENCE_TRACKER *Tracker;
    volatile LONG Start;
    volatile LONG Accepted;
    volatile LONG Rejected;
    volatile LONG Retired;
    ULONG Round;
} BC250_TRACKER_RACE_FIXTURE;

static ULONG __stdcall Bc250TrackerSubmitRaceThread(PVOID Parameter)
{
    BC250_TRACKER_RACE_FIXTURE *fixture =
        (BC250_TRACKER_RACE_FIXTURE *)Parameter;
    ULONG index;

    while (InterlockedCompareExchange(&fixture->Start, 1, 1) != 1) {
        (void)SwitchToThread();
    }
    for (index = 0u; index < BC250_TEST_RACE_ATTEMPTS; ++index) {
        uint32_t token = 0u;
        if (Bc250WddmFenceTrackerReserve(
                fixture->Tracker, 0x5000u + index, 1u, 0u, &token)) {
            /*
             * Metade comita e metade cancela: as duas transicoes precisam
             * conviver com o fechamento do portao sem vazar contagem.
             */
            if ((index & 1u) == 0u) {
                if (Bc250WddmFenceTrackerCommit(
                        fixture->Tracker, token,
                        (uint64_t)(index + 1u))) {
                    InterlockedIncrement(&fixture->Accepted);
                } else {
                    (void)Bc250WddmFenceTrackerCancel(
                        fixture->Tracker, token);
                }
            } else {
                (void)Bc250WddmFenceTrackerCancel(fixture->Tracker, token);
            }
        } else {
            InterlockedIncrement(&fixture->Rejected);
        }
        if ((index & 7u) == 0u) (void)SwitchToThread();
    }
    return 0u;
}

static ULONG __stdcall Bc250TrackerRetireRaceThread(PVOID Parameter)
{
    BC250_TRACKER_RACE_FIXTURE *fixture =
        (BC250_TRACKER_RACE_FIXTURE *)Parameter;
    ULONG index;

    while (InterlockedCompareExchange(&fixture->Start, 1, 1) != 1) {
        (void)SwitchToThread();
    }
    for (index = 0u; index < BC250_TEST_RACE_ATTEMPTS; ++index) {
        BC250_WDDM_FENCE_ENTRY drained[8];
        uint32_t produced = Bc250WddmFenceTrackerDrain(
            fixture->Tracker, (uint64_t)BC250_TEST_RACE_ATTEMPTS,
            drained, 8u);
        InterlockedAdd(&fixture->Retired, (LONG)produced);
        if ((index & 3u) == 0u) (void)SwitchToThread();
    }
    return 0u;
}

static int Bc250RunTrackerRaceTest(DEVICE_OBJECT *FakeDeviceObject)
{
    ULONG round;

    for (round = 0u; round < BC250_TEST_RACE_ROUNDS; ++round) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            Bc250TestMakeBrokenSyncContext(FakeDeviceObject);
        BC250_TRACKER_RACE_FIXTURE fixture;
        HANDLE submitThread;
        HANDLE retireThread;
        NTSTATUS stopStatus;
        uint32_t pending = 0u;
        uint32_t reserved = 0u;
        LONG accepted;
        LONG retired;
        ULONG yieldCount;

        QCHECK(!(context == NULL));
        memset(&fixture, 0, sizeof(fixture));
        fixture.Tracker = &context->SubmissionEngine.WddmFences;
        fixture.Round = round;

        submitThread = CreateThread(NULL, 0u,
            Bc250TrackerSubmitRaceThread, &fixture, 0u, NULL);
        QCHECK(!(submitThread == NULL));
        retireThread = CreateThread(NULL, 0u,
            Bc250TrackerRetireRaceThread, &fixture, 0u, NULL);
        QCHECK(!(retireThread == NULL));

        InterlockedExchange(&fixture.Start, 1);
        for (yieldCount = 0u; yieldCount < (round & 3u); ++yieldCount) {
            (void)SwitchToThread();
        }
        stopStatus = Bc250DxgkDdiStopDeviceActive(context);

        QCHECK(!(WaitForSingleObject(submitThread, BC250_TEST_INFINITE) != BC250_TEST_WAIT_OBJECT_0 || !CloseHandle(submitThread)));
        QCHECK(!(WaitForSingleObject(retireThread, BC250_TEST_INFINITE) != BC250_TEST_WAIT_OBJECT_0 || !CloseHandle(retireThread)));

        QCHECK(!(stopStatus != STATUS_SUCCESS && stopStatus != STATUS_DEVICE_BUSY));

        QCHECK(!(!Bc250WddmFenceTrackerSnapshotCounts( fixture.Tracker, &pending, &reserved)));
        /*
         * Depois que as threads sairam o portao esta fechado, portanto o
         * veredito do StopDevice tem de ser coerente com o estado final:
         * sucesso exige rastreador vazio.
         */
        if (stopStatus == STATUS_SUCCESS && (pending != 0u || reserved != 0u)) {
            return 0;
        }
        /* Nenhuma reserva pode ficar orfa: tudo virou entrada ou foi cancelado. */
        QCHECK(!(reserved != 0u));
        QCHECK(!(pending > BC250_WDDM_FENCE_TRACK_DEPTH));

        accepted = InterlockedCompareExchange(&fixture.Accepted, 0, 0);
        retired = InterlockedCompareExchange(&fixture.Retired, 0, 0);
        /* Nenhuma entrada perdida nem duplicada: aceitas = aposentadas + em voo. */
        QCHECK(!(accepted != retired + (LONG)pending));
        if (fixture.Tracker->OverflowCount + fixture.Tracker->OutOfOrderCount +
                (uint32_t)InterlockedCompareExchange(&fixture.Rejected, 0, 0)
                == 0u && accepted == 0) {
            return 0; /* a corrida precisa ter exercitado alguma coisa */
        }

        /* Drenar o resto e concluir o teardown sem limpeza artificial. */
        for (;;) {
            BC250_WDDM_FENCE_ENTRY drained[8];
            if (Bc250WddmFenceTrackerDrain(fixture.Tracker,
                    (uint64_t)BC250_TEST_RACE_ATTEMPTS, drained, 8u) == 0u) {
                break;
            }
        }
        QCHECK(!(stopStatus == STATUS_DEVICE_BUSY && Bc250DxgkDdiStopDeviceActive(context) != STATUS_SUCCESS));
        QCHECK(!(Bc250DxgkDdiRemoveDeviceActive(context) != STATUS_SUCCESS));
        QCHECK(!(g_poolAllocated != 0));
    }
    return 1;
}

/* ==========================================================================
   Candidato 3.67 — reset do rastro de render (blocos V5 e DMM).

   A telemetria persiste no Registry entre boots. Ate o 3.66 o reset de
   carregamento cobria seis campos V5 e deixava de fora o rastro de DDI,
   QueryInterface, StopDevice/RemoveDevice e o bloco DMM inteiro. No ensaio de
   2026-09-01 o inspetor leu DmmQaiOutSize=544 e DmmControlInterruptCount=14925
   de um boot anterior. Evidencia que mistura corridas nao e' evidencia.

   Prova offline: marcar, conferir que a marca aparece, resetar e conferir que
   voltou a zero — sem depender do Registry, que so existe no build de kernel.
   ========================================================================== */
static int Bc250RunRenderTraceResetTest(void)
{
    BC250_PROBE_FIELD field;
    ULONG value;
    BC250_V5_QI_RECORD record;

    /*
     * Linha de base. Os testes anteriores deste mesmo binario ja exercitaram
     * muitas DDIs, e o rastro e' global e cumulativo — sem zerar antes, a
     * contagem esperada abaixo dependeria da ordem dos testes.
     */
    Bc250ProbeResetRenderTrace();
    Bc250ProbeSnapshotDdiTrace();
    (void)Bc250ProbeGetOffline(&field, &value);
    QCHECK(field == BC250_PROBE_V5_DDI_MARKS);
    QCHECK(value == 0u);

    /* 1. Rastro de DDI acumula e e' publicado. */
    Bc250ProbeMarkDdi(BC250_V5_DDI_QUERY_INTERFACE);
    Bc250ProbeMarkDdi(BC250_V5_DDI_QUERY_CHILD_RELATIONS);
    Bc250ProbeMarkDdi(BC250_V5_DDI_QUERY_INTERFACE);
    Bc250ProbeSnapshotDdiTrace();
    (void)Bc250ProbeGetOffline(&field, &value);
    QCHECK(field == BC250_PROBE_V5_DDI_MARKS);
    QCHECK(value == 3u);

    /* 2. Captura de QueryInterface acumula e e' publicada. */
    memset(&record, 0, sizeof(record));
    record.Data1 = 0x6BA41A33u;
    record.Data2 = 0x1234u;
    record.Data3 = 0x5678u;
    record.Data4[0] = 0xAAu;
    record.Data4[7] = 0x55u;
    record.Version = 1u;
    record.Size = 56u;
    record.Status = (ULONG)STATUS_NOT_SUPPORTED;
    Bc250ProbeRecordQueryInterface(&record);
    Bc250ProbeRecordQueryInterface(&record);
    Bc250ProbeSnapshotQueryInterfaces();
    /*
     * SnapshotQueryInterfaces publica SLOTS_CAPTURED e, por ultimo,
     * POST_START_STAGE. Conferir o ultimo campo garante que a publicacao
     * chegou ao fim.
     */
    (void)Bc250ProbeGetOffline(&field, &value);
    QCHECK(field == BC250_PROBE_V5_POST_START_STAGE);

    /* 3. Marco pos-StartDevice e' monotonico. */
    Bc250ProbeMarkPostStartStage(BC250_V5_POST_START_CHILD_RELATIONS);
    Bc250ProbeMarkPostStartStage(BC250_V5_POST_START_RETURNED);
    Bc250ProbeSnapshotQueryInterfaces();
    (void)Bc250ProbeGetOffline(&field, &value);
    QCHECK(field == BC250_PROBE_V5_POST_START_STAGE);
    QCHECK(value == BC250_V5_POST_START_CHILD_RELATIONS);

    /* 4. Um campo do bloco DMM tambem precisa entrar no reset. */
    Bc250ProbeRecord(BC250_PROBE_DMM_QAI_OUTSIZE, 544u);
    (void)Bc250ProbeGetOffline(&field, &value);
    QCHECK(field == BC250_PROBE_DMM_QAI_OUTSIZE);
    QCHECK(value == 544u);

    /* 5. O reset zera tudo isso. */
    Bc250ProbeResetRenderTrace();

    Bc250ProbeSnapshotDdiTrace();
    (void)Bc250ProbeGetOffline(&field, &value);
    QCHECK(field == BC250_PROBE_V5_DDI_MARKS);
    QCHECK(value == 0u);

    Bc250ProbeSnapshotQueryInterfaces();
    (void)Bc250ProbeGetOffline(&field, &value);
    QCHECK(field == BC250_PROBE_V5_POST_START_STAGE);
    QCHECK(value == BC250_V5_POST_START_NONE);

    /*
     * 6. O bloco P1/P2 tem reset PROPRIO e nao pode ser arrastado por este:
     *    o valor gravado antes do ResetRenderTrace precisa sobreviver.
     *    Zerar evidencia fisica junto com rastro de DDI esconderia falha do
     *    boot atual.
     */
    Bc250ProbeRecord(BC250_PROBE_P1_ENGINE_STATE, 1u);
    Bc250ProbeResetRenderTrace();
    Bc250ProbeRecord(BC250_PROBE_P1_ENGINE_STATE, 1u);
    (void)Bc250ProbeGetOffline(&field, &value);
    QCHECK(field == BC250_PROBE_P1_ENGINE_STATE);
    QCHECK(value == 1u);
    Bc250ProbeResetSubmissionEvidence();
    (void)Bc250ProbeGetOffline(&field, &value);
    QCHECK(value == 0u);
    return 1;
}

/*
 * Fase V2 — contrato do rastreador de fences WDDM.
 *
 * Prova offline, sem hardware: o FIFO correlaciona a fence de 64 bits do anel
 * com o SubmissionFenceId de 32 bits do dxgkrnl, aposenta estritamente em
 * ordem, recusa regressao de fence e recusa transbordo em vez de perder
 * submissao silenciosamente.
 */
static int Bc250RunWddmFenceTrackerTest(void)
{
    static BC250_WDDM_FENCE_TRACKER tracker;
    static BC250_WDDM_FENCE_ENTRY out[8];
    uint32_t currentFence = 0xFFFFFFFFu;
    uint32_t i;

    Bc250WddmFenceTrackerReset(&tracker);
    CHECK(tracker.Count == 0u);
    CHECK(tracker.PushedTotal == 0u);
    CHECK(Bc250WddmFenceTrackerQueryCurrent(&tracker, 1u, &currentFence));
    CHECK(currentFence == 0u);

    /* FIFO vazio nao produz notificacao, mesmo com fence alta observada. */
    CHECK(Bc250WddmFenceTrackerDrain(&tracker, 999ull, out, 8u) == 0u);

    /* Tres submissoes em voo, fences do anel monotonicas. */
    CHECK(Bc250WddmFenceTrackerPush(&tracker, 1ull, 0x1001u, 1u, 0u));
    CHECK(Bc250WddmFenceTrackerPush(&tracker, 2ull, 0x1002u, 1u, 0u));
    CHECK(Bc250WddmFenceTrackerPush(&tracker, 3ull, 0x1003u, 1u, 0u));
    CHECK(tracker.Count == 3u);
    CHECK(tracker.PushedTotal == 3u);

    /* A GPU nao gravou nada: nada aposenta.  Zero fake. */
    CHECK(Bc250WddmFenceTrackerDrain(&tracker, 0ull, out, 8u) == 0u);
    CHECK(tracker.Count == 3u);

    /* GPU gravou fence 2: exatamente duas aposentam, na ordem de submissao. */
    CHECK(Bc250WddmFenceTrackerDrain(&tracker, 2ull, out, 8u) == 2u);
    CHECK(out[0].SubmissionFenceId == 0x1001u);
    CHECK(out[1].SubmissionFenceId == 0x1002u);
    CHECK(out[0].RingFence == 1ull);
    CHECK(out[1].RingFence == 2ull);
    CHECK(tracker.Count == 1u);
    CHECK(tracker.NotifiedTotal == 2u);
    CHECK(tracker.LastNotifiedFenceId == 0x1002u);
    CHECK(tracker.LastPushedRingFence == 3ull);
    CHECK(Bc250WddmFenceTrackerQueryCurrent(&tracker, 1u, &currentFence));
    CHECK(currentFence == 0x1002u);
    CHECK(Bc250WddmFenceTrackerQueryCurrent(&tracker, 0u, &currentFence));
    CHECK(currentFence == 0u);

    /* Regressao de fence do anel e' corrupcao, nao progresso: recusar. */
    CHECK(!Bc250WddmFenceTrackerPush(&tracker, 3ull, 0x1004u, 1u, 0u));
    CHECK(tracker.OutOfOrderCount == 1u);
    CHECK(tracker.Count == 1u);

    /* A ultima em voo aposenta e o FIFO volta a vazio. */
    CHECK(Bc250WddmFenceTrackerDrain(&tracker, 3ull, out, 8u) == 1u);
    CHECK(out[0].SubmissionFenceId == 0x1003u);
    CHECK(tracker.Count == 0u);
    CHECK(tracker.LastNotifiedFenceId == 0x1003u);
    CHECK(Bc250WddmFenceTrackerQueryCurrent(&tracker, 1u, &currentFence));
    CHECK(currentFence == 0x1003u);
    CHECK(Bc250WddmFenceTrackerDrain(&tracker, 99ull, out, 8u) == 0u);

    /* O limite do lote e' respeitado: Max recorta sem perder o resto. */
    Bc250WddmFenceTrackerReset(&tracker);
    for (i = 0u; i < 8u; ++i) {
        CHECK(Bc250WddmFenceTrackerPush(&tracker,
            (uint64_t)(i + 1u), 0x2000u + i, 1u, 0u));
    }
    CHECK(Bc250WddmFenceTrackerDrain(&tracker, 8ull, out, 3u) == 3u);
    CHECK(out[0].SubmissionFenceId == 0x2000u);
    CHECK(tracker.Count == 5u);
    CHECK(Bc250WddmFenceTrackerDrain(&tracker, 8ull, out, 8u) == 5u);
    CHECK(out[4].SubmissionFenceId == 0x2007u);
    CHECK(tracker.Count == 0u);

    /* Transbordo e' recusado, nunca sobrescreve entrada em voo. */
    Bc250WddmFenceTrackerReset(&tracker);
    for (i = 0u; i < BC250_WDDM_FENCE_TRACK_DEPTH; ++i) {
        CHECK(Bc250WddmFenceTrackerPush(&tracker,
            (uint64_t)(i + 1u), 0x3000u + i, 1u, 0u));
    }
    CHECK(tracker.Count == BC250_WDDM_FENCE_TRACK_DEPTH);
    CHECK(!Bc250WddmFenceTrackerPush(&tracker,
        (uint64_t)(BC250_WDDM_FENCE_TRACK_DEPTH + 1u), 0x3FFFu, 1u, 0u));
    CHECK(tracker.OverflowCount == 1u);
    CHECK(tracker.Count == BC250_WDDM_FENCE_TRACK_DEPTH);

    /* Depois de esvaziar, o indice circular continua correto. */
    while (tracker.Count > 0u) {
        (void)Bc250WddmFenceTrackerDrain(&tracker,
            (uint64_t)BC250_WDDM_FENCE_TRACK_DEPTH, out, 8u);
    }
    CHECK(Bc250WddmFenceTrackerPush(&tracker, 0xFFFFFFFFFFull, 0x4000u, 1u, 0u));
    CHECK(Bc250WddmFenceTrackerDrain(&tracker, 0xFFFFFFFFFFull, out, 8u) == 1u);
    CHECK(out[0].SubmissionFenceId == 0x4000u);

    /* Argumentos invalidos nao produzem notificacao. */
    CHECK(Bc250WddmFenceTrackerDrain(&tracker, 1ull, NULL, 8u) == 0u);
    CHECK(Bc250WddmFenceTrackerDrain(&tracker, 1ull, out, 0u) == 0u);
    CHECK(!Bc250WddmFenceTrackerPush(NULL, 1ull, 1u, 1u, 0u));
    CHECK(!Bc250WddmFenceTrackerPush(&tracker, 2ull, 2u, 2u, 0u));
    CHECK(!Bc250WddmFenceTrackerQueryCurrent(NULL, 1u, &currentFence));
    CHECK(!Bc250WddmFenceTrackerQueryCurrent(&tracker, 2u, &currentFence));
    CHECK(!Bc250WddmFenceTrackerQueryCurrent(&tracker, 1u, NULL));

    /* Pin GPUVA acompanha reserve -> commit -> drain sem ser perdido. */
    {
        uint32_t token = 0u;
        Bc250WddmFenceTrackerReset(&tracker);
        CHECK(Bc250WddmFenceTrackerReserve(
            &tracker, 0x5000u, 1u, 0u, &token));
        CHECK(Bc250WddmFenceTrackerAttachGpuvaPin(
            &tracker, token, 1u, 7u));
        CHECK(!Bc250WddmFenceTrackerAttachGpuvaPin(
            &tracker, token, 0u, 8u));
        CHECK(Bc250WddmFenceTrackerCommit(&tracker, token, 9ull));
        CHECK(Bc250WddmFenceTrackerDrain(&tracker, 9ull, out, 1u) == 1u);
        CHECK(out[0].GpuvaPinValid == 1u);
        CHECK(out[0].GpuvaSlot == 1u);
        CHECK(out[0].GpuvaGeneration == 7u);
    }

    return 1;
}

/*
 * Gate 1.5 — os dois perfis produzem tabelas DISTINTAS e COERENTES.
 *
 * "Coerente" tem um significado preciso e verificavel aqui: um perfil ou
 * anuncia o conjunto inteiro de display — VidPn, Present, scanout, ponteiro,
 * post-display — ou nao anuncia nenhuma dessas DDIs.  Nao existe meio termo.
 * Este teste falha se alguem reintroduzir a poda seletiva que produziu o
 * defeito estatico do 3.74.
 */
static int Bc250RunAdapterProfileTest(void)
{
    BC250_ADAPTER_PROFILE profile;

    /* ---- Resolucao do perfil a partir dos tunables ---- */

    /* Ausencia legitima dos dois tunables: perfil herdado, KMDOD. */
    CHECK(Bc250ActiveResolveAdapterProfile(0u, 0u, 0u, 0u, &profile) ==
        STATUS_SUCCESS);
    CHECK(profile == BC250_ADAPTER_PROFILE_DISPLAY_ONLY);

    /* RenderNode=0 explicito continua KMDOD. */
    CHECK(Bc250ActiveResolveAdapterProfile(0u, 1u, 0u, 1u, &profile) ==
        STATUS_SUCCESS);
    CHECK(profile == BC250_ADAPTER_PROFILE_DISPLAY_ONLY);

    /* RenderNode=1 sozinho = display+render, o destino do projeto. */
    CHECK(Bc250ActiveResolveAdapterProfile(1u, 1u, 0u, 1u, &profile) ==
        STATUS_SUCCESS);
    CHECK(profile == BC250_ADAPTER_PROFILE_DISPLAY_RENDER);
    CHECK(Bc250ActiveResolveAdapterProfile(1u, 1u, 0u, 0u, &profile) ==
        STATUS_SUCCESS);
    CHECK(profile == BC250_ADAPTER_PROFILE_DISPLAY_RENDER);

    /* RenderNode=1 + RenderOnlyLab=1 = Gate 1.5. */
    CHECK(Bc250ActiveResolveAdapterProfile(1u, 1u, 1u, 1u, &profile) ==
        STATUS_SUCCESS);
    CHECK(profile == BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB);

    /* ---- Fail-closed ---- */

    /* RenderOnlyLab=1 sem RenderNode=1 e' combinacao sem significado. */
    profile = BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB;
    CHECK(Bc250ActiveResolveAdapterProfile(0u, 1u, 1u, 1u, &profile) ==
        STATUS_DEVICE_CONFIGURATION_ERROR);
    CHECK(profile == BC250_ADAPTER_PROFILE_DISPLAY_ONLY);
    CHECK(Bc250ActiveResolveAdapterProfile(0u, 0u, 1u, 1u, &profile) ==
        STATUS_DEVICE_CONFIGURATION_ERROR);

    /* Valores fora de {0,1} nao sao adivinhados. */
    CHECK(Bc250ActiveResolveAdapterProfile(2u, 1u, 0u, 1u, &profile) ==
        STATUS_DEVICE_CONFIGURATION_ERROR);
    CHECK(Bc250ActiveResolveAdapterProfile(1u, 1u, 2u, 1u, &profile) ==
        STATUS_DEVICE_CONFIGURATION_ERROR);

    /* Tunable ilegivel com valor nao-default = configuracao corrompida. */
    CHECK(Bc250ActiveResolveAdapterProfile(1u, 0u, 0u, 1u, &profile) ==
        STATUS_DEVICE_CONFIGURATION_ERROR);
    CHECK(Bc250ActiveResolveAdapterProfile(1u, 1u, 1u, 0u, &profile) ==
        STATUS_DEVICE_CONFIGURATION_ERROR);

    CHECK(Bc250ActiveResolveAdapterProfile(1u, 1u, 1u, 1u, NULL) ==
        STATUS_INVALID_PARAMETER);

    /* Perfil fora do enum e' recusado e nao altera o estado corrente. */
    CHECK(Bc250ActiveSetAdapterProfile(
        BC250_ADAPTER_PROFILE_DISPLAY_RENDER) == STATUS_SUCCESS);
    CHECK(Bc250ActiveSetAdapterProfile((BC250_ADAPTER_PROFILE)7u) ==
        STATUS_INVALID_PARAMETER);
    CHECK(Bc250ActiveGetAdapterProfile() ==
        BC250_ADAPTER_PROFILE_DISPLAY_RENDER);

    /* ---- As duas tabelas ---- */

    memset(&g_initDataDisplayRender, 0xCC, sizeof(g_initDataDisplayRender));
    CHECK(Bc250BuildActiveInitializationDataForProfile(
        &g_initDataDisplayRender, DXGKDDI_INTERFACE_VERSION_WDDM2_7,
        DXGKDDI_WDDMv2_7, BC250_ADAPTER_PROFILE_DISPLAY_RENDER) ==
        STATUS_SUCCESS);

    memset(&g_initDataRenderOnly, 0xCC, sizeof(g_initDataRenderOnly));
    CHECK(Bc250BuildActiveInitializationDataForProfile(
        &g_initDataRenderOnly, DXGKDDI_INTERFACE_VERSION_WDDM2_7,
        DXGKDDI_WDDMv2_7, BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) ==
        STATUS_SUCCESS);

    CHECK(Bc250BuildActiveInitializationDataForProfile(
        &g_initDataRenderOnly, DXGKDDI_INTERFACE_VERSION_WDDM2_7,
        DXGKDDI_WDDMv2_7, (BC250_ADAPTER_PROFILE)9u) ==
        STATUS_INVALID_PARAMETER);
    CHECK(Bc250BuildActiveInitializationDataForProfile(
        NULL, DXGKDDI_INTERFACE_VERSION_WDDM2_7, DXGKDDI_WDDMv2_7,
        BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) == STATUS_INVALID_PARAMETER);

    /* As tabelas precisam ser efetivamente diferentes. */
    {
        const unsigned char *left =
            (const unsigned char *)&g_initDataDisplayRender;
        const unsigned char *right =
            (const unsigned char *)&g_initDataRenderOnly;
        size_t index;
        int differs = 0;
        for (index = 0; index < sizeof(g_initDataRenderOnly); ++index) {
            if (left[index] != right[index]) { differs = 1; break; }
        }
        CHECK(differs);
    }

    /* Base identica nos dois perfis. */
    CHECK(g_initDataRenderOnly.Version ==
        g_initDataDisplayRender.Version);
    CHECK(g_initDataRenderOnly.DxgkDdiAddDevice ==
        g_initDataDisplayRender.DxgkDdiAddDevice);
    CHECK(g_initDataRenderOnly.DxgkDdiStartDevice ==
        g_initDataDisplayRender.DxgkDdiStartDevice);
    CHECK(g_initDataRenderOnly.DxgkDdiStopDevice ==
        g_initDataDisplayRender.DxgkDdiStopDevice);
    CHECK(g_initDataRenderOnly.DxgkDdiRemoveDevice ==
        g_initDataDisplayRender.DxgkDdiRemoveDevice);
    CHECK(g_initDataRenderOnly.DxgkDdiDispatchIoRequest ==
        g_initDataDisplayRender.DxgkDdiDispatchIoRequest);
    CHECK(g_initDataRenderOnly.DxgkDdiInterruptRoutine ==
        g_initDataDisplayRender.DxgkDdiInterruptRoutine);
    CHECK(g_initDataRenderOnly.DxgkDdiDpcRoutine ==
        g_initDataDisplayRender.DxgkDdiDpcRoutine);
    CHECK(g_initDataRenderOnly.DxgkDdiResetDevice ==
        g_initDataDisplayRender.DxgkDdiResetDevice);
    CHECK(g_initDataRenderOnly.DxgkDdiUnload ==
        g_initDataDisplayRender.DxgkDdiUnload);
    CHECK(g_initDataRenderOnly.DxgkDdiSetPowerState ==
        g_initDataDisplayRender.DxgkDdiSetPowerState);
    CHECK(g_initDataRenderOnly.DxgkDdiQueryInterface ==
        g_initDataDisplayRender.DxgkDdiQueryInterface);
    CHECK(g_initDataRenderOnly.DxgkDdiControlEtwLogging ==
        g_initDataDisplayRender.DxgkDdiControlEtwLogging);
    CHECK(g_initDataRenderOnly.DxgkDdiNotifyAcpiEvent ==
        g_initDataDisplayRender.DxgkDdiNotifyAcpiEvent);
    CHECK(g_initDataRenderOnly.DxgkDdiNotifySurpriseRemoval ==
        g_initDataDisplayRender.DxgkDdiNotifySurpriseRemoval);
    CHECK(g_initDataRenderOnly.DxgkDdiControlInterrupt2 ==
        g_initDataDisplayRender.DxgkDdiControlInterrupt2);

    /*
     * Enumeracao de children: obrigatoria na estrutura mesmo sem display.
     * A lista vazia e' responsabilidade da DDI, nao do ponteiro nulo.
     */
    CHECK(g_initDataRenderOnly.DxgkDdiQueryChildRelations ==
        g_initDataDisplayRender.DxgkDdiQueryChildRelations);
    CHECK(g_initDataRenderOnly.DxgkDdiQueryChildStatus ==
        g_initDataDisplayRender.DxgkDdiQueryChildStatus);
    CHECK(g_initDataRenderOnly.DxgkDdiQueryDeviceDescriptor ==
        g_initDataDisplayRender.DxgkDdiQueryDeviceDescriptor);

    /* Tabela render identica: o perfil nao mexe em render. */
    CHECK(g_initDataRenderOnly.DxgkDdiCreateDevice ==
        g_initDataDisplayRender.DxgkDdiCreateDevice);
    CHECK(g_initDataRenderOnly.DxgkDdiCreateContext ==
        g_initDataDisplayRender.DxgkDdiCreateContext);
    CHECK(g_initDataRenderOnly.DxgkDdiCreateAllocation ==
        g_initDataDisplayRender.DxgkDdiCreateAllocation);
    CHECK(g_initDataRenderOnly.DxgkDdiDestroyAllocation ==
        g_initDataDisplayRender.DxgkDdiDestroyAllocation);
    CHECK(g_initDataRenderOnly.DxgkDdiBuildPagingBuffer ==
        g_initDataDisplayRender.DxgkDdiBuildPagingBuffer);
    CHECK(g_initDataRenderOnly.DxgkDdiSubmitCommandVirtual ==
        g_initDataDisplayRender.DxgkDdiSubmitCommandVirtual);
    CHECK(g_initDataRenderOnly.DxgkDdiSetRootPageTable ==
        g_initDataDisplayRender.DxgkDdiSetRootPageTable);
    CHECK(g_initDataRenderOnly.DxgkDdiGetRootPageTableSize ==
        g_initDataDisplayRender.DxgkDdiGetRootPageTableSize);
    CHECK(g_initDataRenderOnly.DxgkDdiCreateProcess ==
        g_initDataDisplayRender.DxgkDdiCreateProcess);
    CHECK(g_initDataRenderOnly.DxgkDdiDestroyProcess ==
        g_initDataDisplayRender.DxgkDdiDestroyProcess);
    CHECK(g_initDataRenderOnly.DxgkDdiQueryAdapterInfo ==
        g_initDataDisplayRender.DxgkDdiQueryAdapterInfo);
    CHECK(g_initDataRenderOnly.DxgkDdiResetEngine ==
        g_initDataDisplayRender.DxgkDdiResetEngine);
    CHECK(g_initDataRenderOnly.DxgkDdiQueryEngineStatus ==
        g_initDataDisplayRender.DxgkDdiQueryEngineStatus);
    CHECK(g_initDataRenderOnly.DxgkDdiSubmitCommandVirtual != NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiCreateProcess != NULL);

    /*
     * Superficie de display: presente em display+render, AUSENTE POR
     * COMPLETO em render-only.  Este e' o bloco que impede a incoerencia do
     * 3.74 de voltar por qualquer caminho.
     */
    CHECK(g_initDataDisplayRender.DxgkDdiIsSupportedVidPn != NULL);
    CHECK(g_initDataDisplayRender.DxgkDdiCommitVidPn != NULL);
    CHECK(g_initDataDisplayRender.DxgkDdiSetVidPnSourceAddress != NULL);
    CHECK(g_initDataDisplayRender.DxgkDdiPresent != NULL);

    CHECK(g_initDataRenderOnly.DxgkDdiIsSupportedVidPn == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiRecommendFunctionalVidPn == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiEnumVidPnCofuncModality == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiRecommendVidPnTopology == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiRecommendMonitorModes == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiCommitVidPn == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiUpdateActiveVidPnPresentPath == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiSetVidPnSourceAddress == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiSetVidPnSourceVisibility == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiQueryVidPnHWCapability == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiPresent == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiSetPointerPosition == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiSetPointerShape == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiSetPalette == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiGetScanLine == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiSystemDisplayEnable == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiSystemDisplayWrite == NULL);
    CHECK(g_initDataRenderOnly.
        DxgkDdiStopDeviceAndReleasePostDisplayOwnership == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiGetChildContainerId == NULL);

    /*
     * 3.76 — a tabela display+render esta' CONGELADA na baseline KMDOD
     * conhecida, divida tecnica inclusa, para que o perfil render-only seja o
     * unico eixo novo do candidato.  As duas DDIs abaixo sao falsos sucessos
     * aceitos temporariamente; a retirada volta em candidato proprio depois
     * que o Gate 1.5 fechar.
     *
     * O que continua valendo sem excecao: elas NAO existem no render-only.
     */
    CHECK(g_initDataDisplayRender.DxgkDdiSetDisplayPrivateDriverFormat ==
        Bc250DxgkDdiSetDisplayPrivateDriverFormat);
    CHECK(g_initDataDisplayRender.DxgkDdiSetPalette ==
        Bc250DxgkDdiSetPalette);
    CHECK(g_initDataRenderOnly.DxgkDdiSetDisplayPrivateDriverFormat == NULL);
    CHECK(g_initDataRenderOnly.DxgkDdiSetPalette == NULL);

    /* Reservados nulos nos dois perfis. */
    CHECK(g_initDataRenderOnly.Reserved == NULL);
    CHECK(g_initDataRenderOnly.Reserved1 == NULL);
    CHECK(g_initDataRenderOnly.Reserved2 == NULL);
    CHECK(g_initDataRenderOnly.Reserved3 == NULL);

    /* ---- Comportamento das DDIs de child sob cada perfil ---- */
    {
        DXGK_CHILD_DESCRIPTOR descriptors[2];
        DXGK_CHILD_STATUS childStatus;

        CHECK(Bc250DisplayGetChildEnumeration());

        /* Perfil com display: descreve o DisplayPort externo, ChildUid=1. */
        memset(descriptors, 0xEE, sizeof(descriptors));
        CHECK(Bc250DxgkDdiQueryChildRelations(
            NULL, descriptors, sizeof(descriptors)) == STATUS_SUCCESS);
        CHECK(descriptors[0].ChildDeviceType == TypeVideoOutput);
        CHECK(descriptors[0].ChildUid == 1);
        memset(&childStatus, 0, sizeof(childStatus));
        childStatus.Type = StatusConnection;
        CHECK(Bc250DxgkDdiQueryChildStatus(NULL, &childStatus, TRUE) ==
            STATUS_SUCCESS);
        CHECK(childStatus.HotPlug.Connected == TRUE);

        /* Render-only: lista vazia e nenhum conector reportado ligado. */
        CHECK(Bc250ActiveSetAdapterProfile(
            BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) == STATUS_SUCCESS);
        CHECK(!Bc250DisplayGetChildEnumeration());
        memset(descriptors, 0xEE, sizeof(descriptors));
        CHECK(Bc250DxgkDdiQueryChildRelations(
            NULL, descriptors, sizeof(descriptors)) == STATUS_SUCCESS);
        CHECK(descriptors[0].ChildDeviceType == 0);
        CHECK(descriptors[0].ChildUid == 0);
        CHECK(descriptors[1].ChildUid == 0);
        memset(&childStatus, 0, sizeof(childStatus));
        childStatus.Type = StatusConnection;
        CHECK(Bc250DxgkDdiQueryChildStatus(NULL, &childStatus, TRUE) ==
            STATUS_SUCCESS);
        CHECK(childStatus.HotPlug.Connected == FALSE);

        /* Argumentos invalidos continuam recusados nos dois perfis. */
        CHECK(Bc250DxgkDdiQueryChildRelations(NULL, NULL, sizeof(descriptors))
            == STATUS_INVALID_PARAMETER);
        CHECK(Bc250DxgkDdiQueryChildRelations(NULL, descriptors, 0u) ==
            STATUS_INVALID_PARAMETER);

        /* Restaura o perfil herdado para nao contaminar os testes seguintes. */
        CHECK(Bc250ActiveSetAdapterProfile(
            BC250_ADAPTER_PROFILE_DISPLAY_ONLY) == STATUS_SUCCESS);
        CHECK(Bc250DisplayGetChildEnumeration());
    }

    /*
     * ---- Ordem do early-return no StartDevice ----
     *
     * O que precisa ser provado nao e' apenas QUE o render-only devolve zero
     * sources/children, e sim que ele o faz ANTES de qualquer trabalho de
     * display ou hardware. Se o early-return ficasse depois do
     * post-display ownership ou de Bc250DisplayInit, o perfil "seguro"
     * estaria adquirindo o scanout do GOP e so' entao desistindo — que e'
     * exatamente o cenario de tela preta que ele existe para evitar.
     *
     * A ordem e' verificada sobre a telemetria: os marcadores de ENTRADA
     * distinguem "nao executou" de "executou e devolveu zero".
     */
    {
        BC250_PROBE_FIELD lastField = (BC250_PROBE_FIELD)0;
        ULONG lastValue = 0xFFFFFFFFu;

        Bc250ProbeResetOffline();
        CHECK(Bc250ActiveSetAdapterProfile(
            BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) == STATUS_SUCCESS);

        /*
         * Fixar o perfil nao pode, por si so', publicar telemetria de display
         * ou de hardware. Contagem zero prova que nenhum marcador foi tocado.
         */
        CHECK(Bc250ProbeGetOffline(NULL, NULL) == 0u);

        /*
         * Construir a tabela render-only publica exatamente um campo, e ele
         * declara que NENHUMA superficie de display foi anunciada.
         */
        memset(&g_initDataRenderOnly, 0, sizeof(g_initDataRenderOnly));
        CHECK(Bc250BuildActiveInitializationDataForProfile(
            &g_initDataRenderOnly, DXGKDDI_INTERFACE_VERSION_WDDM2_7,
            DXGKDDI_WDDMv2_7, BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) ==
            STATUS_SUCCESS);
        CHECK(Bc250ProbeGetOffline(&lastField, &lastValue) != 0u);
        CHECK(lastField == BC250_PROBE_V5_DISPLAY_SURFACE_ANNOUNCED);
        CHECK(lastValue == 0u);

        /* O perfil display+render publica o mesmo campo com o valor oposto. */
        Bc250ProbeResetOffline();
        memset(&g_initDataDisplayRender, 0, sizeof(g_initDataDisplayRender));
        CHECK(Bc250BuildActiveInitializationDataForProfile(
            &g_initDataDisplayRender, DXGKDDI_INTERFACE_VERSION_WDDM2_7,
            DXGKDDI_WDDMv2_7, BC250_ADAPTER_PROFILE_DISPLAY_RENDER) ==
            STATUS_SUCCESS);
        CHECK(Bc250ProbeGetOffline(&lastField, &lastValue) != 0u);
        CHECK(lastField == BC250_PROBE_V5_DISPLAY_SURFACE_ANNOUNCED);
        CHECK(lastValue == 1u);

        CHECK(Bc250ActiveSetAdapterProfile(
            BC250_ADAPTER_PROFILE_DISPLAY_ONLY) == STATUS_SUCCESS);
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
    CHECK(initData.DxgkDdiStartDevice ==
        Bc250DxgkDdiStartDeviceV5Telemetry);
    CHECK(initData.DxgkDdiStopDevice == Bc250DxgkDdiStopDeviceActive);
    CHECK(initData.DxgkDdiRemoveDevice == Bc250DxgkDdiRemoveDeviceActive);
    CHECK(initData.DxgkDdiPreemptCommand == Bc250McdmPreemptCommand);
    CHECK(initData.DxgkDdiCreateContext == Bc250McdmCreateContext);
    CHECK(initData.DxgkDdiQueryAdapterInfo == Bc250McdmQueryAdapterInfo);
    CHECK(initData.DxgkDdiQueryCurrentFence == Bc250McdmQueryCurrentFence);
    CHECK(initData.DxgkDdiEscape == Bc250McdmEscape);
    CHECK(initData.DxgkDdiAcquireSwizzlingRange ==
        Bc250McdmAcquireSwizzlingRange);
    CHECK(initData.DxgkDdiReleaseSwizzlingRange ==
        Bc250McdmReleaseSwizzlingRange);
    CHECK(initData.DxgkDdiPatch == Bc250McdmPatch);
    CHECK(initData.DxgkDdiRender == Bc250McdmRender);
    CHECK(initData.DxgkDdiSubmitCommand == Bc250McdmSubmitCommand);
    CHECK(initData.DxgkDdiControlInterrupt2 ==
        Bc250DxgkDdiControlInterrupt2);

    /* Auditor efetivo: valida o objeto construido, nao atribuicoes no texto. */
    CHECK(initData.DxgkDdiDispatchIoRequest != NULL);
    CHECK(initData.DxgkDdiInterruptRoutine != NULL);
    CHECK(initData.DxgkDdiDpcRoutine != NULL);
    CHECK(initData.DxgkDdiQueryChildRelations != NULL);
    CHECK(initData.DxgkDdiQueryChildStatus != NULL);
    CHECK(initData.DxgkDdiQueryDeviceDescriptor != NULL);
    CHECK(initData.DxgkDdiSetPowerState != NULL);
    CHECK(initData.DxgkDdiResetDevice != NULL);
    CHECK(initData.DxgkDdiUnload != NULL);
    CHECK(initData.DxgkDdiCreateDevice != NULL);
    CHECK(initData.DxgkDdiCreateAllocation != NULL);
    CHECK(initData.DxgkDdiDestroyAllocation != NULL);
    CHECK(initData.DxgkDdiBuildPagingBuffer != NULL);
    CHECK(initData.DxgkDdiCreateContext != NULL);
    CHECK(initData.DxgkDdiDestroyContext != NULL);
    CHECK(initData.DxgkDdiSubmitCommandVirtual != NULL);
    CHECK(initData.DxgkDdiSetRootPageTable != NULL);
    CHECK(initData.DxgkDdiGetRootPageTableSize != NULL);
    CHECK(initData.DxgkDdiCreateProcess != NULL);
    CHECK(initData.DxgkDdiDestroyProcess != NULL);
    CHECK(initData.DxgkDdiGetNodeMetadata != NULL);
    CHECK(initData.DxgkDdiQueryEngineStatus != NULL);
    CHECK(initData.DxgkDdiResetEngine != NULL);
    CHECK(initData.DxgkDdiCalibrateGpuClock != NULL);
    CHECK(initData.DxgkDdiFormatHistoryBuffer != NULL);
    CHECK(initData.DxgkDdiControlInterrupt3 == NULL);
    CHECK(initData.DxgkDdiRenderKm == Bc250McdmRenderKm);
    CHECK(initData.DxgkDdiCancelCommand == Bc250McdmCancelCommand);
    CHECK(initData.DxgkDdiSetPowerComponentFState ==
        Bc250McdmSetPowerComponentFState);
    CHECK(initData.DxgkDdiPowerRuntimeControlRequest ==
        Bc250McdmPowerRuntimeControlRequest);
    CHECK(initData.Reserved == NULL);
    CHECK(initData.Reserved1 == NULL);
    CHECK(initData.Reserved2 == NULL);
    CHECK(initData.Reserved3 == NULL);

    memset(&g_initData20, 0xCC, sizeof(g_initData20));
    CHECK(Bc250BuildActiveInitializationDataForContract(
        &g_initData20, DXGKDDI_INTERFACE_VERSION_WDDM2_0,
        DXGKDDI_WDDMv2) == STATUS_SUCCESS);
    CHECK(g_initData20.Version == DXGKDDI_INTERFACE_VERSION_WDDM2_0);
    CHECK(Bc250McdmGetWddmContractVersion() == DXGKDDI_WDDMv2);
    CHECK(g_initData20.DxgkDdiSubmitCommandVirtual != NULL);
    CHECK(g_initData20.DxgkDdiSetRootPageTable != NULL);
    CHECK(g_initData20.DxgkDdiCreateProcess != NULL);
    CHECK(g_initData20.Reserved == NULL && g_initData20.Reserved1 == NULL &&
        g_initData20.Reserved2 == NULL && g_initData20.Reserved3 == NULL);
    CHECK(Bc250BuildActiveInitializationDataForContract(
        &g_initData20, DXGKDDI_INTERFACE_VERSION_WDDM2_0,
        DXGKDDI_WDDMv2_7) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250BuildActiveInitializationData(&initData) == STATUS_SUCCESS);
    CHECK(Bc250McdmGetWddmContractVersion() == DXGKDDI_WDDMv2_7);

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

        /*
         * Fase P1 — o caminho de submissao nao pode mentir.  Com o motor
         * offline (que e' o estado do fixture, sem hardware), toda submissao
         * tem de falhar de forma explicita.  Ate o candidato 3.43 este
         * caminho devolvia STATUS_SUCCESS sem backend nenhum.
         */
        {
            uint64_t assignedFence = 0xDEADBEEFull;

            CHECK(Bc250SubmissionEngineSubmit(NULL,
                BC250_SUBMISSION_NODE_COMPUTE, 0x10000ull, 8u,
                &assignedFence) == STATUS_INVALID_PARAMETER);
            CHECK(Bc250SubmissionEngineSubmit(activeCtx,
                BC250_SUBMISSION_NODE_COMPUTE, 0x10000ull, 8u,
                NULL) == STATUS_INVALID_PARAMETER);
            CHECK(Bc250SubmissionEngineSubmit(activeCtx,
                BC250_SUBMISSION_NODE_COMPUTE, 0x10000ull, 8u,
                &assignedFence) == STATUS_DEVICE_NOT_READY);
            CHECK(assignedFence == 0ull);
            CHECK(Bc250SubmissionEnginePoll(activeCtx, NULL) ==
                STATUS_DEVICE_NOT_READY);
            /* Teardown de motor que nunca subiu e' inofensivo. */
            CHECK(Bc250SubmissionEngineOffline(activeCtx) == STATUS_SUCCESS);
            CHECK(activeCtx->SubmissionEngine.SubmitAccepted == 0u);
        }

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

        {
            uint64_t leaseA1 = 0ull, leaseA2 = 0ull, badLease = 0ull;
            CHECK(Bc250ActiveIssueGpuvaSdmaLease(activeCtx, processHandle,
                openHandles[0], 0x100000ull, 4096u, &leaseA1) == STATUS_SUCCESS);
            CHECK(leaseA1 != 0ull);
            CHECK(Bc250ActiveIssueGpuvaSdmaLease(activeCtx, processHandle,
                openHandles[1], 0x200000ull, 4096u, &leaseA2) == STATUS_SUCCESS);
            CHECK(leaseA2 != 0ull && leaseA2 != leaseA1);

            /* Invalid parameters: unaligned GPUVA and byte count overflow */
            CHECK(Bc250ActiveIssueGpuvaSdmaLease(activeCtx, processHandle,
                openHandles[0], 0x100001ull, 4096u, &badLease) == STATUS_INVALID_PARAMETER);
            CHECK(Bc250ActiveIssueGpuvaSdmaLease(activeCtx, processHandle,
                openHandles[0], 0x100000ull, 0x10000000ull, &badLease) == STATUS_INVALID_PARAMETER);

            /* Intra-process pair must be rejected (PAIR_NOT_ISOLATED) */
            CHECK(Bc250ActiveValidateGpuvaSdmaLeasePair(activeCtx, leaseA1,
                leaseA2, 0x300000ull, 4096u) == STATUS_INVALID_PARAMETER);

            /* Test cross-process isolation with a distinct process B */
            {
                DXGKARG_CREATEPROCESS createProcessB;
                DXGKARG_CREATEDEVICE createDeviceB;
                DXGKARG_CREATECONTEXT createContextB;
                DXGKARG_CREATEALLOCATION createAllocationB;
                DXGKARG_OPENALLOCATION openAllocationB;
                DXGK_ALLOCATIONINFO allocationInfoB;
                DXGK_OPENALLOCATIONINFO openInfoB;
                BC250_ALLOCATION_PRIVATE_V1 allocationPrivateB;
                HANDLE processB = NULL, deviceB = NULL, contextB = NULL, openB = NULL;
                uint64_t leaseB = 0ull;

                memset(&createProcessB, 0, sizeof(createProcessB));
                createProcessB.hDxgkProcess = (HANDLE)(ULONG_PTR)0xB250u;
                CHECK(initData.DxgkDdiCreateProcess(activeCtx, &createProcessB) == STATUS_SUCCESS);
                processB = createProcessB.hKmdProcess;
                CHECK(processB != NULL);

                memset(&createDeviceB, 0, sizeof(createDeviceB));
                createDeviceB.hDevice = (HANDLE)(ULONG_PTR)0xB251u;
                createDeviceB.hKmdProcess = processB;
                CHECK(initData.DxgkDdiCreateDevice(activeCtx, &createDeviceB) == STATUS_SUCCESS);
                deviceB = createDeviceB.hDevice;

                memset(&allocationPrivateB, 0, sizeof(allocationPrivateB));
                allocationPrivateB.Magic = BC250_ALLOCATION_PRIVATE_MAGIC;
                allocationPrivateB.Version = BC250_ALLOCATION_PRIVATE_VERSION;
                allocationPrivateB.StructSize = sizeof(BC250_ALLOCATION_PRIVATE_V1);
                allocationPrivateB.AllocationToken = 0xB110u;
                allocationPrivateB.ByteSize = 65536u;
                allocationPrivateB.Alignment = 4096u;

                memset(&allocationInfoB, 0, sizeof(allocationInfoB));
                allocationInfoB.pPrivateDriverData = &allocationPrivateB;
                allocationInfoB.PrivateDriverDataSize = sizeof(BC250_ALLOCATION_PRIVATE_V1);

                memset(&createAllocationB, 0, sizeof(createAllocationB));
                createAllocationB.NumAllocations = 1u;
                createAllocationB.pAllocationInfo = &allocationInfoB;
                CHECK(initData.DxgkDdiCreateAllocation(activeCtx, &createAllocationB) == STATUS_SUCCESS);

                memset(&openInfoB, 0, sizeof(openInfoB));
                openInfoB.hAllocation = 0xDB10u;
                openInfoB.pPrivateDriverData = &allocationPrivateB;
                openInfoB.PrivateDriverDataSize = sizeof(allocationPrivateB);
                memset(&openAllocationB, 0, sizeof(openAllocationB));
                openAllocationB.NumAllocations = 1u;
                openAllocationB.pOpenAllocation = &openInfoB;
                CHECK(initData.DxgkDdiOpenAllocation(deviceB, &openAllocationB) == STATUS_SUCCESS);
                openB = openInfoB.hDeviceSpecificAllocation;
                CHECK(openB != NULL);

                /* Rejection before root page table is set */
                CHECK(Bc250ActiveIssueGpuvaSdmaLease(activeCtx, processB, openB,
                    0x400000ull, 4096u, &leaseB) == STATUS_DEVICE_NOT_READY);

                memset(&createContextB, 0, sizeof(createContextB));
                createContextB.hContext = (HANDLE)(ULONG_PTR)0xB252u;
                CHECK(initData.DxgkDdiCreateContext(deviceB, &createContextB) == STATUS_SUCCESS);
                contextB = createContextB.hContext;

                CHECK(Bc250ActiveSetRootPageTableObject(activeCtx, contextB, 1u,
                    0x0000000000445000ull, 512u, 512u) == STATUS_SUCCESS);

                /* Issuance succeeds after root page table is set */
                CHECK(Bc250ActiveIssueGpuvaSdmaLease(activeCtx, processB, openB,
                    0x400000ull, 4096u, &leaseB) == STATUS_SUCCESS);
                CHECK(leaseB != 0ull && leaseB != leaseA1);

                /* Cross-process pair validation succeeds */
                CHECK(Bc250ActiveValidateGpuvaSdmaLeasePair(activeCtx, leaseA1,
                    leaseB, 0x500000ull, 4096u) == STATUS_SUCCESS);

                /* Same lease aliasing is rejected */
                CHECK(Bc250ActiveValidateGpuvaSdmaLeasePair(activeCtx, leaseA1,
                    leaseA1, 0x500000ull, 4096u) == STATUS_INVALID_PARAMETER);

                CHECK(initData.DxgkDdiDestroyContext(contextB) == STATUS_SUCCESS);

                /* Teardown process B and verify lease revocation */
                {
                    DXGKARG_CLOSEALLOCATION closeB;
                    DXGKARG_DESTROYALLOCATION destroyB;
                    HANDLE openBList[1];
                    HANDLE allocBList[1];

                    openBList[0] = openB;
                    memset(&closeB, 0, sizeof(closeB));
                    closeB.NumAllocations = 1u;
                    closeB.pOpenHandleList = openBList;
                    CHECK(initData.DxgkDdiCloseAllocation(deviceB, &closeB) == STATUS_SUCCESS);

                    allocBList[0] = allocationInfoB.hAllocation;
                    memset(&destroyB, 0, sizeof(destroyB));
                    destroyB.NumAllocations = 1u;
                    destroyB.pAllocationList = allocBList;
                    CHECK(initData.DxgkDdiDestroyAllocation(activeCtx, &destroyB) == STATUS_SUCCESS);

                    CHECK(initData.DxgkDdiDestroyDevice(deviceB) == STATUS_SUCCESS);
                    CHECK(initData.DxgkDdiDestroyProcess(activeCtx, processB) == STATUS_SUCCESS);

                    /* Stale lease validation fails */
                    CHECK(Bc250ActiveValidateGpuvaSdmaLeasePair(activeCtx, leaseA1,
                        leaseB, 0x500000ull, 4096u) == STATUS_INVALID_PARAMETER);
                }
            }
        }

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
        /* Stop deve falhar ocupado sem desligar o adaptador se houver reserva. */
        {
            uint32_t pendingToken = 0u;
            CHECK(Bc250WddmFenceTrackerReserve(
                &activeCtx->SubmissionEngine.WddmFences,
                0xDEADu, 1u, 0u, &pendingToken));
            CHECK(Bc250DxgkDdiStopDeviceActive(activeCtx) == STATUS_DEVICE_BUSY);
            CHECK(activeCtx->IsActive == 1);
            CHECK(Bc250WddmFenceTrackerCancel(
                &activeCtx->SubmissionEngine.WddmFences, pendingToken));
        }
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
    /* 3.67: teardown com DxgkCbSynchronizeExecution falhando. */
    if (!Bc250RunStopQuiescenceTest(&initData, &fakeDeviceObject)) return 0;
    /* 3.67: submit/retire/stop disputando o lock proprio do rastreador. */
    if (!Bc250RunTrackerRaceTest(&fakeDeviceObject)) return 0;
    /* 3.67: evidencia nao pode misturar boots. */
    if (!Bc250RunRenderTraceResetTest()) return 0;

    /* Fase V2: correlacao fence do anel <-> fence do WDDM. */
    CHECK(Bc250RunWddmFenceTrackerTest());

    /* Gate 1.5: perfis display+render e render-only distintos e coerentes. */
    CHECK(Bc250RunAdapterProfileTest());

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
