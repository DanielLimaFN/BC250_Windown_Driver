#include "bc250_persistent_ring_contract.h"

#include <string.h>

static int Bc250PringIsPowerOfTwo(uint32_t Value)
{
    return Value != 0u && (Value & (Value - 1u)) == 0u;
}

static uint32_t Bc250PringPayloadDwords(uint32_t EngineType)
{
    return (EngineType == (uint32_t)BC250_ENGINE_TYPE_SDMA) ?
        BC250_PRING_SDMA_PAYLOAD_DWORDS :
        BC250_PRING_COMPUTE_PAYLOAD_DWORDS;
}

static uint32_t Bc250PringIbAlignment(uint32_t EngineType)
{
    return (EngineType == (uint32_t)BC250_ENGINE_TYPE_SDMA) ?
        BC250_SDMA_IB_ALIGNMENT_BYTES : 4u;
}

BC250_PRING_STATUS Bc250PersistentRingInit(
    BC250_PERSISTENT_RING *Ring,
    uint32_t EngineType,
    uint32_t NodeOrdinal,
    uint64_t RingGpuAddress,
    uint32_t RingSizeDwords,
    uint64_t FenceGpuAddress,
    uint64_t CsaGpuAddress,
    uint32_t Vmid,
    uint32_t CompletionMode)
{
    if (Ring == NULL) {
        return BC250_PRING_BAD_ARGUMENT;
    }
    memset(Ring, 0, sizeof(*Ring));

    if (EngineType != (uint32_t)BC250_ENGINE_TYPE_SDMA &&
        EngineType != (uint32_t)BC250_ENGINE_TYPE_COMPUTE) {
        return BC250_PRING_INVALID_ENGINE;
    }
    if (CompletionMode != (uint32_t)BC250_PRING_COMPLETION_POLL &&
        CompletionMode != (uint32_t)BC250_PRING_COMPLETION_INTERRUPT) {
        return BC250_PRING_BAD_ARGUMENT;
    }
    if (Vmid > 0xFu) {
        return BC250_PRING_BAD_ARGUMENT;
    }
    if (!Bc250PringIsPowerOfTwo(RingSizeDwords) ||
        RingSizeDwords < BC250_PRING_MIN_SIZE_DWORDS ||
        RingSizeDwords > BC250_PRING_MAX_SIZE_DWORDS) {
        return BC250_PRING_INVALID_RING_SIZE;
    }
    /* O anel precisa comportar varios lotes, inclusive com padding maximo. */
    if (RingSizeDwords < (Bc250PringPayloadDwords(EngineType) * 4u)) {
        return BC250_PRING_INVALID_RING_SIZE;
    }
    if (RingGpuAddress == 0ull ||
        (RingGpuAddress & (uint64_t)(BC250_PRING_ALIGNMENT_BYTES - 1u)) != 0) {
        return BC250_PRING_MISALIGNED_ADDRESS;
    }
    if (FenceGpuAddress == 0ull || (FenceGpuAddress & 3u) != 0) {
        return BC250_PRING_MISALIGNED_ADDRESS;
    }
    if (CsaGpuAddress != 0ull && (CsaGpuAddress & 3u) != 0) {
        return BC250_PRING_MISALIGNED_ADDRESS;
    }

    Ring->EngineType = EngineType;
    Ring->NodeOrdinal = NodeOrdinal;
    Ring->Vmid = Vmid;
    Ring->RingGpuAddress = RingGpuAddress;
    Ring->RingSizeDwords = RingSizeDwords;
    Ring->FenceGpuAddress = FenceGpuAddress;
    Ring->CsaGpuAddress = CsaGpuAddress;
    Ring->CompletionMode = CompletionMode;
    Ring->IsActive = 1u;

    return BC250_PRING_OK;
}

