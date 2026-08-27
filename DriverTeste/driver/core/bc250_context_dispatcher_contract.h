#ifndef BC250_CONTEXT_DISPATCHER_CONTRACT_H
#define BC250_CONTEXT_DISPATCHER_CONTRACT_H

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

#include "bc250_sdma_packet_builder.h"
#include "bc250_pm4_packet_builder.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum BC250_ENGINE_TYPE {
    BC250_ENGINE_TYPE_SDMA = 0,
    BC250_ENGINE_TYPE_COMPUTE = 1
} BC250_ENGINE_TYPE;

typedef enum BC250_DISPATCH_STATUS {
    BC250_DISPATCH_OK = 0,
    BC250_DISPATCH_BAD_ARGUMENT,
    BC250_DISPATCH_INVALID_ENGINE,
    BC250_DISPATCH_BUFFER_TOO_LARGE,
    BC250_DISPATCH_MISALIGNED_ADDRESS,
    BC250_DISPATCH_RING_FULL,
    BC250_DISPATCH_CONTEXT_INACTIVE
} BC250_DISPATCH_STATUS;

typedef struct BC250_GPU_CONTEXT {
    uint32_t ContextId;
    BC250_ENGINE_TYPE EngineType;
    uint32_t NodeOrdinal;
    uint32_t EngineAffinity;
    uint32_t Vmid;
    uint64_t RingGpuAddress;
    uint32_t RingSizeDwords;
    uint32_t LastSubmittedFence;
    uint32_t CompletedFence;
    uint32_t IsActive;
} BC250_GPU_CONTEXT;

typedef struct BC250_SUBMIT_COMMAND_DESC {
    uint32_t ContextId;
    BC250_ENGINE_TYPE EngineType;
    uint64_t CommandBufferGpuAddress;
    uint32_t CommandBufferSizeDwords;
    uint64_t FenceGpuAddress;
    uint32_t FenceSequence;
    uint64_t CopySrcGpuAddress; /* For SDMA */
    uint64_t CopyDstGpuAddress; /* For SDMA */
    uint32_t CopySizeBytes;     /* For SDMA */
} BC250_SUBMIT_COMMAND_DESC;

BC250_DISPATCH_STATUS Bc250ContextCreate(
    BC250_GPU_CONTEXT *Context,
    uint32_t ContextId,
    BC250_ENGINE_TYPE EngineType,
    uint32_t NodeOrdinal,
    uint64_t RingGpuAddress,
    uint32_t RingSizeDwords);

BC250_DISPATCH_STATUS Bc250ContextDestroy(
    BC250_GPU_CONTEXT *Context);

BC250_DISPATCH_STATUS Bc250ContextValidateSubmission(
    const BC250_GPU_CONTEXT *Context,
    const BC250_SUBMIT_COMMAND_DESC *Desc);

BC250_DISPATCH_STATUS Bc250ContextBuildSdmaSubmission(
    const BC250_GPU_CONTEXT *Context,
    const BC250_SUBMIT_COMMAND_DESC *Desc,
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords);

BC250_DISPATCH_STATUS Bc250ContextBuildComputeSubmission(
    const BC250_GPU_CONTEXT *Context,
    const BC250_SUBMIT_COMMAND_DESC *Desc,
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords);

const char *Bc250DispatchStatusName(BC250_DISPATCH_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
