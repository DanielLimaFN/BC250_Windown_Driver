#ifndef BC250_SUBMIT_COMMAND_CONTRACT_H
#define BC250_SUBMIT_COMMAND_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#include "bc250_adapter_info_contract.h"
#include "bc250_sdma_packet_builder.h"
#include "bc250_pm4_packet_builder.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum BC250_SUBMISSION_STATUS {
    BC250_SUBMISSION_OK = 0,
    BC250_SUBMISSION_BAD_ARGUMENT,
    BC250_SUBMISSION_INVALID_NODE,
    BC250_SUBMISSION_MISALIGNED_ADDRESS,
    BC250_SUBMISSION_RING_FULL,
    BC250_SUBMISSION_PAYLOAD_TOO_LARGE
} BC250_SUBMISSION_STATUS;

typedef struct BC250_SUBMISSION_REQUEST {
    uint32_t NodeIndex;
    uint32_t ContextId;
    uint64_t DmaBufferGpuVa;
    uint32_t DmaBufferSizeBytes;
    uint64_t FenceGpuVa;
    uint64_t FenceValue;
    uint32_t RingIndex;
} BC250_SUBMISSION_REQUEST;

typedef struct BC250_SUBMISSION_RESULT {
    uint32_t EmittedDwords;
    uint32_t UpdatedWptr;
    uint32_t DoorbellOffset;
    uint32_t DoorbellValue;
    uint32_t IsSuccessful;
} BC250_SUBMISSION_RESULT;

BC250_SUBMISSION_STATUS Bc250ValidateSubmissionRequest(
    const BC250_SUBMISSION_REQUEST *Request);

BC250_SUBMISSION_STATUS Bc250BuildSdmaRingSubmission(
    const BC250_SUBMISSION_REQUEST *Request,
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    BC250_SUBMISSION_RESULT *Result);

BC250_SUBMISSION_STATUS Bc250BuildComputeRingSubmission(
    const BC250_SUBMISSION_REQUEST *Request,
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    BC250_SUBMISSION_RESULT *Result);

BC250_SUBMISSION_STATUS Bc250SubmitCommand(
    const BC250_SUBMISSION_REQUEST *Request,
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    BC250_SUBMISSION_RESULT *Result);

const char *Bc250SubmissionStatusName(BC250_SUBMISSION_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