BC250_PRING_STATUS Bc250PersistentRingAvailableDwords(
    const BC250_PERSISTENT_RING *Ring,
    uint32_t *AvailableDwords)
{
    uint32_t mask;

    if (Ring == NULL || AvailableDwords == NULL) {
        return BC250_PRING_BAD_ARGUMENT;
    }
    if (Ring->IsActive == 0u) {
        return BC250_PRING_NOT_ACTIVE;
    }
    mask = Ring->RingSizeDwords - 1u;
    /*
     * Um dword permanece sempre livre para que WPTR == RPTR signifique anel
     * vazio, nunca anel cheio.
     */
    *AvailableDwords =
        (Ring->RptrShadowDwords - Ring->WptrDwords - 1u) & mask;
    return BC250_PRING_OK;
}

BC250_PRING_STATUS Bc250PersistentRingPlanSubmission(
    const BC250_PERSISTENT_RING *Ring,
    uint64_t IbGpuAddress,
    uint32_t IbSizeDwords,
    uint64_t FenceValue,
    BC250_PRING_SUBMISSION_PLAN *Plan)
{
    BC250_PRING_STATUS status;
    uint32_t payload;
    uint32_t distanceToEnd;
    uint32_t pad;
    uint32_t total;
    uint32_t available;
    uint32_t alignment;

    if (Plan == NULL) {
        return BC250_PRING_BAD_ARGUMENT;
    }
    memset(Plan, 0, sizeof(*Plan));

    if (Ring == NULL) {
        return BC250_PRING_BAD_ARGUMENT;
    }
    if (Ring->IsActive == 0u) {
        return BC250_PRING_NOT_ACTIVE;
    }
    if (IbSizeDwords == 0u || IbSizeDwords > 0xFFFFFu) {
        return BC250_PRING_BAD_ARGUMENT;
    }
    if (IbGpuAddress == 0ull) {
        return BC250_PRING_BAD_ARGUMENT;
    }
    alignment = Bc250PringIbAlignment(Ring->EngineType);
    if ((IbGpuAddress & (uint64_t)(alignment - 1u)) != 0) {
        return BC250_PRING_MISALIGNED_ADDRESS;
    }
    /*
     * A sequencia de fence avanca exatamente um por submissao.  E' o que torna
     * a aposentadoria nao ambigua a partir dos 32 bits que a GPU grava, sem
     * tabela de busca e sem heuristica.
     */
    if (FenceValue != Ring->LastSubmittedFence + 1ull) {
        return BC250_PRING_FENCE_REGRESSION;
    }

    payload = Bc250PringPayloadDwords(Ring->EngineType);
    distanceToEnd = Ring->RingSizeDwords - Ring->WptrDwords;
    pad = (distanceToEnd < payload) ? distanceToEnd : 0u;
    total = pad + payload;

    status = Bc250PersistentRingAvailableDwords(Ring, &available);
    if (status != BC250_PRING_OK) {
        return status;
    }
    if (total > available) {
        return BC250_PRING_RING_FULL;
    }

    Plan->StartWptrDwords =
        (Ring->WptrDwords + pad) & (Ring->RingSizeDwords - 1u);
    Plan->PadDwords = pad;
    Plan->PayloadDwords = payload;
    Plan->TotalDwords = total;
    Plan->NewWptrDwords =
        (Ring->WptrDwords + total) & (Ring->RingSizeDwords - 1u);
    Plan->Wrapped = (pad > 0u) ? 1u : 0u;
    Plan->AvailableBefore = available;
    Plan->FenceValue = FenceValue;
    Plan->IbGpuAddress = IbGpuAddress;
    Plan->IbSizeDwords = IbSizeDwords;

    return BC250_PRING_OK;
}

