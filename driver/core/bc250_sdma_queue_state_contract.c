#include "bc250_sdma_queue_state_contract.h"

BC250_SDMA_QUEUE_STATE_STATUS Bc250SdmaDecodeQueueState(
    uint32_t IbCntl,
    uint32_t IbRptr,
    uint32_t IbOffset,
    uint32_t IbSize,
    uint32_t SkipCntl,
    uint32_t ContextStatus,
    uint64_t IbBase,
    uint32_t IbSubRemain,
    BC250_SDMA_QUEUE_STATE *State)
{
    unsigned char *bytes;
    uint32_t index;

    if (State == NULL) {
        return BC250_SDMA_QUEUE_STATE_BAD_ARGUMENT;
    }

    bytes = (unsigned char *)State;
    for (index = 0u; index < (uint32_t)sizeof(*State); ++index) {
        bytes[index] = 0u;
    }

    State->RawIbCntl = IbCntl;
    State->RawIbRptr = IbRptr;
    State->RawIbOffset = IbOffset;
    State->RawIbSize = IbSize;
    State->RawSkipCntl = SkipCntl;
    State->RawContextStatus = ContextStatus;
    State->RawIbSubRemain = IbSubRemain;
    State->RawIbBase = IbBase;

    State->IbEnable = (IbCntl & BC250_SDMA_IB_CNTL_IB_ENABLE_MASK) ? 1u : 0u;
    State->IbSwapEnable =
        (IbCntl & BC250_SDMA_IB_CNTL_IB_SWAP_ENABLE_MASK) ? 1u : 0u;
    State->SwitchInsideIb =
        (IbCntl & BC250_SDMA_IB_CNTL_SWITCH_INSIDE_IB_MASK) ? 1u : 0u;
    State->CmdVmid = (IbCntl & BC250_SDMA_IB_CNTL_CMD_VMID_MASK) >>
        BC250_SDMA_IB_CNTL_CMD_VMID_SHIFT;

    /*
     * O campo OFFSET comeca no bit 2. Extrair sem o shift devolveria o offset
     * em bytes disfarcado de dwords -- quatro vezes o valor certo.
     */
    State->IbRptrDwords = (IbRptr & BC250_SDMA_IB_RPTR_OFFSET_MASK) >>
        BC250_SDMA_IB_RPTR_OFFSET_SHIFT;
    State->IbOffsetDwords = (IbOffset & BC250_SDMA_IB_RPTR_OFFSET_MASK) >>
        BC250_SDMA_IB_RPTR_OFFSET_SHIFT;

    State->IbSizeDwords = IbSize & BC250_SDMA_IB_SIZE_SIZE_MASK;
    State->SkipCount = SkipCntl & BC250_SDMA_SKIP_CNTL_SKIP_COUNT_MASK;
    State->IbSubRemainDwords =
        IbSubRemain & BC250_SDMA_IB_SUB_REMAIN_REMAIN_DW_MASK;

    State->Selected =
        (ContextStatus & BC250_SDMA_CONTEXT_STATUS_SELECTED_MASK) ? 1u : 0u;
    State->Idle =
        (ContextStatus & BC250_SDMA_CONTEXT_STATUS_IDLE_MASK) ? 1u : 0u;
    State->Expired =
        (ContextStatus & BC250_SDMA_CONTEXT_STATUS_EXPIRED_MASK) ? 1u : 0u;
    State->Exception =
        (ContextStatus & BC250_SDMA_CONTEXT_STATUS_EXCEPTION_MASK) >>
        BC250_SDMA_CONTEXT_STATUS_EXCEPTION_SHIFT;
    State->CtxSwAble =
        (ContextStatus & BC250_SDMA_CONTEXT_STATUS_CTXSW_ABLE_MASK) ? 1u : 0u;
    State->CtxSwReady =
        (ContextStatus & BC250_SDMA_CONTEXT_STATUS_CTXSW_READY_MASK) ? 1u : 0u;
    State->Preempted =
        (ContextStatus & BC250_SDMA_CONTEXT_STATUS_PREEMPTED_MASK) ? 1u : 0u;
    State->PreemptDisable =
        (ContextStatus & BC250_SDMA_CONTEXT_STATUS_PREEMPT_DISABLE_MASK) ?
        1u : 0u;

    /*
     * Guardar o que NAO foi decodificado. O header do upstream nao nomeia o
     * bit 1 deste registrador; se ele aparecer ligado numa corrida, o log tem
     * de dizer isso em vez de fingir que o valor foi todo entendido.
     */
    State->UnknownBits = ContextStatus & ~BC250_SDMA_CONTEXT_STATUS_KNOWN_MASK;

    return BC250_SDMA_QUEUE_STATE_OK;
}

BC250_SDMA_QUEUE_VERDICT Bc250SdmaClassifyQueueState(
    const BC250_SDMA_QUEUE_STATE *State)
{
    if (State == NULL) {
        return BC250_SDMA_QUEUE_EXCEPTION;
    }
    /*
     * Ordem de precedencia: primeiro o que denuncia erro, depois o que
     * denuncia estado transitorio, e so' no fim o desfecho benigno. Falhar
     * fechado significa que um estado ambiguo nunca sai como DRAINED.
     */
    if (State->Exception != 0u) {
        return BC250_SDMA_QUEUE_EXCEPTION;
    }
    if (State->IbEnable == 0u) {
        return BC250_SDMA_QUEUE_IB_DISABLED;
    }
    if (State->Preempted != 0u) {
        return BC250_SDMA_QUEUE_PREEMPTED;
    }
    if (State->Idle == 0u) {
        return BC250_SDMA_QUEUE_BUSY_OR_STALLED;
    }
    return BC250_SDMA_QUEUE_DRAINED;
}

const char *Bc250SdmaQueueVerdictName(BC250_SDMA_QUEUE_VERDICT Verdict)
{
    switch (Verdict) {
    case BC250_SDMA_QUEUE_DRAINED:          return "DRAINED";
    case BC250_SDMA_QUEUE_BUSY_OR_STALLED:  return "BUSY_OR_STALLED";
    case BC250_SDMA_QUEUE_EXCEPTION:        return "EXCEPTION";
    case BC250_SDMA_QUEUE_PREEMPTED:        return "PREEMPTED";
    case BC250_SDMA_QUEUE_IB_DISABLED:      return "IB_DISABLED";
    default:                                return "DESCONHECIDO";
    }
}

uint32_t Bc250SdmaQueueLooksStalledInsideIb(
    const BC250_SDMA_QUEUE_STATE *State,
    uint32_t RingDrained)
{
    if (State == NULL || RingDrained == 0u) {
        return 0u;
    }
    /*
     * O padrao medido em 05/09 na variante de VMID 8: o anel drenou, a fila
     * esta SELECTED, a engine chegou a carregar um IB (IB_ENABLE ligado e base
     * nao-zero) e ainda assim IDLE = 0.
     *
     * Exigir IbBase != 0 importa: sem base carregada, "nao ocioso" pode ser
     * simplesmente trabalho de anel pendente, que e' outra coisa.
     */
    if (State->Idle != 0u) {
        return 0u;
    }
    if (State->Selected == 0u || State->IbEnable == 0u) {
        return 0u;
    }
    if (State->RawIbBase == 0ull) {
        return 0u;
    }
    return 1u;
}
