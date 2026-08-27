#ifndef BC250_PREEMPTION_CONTRACT_H
#define BC250_PREEMPTION_CONTRACT_H

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

typedef enum BC250_PREEMPTION_STATUS {
    BC250_PREEMPTION_OK = 0,
    BC250_PREEMPTION_BAD_ARGUMENT,
    BC250_PREEMPTION_INVALID_NODE,
    BC250_PREEMPTION_MISALIGNED_ADDRESS,
    BC250_PREEMPTION_TIMEOUT,
    BC250_PREEMPTION_ENGINE_BUSY
} BC250_PREEMPTION_STATUS;

typedef struct BC250_PREEMPTION_REQUEST {
    uint32_t NodeIndex;
    uint32_t EngineIndex;
    uint64_t PreemptionFenceId;
    uint64_t PreemptionFenceGpuVa;
    uint32_t TimeoutUs;
} BC250_PREEMPTION_REQUEST;

typedef struct BC250_PREEMPTION_RESULT {
    uint32_t IsSuccessful;
    uint32_t PreemptedWptr;
    uint32_t HqdDequeued;
    uint32_t EmittedFenceDwords;
} BC250_PREEMPTION_RESULT;

BC250_PREEMPTION_STATUS Bc250ValidatePreemptionRequest(
    const BC250_PREEMPTION_REQUEST *Request);

BC250_PREEMPTION_STATUS Bc250BuildPreemptionPlan(
    const BC250_PREEMPTION_REQUEST *Request,
    uint32_t CurrentWptr,
    uint32_t CurrentRptr,
    BC250_PREEMPTION_RESULT *Result);

BC250_PREEMPTION_STATUS Bc250PreemptCommand(
    const BC250_PREEMPTION_REQUEST *Request,
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    BC250_PREEMPTION_RESULT *Result);

const char *Bc250PreemptionStatusName(BC250_PREEMPTION_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
