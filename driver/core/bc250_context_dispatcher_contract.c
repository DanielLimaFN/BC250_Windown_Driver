#include "bc250_context_dispatcher_contract.h"

#include <string.h>

static int Bc250IsPowerOfTwo32(uint32_t Value)
{
    return Value != 0 && (Value & (Value - 1u)) == 0;
}

BC250_DISPATCH_STATUS Bc250ContextCreate(
    BC250_GPU_CONTEXT *Context,
    uint32_t ContextId,
    BC250_ENGINE_TYPE EngineType,
    uint32_t NodeOrdinal,
    uint64_t RingGpuAddress,
    uint32_t RingSizeDwords)
{
    if (Context == NULL) {
        return BC250_DISPATCH_BAD_ARGUMENT;
    }
    if (!Bc250IsPowerOfTwo32(RingSizeDwords) || RingSizeDwords < 64u) {
        return BC250_DISPATCH_BAD_ARGUMENT;
    }
    if ((RingGpuAddress & 0xFFu) != 0) {
        return BC250_DISPATCH_MISALIGNED_ADDRESS;
    }

    memset(Context, 0, sizeof(BC250_GPU_CONTEXT));
    Context->ContextId = ContextId;
    Context->EngineType = EngineType;
    Context->NodeOrdinal = NodeOrdinal;
    Context->EngineAffinity = (1u << NodeOrdinal);
    Context->Vmid = 0;
    Context->RingGpuAddress = RingGpuAddress;
    Context->RingSizeDwords = RingSizeDwords;
    Context->LastSubmittedFence = 0;
    Context->CompletedFence = 0;
    Context->IsActive = 1;

    return BC250_DISPATCH_OK;
}

BC250_DISPATCH_STATUS Bc250ContextDestroy(
    BC250_GPU_CONTEXT *Context)
{
    if (Context == NULL) {
        return BC250_DISPATCH_BAD_ARGUMENT;
    }
    if (!Context->IsActive) {
        return BC250_DISPATCH_CONTEXT_INACTIVE;
    }

    memset(Context, 0, sizeof(BC250_GPU_CONTEXT));
    return BC250_DISPATCH_OK;
}

BC250_DISPATCH_STATUS Bc250ContextValidateSubmission(
    const BC250_GPU_CONTEXT *Context,
    const BC250_SUBMIT_COMMAND_DESC *Desc)
{
    if (Context == NULL || Desc == NULL) {
        return BC250_DISPATCH_BAD_ARGUMENT;
    }
    if (!Context->IsActive) {
        return BC250_DISPATCH_CONTEXT_INACTIVE;
    }
    if (Context->ContextId != Desc->ContextId || Context->EngineType != Desc->EngineType) {
        return BC250_DISPATCH_INVALID_ENGINE;
    }

    if ((Desc->FenceGpuAddress & 3u) != 0) {
        return BC250_DISPATCH_MISALIGNED_ADDRESS;
    }

    if (Desc->EngineType == BC250_ENGINE_TYPE_SDMA) {
        if (Desc->CopySizeBytes > 0) {
            if (Desc->CopySizeBytes > BC250_SDMA_COPY_MAX_BYTES) {
                return BC250_DISPATCH_BUFFER_TOO_LARGE;
            }
        }
    } else if (Desc->EngineType == BC250_ENGINE_TYPE_COMPUTE) {
        if (Desc->CommandBufferSizeDwords > 0) {
            if (Desc->CommandBufferSizeDwords > 0x3FF0u) {
                return BC250_DISPATCH_BUFFER_TOO_LARGE;
            }
            if ((Desc->CommandBufferGpuAddress & 3u) != 0) {
                return BC250_DISPATCH_MISALIGNED_ADDRESS;
            }
        }
    } else {
        return BC250_DISPATCH_INVALID_ENGINE;
    }

    return BC250_DISPATCH_OK;
}

