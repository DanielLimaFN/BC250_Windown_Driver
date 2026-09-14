#include "../core/bc250_gpuva_sdma_executor_contract.h"

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
/* Falha devolve 0 porque main() faz `return Run() ? 0 : 1`: devolver 1
 * aqui fazia a falha virar SUCESSO no build offline. Medido em 14/09
 * quebrando o emissor de proposito -- suite offline saiu EXIT=0 e a
 * kernel EXIT=1. Travado por audit_offline_check_is_not_inverted. */
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#endif

typedef struct TEST_EXECUTOR_CONTEXT {
    uint32_t FailSnapshot;
    uint32_t FailSubmit;
    uint32_t Mismatch;
    uint32_t Rollbacks;
} TEST_EXECUTOR_CONTEXT;

static BC250_GPUVA_SDMA_EXECUTOR_STATUS TestSnapshot(void *Context,
    BC250_GPUVA_SDMA_EXECUTOR_EVIDENCE *Evidence)
{
    TEST_EXECUTOR_CONTEXT *test = (TEST_EXECUTOR_CONTEXT *)Context;
    Evidence->RptrBefore = 3u; Evidence->WptrBefore = 3u;
    return test->FailSnapshot ? BC250_GPUVA_SDMA_EXECUTOR_SNAPSHOT_FAILED :
        BC250_GPUVA_SDMA_EXECUTOR_OK;
}
static BC250_GPUVA_SDMA_EXECUTOR_STATUS TestSubmit(void *Context,
    const BC250_GPUVA_SDMA_EXECUTOR_EVIDENCE *Evidence)
{
    TEST_EXECUTOR_CONTEXT *test = (TEST_EXECUTOR_CONTEXT *)Context;
    (void)Evidence;
    return test->FailSubmit ? BC250_GPUVA_SDMA_EXECUTOR_SUBMIT_FAILED :
        BC250_GPUVA_SDMA_EXECUTOR_OK;
}
static BC250_GPUVA_SDMA_EXECUTOR_STATUS TestReadback(void *Context,
    BC250_GPUVA_SDMA_EXECUTOR_EVIDENCE *Evidence)
{
    TEST_EXECUTOR_CONTEXT *test = (TEST_EXECUTOR_CONTEXT *)Context;
    Evidence->RptrAfter = 7u; Evidence->WptrAfter = 7u;
    Evidence->FenceObserved = Evidence->FenceExpected;
    Evidence->DataMismatches = test->Mismatch;
    return BC250_GPUVA_SDMA_EXECUTOR_OK;
}
static BC250_GPUVA_SDMA_EXECUTOR_STATUS TestRollback(void *Context,
    BC250_GPUVA_SDMA_EXECUTOR_EVIDENCE *Evidence)
{
    TEST_EXECUTOR_CONTEXT *test = (TEST_EXECUTOR_CONTEXT *)Context;
    ++test->Rollbacks; (void)Evidence;
    return BC250_GPUVA_SDMA_EXECUTOR_OK;
}

static int Bc250RunGpuvaSdmaExecutorContractTest(void)
{
    BC250_GPUVA_SDMA_BROKER broker = { 0 };
    BC250_GPUVA_SDMA_EXECUTOR_EVIDENCE evidence = { 0 };
    TEST_EXECUTOR_CONTEXT context = { 0 };
    BC250_GPUVA_SDMA_EXECUTOR executor;

    CHECK(Bc250GpuvaSdmaBrokerInitialize(&broker, 1ull) == BC250_GPUVA_SDMA_BROKER_OK);
    broker.Leases[0].LeaseId = 10ull; broker.Leases[0].Live = 1u;
    broker.Leases[0].GpuVa = 0x100000ull; broker.Leases[0].ByteCount = 4096u;
    broker.Leases[0].OwnerProcess = (void *)1; broker.Leases[0].Allocation = (void *)11;
    broker.Leases[1].LeaseId = 11ull; broker.Leases[1].Live = 1u;
    broker.Leases[1].GpuVa = 0x200000ull; broker.Leases[1].ByteCount = 4096u;
    broker.Leases[1].OwnerProcess = (void *)2; broker.Leases[1].Allocation = (void *)12;
    executor.Context = &context; executor.Snapshot = TestSnapshot; executor.Submit = TestSubmit;
    executor.Readback = TestReadback; executor.Rollback = TestRollback;
    evidence.SourceLeaseId = 10ull; evidence.DestinationLeaseId = 11ull;
    evidence.FenceGpuVa = 0x300000ull; evidence.ByteCount = 4096u;
    evidence.FenceExpected = 0xBC250001u;
    CHECK(Bc250GpuvaSdmaExecuteTransaction(&broker, &executor, &evidence) ==
        BC250_GPUVA_SDMA_EXECUTOR_OK);
    CHECK(evidence.SnapshotTaken == 1u && evidence.SubmitAttempted == 1u &&
        evidence.ReadbackTaken == 1u && evidence.RollbackPerformed == 1u &&
        evidence.EvidenceTripletPassed == 1u && context.Rollbacks == 1u);
    context.Mismatch = 1u;
    CHECK(Bc250GpuvaSdmaExecuteTransaction(&broker, &executor, &evidence) ==
        BC250_GPUVA_SDMA_EXECUTOR_PHYSICAL_PROOF_FAILED);
    CHECK(evidence.EvidenceTripletPassed == 0u && evidence.RollbackPerformed == 1u &&
        context.Rollbacks == 2u);
    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void) { ExitProcess(Bc250RunGpuvaSdmaExecutorContractTest() ? 0ul : 1ul); }
#else
int main(void) { return Bc250RunGpuvaSdmaExecutorContractTest() ? 0 : 1; }
#endif
