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

BC250_PM4_STATUS Bc250Pm4EmitDrawIndexAuto(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t IndexCount,
    uint32_t DrawInitiator)
{
    uint32_t header;

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_PM4_BAD_ARGUMENT;
    }
    if (IndexCount == 0u) {
        return BC250_PM4_BAD_ARGUMENT;
    }

    header = BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_DRAW_INDEX_AUTO, 2u);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, header);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, IndexCount);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, DrawInitiator);

    return BC250_PM4_OK;
}

BC250_PM4_STATUS Bc250Pm4EmitColorTarget0(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t BaseGpuVa,
    uint32_t Width,
    uint32_t Height,
    uint32_t PitchBytes,
    uint32_t FormatIndex,
    uint32_t CompSwap)
{
    uint32_t regs[6];
    uint32_t bpp = 4u;
    uint32_t pitchPixels;
    uint32_t pitchTiles;
    uint32_t sliceTiles;

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_PM4_BAD_ARGUMENT;
    }
    if (BaseGpuVa == 0u || Width == 0u || Height == 0u || PitchBytes == 0u) {
        return BC250_PM4_BAD_ARGUMENT;
    }
    /* Base address must be 256-byte aligned for GFX10 CB */
    if ((BaseGpuVa & 0xFFull) != 0u) {
        return BC250_PM4_MISALIGNED;
    }

    pitchPixels = PitchBytes / bpp;
    if (pitchPixels == 0u) pitchPixels = Width;
    pitchTiles = (pitchPixels + 7u) / 8u;
    if (pitchTiles > 0u) pitchTiles -= 1u;
    sliceTiles = ((pitchPixels + 7u) / 8u) * ((Height + 7u) / 8u);
    if (sliceTiles > 0u) sliceTiles -= 1u;

    /*
     * FAIL-CLOSED DE LARGURA DE CAMPO (docs/03, bloco "GFX10 3D").
     * CB_COLOR0_PITCH.TILE_MAX ocupa [10:0] e CB_COLOR0_SLICE.TILE_MAX ocupa [21:0]
     * (gc_10_1_0_sh_mask.h). Mascarar silenciosamente um valor que nao cabe produz
     * um render target com geometria ERRADA e aceita pelo hardware -- exatamente a
     * classe de falso sucesso que a Lei 4 proibe. Aqui a divergencia RECUSA.
     */
    if (pitchTiles > 0x7FFu || sliceTiles > 0x3FFFFFu) {
        return BC250_PM4_BAD_ARGUMENT;
    }

    /* CB_COLOR0_BASE: GPUVA >> 8 */
    regs[0] = (uint32_t)(BaseGpuVa >> 8);
    /* CB_COLOR0_PITCH: TILE_MAX[10:0] = (pitchPixels / 8) - 1 */
    regs[1] = pitchTiles;
    /* CB_COLOR0_SLICE: TILE_MAX[21:0] */
    regs[2] = sliceTiles;
    /* CB_COLOR0_VIEW: slice start (0) and slice max (0) */
    regs[3] = 0u;
    /* CB_COLOR0_INFO: Format, NumberType=UNORM (0), CompSwap, BlendBypass=1 */
    regs[4] = ((FormatIndex & 0x1Fu) << 2) | ((CompSwap & 0x3u) << 11) | (1u << 16);
    /* CB_COLOR0_ATTRIB: TileMode=linear/general (0) */
    regs[5] = 0u;

    return Bc250Pm4EmitSetContextReg(RingBuffer, RingDwords, CursorDwords,
        BC250_PM4_REG_CB_COLOR0_BASE, regs, 6u);
}

BC250_PM4_STATUS Bc250Pm4EmitWindowScissor(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t Left,
    uint32_t Top,
    uint32_t Right,
    uint32_t Bottom)
{
    uint32_t scissorRegs[2];

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_PM4_BAD_ARGUMENT;
    }
    if (Right < Left || Bottom < Top) {
        return BC250_PM4_BAD_ARGUMENT;
    }

    /* PA_SC_WINDOW_SCISSOR_TL: TL_X in [14:0], TL_Y in [30:16], OffsetDisable bit 31 */
    scissorRegs[0] = (Left & 0x7FFFu) | ((Top & 0x7FFFu) << 16) | 0x80000000u;
    /* PA_SC_WINDOW_SCISSOR_BR: BR_X in [14:0], BR_Y in [30:16] */
    scissorRegs[1] = (Right & 0x7FFFu) | ((Bottom & 0x7FFFu) << 16);

    return Bc250Pm4EmitSetContextReg(RingBuffer, RingDwords, CursorDwords,
        BC250_PM4_REG_PA_SC_WINDOW_SCISSOR_TL, scissorRegs, 2u);
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
    if (ExtraCount == 0u || ExtraCount > 0x3FF0u) {
        return BC250_PM4_PAYLOAD_TOO_LARGE;
    }

    header = BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_NOP, ExtraCount);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, header);

    for (i = 0; i < ExtraCount; ++i) {
        Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, 0);
    }

    return BC250_PM4_OK;
}

BC250_PM4_STATUS Bc250Pm4EmitPacket2Nop(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords)
{
    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_PM4_BAD_ARGUMENT;
    }

    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, BC250_PM4_PACKET2_NOP);
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
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)((IbAddress >> 32) & 0xFFFFu));
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (SizeInDwords & BC250_PM4_IB_SIZE_MASK) | ((Vmid & 0xFu) << BC250_PM4_IB_VMID_SHIFT) | BC250_PM4_IB_VALID_BIT);

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
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (2u << 29)); /* 64-bit data write */
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(FenceAddress & 0xFFFFFFFFull));
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(FenceAddress >> 32));
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(FenceSequence & 0xFFFFFFFFull));
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(FenceSequence >> 32));
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, 0u); /* ctxid/reserved */

    return BC250_PM4_OK;
}

BC250_PM4_STATUS Bc250Pm4EmitReleaseMemEop32(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t FenceAddress,
    uint32_t FenceValue,
    uint32_t IntSel)
{
    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_PM4_BAD_ARGUMENT;
    }
    if (IntSel != BC250_PM4_RELEASE_MEM_INT_SEL_NONE &&
        IntSel != BC250_PM4_RELEASE_MEM_INT_SEL_WRITE_CONFIRM) {
        return BC250_PM4_BAD_ARGUMENT;
    }
    if ((FenceAddress & 3u) != 0) {
        return BC250_PM4_MISALIGNED;
    }

    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords,
        BC250_PM4_RELEASE_MEM_HEADER_PROVEN);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords,
        BC250_PM4_RELEASE_MEM_EVENT_PROVEN);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords,
        BC250_PM4_RELEASE_MEM_DATA_SEL_32 | IntSel);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords,
        (uint32_t)(FenceAddress & 0xFFFFFFFFull));
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords,
        (uint32_t)(FenceAddress >> 32));
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, FenceValue);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, 0u);
    Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords, 0u);

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
