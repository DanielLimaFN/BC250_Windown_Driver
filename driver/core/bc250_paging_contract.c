#include "bc250_paging_contract.h"

#include <string.h>

BC250_PAGING_STATUS Bc250PagingBuildTransfer(
    const BC250_PAGING_TRANSFER_PARAMS *Params,
    uint32_t *PagingBuffer,
    uint32_t BufferDwords,
    uint32_t *CursorDwords)
{
    BC250_SDMA_STATUS sdmaStatus;

    if (Params == NULL || PagingBuffer == NULL || CursorDwords == NULL) {
        return BC250_PAGING_BAD_ARGUMENT;
    }
    if (Params->TransferSizeBytes == 0 || Params->TransferSizeBytes > BC250_SDMA_COPY_MAX_BYTES) {
        return BC250_PAGING_BAD_ARGUMENT;
    }

    sdmaStatus = Bc250SdmaEmitCopyLinear(
        PagingBuffer, BufferDwords, CursorDwords,
        Params->SourceGpuAddress, Params->DestinationGpuAddress, Params->TransferSizeBytes);

    if (sdmaStatus == BC250_SDMA_RING_FULL) {
        return BC250_PAGING_BUFFER_TOO_SMALL;
    }
    if (sdmaStatus != BC250_SDMA_OK) {
        return BC250_PAGING_BAD_ARGUMENT;
    }

    return BC250_PAGING_OK;
}

BC250_PAGING_STATUS Bc250PagingBuildFill(
    const BC250_PAGING_FILL_PARAMS *Params,
    uint32_t *PagingBuffer,
    uint32_t BufferDwords,
    uint32_t *CursorDwords)
{
    BC250_SDMA_STATUS sdmaStatus;

    if (Params == NULL || PagingBuffer == NULL || CursorDwords == NULL) {
        return BC250_PAGING_BAD_ARGUMENT;
    }
    if (Params->FillSizeBytes == 0 || Params->FillSizeBytes > BC250_SDMA_FILL_MAX_BYTES) {
        return BC250_PAGING_BAD_ARGUMENT;
    }

    sdmaStatus = Bc250SdmaEmitConstFill(
        PagingBuffer, BufferDwords, CursorDwords,
        Params->DestinationGpuAddress, Params->FillValue, Params->FillSizeBytes);

    if (sdmaStatus == BC250_SDMA_RING_FULL) {
        return BC250_PAGING_BUFFER_TOO_SMALL;
    }
    if (sdmaStatus != BC250_SDMA_OK) {
        return BC250_PAGING_BAD_ARGUMENT;
    }

    return BC250_PAGING_OK;
}

BC250_PAGING_STATUS Bc250PagingBuildUpdatePageTable(
    const BC250_PAGING_UPDATE_PAGE_TABLE_PARAMS *Params,
    uint64_t *PteOutputBuffer,
    uint32_t MaxPteCount,
    uint32_t *WrittenPteCount)
{
    uint32_t i;
    uint64_t physAddr;

    if (Params == NULL || PteOutputBuffer == NULL || WrittenPteCount == NULL) {
        return BC250_PAGING_BAD_ARGUMENT;
    }
    if (Params->EntryCount == 0 || Params->EntryCount > MaxPteCount) {
        return BC250_PAGING_BUFFER_TOO_SMALL;
    }
    if ((Params->PageTableGpuAddress & 7u) != 0 || (Params->PhysicalAddressBase & 0xFFFu) != 0) {
        return BC250_PAGING_MISALIGNED_ADDRESS;
    }

    *WrittenPteCount = 0;
    for (i = 0; i < Params->EntryCount; ++i) {
        physAddr = Params->PhysicalAddressBase + ((uint64_t)i * (uint64_t)BC250_PAGE_SIZE_4K);
        if (Bc250GcvmEncodePte(physAddr, Params->PteFlags, &PteOutputBuffer[i]) != BC250_GCVM_OK) {
            return BC250_PAGING_BAD_ARGUMENT;
        }
        (*WrittenPteCount)++;
    }

    return BC250_PAGING_OK;
}

BC250_PAGING_STATUS Bc250PagingBuildFlushTlb(
    const BC250_PAGING_FLUSH_TLB_PARAMS *Params,
    uint32_t *PagingBuffer,
    uint32_t BufferDwords,
    uint32_t *CursorDwords)
{
    BC250_SDMA_STATUS sdmaStatus;

    if (Params == NULL || PagingBuffer == NULL || CursorDwords == NULL) {
        return BC250_PAGING_BAD_ARGUMENT;
    }
    if (Params->Vmid >= 16u) {
        return BC250_PAGING_BAD_ARGUMENT;
    }

    /* Emit SDMA NOP sequence representing synchronized TLB flush barrier */
    sdmaStatus = Bc250SdmaEmitNop(PagingBuffer, BufferDwords, CursorDwords, 0u);
    if (sdmaStatus == BC250_SDMA_RING_FULL) {
        return BC250_PAGING_BUFFER_TOO_SMALL;
    }
    if (sdmaStatus != BC250_SDMA_OK) {
        return BC250_PAGING_BAD_ARGUMENT;
    }

    return BC250_PAGING_OK;
}

BC250_PAGING_STATUS Bc250PagingBuildRequest(
    const BC250_PAGING_REQUEST *Request,
    uint32_t *PagingBuffer,
    uint32_t BufferDwords,
    uint32_t *CursorDwords)
{
    if (Request == NULL) {
        return BC250_PAGING_BAD_ARGUMENT;
    }

    switch (Request->Operation) {
    case BC250_PAGING_OP_TRANSFER:
        return Bc250PagingBuildTransfer(&Request->Params.Transfer, PagingBuffer, BufferDwords, CursorDwords);
    case BC250_PAGING_OP_FILL:
        return Bc250PagingBuildFill(&Request->Params.Fill, PagingBuffer, BufferDwords, CursorDwords);
    case BC250_PAGING_OP_FLUSH_TLB:
        return Bc250PagingBuildFlushTlb(&Request->Params.FlushTlb, PagingBuffer, BufferDwords, CursorDwords);
    case BC250_PAGING_OP_UPDATE_PAGE_TABLE:
    default:
        return BC250_PAGING_UNSUPPORTED_OPERATION;
    }
}

const char *Bc250PagingStatusName(BC250_PAGING_STATUS Status)
{
    switch (Status) {
    case BC250_PAGING_OK: return "ok";
    case BC250_PAGING_BAD_ARGUMENT: return "bad_argument";
    case BC250_PAGING_BUFFER_TOO_SMALL: return "buffer_too_small";
    case BC250_PAGING_MISALIGNED_ADDRESS: return "misaligned_address";
    case BC250_PAGING_UNSUPPORTED_OPERATION: return "unsupported_operation";
    default: return "unknown";
    }
}