BC250_PRING_STATUS Bc250PersistentRingEmitSubmission(
    const BC250_PERSISTENT_RING *Ring,
    const BC250_PRING_SUBMISSION_PLAN *Plan,
    uint32_t *RingBuffer,
    uint32_t RingDwords)
{
    uint32_t cursor;
    uint32_t i;
    uint32_t padCursor;

    if (Ring == NULL || Plan == NULL || RingBuffer == NULL) {
        return BC250_PRING_BAD_ARGUMENT;
    }
    if (Ring->IsActive == 0u) {
        return BC250_PRING_NOT_ACTIVE;
    }
    if (RingDwords != Ring->RingSizeDwords) {
        return BC250_PRING_INVALID_RING_SIZE;
    }
    if (Plan->PayloadDwords != Bc250PringPayloadDwords(Ring->EngineType)) {
        return BC250_PRING_BAD_ARGUMENT;
    }

    /*
     * Padding ate o fim do anel.  PACKET2 (0x80000000) e' o NOP de UM dword no
     * CP; no SDMA o NOP de um dword e' o header com op=0 e count=0.
     */
    padCursor = Ring->WptrDwords;
    for (i = 0u; i < Plan->PadDwords; ++i) {
        if (padCursor >= RingDwords) {
            return BC250_PRING_EMIT_FAILED;
        }
        RingBuffer[padCursor] =
            (Ring->EngineType == (uint32_t)BC250_ENGINE_TYPE_SDMA) ?
            0x00000000u : BC250_PM4_PACKET2_NOP;
        ++padCursor;
    }

    cursor = Plan->StartWptrDwords;
    if (Ring->EngineType == (uint32_t)BC250_ENGINE_TYPE_SDMA) {
        if (Bc250SdmaEmitIndirect(RingBuffer, RingDwords, &cursor,
                Plan->IbGpuAddress, Plan->IbSizeDwords,
                Ring->CsaGpuAddress, Ring->Vmid) != BC250_SDMA_OK) {
            return BC250_PRING_EMIT_FAILED;
        }
        if (Bc250SdmaEmitFence(RingBuffer, RingDwords, &cursor,
                Ring->FenceGpuAddress,
                (uint32_t)Plan->FenceValue) != BC250_SDMA_OK) {
            return BC250_PRING_EMIT_FAILED;
        }
        if (Bc250SdmaEmitTrap(RingBuffer, RingDwords, &cursor,
                Ring->NodeOrdinal) != BC250_SDMA_OK) {
            return BC250_PRING_EMIT_FAILED;
        }
    } else {
        uint32_t intSel =
            (Ring->CompletionMode ==
                (uint32_t)BC250_PRING_COMPLETION_INTERRUPT) ?
            BC250_PM4_RELEASE_MEM_INT_SEL_WRITE_CONFIRM :
            BC250_PM4_RELEASE_MEM_INT_SEL_NONE;

        if (Bc250Pm4EmitIndirectBuffer(RingBuffer, RingDwords, &cursor,
                Plan->IbGpuAddress, Plan->IbSizeDwords,
                Ring->Vmid) != BC250_PM4_OK) {
            return BC250_PRING_EMIT_FAILED;
        }
        if (Bc250Pm4EmitReleaseMemEop32(RingBuffer, RingDwords, &cursor,
                Ring->FenceGpuAddress, (uint32_t)Plan->FenceValue,
                intSel) != BC250_PM4_OK) {
            return BC250_PRING_EMIT_FAILED;
        }
    }

    if (cursor != Plan->NewWptrDwords) {
        return BC250_PRING_EMIT_FAILED;
    }
    return BC250_PRING_OK;
}

