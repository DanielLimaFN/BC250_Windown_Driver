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
/* Falha devolve 0 porque main() faz `return Run() ? 0 : 1`: devolver 1
 * aqui fazia a falha virar SUCESSO no build offline. Medido em 14/09
 * quebrando o emissor de proposito -- suite offline saiu EXIT=0 e a
 * kernel EXIT=1. Travado por audit_offline_check_is_not_inverted. */
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
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
    CHECK((g_testPlan.ActionsRequired & BC250_TDR_ACTION_FULL_BLOCK_RESET) != 0);
    CHECK(g_testPlan.RestoredRptr == 10u);
    CHECK(g_testPlan.RestoredWptr == 10u);

    /* 3. Test Compute MEC Reset Plan Construction (Node 1) */
    CHECK(Bc250TdrBuildResetPlan(
        BC250_ADAPTER_NODE_COMPUTE, 99u, 50u, 100u, &g_testPlan) == BC250_TDR_OK);
    CHECK(g_testPlan.NodeIndex == BC250_ADAPTER_NODE_COMPUTE);
    CHECK(g_testPlan.FaultedContextId == 99u);
    CHECK((g_testPlan.ActionsRequired & BC250_TDR_ACTION_DEQUEUE_HQD) != 0);
    CHECK((g_testPlan.ActionsRequired & BC250_TDR_ACTION_FULL_BLOCK_RESET) == 0);
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

    /* 6. Readback minimo antes da fence de prova do restart SDMA. */
    CHECK(Bc250TdrValidateSdmaRestartReadback(
        0u, 0u, 0u, 0u, 0x00001015u, 0x1u,
        0u, 0x1u, 0u, 0x00800000u) == BC250_TDR_OK);

    /* Valores medidos na falha 20:41: RPTR residual invalida o restart. */
    CHECK(Bc250TdrValidateSdmaRestartReadback(
        68u, 0u, 0u, 0u, 0x00001015u, 0x1u,
        0u, 0x1u, 0u, 0x00800000u) == BC250_TDR_RESTART_NOT_READY);
    CHECK(Bc250TdrValidateSdmaRestartReadback(
        0u, 0u, 0u, 0u, 0x00001014u, 0x1u,
        0u, 0x1u, 0u, 0x00800000u) == BC250_TDR_RESTART_NOT_READY);
    CHECK(Bc250TdrValidateSdmaRestartReadback(
        0u, 0u, 0u, 0u, 0x00001015u, 0x1u,
        1u, 0x1u, 0u, 0x00800000u) == BC250_TDR_RESTART_NOT_READY);
    CHECK(Bc250TdrValidateSdmaRestartReadback(
        0u, 0u, 0u, 0u, 0x00001015u, 0x1u,
        0u, 0x1u, 0x00800000u, 0x00800000u) == BC250_TDR_RESTART_NOT_READY);
    CHECK(Bc250TdrValidateSdmaRestartReadback(
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, 1u) == BC250_TDR_BAD_ARGUMENT);

    /* 7. Readback minimo antes do restart do compute MEC (Node 1). */
    CHECK(Bc250TdrValidateMecRestartReadback(0u, 0u, 0u, 0u, 0u, 0u) == BC250_TDR_OK);
    /* Bandeiras benignas do CP (CE/DE active buffer flags 0xC00): valido */
    CHECK(Bc250TdrValidateMecRestartReadback(0u, 0u, 0u, 0u, 0u, 0x0C00u) == BC250_TDR_OK);
    /* Fila ainda ativa: restart invalido */
    CHECK(Bc250TdrValidateMecRestartReadback(1u, 0u, 0u, 0u, 0u, 0u) == BC250_TDR_RESTART_NOT_READY);
    /* Dequeue request pendente: restart invalido */
    CHECK(Bc250TdrValidateMecRestartReadback(0u, 0u, 0u, 1u, 0u, 0u) == BC250_TDR_RESTART_NOT_READY);
    /* Erro residual no HQD: restart invalido */
    CHECK(Bc250TdrValidateMecRestartReadback(0u, 0u, 0u, 0u, 0x10u, 0u) == BC250_TDR_RESTART_NOT_READY);
    /* Pipeline do CP stalled (ex: bit 0 RBIU_TO_DMA_NOT_RDY_TO_RCV): restart invalido */
    CHECK(Bc250TdrValidateMecRestartReadback(0u, 0u, 0u, 0u, 0u, 1u) == BC250_TDR_RESTART_NOT_READY);
    /* Pipeline do CP stalled combinando flag benigno e stall real: restart invalido */
    CHECK(Bc250TdrValidateMecRestartReadback(0u, 0u, 0u, 0u, 0u, 0x0C01u) == BC250_TDR_RESTART_NOT_READY);
    /* Ponteiros nao zerados: restart invalido */
    CHECK(Bc250TdrValidateMecRestartReadback(0u, 4u, 0u, 0u, 0u, 0u) == BC250_TDR_RESTART_NOT_READY);
    CHECK(Bc250TdrValidateMecRestartReadback(0u, 0u, 4u, 0u, 0u, 0u) == BC250_TDR_RESTART_NOT_READY);

    /* 8. Readback minimo antes do restart do anel grafico CP_RB0 (Node 1 GFX). */
    /* Estado nominal: RB0_ACTIVE=1, RPTR==WPTR==0, MeCntl unhalted (bits 0..2=0), SoftReset=0 */
    CHECK(Bc250TdrValidateGfxRestartReadback(1u, 0u, 0u, 0u, 0u) == BC250_TDR_OK);
    CHECK(Bc250TdrValidateGfxRestartReadback(1u, 16u, 16u, 0u, 0u) == BC250_TDR_OK);
    /* Anel inativo: restart invalido */
    CHECK(Bc250TdrValidateGfxRestartReadback(0u, 0u, 0u, 0u, 0u) == BC250_TDR_RESTART_NOT_READY);
    /* Ponteiros dessincronizados (RPTR != WPTR): restart invalido */
    CHECK(Bc250TdrValidateGfxRestartReadback(1u, 0u, 16u, 0u, 0u) == BC250_TDR_RESTART_NOT_READY);
    CHECK(Bc250TdrValidateGfxRestartReadback(1u, 16u, 0u, 0u, 0u) == BC250_TDR_RESTART_NOT_READY);
    /* Micro-engines ainda halted (bits 0..2 de CP_ME_CNTL): restart invalido */
    CHECK(Bc250TdrValidateGfxRestartReadback(1u, 0u, 0u, 1u, 0u) == BC250_TDR_RESTART_NOT_READY);
    CHECK(Bc250TdrValidateGfxRestartReadback(1u, 0u, 0u, 7u, 0u) == BC250_TDR_RESTART_NOT_READY);
    /* Soft reset preso em GRBM_SOFT_RESET (bit 0 CP ou bit 16 GFX): restart invalido */
    CHECK(Bc250TdrValidateGfxRestartReadback(1u, 0u, 0u, 0u, 0x00000001u) == BC250_TDR_RESTART_NOT_READY);
    CHECK(Bc250TdrValidateGfxRestartReadback(1u, 0u, 0u, 0u, 0x00010000u) == BC250_TDR_RESTART_NOT_READY);

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
