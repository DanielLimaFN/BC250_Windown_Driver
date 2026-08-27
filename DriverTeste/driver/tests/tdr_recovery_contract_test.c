#include "../core/bc250_tdr_recovery_contract.h"

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

static BC250_TDR_RESET_PLAN g_testPlan;
static BC250_TDR_ENGINE_STATE g_testState;

static int Bc250RunTdrRecoveryContractTest(void)
{
    uint32_t isHung = 0;

    memset(&g_testPlan, 0, sizeof(g_testPlan));
    memset(&g_testState, 0, sizeof(g_testState));

    /* 1. Test Hang Detection */
    /* Normal active forward progress */
    CHECK(Bc250TdrDetectHang(0u, 10u, 30u, 25u, 30u, &isHung) == BC250_TDR_OK);
    CHECK(isHung == 0u);

    /* Normal completion */
    CHECK(Bc250TdrDetectHang(0u, 10u, 30u, 30u, 30u, &isHung) == BC250_TDR_OK);
    CHECK(isHung == 0u);

    /* Idle queue */
    CHECK(Bc250TdrDetectHang(0u, 10u, 10u, 10u, 10u, &isHung) == BC250_TDR_OK);
    CHECK(isHung == 0u);

    /* Hanging queue with pending work and zero RPTR progress */
    CHECK(Bc250TdrDetectHang(0u, 10u, 30u, 10u, 30u, &isHung) == BC250_TDR_OK);
    CHECK(isHung == 1u);

    /* 2. Test SDMA Reset Plan Construction (Node 0) */
    CHECK(Bc250TdrBuildResetPlan(
        BC250_ADAPTER_NODE_SDMA, 42u, 10u, 30u, &g_testPlan) == BC250_TDR_OK);
    CHECK(g_testPlan.NodeIndex == BC250_ADAPTER_NODE_SDMA);
    CHECK(g_testPlan.FaultedContextId == 42u);
    CHECK((g_testPlan.ActionsRequired & BC250_TDR_ACTION_FREEZE) != 0);
    CHECK((g_testPlan.ActionsRequired & BC250_TDR_ACTION_RESET_POINTERS) != 0);
    CHECK((g_testPlan.ActionsRequired & BC250_TDR_ACTION_FAULT_CONTEXT) != 0);
    CHECK((g_testPlan.ActionsRequired & BC250_TDR_ACTION_RESUME) != 0);
    CHECK((g_testPlan.ActionsRequired & BC250_TDR_ACTION_DEQUEUE_HQD) == 0);
    CHECK(g_testPlan.RestoredRptr == 10u);
    CHECK(g_testPlan.RestoredWptr == 10u);

    /* 3. Test Compute MEC Reset Plan Construction (Node 1) */
    CHECK(Bc250TdrBuildResetPlan(
        BC250_ADAPTER_NODE_COMPUTE, 99u, 50u, 100u, &g_testPlan) == BC250_TDR_OK);
    CHECK(g_testPlan.NodeIndex == BC250_ADAPTER_NODE_COMPUTE);
    CHECK(g_testPlan.FaultedContextId == 99u);
    CHECK((g_testPlan.ActionsRequired & BC250_TDR_ACTION_DEQUEUE_HQD) != 0);
    CHECK(g_testPlan.RestoredRptr == 50u);
    CHECK(g_testPlan.RestoredWptr == 50u);

    /* 4. Test Reset Execution & State Transition */
    CHECK(Bc250TdrExecuteResetPlan(&g_testPlan, &g_testState) == BC250_TDR_OK);
    CHECK(g_testState.NodeIndex == BC250_ADAPTER_NODE_COMPUTE);
    CHECK(g_testState.LastRptr == 50u);
    CHECK(g_testState.LastWptr == 50u);
    CHECK(g_testState.ResetCount == 1u);
    CHECK(g_testState.IsRecovered == 1u);

    /* Second reset execution */
    CHECK(Bc250TdrExecuteResetPlan(&g_testPlan, &g_testState) == BC250_TDR_OK);
    CHECK(g_testState.ResetCount == 2u);

    /* 5. Test Error Handling */
    CHECK(Bc250TdrDetectHang(2u, 0, 0, 0, 0, &isHung) == BC250_TDR_INVALID_NODE);
    CHECK(Bc250TdrDetectHang(0u, 0, 0, 0, 0, NULL) == BC250_TDR_BAD_ARGUMENT);
    CHECK(Bc250TdrBuildResetPlan(2u, 0, 0, 0, &g_testPlan) == BC250_TDR_INVALID_NODE);
    CHECK(Bc250TdrBuildResetPlan(0u, 0, 0, 0, NULL) == BC250_TDR_BAD_ARGUMENT);
    CHECK(Bc250TdrExecuteResetPlan(NULL, &g_testState) == BC250_TDR_BAD_ARGUMENT);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunTdrRecoveryContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunTdrRecoveryContractTest() ? 0 : 1;
}
#endif
