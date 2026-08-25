#include "bc250_pm4_packet_builder.h"

static int Bc250IsPowerOfTwo32(uint32_t Value)
{
    return Value != 0 && (Value & (Value - 1u)) == 0;
}

static void Bc250Pm4WriteDword(
    uint32_t *Ring,
    uint32_t RingDwords,
    uint32_t *Cursor,
    uint32_t Value)
{
    Ring[*Cursor & (RingDwords - 1u)] = Value;
    (*Cursor)++;
}

BC250_PM4_STATUS Bc250Pm4EmitNop(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t ExtraCount)
{
    uint32_t header;
    uint32_t i;

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_PM4_BAD_ARGUMENT;
    }
    if (ExtraCount > 0x3FF0u) {
        return BC250_PM4_PAYLOAD_TOO_LARGE;
    }

    header = BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_NOP, 1u + ExtraCount);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, header);

    for (i = 0; i < ExtraCount; ++i) {
        Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, 0);
    }

    return BC250_PM4_OK;
}

BC250_PM4_STATUS Bc250Pm4EmitWriteData(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t Control,
    uint64_t DstAddress,
    const uint32_t *Data,
    uint32_t DwordCount)
{
    uint32_t header;
    uint32_t i;

    if (RingBuffer == NULL || CursorDwords == NULL || Data == NULL ||
        !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_PM4_BAD_ARGUMENT;
    }
    if (DwordCount == 0 || DwordCount > 0x3FF0u) {
        return BC250_PM4_PAYLOAD_TOO_LARGE;
    }

    header = BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_WRITE_DATA, 3u + DwordCount);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, header);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, Control);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(DstAddress & 0xFFFFFFFFull));
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(DstAddress >> 32));

    for (i = 0; i < DwordCount; ++i) {
        Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, Data[i]);
    }

    return BC250_PM4_OK;
}

BC250_PM4_STATUS Bc250Pm4EmitSetShReg(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t RegDwordOffset,
    const uint32_t *Values,
    uint32_t Count)
{
    uint32_t header;
    uint32_t regOffset;
    uint32_t i;

    if (RingBuffer == NULL || CursorDwords == NULL || Values == NULL ||
        !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_PM4_BAD_ARGUMENT;
    }
    if (Count == 0 || Count > 0x3FF0u) {
        return BC250_PM4_PAYLOAD_TOO_LARGE;
    }

    regOffset = (RegDwordOffset >= BC250_PM4_SH_REG_BASE)
              ? (RegDwordOffset - BC250_PM4_SH_REG_BASE)
              : RegDwordOffset;

    header = BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_SET_SH_REG, 1u + Count);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, header);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, regOffset);

    for (i = 0; i < Count; ++i) {
        Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, Values[i]);
    }

    return BC250_PM4_OK;
}

BC250_PM4_STATUS Bc250Pm4EmitSetContextReg(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t RegDwordOffset,
    const uint32_t *Values,
    uint32_t Count)
{
    uint32_t header;
    uint32_t regOffset;
    uint32_t i;

    if (RingBuffer == NULL || CursorDwords == NULL || Values == NULL ||
        !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_PM4_BAD_ARGUMENT;
    }
    if (Count == 0 || Count > 0x3FF0u) {
        return BC250_PM4_PAYLOAD_TOO_LARGE;
    }

    regOffset = (RegDwordOffset >= BC250_PM4_CONTEXT_REG_BASE)
              ? (RegDwordOffset - BC250_PM4_CONTEXT_REG_BASE)
              : RegDwordOffset;

    header = BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_SET_CONTEXT_REG, 1u + Count);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, header);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, regOffset);

    for (i = 0; i < Count; ++i) {
        Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, Values[i]);
    }

    return BC250_PM4_OK;
}

BC250_PM4_STATUS Bc250Pm4EmitSetUconfigReg(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t RegDwordOffset,
    const uint32_t *Values,
    uint32_t Count)
{
    uint32_t header;
    uint32_t regOffset;
    uint32_t i;

    if (RingBuffer == NULL || CursorDwords == NULL || Values == NULL ||
        !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_PM4_BAD_ARGUMENT;
    }
    if (Count == 0 || Count > 0x3FF0u) {
        return BC250_PM4_PAYLOAD_TOO_LARGE;
    }

    regOffset = (RegDwordOffset >= BC250_PM4_UCONFIG_REG_BASE)
              ? (RegDwordOffset - BC250_PM4_UCONFIG_REG_BASE)
              : RegDwordOffset;

    header = BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_SET_UCONFIG_REG, 1u + Count);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, header);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, regOffset);

    for (i = 0; i < Count; ++i) {
        Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, Values[i]);
    }

    return BC250_PM4_OK;
}

