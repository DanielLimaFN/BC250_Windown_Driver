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

    if (NodeIndex == BC250_ADAPTER_NODE_COMPUTE) {
        Plan->ActionsRequired |= BC250_TDR_ACTION_DEQUEUE_HQD;
    }

    Plan->RestoredRptr = CurrentRptr;
    Plan->RestoredWptr = CurrentRptr; /* Equalize pointers to drop hung packets */
    Plan->RingBaseReset = 1u;

    (void)CurrentWptr;

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
    default: return "unknown";
    }
}
