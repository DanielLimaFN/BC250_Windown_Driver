#include "../core/bc250_preemption_contract.h"

#include <string.h>

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

#define TEST_PREEMPT_RING_DWORDS 256u

static uint32_t g_preemptRing[TEST_PREEMPT_RING_DWORDS];
static BC250_PREEMPTION_REQUEST g_preemptReq;
static BC250_PREEMPTION_RESULT g_preemptResult;

static int Bc250RunPreemptionContractTest(void)
{
    uint32_t cursor = 0;

    memset(g_preemptRing, 0, sizeof(g_preemptRing));
    memset(&g_preemptReq, 0, sizeof(g_preemptReq));
    memset(&g_preemptResult, 0, sizeof(g_preemptResult));

    /* 1. Test Preemption Plan - Node 0 (SDMA) */
    g_preemptReq.NodeIndex = BC250_ADAPTER_NODE_SDMA;
    g_preemptReq.EngineIndex = 0u;
    g_preemptReq.PreemptionFenceId = 501ull;
    g_preemptReq.PreemptionFenceGpuVa = 0x0000000020000000ull;
    g_preemptReq.TimeoutUs = 2000000u;

    CHECK(Bc250BuildPreemptionPlan(&g_preemptReq, 100u, 50u, &g_preemptResult) == BC250_PREEMPTION_OK);
    CHECK(g_preemptResult.IsSuccessful == 1u);
    CHECK(g_preemptResult.PreemptedWptr == 50u);
    CHECK(g_preemptResult.HqdDequeued == 0u);

    /* 2. Test Preemption Plan - Node 1 (Compute MEC) */
    g_preemptReq.NodeIndex = BC250_ADAPTER_NODE_COMPUTE;
    g_preemptReq.PreemptionFenceId = 601ull;

    CHECK(Bc250BuildPreemptionPlan(&g_preemptReq, 200u, 120u, &g_preemptResult) == BC250_PREEMPTION_OK);
    CHECK(g_preemptResult.IsSuccessful == 1u);
    CHECK(g_preemptResult.PreemptedWptr == 120u);
    CHECK(g_preemptResult.HqdDequeued == 1u);

    /* 3. Test Preempt Command Execution - Node 0 (SDMA Fence) */
    cursor = 0;
    g_preemptReq.NodeIndex = BC250_ADAPTER_NODE_SDMA;
    CHECK(Bc250PreemptCommand(
        &g_preemptReq, g_preemptRing, TEST_PREEMPT_RING_DWORDS, &cursor, &g_preemptResult) == BC250_PREEMPTION_OK);
    CHECK(cursor == 4u);
    CHECK(g_preemptResult.EmittedFenceDwords == 4u);
    CHECK(g_preemptResult.IsSuccessful == 1u);
    CHECK(g_preemptRing[0] == BC250_SDMA_OP_FENCE);
    CHECK(g_preemptRing[1] == 0x20000000u);
    CHECK(g_preemptRing[3] == 601u);

    /* 4. Test Preempt Command Execution - Node 1 (Compute Release Mem) */
    cursor = 0;
    g_preemptReq.NodeIndex = BC250_ADAPTER_NODE_COMPUTE;
    CHECK(Bc250PreemptCommand(
        &g_preemptReq, g_preemptRing, TEST_PREEMPT_RING_DWORDS, &cursor, &g_preemptResult) == BC250_PREEMPTION_OK);
    CHECK(cursor == 7u);
    CHECK(g_preemptResult.EmittedFenceDwords == 7u);
    CHECK(g_preemptResult.HqdDequeued == 1u);
    CHECK(g_preemptResult.IsSuccessful == 1u);
    CHECK(g_preemptRing[0] == 0xC0064900u); /* IT_RELEASE_MEM */
    CHECK(g_preemptRing[3] == 0x20000000u); /* Fence addr lo */
    CHECK(g_preemptRing[5] == 601u);        /* Fence data lo */

    /* 5. Test Error Handling */
    g_preemptReq.PreemptionFenceGpuVa = 0x0000000020000001ull;
    CHECK(Bc250ValidatePreemptionRequest(&g_preemptReq) == BC250_PREEMPTION_MISALIGNED_ADDRESS);
    g_preemptReq.PreemptionFenceGpuVa = 0x0000000020000000ull;

    g_preemptReq.NodeIndex = 2u;
    CHECK(Bc250ValidatePreemptionRequest(&g_preemptReq) == BC250_PREEMPTION_INVALID_NODE);
    CHECK(Bc250PreemptCommand(NULL, g_preemptRing, 256u, &cursor, &g_preemptResult) == BC250_PREEMPTION_BAD_ARGUMENT);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunPreemptionContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunPreemptionContractTest() ? 0 : 1;
}
#endif
