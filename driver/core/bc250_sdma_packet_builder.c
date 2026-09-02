#include "bc250_sdma_packet_builder.h"

static int Bc250IsPowerOfTwo32(uint32_t Value)
{
    return Value != 0 && (Value & (Value - 1u)) == 0;
}

static void Bc250SdmaWriteDword(
    uint32_t *Ring,
    uint32_t RingDwords,
    uint32_t *Cursor,
    uint32_t Value)
{
    Ring[*Cursor & (RingDwords - 1u)] = Value;
    (*Cursor)++;
}

BC250_SDMA_STATUS Bc250SdmaEmitNop(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t ExtraCount)
{
    uint32_t header;
    uint32_t i;

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_SDMA_BAD_ARGUMENT;
    }
    if (ExtraCount > 0x3FFFu) {
        return BC250_SDMA_PAYLOAD_TOO_LARGE;
    }

    header = (BC250_SDMA_OP_NOP & 0xFFu) | ((ExtraCount & 0x3FFFu) << 16);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, header);

    for (i = 0; i < ExtraCount; ++i) {
        Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, 0);
    }

    return BC250_SDMA_OK;
}

BC250_SDMA_STATUS Bc250SdmaEmitCopyLinear(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t SrcAddress,
    uint64_t DstAddress,
    uint32_t ByteCount)
{
    uint32_t header;
    uint32_t countField;

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_SDMA_BAD_ARGUMENT;
    }
    if (ByteCount == 0 || ByteCount > BC250_SDMA_COPY_MAX_BYTES) {
        return BC250_SDMA_PAYLOAD_TOO_LARGE;
    }

    header = (BC250_SDMA_OP_COPY & 0xFFu) |
             ((BC250_SDMA_SUBOP_COPY_LINEAR & 0xFFu) << 8);
    countField = (ByteCount - 1u) & 0x3FFFFFu;

    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, header);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, countField);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, 0u); /* endian */
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(SrcAddress & 0xFFFFFFFFull));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(SrcAddress >> 32));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(DstAddress & 0xFFFFFFFFull));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(DstAddress >> 32));

    return BC250_SDMA_OK;
}

BC250_SDMA_STATUS Bc250SdmaEmitConstFill(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t DstAddress,
    uint32_t FillValue,
    uint32_t ByteCount)
{
    uint32_t header;
    uint32_t countField;

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_SDMA_BAD_ARGUMENT;
    }
    if (ByteCount == 0 || ByteCount > BC250_SDMA_FILL_MAX_BYTES) {
        return BC250_SDMA_PAYLOAD_TOO_LARGE;
    }
    if ((ByteCount & 3u) != 0 || (DstAddress & 3u) != 0) {
        return BC250_SDMA_MISALIGNED;
    }

    header = (BC250_SDMA_OP_CONST_FILL & 0xFFu) |
             ((BC250_SDMA_FILL_SIZE_DWORD & 0x3u) << 8);
    countField = (ByteCount - 1u) & 0x3FFFFFu;

    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, header);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(DstAddress & 0xFFFFFFFFull));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(DstAddress >> 32));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, FillValue);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, countField);

    return BC250_SDMA_OK;
}

BC250_SDMA_STATUS Bc250SdmaEmitWriteLinear(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t DstAddress,
    const uint32_t *Data,
    uint32_t DwordCount)
{
    uint32_t i;
    if (RingBuffer == NULL || CursorDwords == NULL || Data == NULL ||
        !Bc250IsPowerOfTwo32(RingDwords)) return BC250_SDMA_BAD_ARGUMENT;
    if (DwordCount == 0u || DwordCount > 64u)
        return BC250_SDMA_PAYLOAD_TOO_LARGE;
    if ((DstAddress & 3u) != 0u) return BC250_SDMA_MISALIGNED;
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        BC250_SDMA_OP_WRITE | (BC250_SDMA_SUBOP_WRITE_LINEAR << 8));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (uint32_t)DstAddress);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (uint32_t)(DstAddress >> 32));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        DwordCount - 1u);
    for (i = 0u; i < DwordCount; ++i)
        Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, Data[i]);
    return BC250_SDMA_OK;
}

BC250_SDMA_STATUS Bc250SdmaEmitFence(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t FenceAddress,
    uint32_t FenceSequence)
{
    uint32_t header;

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_SDMA_BAD_ARGUMENT;
    }
    if ((FenceAddress & 3u) != 0) {
        return BC250_SDMA_MISALIGNED;
    }

    header = (BC250_SDMA_OP_FENCE & 0xFFu) | (3u << 16); /* MTYPE_UC */

    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, header);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(FenceAddress & 0xFFFFFFFFull));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(FenceAddress >> 32));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, FenceSequence);

    return BC250_SDMA_OK;
}

