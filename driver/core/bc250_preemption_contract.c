#include "bc250_preemption_contract.h"

#include <string.h>

BC250_PREEMPTION_STATUS Bc250ValidatePreemptionRequest(
    const BC250_PREEMPTION_REQUEST *Request)
{
    if (Request == NULL) {
        return BC250_PREEMPTION_BAD_ARGUMENT;
    }
    if (Request->NodeIndex >= BC250_ADAPTER_NODE_COUNT) {
        return BC250_PREEMPTION_INVALID_NODE;
    }
    if ((Request->PreemptionFenceGpuVa & 3u) != 0) {
        return BC250_PREEMPTION_MISALIGNED_ADDRESS;
    }
    return BC250_PREEMPTION_OK;
}

BC250_PREEMPTION_STATUS Bc250BuildPreemptionPlan(
    const BC250_PREEMPTION_REQUEST *Request,
    uint32_t CurrentWptr,
    uint32_t CurrentRptr,
    BC250_PREEMPTION_RESULT *Result)
{
    BC250_PREEMPTION_STATUS status;

    if (Result == NULL) {
        return BC250_PREEMPTION_BAD_ARGUMENT;
    }

    status = Bc250ValidatePreemptionRequest(Request);
    if (status != BC250_PREEMPTION_OK) {
        return status;
    }

    memset(Result, 0, sizeof(BC250_PREEMPTION_RESULT));
    UNREFERENCED_PARAMETER(CurrentWptr);
    Result->PreemptedWptr = CurrentRptr;
    Result->HqdDequeued = (Request->NodeIndex == BC250_ADAPTER_NODE_COMPUTE) ? 1u : 0u;
    Result->IsSuccessful = 1u;

    return BC250_PREEMPTION_OK;
}

BC250_PREEMPTION_STATUS Bc250PreemptCommand(
    const BC250_PREEMPTION_REQUEST *Request,
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    BC250_PREEMPTION_RESULT *Result)
{
    BC250_PREEMPTION_STATUS status;

    if (RingBuffer == NULL || CursorDwords == NULL || Result == NULL) {
        return BC250_PREEMPTION_BAD_ARGUMENT;
    }

    status = Bc250ValidatePreemptionRequest(Request);
    if (status != BC250_PREEMPTION_OK) {
        return status;
    }

    memset(Result, 0, sizeof(BC250_PREEMPTION_RESULT));

    if (Request->NodeIndex == BC250_ADAPTER_NODE_SDMA) {
        BC250_SDMA_STATUS sdmaStatus = Bc250SdmaEmitFence(
            RingBuffer,
            RingDwords,
            CursorDwords,
            Request->PreemptionFenceGpuVa,
            (uint32_t)(Request->PreemptionFenceId & 0xFFFFFFFFull));

        if (sdmaStatus != BC250_SDMA_OK) {
            return BC250_PREEMPTION_BAD_ARGUMENT;
        }

        Result->EmittedFenceDwords = 4u;
        Result->HqdDequeued = 0u;
        Result->PreemptedWptr = *CursorDwords;
        Result->IsSuccessful = 1u;
    } else if (Request->NodeIndex == BC250_ADAPTER_NODE_COMPUTE) {
        BC250_PM4_STATUS pm4Status = Bc250Pm4EmitReleaseMem(
            RingBuffer,
            RingDwords,
            CursorDwords,
            BC250_PM4_EVENT_BOTTOM_OF_PIPE,
            0u,
            Request->PreemptionFenceGpuVa,
            Request->PreemptionFenceId);

        if (pm4Status != BC250_PM4_OK) {
            return BC250_PREEMPTION_BAD_ARGUMENT;
        }

        Result->EmittedFenceDwords = 7u;
        Result->HqdDequeued = 1u;
        Result->PreemptedWptr = *CursorDwords;
        Result->IsSuccessful = 1u;
    }

    return BC250_PREEMPTION_OK;
}

const char *Bc250PreemptionStatusName(BC250_PREEMPTION_STATUS Status)
{
    switch (Status) {
    case BC250_PREEMPTION_OK: return "ok";
    case BC250_PREEMPTION_BAD_ARGUMENT: return "bad_argument";
    case BC250_PREEMPTION_INVALID_NODE: return "invalid_node";
    case BC250_PREEMPTION_MISALIGNED_ADDRESS: return "misaligned_address";
    case BC250_PREEMPTION_TIMEOUT: return "timeout";
    case BC250_PREEMPTION_ENGINE_BUSY: return "engine_busy";
    default: return "unknown";
    }
}
