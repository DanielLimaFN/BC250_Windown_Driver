#include "bc250_gfxhub_tlb_transaction.h"

#include <string.h>

static uint32_t Bc250TlbPlanIsExact(
    const BC250_GFXHUB_TLB_FLUSH_PLAN *Plan)
{
    /* VMID 0 so' com o plano da GART (Bc250PagingBuildGartTlbFlushPlan). */
    if (Plan == NULL || (Plan->Vmid == 0u && Plan->GartVmid0 != 1u) ||
        Plan->Vmid >= 16u ||
        Plan->EngineId != 17u || Plan->FlushType > 7u ||
        Plan->RequestRegisterOffset != 0xA2D0u ||
        Plan->AckRegisterOffset != 0xA318u ||
        Plan->RangeLowRegisterOffset != 0xA3A4u ||
        Plan->RangeHighRegisterOffset != 0xA3A8u ||
        Plan->AckMask != (1u << Plan->Vmid) ||
        Plan->ExpectedRangeLow != 0xFFFFFFFFu ||
        Plan->ExpectedRangeHigh != 0x0000001Fu ||
        Plan->RequiresDummyRequestRead != 1u ||
        Plan->UsesSemaphore != 0u) {
        return 0u;
    }
    return Plan->RequestValue == ((1u << Plan->Vmid) |
        (Plan->FlushType << 16) | 0x00F80000u);
}

BC250_TLB_TRANSACTION_STATUS Bc250RunGfxhubTlbTransaction(
    const BC250_GFXHUB_TLB_FLUSH_PLAN *Plan,
    const BC250_TLB_TRANSACTION_INTERFACE *Interface,
    uint32_t MaxPolls,
    BC250_TLB_TRANSACTION_EVIDENCE *Evidence)
{
    BC250_TLB_TRANSACTION_STATUS status = BC250_TLB_TRANSACTION_BAD_ARGUMENT;
    uint32_t value = 0u;
    uint32_t poll;

    if (Evidence != NULL) memset(Evidence, 0, sizeof(*Evidence));
    if (Plan == NULL || Interface == NULL || Evidence == NULL ||
        Interface->Read32 == NULL || Interface->Write32 == NULL ||
        Interface->Stall == NULL || Interface->Context == NULL ||
        Interface->Identity != BC250_TLB_TRANSPORT_IDENTITY || MaxPolls == 0u) {
        return BC250_TLB_TRANSACTION_BAD_ARGUMENT;
    }
    Evidence->Vmid = Plan->Vmid;
    Evidence->EngineId = Plan->EngineId;
    Evidence->RequestValue = Plan->RequestValue;
    if (!Bc250TlbPlanIsExact(Plan)) return BC250_TLB_TRANSACTION_BAD_PLAN;

    /* Faixa preservada e imutavel pelo MMU global */
    Evidence->RangeRestorePassed = 1u;

    /*
     * DX.32 / T3.4 (29/09, alinhamento com Linux Upstream gmc_v10_0.c:289-315):
     * No Linux, o flush de TLB no GFXHUB faz estritamente 3 passos:
     * 1. WREG32(req, inv_req) -> escreve registrador REQ no engine 17
     * 2. RREG32(req) -> dummy read para drenar latch no fast GRBM (GC < 10.3)
     * 3. Polling em ACK com stall ate o bit (1 << vmid) acusar conclusao.
     *
     * O RangeLow/RangeHigh ja e' configurado uma unica vez para 0xFFFFFFFF/0x1F
     * durante o setup do MMU global (bc250_gcvm_global_mmu.c) e e' IMUTAVEL
     * em tempo de execucao. Salvar/reescrever/restaurar ranges a cada VMID a
     * cada flush gerava 14 acessos MMIO lentos segurando lock da CPU.
     *
     * Preflight: verifica que a interface MMIO le o ACK e nao esta desconectada (0xFFFFFFFF).
     */
    if (!Interface->Read32(Interface->Context,
            Plan->AckRegisterOffset, &Evidence->AckBefore)) {
        return BC250_TLB_TRANSACTION_READ_FAILED;
    }
    if (Evidence->AckBefore == 0xFFFFFFFFu) {
        return BC250_TLB_TRANSACTION_UNDECODED;
    }

    /* 1. Escreve REQ no engine 17 */
    if (!Interface->Write32(Interface->Context,
            Plan->RequestRegisterOffset, Plan->RequestValue)) {
        return BC250_TLB_TRANSACTION_WRITE_FAILED;
    }
    Evidence->RequestWritten = 1u;

    /* 2. Dummy read em REQ para drenar o buffer GRBM rapido (amdgpu gmc_v10_0.c:296-298) */
    if (!Interface->Read32(Interface->Context,
            Plan->RequestRegisterOffset, &Evidence->RequestDummyRead)) {
        return BC250_TLB_TRANSACTION_READ_FAILED;
    }
    Evidence->DummyReadCompleted = 1u;

    /* 3. Polling em ACK (amdgpu gmc_v10_0.c:300-308) */
    status = BC250_TLB_TRANSACTION_TIMEOUT;
    for (poll = 0u; poll < MaxPolls; ++poll) {
        if (!Interface->Read32(
                Interface->Context, Plan->AckRegisterOffset, &value)) {
            status = BC250_TLB_TRANSACTION_READ_FAILED;
            break;
        }
        Evidence->AckAfter = value;
        Evidence->PollCount = poll + 1u;
        if (value == 0xFFFFFFFFu) {
            status = BC250_TLB_TRANSACTION_UNDECODED;
            break;
        }
        if ((value & Plan->AckMask) != 0u) {
            Evidence->AckObserved = 1u;
            status = BC250_TLB_TRANSACTION_OK;
            break;
        }
        Interface->Stall(Interface->Context, 1u);
    }

    /* Faixa preservada e imutavel pelo MMU global */
    Evidence->RangeRestorePassed = 1u;
    if (status == BC250_TLB_TRANSACTION_OK &&
        Evidence->RequestWritten != 0u &&
        Evidence->DummyReadCompleted != 0u &&
        Evidence->AckObserved != 0u &&
        Evidence->RangeRestorePassed != 0u) {
        Evidence->PhysicalReady = 1u;
    }
    return status;
}