BC250_PRING_STATUS Bc250PersistentRingCommit(
    BC250_PERSISTENT_RING *Ring,
    const BC250_PRING_SUBMISSION_PLAN *Plan)
{
    uint32_t expectedStart;

    if (Ring == NULL || Plan == NULL) {
        return BC250_PRING_BAD_ARGUMENT;
    }
    if (Ring->IsActive == 0u) {
        return BC250_PRING_NOT_ACTIVE;
    }
    expectedStart =
        (Ring->WptrDwords + Plan->PadDwords) & (Ring->RingSizeDwords - 1u);
    if (Plan->StartWptrDwords != expectedStart ||
        Plan->TotalDwords != (Plan->PadDwords + Plan->PayloadDwords) ||
        Plan->NewWptrDwords !=
            ((Ring->WptrDwords + Plan->TotalDwords) &
             (Ring->RingSizeDwords - 1u))) {
        return BC250_PRING_BAD_ARGUMENT;
    }
    if (Plan->FenceValue != Ring->LastSubmittedFence + 1ull) {
        return BC250_PRING_FENCE_REGRESSION;
    }

    Ring->WptrDwords = Plan->NewWptrDwords;
    Ring->LastSubmittedFence = Plan->FenceValue;
    Ring->PadDwordsTotal += Plan->PadDwords;
    Ring->WrapCount += Plan->Wrapped;
    ++Ring->SubmissionCount;

    return BC250_PRING_OK;
}

BC250_PRING_STATUS Bc250PersistentRingRetire(
    BC250_PERSISTENT_RING *Ring,
    uint32_t ObservedFenceLow32,
    uint32_t RptrDwords,
    uint32_t *RetiredNow)
{
    uint32_t lastRetiredLow;
    uint32_t inFlight;
    uint32_t delta;

    if (Ring == NULL || RetiredNow == NULL) {
        return BC250_PRING_BAD_ARGUMENT;
    }
    *RetiredNow = 0u;
    if (Ring->IsActive == 0u) {
        return BC250_PRING_NOT_ACTIVE;
    }
    if (RptrDwords >= Ring->RingSizeDwords) {
        return BC250_PRING_BAD_ARGUMENT;
    }

    lastRetiredLow = (uint32_t)Ring->LastRetiredFence;
    inFlight = (uint32_t)(Ring->LastSubmittedFence - Ring->LastRetiredFence);
    delta = ObservedFenceLow32 - lastRetiredLow; /* wrap-safe em 32 bits */

    if (delta > inFlight) {
        /*
         * A GPU gravou um valor fora da janela em voo.  Isso e' corrupcao ou
         * fence de outra engine: nada e' aposentado e o chamador precisa
         * tratar como falha fisica.
         */
        return BC250_PRING_FENCE_REGRESSION;
    }

    Ring->RptrShadowDwords = RptrDwords;
    if (delta == 0u) {
        return BC250_PRING_OK;
    }

    Ring->LastRetiredFence += (uint64_t)delta;
    Ring->RetiredCount += delta;
    *RetiredNow = delta;

    return BC250_PRING_OK;
}

BC250_PRING_STATUS Bc250PersistentRingIsFenceComplete(
    const BC250_PERSISTENT_RING *Ring,
    uint64_t FenceValue,
    uint32_t *IsComplete)
{
    if (Ring == NULL || IsComplete == NULL) {
        return BC250_PRING_BAD_ARGUMENT;
    }
    *IsComplete = (FenceValue <= Ring->LastRetiredFence) ? 1u : 0u;
    return BC250_PRING_OK;
}

const char *Bc250PersistentRingStatusName(BC250_PRING_STATUS Status)
{
    switch (Status) {
    case BC250_PRING_OK: return "ok";
    case BC250_PRING_BAD_ARGUMENT: return "bad_argument";
    case BC250_PRING_INVALID_ENGINE: return "invalid_engine";
    case BC250_PRING_INVALID_RING_SIZE: return "invalid_ring_size";
    case BC250_PRING_MISALIGNED_ADDRESS: return "misaligned_address";
    case BC250_PRING_NOT_ACTIVE: return "not_active";
    case BC250_PRING_RING_FULL: return "ring_full";
    case BC250_PRING_FENCE_REGRESSION: return "fence_regression";
    case BC250_PRING_EMIT_FAILED: return "emit_failed";
    default: return "unknown";
    }
}
