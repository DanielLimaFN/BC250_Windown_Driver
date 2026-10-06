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
    if (Params == NULL || PagingBuffer == NULL || CursorDwords == NULL) {
        return BC250_PAGING_BAD_ARGUMENT;
    }
    (void)BufferDwords;

    /* A GFXHUB invalidation is MMIO + dummy read + ACK, never an SDMA NOP. */
    return BC250_PAGING_UNSUPPORTED_OPERATION;
}

BC250_PAGING_STATUS Bc250PagingBuildGfxhubTlbFlushPlan(
    const BC250_PAGING_FLUSH_TLB_PARAMS *Params,
    BC250_GFXHUB_TLB_FLUSH_PLAN *Plan)
{
    /* Flushes da CPU (MMIO) ficam no engine 17, como o flush de GART do upstream. */
    return Bc250PagingBuildGfxhubTlbFlushPlanForEngine(Params, 17u, Plan);
}

BC250_PAGING_STATUS Bc250PagingBuildGartTlbFlushPlan(
    BC250_GFXHUB_TLB_FLUSH_PLAN *Plan)
{
    if (Plan == NULL) {
        return BC250_PAGING_BAD_ARGUMENT;
    }
    memset(Plan, 0, sizeof(*Plan));
    Plan->Vmid = 0u;
    Plan->EngineId = 17u;
    Plan->FlushType = 0u;
    Plan->RequestRegisterOffset = 0xA28Cu + 4u * 17u;
    Plan->AckRegisterOffset = 0xA2D4u + 4u * 17u;
    Plan->RangeLowRegisterOffset = 0xA31Cu + 8u * 17u;
    Plan->RangeHighRegisterOffset = 0xA320u + 8u * 17u;
    Plan->RequestValue = 1u | 0x00F80000u;
    Plan->AckMask = 1u;
    Plan->ExpectedRangeLow = 0xFFFFFFFFu;
    Plan->ExpectedRangeHigh = 0x0000001Fu;
    Plan->RequiresDummyRequestRead = 1u;
    Plan->UsesSemaphore = 0u;
    Plan->GartVmid0 = 1u;
    return BC250_PAGING_OK;
}

/*
 * DX.32 -- plano de invalidacao para QUALQUER engine do GFXHUB.
 *
 * Offsets por gc_10_1_0_offset.h com GC_BASE[0]=0x1260 (docs/03):
 *   REQ_ENGn = (0x1260 + 0x1643 + n) * 4 = 0xA28C + 4n   (ENG17 = 0xA2D0)
 *   ACK_ENGn = (0x1260 + 0x1655 + n) * 4 = 0xA2D4 + 4n   (ENG17 = 0xA318)
 *   RANGE_LO_ENGn = (0x1260 + 0x1667 + 2n) * 4 = 0xA31C + 8n (ENG17 = 0xA3A4)
 * eng_addr_distance = ENG1_ADDR_RANGE_LO32 - ENG0_ADDR_RANGE_LO32 = 2 dwords
 * (gfxhub_v2_0.c:461). A faixa de TODOS os engines e' 0xFFFFFFFF/0x1F no
 * upstream (gfxhub_v2_0_program_invalidation); o nosso programa global ja faz
 * isso para os engines 0 e 17 (bc250_gcvm_global_mmu.c).
 *
 * Por que existe: na Corrida 76/77 o anel grafico invalidava no MESMO engine 17
 * que a CPU usa por MMIO, sem serializacao entre os dois. O upstream da um
 * engine proprio a cada anel (amdgpu_gmc_allocate_vm_inv_eng) e deixa o 17 para
 * o flush do driver. Aqui so' os engines 0 e 17 tem faixa programada.
 */
BC250_PAGING_STATUS Bc250PagingBuildGfxhubTlbFlushPlanForEngine(
    const BC250_PAGING_FLUSH_TLB_PARAMS *Params,
    uint32_t EngineId,
    BC250_GFXHUB_TLB_FLUSH_PLAN *Plan)
{
    const uint32_t invalidateAllLevels = 0x00F80000u;

    if (Params == NULL || Plan == NULL) {
        return BC250_PAGING_BAD_ARGUMENT;
    }
    if (Params->HubIndex != 0u || Params->Vmid == 0u ||
        Params->Vmid >= 16u || Params->FlushType > 7u ||
        (EngineId != 0u && EngineId != 17u)) {
        return BC250_PAGING_BAD_ARGUMENT;
    }

    memset(Plan, 0, sizeof(*Plan));
    Plan->Vmid = Params->Vmid;
    Plan->EngineId = EngineId;
    Plan->FlushType = Params->FlushType;
    Plan->RequestRegisterOffset = 0xA28Cu + 4u * EngineId;
    Plan->AckRegisterOffset = 0xA2D4u + 4u * EngineId;
    Plan->RangeLowRegisterOffset = 0xA31Cu + 8u * EngineId;
    Plan->RangeHighRegisterOffset = 0xA320u + 8u * EngineId;
    Plan->RequestValue = (1u << Params->Vmid) |
        (Params->FlushType << 16) | invalidateAllLevels;
    Plan->AckMask = 1u << Params->Vmid;
    Plan->ExpectedRangeLow = 0xFFFFFFFFu;
    Plan->ExpectedRangeHigh = 0x0000001Fu;
    Plan->RequiresDummyRequestRead = 1u;
    Plan->UsesSemaphore = 0u;
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