BC250_DISPATCH_STATUS Bc250ContextBuildSdmaSubmission(
    const BC250_GPU_CONTEXT *Context,
    const BC250_SUBMIT_COMMAND_DESC *Desc,
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords)
{
    BC250_DISPATCH_STATUS status;
    BC250_SDMA_STATUS sdmaStatus;

    status = Bc250ContextValidateSubmission(Context, Desc);
    if (status != BC250_DISPATCH_OK) {
        return status;
    }

    if (Desc->CopySizeBytes > 0) {
        sdmaStatus = Bc250SdmaEmitCopyLinear(
            RingBuffer, RingDwords, CursorDwords,
            Desc->CopySrcGpuAddress, Desc->CopyDstGpuAddress, Desc->CopySizeBytes);
        if (sdmaStatus != BC250_SDMA_OK) {
            return BC250_DISPATCH_BAD_ARGUMENT;
        }
    }

    sdmaStatus = Bc250SdmaEmitFence(
        RingBuffer, RingDwords, CursorDwords,
        Desc->FenceGpuAddress, Desc->FenceSequence);
    if (sdmaStatus != BC250_SDMA_OK) {
        return BC250_DISPATCH_BAD_ARGUMENT;
    }

    return BC250_DISPATCH_OK;
}

BC250_DISPATCH_STATUS Bc250ContextBuildComputeSubmission(
    const BC250_GPU_CONTEXT *Context,
    const BC250_SUBMIT_COMMAND_DESC *Desc,
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords)
{
    BC250_DISPATCH_STATUS status;
    BC250_PM4_STATUS pm4Status;
    uint32_t ibHeader;

    status = Bc250ContextValidateSubmission(Context, Desc);
    if (status != BC250_DISPATCH_OK) {
        return status;
    }

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_DISPATCH_BAD_ARGUMENT;
    }

    if (Desc->CommandBufferSizeDwords > 0) {
        /* Emit IT_INDIRECT_BUFFER (4 dwords packet) */
        ibHeader = BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_INDIRECT_BUFFER, 3u);
        RingBuffer[*CursorDwords & (RingDwords - 1u)] = ibHeader;
        (*CursorDwords)++;

        RingBuffer[*CursorDwords & (RingDwords - 1u)] = (uint32_t)(Desc->CommandBufferGpuAddress & 0xFFFFFFFCull);
        (*CursorDwords)++;

        RingBuffer[*CursorDwords & (RingDwords - 1u)] = (uint32_t)(Desc->CommandBufferGpuAddress >> 32);
        (*CursorDwords)++;

        RingBuffer[*CursorDwords & (RingDwords - 1u)] = (Desc->CommandBufferSizeDwords & 0x000FFFFFu) |
                                                       ((Context->Vmid & 0xFu) << 24);
        (*CursorDwords)++;
    }

    pm4Status = Bc250Pm4EmitReleaseMem(
        RingBuffer, RingDwords, CursorDwords,
        BC250_PM4_EVENT_EOP, 0,
        Desc->FenceGpuAddress, Desc->FenceSequence);
    if (pm4Status != BC250_PM4_OK) {
        return BC250_DISPATCH_BAD_ARGUMENT;
    }

    return BC250_DISPATCH_OK;
}

const char *Bc250DispatchStatusName(BC250_DISPATCH_STATUS Status)
{
    switch (Status) {
    case BC250_DISPATCH_OK: return "ok";
    case BC250_DISPATCH_BAD_ARGUMENT: return "bad_argument";
    case BC250_DISPATCH_INVALID_ENGINE: return "invalid_engine";
    case BC250_DISPATCH_BUFFER_TOO_LARGE: return "buffer_too_large";
    case BC250_DISPATCH_MISALIGNED_ADDRESS: return "misaligned_address";
    case BC250_DISPATCH_RING_FULL: return "ring_full";
    case BC250_DISPATCH_CONTEXT_INACTIVE: return "context_inactive";
    default: return "unknown";
    }
}
