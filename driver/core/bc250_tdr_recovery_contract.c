#include "bc250_tdr_recovery_contract.h"

#include <string.h>

BC250_TDR_STATUS Bc250TdrDetectHang(
    uint32_t NodeIndex,
    uint32_t Rptr1,
    uint32_t Wptr1,
    uint32_t Rptr2,
    uint32_t Wptr2,
    uint32_t *IsHung)
{
    if (IsHung == NULL) {
        return BC250_TDR_BAD_ARGUMENT;
    }
    if (NodeIndex >= BC250_ADAPTER_NODE_COUNT) {
        return BC250_TDR_INVALID_NODE;
    }

    /* If queue was idle at pass 1 or finished by pass 2, it is not hanging */
    if (Wptr1 == Rptr1 || Wptr2 == Rptr2) {
        *IsHung = 0u;
        return BC250_TDR_OK;
    }

    /* If RPTR moved between samples, engine is making forward progress */
    if (Rptr1 != Rptr2) {
        *IsHung = 0u;
        return BC250_TDR_OK;
    }

    /* Engine has pending work and RPTR made zero progress */
    *IsHung = 1u;
    return BC250_TDR_OK;
}

BC250_TDR_STATUS Bc250TdrBuildResetPlan(
    uint32_t NodeIndex,
    uint32_t FaultedContextId,
    uint32_t CurrentRptr,
    uint32_t CurrentWptr,
    BC250_TDR_RESET_PLAN *Plan)
{
    if (Plan == NULL) {
        return BC250_TDR_BAD_ARGUMENT;
    }
    if (NodeIndex >= BC250_ADAPTER_NODE_COUNT) {
        return BC250_TDR_INVALID_NODE;
    }

    memset(Plan, 0, sizeof(BC250_TDR_RESET_PLAN));
    Plan->NodeIndex = NodeIndex;
    Plan->FaultedContextId = FaultedContextId;

    Plan->ActionsRequired = BC250_TDR_ACTION_FREEZE |
                            BC250_TDR_ACTION_RESET_POINTERS |
                            BC250_TDR_ACTION_FAULT_CONTEXT |
                            BC250_TDR_ACTION_RESUME;

    if (NodeIndex == BC250_ADAPTER_NODE_SDMA) {
        Plan->ActionsRequired |= BC250_TDR_ACTION_FULL_BLOCK_RESET;
    } else if (NodeIndex == BC250_ADAPTER_NODE_COMPUTE) {
        Plan->ActionsRequired |= BC250_TDR_ACTION_DEQUEUE_HQD;
    }

    Plan->RestoredRptr = CurrentRptr;
    Plan->RestoredWptr = CurrentRptr; /* Equalize pointers to drop hung packets */
    Plan->RingBaseReset = 1u;

    (void)CurrentWptr;

    return BC250_TDR_OK;
}

BC250_TDR_STATUS Bc250TdrValidateSdmaRestartReadback(
    uint32_t Rptr,
    uint32_t RptrHi,
    uint32_t Wptr,
    uint32_t WptrHi,
    uint32_t RbCntl,
    uint32_t RbEnableMask,
    uint32_t F32Cntl,
    uint32_t F32HaltMask,
    uint32_t GrbmSoftReset,
    uint32_t SdmaResetMask)
{
    if (RbEnableMask == 0u || F32HaltMask == 0u || SdmaResetMask == 0u) {
        return BC250_TDR_BAD_ARGUMENT;
    }
    if (Rptr != 0u || RptrHi != 0u || Wptr != 0u || WptrHi != 0u ||
        (RbCntl & RbEnableMask) == 0u ||
        (F32Cntl & F32HaltMask) != 0u ||
        (GrbmSoftReset & SdmaResetMask) != 0u) {
        return BC250_TDR_RESTART_NOT_READY;
    }
    return BC250_TDR_OK;
}

BC250_TDR_STATUS Bc250TdrValidateMecRestartReadback(
    uint32_t HqdActive,
    uint32_t HqdRptr,
    uint32_t HqdWptr,
    uint32_t DequeueRequest,
    uint32_t HqdError,
    uint32_t CpStalled)
{
    /*
     * CP_STALLED_STAT1 (0x8674 / gc_10_1_0_sh_mask.h:6428-6429):
     * Bits 10 e 11 (0x00000C00) correspondem a:
     *   bit 10: ME_HAS_ACTIVE_CE_BUFFER_FLAG
     *   bit 11: ME_HAS_ACTIVE_DE_BUFFER_FLAG
     * Na arquitetura GFX10/Navi, estas duas bandeiras indicam que as maquinas
     * de estado CE e DE estao inicializadas e ativas em repouso. Elas NAO sao
     * condicoes de stall. Stalls reais do pipeline estao nos demais bits
     * (ex: bits 0..4, 12..15 e 23..29).
     */
    uint32_t actualStalls = CpStalled & ~0x00000C00u;

    if ((HqdActive & 1u) != 0u ||
        (DequeueRequest & 1u) != 0u ||
        HqdError != 0u ||
        actualStalls != 0u ||
        HqdRptr != 0u ||
        HqdWptr != 0u) {
        return BC250_TDR_RESTART_NOT_READY;
    }
    return BC250_TDR_OK;
}

BC250_TDR_STATUS Bc250TdrExecuteResetPlan(
    const BC250_TDR_RESET_PLAN *Plan,
    BC250_TDR_ENGINE_STATE *State)
{
    if (Plan == NULL || State == NULL) {
        return BC250_TDR_BAD_ARGUMENT;
    }
    if (Plan->NodeIndex >= BC250_ADAPTER_NODE_COUNT) {
        return BC250_TDR_INVALID_NODE;
    }

    State->NodeIndex = Plan->NodeIndex;
    State->LastRptr = Plan->RestoredRptr;
    State->LastWptr = Plan->RestoredWptr;
    State->HangDwords = 0u;
    State->ResetCount++;
    State->IsRecovered = 1u;

    return BC250_TDR_OK;
}

const char *Bc250TdrStatusName(BC250_TDR_STATUS Status)
{
    switch (Status) {
    case BC250_TDR_OK: return "ok";
    case BC250_TDR_BAD_ARGUMENT: return "bad_argument";
    case BC250_TDR_INVALID_NODE: return "invalid_node";
    case BC250_TDR_NOT_HANGING: return "not_hanging";
    case BC250_TDR_RESTART_NOT_READY: return "restart_not_ready";
    default: return "unknown";
    }
}
