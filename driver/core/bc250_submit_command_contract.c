#include "bc250_submit_command_contract.h"

#include <string.h>

BC250_SUBMISSION_STATUS Bc250ValidateSubmissionRequest(
    const BC250_SUBMISSION_REQUEST *Request)
{
    if (Request == NULL) {
        return BC250_SUBMISSION_BAD_ARGUMENT;
    }
    if (Request->NodeIndex >= BC250_ADAPTER_NODE_COUNT) {
        return BC250_SUBMISSION_INVALID_NODE;
    }
    if ((Request->DmaBufferGpuVa & 3u) != 0 || (Request->FenceGpuVa & 3u) != 0) {
        return BC250_SUBMISSION_MISALIGNED_ADDRESS;
    }
    if ((Request->DmaBufferSizeBytes & 3u) != 0) {
        return BC250_SUBMISSION_MISALIGNED_ADDRESS;
    }
    if (Request->DmaBufferSizeBytes == 0 || Request->DmaBufferSizeBytes > (4u * 1024u * 1024u)) {
        return BC250_SUBMISSION_PAYLOAD_TOO_LARGE;
    }

    return BC250_SUBMISSION_OK;
}

BC250_SUBMISSION_STATUS Bc250BuildSdmaRingSubmission(
    const BC250_SUBMISSION_REQUEST *Request,
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    BC250_SUBMISSION_RESULT *Result)
{
    BC250_SUBMISSION_STATUS status;
    BC250_SDMA_STATUS sdmaStatus;
    uint32_t startCursor;

    if (RingBuffer == NULL || CursorDwords == NULL || Result == NULL) {
        return BC250_SUBMISSION_BAD_ARGUMENT;
    }

    status = Bc250ValidateSubmissionRequest(Request);
    if (status != BC250_SUBMISSION_OK) {
        return status;
    }

    startCursor = *CursorDwords;

    /* Emit SDMA Fence packet (4 dwords) */
    sdmaStatus = Bc250SdmaEmitFence(
        RingBuffer,
        RingDwords,
        CursorDwords,
        Request->FenceGpuVa,
        (uint32_t)(Request->FenceValue & 0xFFFFFFFFull));

    if (sdmaStatus == BC250_SDMA_RING_FULL) {
        return BC250_SUBMISSION_RING_FULL;
    }
    if (sdmaStatus != BC250_SDMA_OK) {
        return BC250_SUBMISSION_BAD_ARGUMENT;
    }

    memset(Result, 0, sizeof(BC250_SUBMISSION_RESULT));
    Result->EmittedDwords = *CursorDwords - startCursor;
    Result->UpdatedWptr = *CursorDwords;
    Result->DoorbellOffset = 0x1000u + (Request->RingIndex * 4u);
    Result->DoorbellValue = *CursorDwords;
    Result->IsSuccessful = 1u;

    return BC250_SUBMISSION_OK;
}

BC250_SUBMISSION_STATUS Bc250BuildComputeRingSubmission(
    const BC250_SUBMISSION_REQUEST *Request,
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    BC250_SUBMISSION_RESULT *Result)
{
    BC250_SUBMISSION_STATUS status;
    BC250_PM4_STATUS pm4Status;
    uint32_t startCursor;

    if (RingBuffer == NULL || CursorDwords == NULL || Result == NULL) {
        return BC250_SUBMISSION_BAD_ARGUMENT;
    }

    status = Bc250ValidateSubmissionRequest(Request);
    if (status != BC250_SUBMISSION_OK) {
        return status;
    }

    startCursor = *CursorDwords;

    /* 1. Emit Indirect Buffer Packet */
    pm4Status = Bc250Pm4EmitIndirectBuffer(
        RingBuffer,
        RingDwords,
        CursorDwords,
        Request->DmaBufferGpuVa,
        Request->DmaBufferSizeBytes / 4u,
        0u);

    if (pm4Status == BC250_PM4_RING_FULL) {
        return BC250_SUBMISSION_RING_FULL;
    }
    if (pm4Status != BC250_PM4_OK) {
        return BC250_SUBMISSION_BAD_ARGUMENT;
    }

    /* 2. Emit Release Mem (EOP Fence) Packet */
    pm4Status = Bc250Pm4EmitReleaseMem(
        RingBuffer,
        RingDwords,
        CursorDwords,
        BC250_PM4_EVENT_BOTTOM_OF_PIPE,
        0u,
        Request->FenceGpuVa,
        Request->FenceValue);

    if (pm4Status == BC250_PM4_RING_FULL) {
        return BC250_SUBMISSION_RING_FULL;
    }
    if (pm4Status != BC250_PM4_OK) {
        return BC250_SUBMISSION_BAD_ARGUMENT;
    }

    memset(Result, 0, sizeof(BC250_SUBMISSION_RESULT));
    Result->EmittedDwords = *CursorDwords - startCursor;
    Result->UpdatedWptr = *CursorDwords;
    Result->DoorbellOffset = 0x2000u + (Request->RingIndex * 4u);
    Result->DoorbellValue = *CursorDwords;
    Result->IsSuccessful = 1u;

    return BC250_SUBMISSION_OK;
}

BC250_SUBMISSION_STATUS Bc250SubmitCommand(
    const BC250_SUBMISSION_REQUEST *Request,
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    BC250_SUBMISSION_RESULT *Result)
{
    if (Request == NULL) {
        return BC250_SUBMISSION_BAD_ARGUMENT;
    }

    if (Request->NodeIndex == BC250_ADAPTER_NODE_SDMA) {
        return Bc250BuildSdmaRingSubmission(Request, RingBuffer, RingDwords, CursorDwords, Result);
    } else if (Request->NodeIndex == BC250_ADAPTER_NODE_COMPUTE) {
        return Bc250BuildComputeRingSubmission(Request, RingBuffer, RingDwords, CursorDwords, Result);
    }

    return BC250_SUBMISSION_INVALID_NODE;
}

const char *Bc250SubmissionStatusName(BC250_SUBMISSION_STATUS Status)
{
    switch (Status) {
    case BC250_SUBMISSION_OK: return "ok";
    case BC250_SUBMISSION_BAD_ARGUMENT: return "bad_argument";
    case BC250_SUBMISSION_INVALID_NODE: return "invalid_node";
    case BC250_SUBMISSION_MISALIGNED_ADDRESS: return "misaligned_address";
    case BC250_SUBMISSION_RING_FULL: return "ring_full";
    case BC250_SUBMISSION_PAYLOAD_TOO_LARGE: return "payload_too_large";
    default: return "unknown";
    }
}