BC250_PM4_STATUS Bc250Pm4EmitWaitRegMem(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t Control,
    uint64_t Address,
    uint32_t RefValue,
    uint32_t Mask,
    uint32_t PollIntervalUs)
{
    uint32_t header;

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_PM4_BAD_ARGUMENT;
    }

    header = BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_WAIT_REG_MEM, 6u);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, header);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, Control);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(Address & 0xFFFFFFFFull));
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(Address >> 32));
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, RefValue);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, Mask);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, PollIntervalUs);

    return BC250_PM4_OK;
}

BC250_PM4_STATUS Bc250Pm4EmitIndirectBuffer(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t IbAddress,
    uint32_t SizeInDwords,
    uint32_t Vmid)
{
    uint32_t header;

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_PM4_BAD_ARGUMENT;
    }
    if ((IbAddress & 3u) != 0) {
        return BC250_PM4_MISALIGNED;
    }

    header = BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_INDIRECT_BUFFER, 3u);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, header);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(IbAddress & 0xFFFFFFFFull));
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)((IbAddress >> 32) & 0xFFFFu) | ((Vmid & 0xFu) << 16));
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (SizeInDwords & 0xFFFFFu) | (1u << 20));

    return BC250_PM4_OK;
}

BC250_PM4_STATUS Bc250Pm4EmitReleaseMem(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t EventType,
    uint32_t EventFlags,
    uint64_t FenceAddress,
    uint64_t FenceSequence)
{
    uint32_t header;
    uint32_t eventDword;

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_PM4_BAD_ARGUMENT;
    }

    header = BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_RELEASE_MEM, 7u);
    eventDword = (EventType & 0x7Fu) | EventFlags;

    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, header);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, eventDword);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (3u << 29)); /* 64-bit data write */
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(FenceAddress & 0xFFFFFFFFull));
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(FenceAddress >> 32));
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(FenceSequence & 0xFFFFFFFFull));
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(FenceSequence >> 32));

    return BC250_PM4_OK;
}

BC250_PM4_STATUS Bc250Pm4EmitEventWrite(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t EventType)
{
    uint32_t header;

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_PM4_BAD_ARGUMENT;
    }

    header = BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_EVENT_WRITE, 1u);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, header);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, EventType & 0xFFu);

    return BC250_PM4_OK;
}

BC250_PM4_STATUS Bc250Pm4RingComputeAvailableSpace(
    uint32_t RingDwords,
    uint32_t RptrDwords,
    uint32_t WptrDwords,
    uint32_t *AvailableDwords)
{
    uint32_t used;

    if (AvailableDwords == NULL) return BC250_PM4_BAD_ARGUMENT;
    *AvailableDwords = 0;
    if (!Bc250IsPowerOfTwo32(RingDwords)) return BC250_PM4_BAD_ARGUMENT;

    used = (WptrDwords - RptrDwords) & (RingDwords - 1u);
    *AvailableDwords = (RingDwords - 1u) - used;
    return BC250_PM4_OK;
}

BC250_PM4_STATUS Bc250Pm4RingAdvanceWptr(
    uint32_t RingDwords,
    uint32_t CurrentWptrDwords,
    uint32_t EmittedDwords,
    uint32_t *NewWptrDwords)
{
    if (NewWptrDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_PM4_BAD_ARGUMENT;
    }

    *NewWptrDwords = (CurrentWptrDwords + EmittedDwords) & (RingDwords - 1u);
    return BC250_PM4_OK;
}

const char *Bc250Pm4StatusName(BC250_PM4_STATUS Status)
{
    switch (Status) {
    case BC250_PM4_OK: return "ok";
    case BC250_PM4_BAD_ARGUMENT: return "bad_argument";
    case BC250_PM4_RING_FULL: return "ring_full";
    case BC250_PM4_MISALIGNED: return "misaligned";
    case BC250_PM4_PAYLOAD_TOO_LARGE: return "payload_too_large";
    case BC250_PM4_OUT_OF_BOUNDS: return "out_of_bounds";
    default: return "unknown";
    }
}