BC250_SDMA_STATUS Bc250SdmaEmitGcr(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords)
{
    if (RingBuffer == NULL || CursorDwords == NULL ||
        !Bc250IsPowerOfTwo32(RingDwords)) return BC250_SDMA_BAD_ARGUMENT;
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        BC250_SDMA_OP_GCR_REQ);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, 0u);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, 0xC3A10000u);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, 0u);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, 0u);
    return BC250_SDMA_OK;
}

BC250_SDMA_STATUS Bc250SdmaEmitIndirect(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t IbAddress,
    uint32_t IbSizeDwords,
    uint64_t CsaAddress,
    uint32_t Vmid)
{
    uint32_t header;

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_SDMA_BAD_ARGUMENT;
    }
    if (IbSizeDwords == 0u || IbSizeDwords > 0xFFFFFu || Vmid > 0xFu) {
        return BC250_SDMA_BAD_ARGUMENT;
    }
    if ((IbAddress & (uint64_t)(BC250_SDMA_IB_ALIGNMENT_BYTES - 1u)) != 0 ||
        (CsaAddress & 3u) != 0) {
        return BC250_SDMA_MISALIGNED;
    }

    header = (BC250_SDMA_OP_INDIRECT & 0xFFu) | ((Vmid & 0xFu) << 16);

    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, header);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (uint32_t)(IbAddress & 0xFFFFFFE0ull));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (uint32_t)(IbAddress >> 32));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, IbSizeDwords);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (uint32_t)(CsaAddress & 0xFFFFFFFFull));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (uint32_t)(CsaAddress >> 32));

    return BC250_SDMA_OK;
}

BC250_SDMA_STATUS Bc250SdmaEmitTrap(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t IntContext)
{
    uint32_t header;

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_SDMA_BAD_ARGUMENT;
    }

    header = (BC250_SDMA_OP_TRAP & 0xFFu);

    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, header);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, IntContext);

    return BC250_SDMA_OK;
}

BC250_SDMA_STATUS Bc250SdmaEmitPollRegMem(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t Address,
    uint32_t Value,
    uint32_t Mask,
    uint32_t Func,
    uint32_t MemPoll,
    uint32_t IntervalUs,
    uint32_t RetryCount)
{
    uint32_t header;
    uint32_t timing;

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_SDMA_BAD_ARGUMENT;
    }
    if ((Address & 3u) != 0) {
        return BC250_SDMA_MISALIGNED;
    }

    header = (BC250_SDMA_OP_POLL_REGMEM & 0xFFu) |
             ((Func & 0x7u) << 28) |
             ((MemPoll ? 1u : 0u) << 31);
    timing = (IntervalUs & 0xFFFFu) | ((RetryCount & 0xFFFu) << 16);

    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, header);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(Address & 0xFFFFFFFFull));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(Address >> 32));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, Value);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, Mask);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, timing);

    return BC250_SDMA_OK;
}

BC250_SDMA_STATUS Bc250SdmaRingComputeAvailableSpace(
    uint32_t RingDwords,
    uint32_t RptrDwords,
    uint32_t WptrDwords,
    uint32_t *AvailableDwords)
{
    uint32_t used;

    if (AvailableDwords == NULL) return BC250_SDMA_BAD_ARGUMENT;
    *AvailableDwords = 0;
    if (!Bc250IsPowerOfTwo32(RingDwords)) return BC250_SDMA_BAD_ARGUMENT;

    used = (WptrDwords - RptrDwords) & (RingDwords - 1u);
    *AvailableDwords = (RingDwords - 1u) - used;
    return BC250_SDMA_OK;
}

BC250_SDMA_STATUS Bc250SdmaRingAdvanceWptr(
    uint32_t RingDwords,
    uint32_t CurrentWptrDwords,
    uint32_t EmittedDwords,
    uint32_t *NewWptrDwords)
{
    if (NewWptrDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_SDMA_BAD_ARGUMENT;
    }

    *NewWptrDwords = (CurrentWptrDwords + EmittedDwords) & (RingDwords - 1u);
    return BC250_SDMA_OK;
}

const char *Bc250SdmaStatusName(BC250_SDMA_STATUS Status)
{
    switch (Status) {
    case BC250_SDMA_OK: return "ok";
    case BC250_SDMA_BAD_ARGUMENT: return "bad_argument";
    case BC250_SDMA_RING_FULL: return "ring_full";
    case BC250_SDMA_MISALIGNED: return "misaligned";
    case BC250_SDMA_PAYLOAD_TOO_LARGE: return "payload_too_large";
    case BC250_SDMA_OUT_OF_BOUNDS: return "out_of_bounds";
    default: return "unknown";
    }
}
