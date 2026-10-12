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

BC250_SDMA_STATUS Bc250SdmaEmitCopyLinearSubWindow32(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t SrcAddress,
    uint32_t SrcPitchBytes,
    uint64_t DstAddress,
    uint32_t DstPitchBytes,
    uint32_t DstX,
    uint32_t DstY,
    uint32_t Width,
    uint32_t Height)
{
    uint32_t srcPitchElements;
    uint32_t dstPitchElements;
    uint64_t srcSliceElements;
    uint64_t dstSliceElements;

    if (RingBuffer == NULL || CursorDwords == NULL ||
        !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_SDMA_BAD_ARGUMENT;
    }
    if ((SrcAddress & 3ull) != 0ull || (DstAddress & 3ull) != 0ull ||
        (SrcPitchBytes & 3u) != 0u || (DstPitchBytes & 3u) != 0u) {
        return BC250_SDMA_MISALIGNED;
    }
    if (Width == 0u || Height == 0u || Width > 0x4000u ||
        Height > 0x4000u || DstX > 0x3FFFu || DstY > 0x3FFFu) {
        return BC250_SDMA_PAYLOAD_TOO_LARGE;
    }
    srcPitchElements = SrcPitchBytes / 4u;
    dstPitchElements = DstPitchBytes / 4u;
    if (srcPitchElements < Width || dstPitchElements < Width ||
        DstX > dstPitchElements - Width ||
        srcPitchElements > 0x80000u || dstPitchElements > 0x80000u) {
        return BC250_SDMA_OUT_OF_BOUNDS;
    }
    srcSliceElements = (uint64_t)srcPitchElements * Height;
    dstSliceElements = (uint64_t)dstPitchElements * (DstY + Height);
    if (srcSliceElements == 0ull || srcSliceElements > 0x10000000ull ||
        dstSliceElements == 0ull || dstSliceElements > 0x10000000ull) {
        return BC250_SDMA_PAYLOAD_TOO_LARGE;
    }

    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (BC250_SDMA_OP_COPY & 0xFFu) |
        ((BC250_SDMA_SUBOP_COPY_LINEAR_SUB_WIND & 0xFFu) << 8) |
        (BC250_SDMA_FILL_SIZE_DWORD << 29));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (uint32_t)SrcAddress);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (uint32_t)(SrcAddress >> 32));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, 0u); /* src x/y */
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (srcPitchElements - 1u) << 13); /* src z=0 */
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (uint32_t)(srcSliceElements - 1ull));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (uint32_t)DstAddress);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (uint32_t)(DstAddress >> 32));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (DstX & 0x3FFFu) | ((DstY & 0x3FFFu) << 16));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (dstPitchElements - 1u) << 13); /* dst z=0 */
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (uint32_t)(dstSliceElements - 1ull));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        ((Width - 1u) & 0x3FFFu) | (((Height - 1u) & 0x3FFFu) << 16));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, 0u); /* rect z/endian */
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
    if (ByteCount == 0 || ByteCount > BC250_SDMA_CONST_FILL_PACKET_MAX_BYTES) {
        return BC250_SDMA_PAYLOAD_TOO_LARGE;
    }
    if ((ByteCount & 3u) != 0 || (DstAddress & 3u) != 0) {
        return BC250_SDMA_MISALIGNED;
    }

    header = (BC250_SDMA_OP_CONST_FILL & 0xFFu) |
             ((BC250_SDMA_FILL_SIZE_DWORD & 0x3u) << 30);
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

BC250_SDMA_STATUS Bc250SdmaEmitFence64(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t FenceAddress,
    uint64_t FenceValue)
{
    uint32_t header;

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_SDMA_BAD_ARGUMENT;
    }
    if ((FenceAddress & 3u) != 0) {
        return BC250_SDMA_MISALIGNED;
    }

    /*
     * SDMA 5.0 (Navi/GFX10) 64-bit Fence Packet:
     * DW0: opcode 5, MTYPE_UC (bits 18:16 = 3), WRITE64 (bit 31 = 1) -> 0x80030005
     * DW1: FenceAddress[31:0]
     * DW2: FenceAddress[63:32]
     * DW3: FenceValue[31:0]
     * DW4: FenceValue[63:32]
     */
    header = (BC250_SDMA_OP_FENCE & 0xFFu) | (3u << 16) | (1u << 31);

    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, header);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(FenceAddress & 0xFFFFFFFFull));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(FenceAddress >> 32));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(FenceValue & 0xFFFFFFFFull));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, (uint32_t)(FenceValue >> 32));

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

uint32_t Bc250SdmaIndirectPrePadDwords(uint32_t WptrDwords)
{
    return (2u - WptrDwords) &
        (BC250_SDMA_IB_PACKET_ALIGNMENT_DWORDS - 1u);
}

uint32_t Bc250SdmaIbSizePadDwords(uint32_t IbSizeDwords)
{
    return (0u - IbSizeDwords) &
        (BC250_SDMA_IB_PACKET_ALIGNMENT_DWORDS - 1u);
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

BC250_SDMA_STATUS Bc250SdmaEmitSrbmWrite(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t RegisterDword,
    uint32_t Value)
{
    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_SDMA_BAD_ARGUMENT;
    }
    /* ADDR tem 18 bits (SDMA_PKT_SRBM_WRITE_ADDR_addr_mask); acima disso o
     * pacote apontaria outro registrador, entao e' recusa, nunca truncamento. */
    if ((RegisterDword & ~0x0003FFFFu) != 0u) {
        return BC250_SDMA_OUT_OF_BOUNDS;
    }
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords,
        (BC250_SDMA_OP_SRBM_WRITE & 0xFFu) | (0xFu << 28));
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, RegisterDword);
    Bc250SdmaWriteDword(RingBuffer, RingDwords, CursorDwords, Value);
    return BC250_SDMA_OK;
}

BC250_SDMA_STATUS Bc250SdmaEmitRegWait(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t RegisterDword,
    uint32_t Reference,
    uint32_t Mask)
{
    if ((RegisterDword & ~0x0003FFFFu) != 0u) {
        return BC250_SDMA_OUT_OF_BOUNDS;
    }
    return Bc250SdmaEmitPollRegMem(RingBuffer, RingDwords, CursorDwords,
        (uint64_t)RegisterDword << 2, Reference, Mask,
        BC250_SDMA_POLL_FUNC_EQUAL, 0u,
        BC250_SDMA_REG_WAIT_INTERVAL, BC250_SDMA_REG_WAIT_RETRY_COUNT);
}

uint32_t Bc250GcvmInvalidateRequestValue(uint32_t Vmid, uint32_t FlushType)
{
    return (1u << (Vmid & 0xFu)) | ((FlushType & 0x7u) << 16) |
        BC250_GCVM_INV_REQ_ALL_LEVELS;
}

BC250_SDMA_STATUS Bc250SdmaEmitVmInvalidate(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t Engine,
    uint32_t Vmid,
    uint32_t FlushType)
{
    BC250_SDMA_STATUS status;
    uint32_t reqDword;
    uint32_t ackDword;
    uint32_t ackMask;

    if (RingBuffer == NULL || CursorDwords == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_SDMA_BAD_ARGUMENT;
    }
    if (Vmid == 0u || Vmid >= 16u || FlushType > 7u ||
        Engine >= BC250_GCVM_INV_ENG_COUNT || Engine == 17u) {
        return BC250_SDMA_BAD_ARGUMENT;
    }
    reqDword = BC250_GCVM_INV_ENG0_REQ_DWORD + Engine;
    ackDword = BC250_GCVM_INV_ENG0_ACK_DWORD + Engine;
    ackMask = 1u << Vmid;

    status = Bc250SdmaEmitSrbmWrite(RingBuffer, RingDwords, CursorDwords,
        reqDword, Bc250GcvmInvalidateRequestValue(Vmid, FlushType));
    if (status != BC250_SDMA_OK) return status;
    /* "wait for a cycle to reset vm_inv_eng*_ack" (sdma_v5_0.c) */
    status = Bc250SdmaEmitRegWait(RingBuffer, RingDwords, CursorDwords,
        reqDword, 0u, 0u);
    if (status != BC250_SDMA_OK) return status;
    return Bc250SdmaEmitRegWait(RingBuffer, RingDwords, CursorDwords,
        ackDword, ackMask, ackMask);
}

static uint32_t Bc250SdmaPopCount16(uint32_t Mask)
{
    uint32_t count = 0u;
    Mask &= 0xFFFFu;
    while (Mask != 0u) {
        Mask &= Mask - 1u;
        ++count;
    }
    return count;
}

uint32_t Bc250SdmaSplitIndirectWorstDwords(
    const BC250_SDMA_IB_SPLIT *Splits,
    uint32_t SplitCount)
{
    /* Cada segmento: pre-pad (ate 7) + INDIRECT + GCR. Segmentos <= splits+1. */
    uint32_t total = (SplitCount + 1u) *
        ((BC250_SDMA_IB_PACKET_ALIGNMENT_DWORDS - 1u) +
         BC250_SDMA_DWORDS_INDIRECT + BC250_SDMA_DWORDS_GCR_REQ);
    uint32_t i;
    if (Splits == NULL) return total;
    for (i = 0u; i < SplitCount; ++i) {
        total += Bc250SdmaPopCount16(Splits[i].VmidMask) *
            BC250_SDMA_DWORDS_VM_INVALIDATE;
        if (Splits[i].Sdma1EndDwords != 0u) total += BC250_SDMA_DWORDS_CROSS_HANDOFF;
    }
    return total;
}

uint32_t Bc250SdmaSplitNextChunk(
    const BC250_SDMA_IB_SPLIT *Splits,
    uint32_t SplitCount,
    uint32_t First,
    uint32_t ChunkStartDwords,
    uint32_t PreDwords,
    uint32_t MaxDwords)
{
    uint32_t b = First;
    if (Splits == NULL) return First;
    while (b < SplitCount) {
        uint32_t w = PreDwords +
            Bc250SdmaSplitIndirectWorstDwords(&Splits[First], b + 1u - First) +
            BC250_SDMA_DWORDS_GCR_REQ + BC250_SDMA_DWORDS_FENCE +
            BC250_SDMA_DWORDS_TRAP;
        if (w > MaxDwords && b > First &&
            Bc250SdmaSplitEndDwords(&Splits[b - 1u]) > ChunkStartDwords) {
            break;
        }
        ++b;
    }
    return b;
}

static BC250_SDMA_STATUS Bc250SdmaEmitIbSegment(
    uint32_t *Buffer,
    uint32_t BufferDwords,
    uint32_t *CursorDwords,
    uint32_t RingPosBase,
    uint64_t IbAddress,
    uint32_t FirstDword,
    uint32_t EndDword,
    uint32_t Vmid,
    uint32_t *LastIndirectEndDwords)
{
    BC250_SDMA_STATUS status;
    uint32_t pad = Bc250SdmaIndirectPrePadDwords(RingPosBase + *CursorDwords);
    uint32_t i;
    for (i = 0u; i < pad; ++i) {
        status = Bc250SdmaEmitNop(Buffer, BufferDwords, CursorDwords, 0u);
        if (status != BC250_SDMA_OK) return status;
    }
    status = Bc250SdmaEmitIndirect(Buffer, BufferDwords, CursorDwords,
        IbAddress + ((uint64_t)FirstDword * 4u), EndDword - FirstDword,
        0ull, Vmid);
    if (status != BC250_SDMA_OK) return status;
    if (LastIndirectEndDwords != NULL) {
        *LastIndirectEndDwords = RingPosBase + *CursorDwords;
    }
    return Bc250SdmaEmitGcr(Buffer, BufferDwords, CursorDwords);
}

uint32_t Bc250SdmaSplitEndDwords(const BC250_SDMA_IB_SPLIT *Split)
{
    if (Split == NULL) return 0u;
    return Split->Sdma1EndDwords != 0u ? Split->Sdma1EndDwords : Split->OffsetDwords;
}

static BC250_SDMA_STATUS Bc250SdmaEmitFenceUc(
    uint32_t *Buffer, uint32_t BufferDwords, uint32_t *CursorDwords,
    uint64_t Address, uint32_t Value)
{
    /* Bc250SdmaEmitFence ja' leva MTYPE_UC no cabecalho, como o Linux. */
    return Bc250SdmaEmitFence(Buffer, BufferDwords, CursorDwords, Address, Value);
}

static BC250_SDMA_STATUS Bc250SdmaEmitCrossWait(
    uint32_t *Buffer, uint32_t BufferDwords, uint32_t *CursorDwords,
    uint64_t Address, uint32_t Value)
{
    return Bc250SdmaEmitPollRegMem(Buffer, BufferDwords, CursorDwords,
        Address, Value, 0xFFFFFFFFu, BC250_SDMA_CROSS_POLL_FUNC, 1u,
        BC250_SDMA_CROSS_POLL_INTERVAL, BC250_SDMA_CROSS_POLL_RETRY);
}

BC250_SDMA_STATUS Bc250SdmaEmitSdma1RangeJob(
    uint32_t *Buffer,
    uint32_t BufferDwords,
    uint32_t *CursorDwords,
    uint32_t RingPosBase,
    const BC250_SDMA_CROSS_ENGINE *Cross,
    uint32_t Token,
    uint64_t IbAddress,
    uint32_t FirstDword,
    uint32_t EndDword,
    uint32_t Vmid)
{
    BC250_SDMA_STATUS status;
    if (Buffer == NULL || CursorDwords == NULL || Cross == NULL ||
        !Bc250IsPowerOfTwo32(BufferDwords) || Token == 0u ||
        EndDword <= FirstDword ||
        (FirstDword % BC250_SDMA_IB_PACKET_ALIGNMENT_DWORDS) != 0u ||
        (Cross->GoAddress & 3u) != 0u || (Cross->DoneAddress & 3u) != 0u ||
        Cross->GoAddress == 0ull || Cross->DoneAddress == 0ull) {
        return BC250_SDMA_BAD_ARGUMENT;
    }
    if (*CursorDwords + BC250_SDMA_DWORDS_SDMA1_RANGE_JOB_WORST > BufferDwords) {
        return BC250_SDMA_RING_FULL;
    }
    status = Bc250SdmaEmitCrossWait(Buffer, BufferDwords, CursorDwords,
        Cross->GoAddress, Token);
    if (status != BC250_SDMA_OK) return status;
    status = Bc250SdmaEmitIbSegment(Buffer, BufferDwords, CursorDwords,
        RingPosBase, IbAddress, FirstDword, EndDword, Vmid, NULL);
    if (status != BC250_SDMA_OK) return status;
    return Bc250SdmaEmitFenceUc(Buffer, BufferDwords, CursorDwords,
        Cross->DoneAddress, Token);
}

BC250_SDMA_STATUS Bc250SdmaEmitSplitIndirect(
    uint32_t *Buffer,
    uint32_t BufferDwords,
    uint32_t *CursorDwords,
    uint32_t RingPosBase,
    uint64_t IbAddress,
    uint32_t IbDwords,
    uint32_t Vmid,
    const BC250_SDMA_IB_SPLIT *Splits,
    uint32_t SplitCount,
    uint32_t *LastIndirectEndDwords)
{
    return Bc250SdmaEmitSplitIndirectCross(Buffer, BufferDwords, CursorDwords,
        RingPosBase, IbAddress, IbDwords, Vmid, Splits, SplitCount, NULL,
        LastIndirectEndDwords);
}

BC250_SDMA_STATUS Bc250SdmaEmitSplitIndirectCross(
    uint32_t *Buffer,
    uint32_t BufferDwords,
    uint32_t *CursorDwords,
    uint32_t RingPosBase,
    uint64_t IbAddress,
    uint32_t IbDwords,
    uint32_t Vmid,
    const BC250_SDMA_IB_SPLIT *Splits,
    uint32_t SplitCount,
    const BC250_SDMA_CROSS_ENGINE *Cross,
    uint32_t *LastIndirectEndDwords)
{
    BC250_SDMA_STATUS status;
    uint32_t prev = 0u;
    uint32_t i = 0u;

    if (Buffer == NULL || CursorDwords == NULL ||
        !Bc250IsPowerOfTwo32(BufferDwords) || IbDwords == 0u ||
        (SplitCount != 0u && Splits == NULL) ||
        SplitCount > BC250_SDMA_IB_SPLIT_MAX) {
        return BC250_SDMA_BAD_ARGUMENT;
    }
    if ((IbAddress & (uint64_t)(BC250_SDMA_IB_ALIGNMENT_BYTES - 1u)) != 0u) {
        return BC250_SDMA_MISALIGNED;
    }
    /* Validacao inteira ANTES de escrever qualquer dword. */
    for (i = 0u; i < SplitCount; ++i) {
        uint32_t mask = Splits[i].VmidMask;
        uint32_t end = Splits[i].Sdma1EndDwords;
        if (end != 0u) {
            /* Faixa do SDMA1: sem invalidacao, alinhada, depois do ponto anterior. */
            if (Cross == NULL || mask != 0u || Splits[i].Sdma1Token == 0u ||
                end <= Splits[i].OffsetDwords || end > IbDwords ||
                (i > 0u && Splits[i].OffsetDwords < Bc250SdmaSplitEndDwords(&Splits[i - 1u]))) {
                return BC250_SDMA_BAD_ARGUMENT;
            }
            if ((Splits[i].OffsetDwords % BC250_SDMA_IB_PACKET_ALIGNMENT_DWORDS) != 0u) {
                return BC250_SDMA_MISALIGNED;
            }
            continue;
        }
        if (mask == 0u || (mask & 1u) != 0u || mask > 0xFFFFu ||
            Splits[i].OffsetDwords > IbDwords ||
            (i > 0u && Splits[i].OffsetDwords < Bc250SdmaSplitEndDwords(&Splits[i - 1u]))) {
            return BC250_SDMA_BAD_ARGUMENT;
        }
        if ((Splits[i].OffsetDwords % BC250_SDMA_IB_PACKET_ALIGNMENT_DWORDS) != 0u &&
            Splits[i].OffsetDwords != IbDwords) {
            return BC250_SDMA_MISALIGNED;
        }
    }
    if (*CursorDwords + Bc250SdmaSplitIndirectWorstDwords(Splits, SplitCount) >
        BufferDwords) {
        return BC250_SDMA_RING_FULL;
    }

    i = 0u;
    while (i < SplitCount) {
        uint32_t offset = Splits[i].OffsetDwords;
        uint32_t mask = Splits[i].VmidMask;
        uint32_t vmid;
        uint32_t j = i + 1u;
        if (Splits[i].Sdma1EndDwords != 0u) {
            /* P4.2: o SDMA0 roda ate a faixa, entrega a vez e espera o SDMA1. */
            if (offset > prev) {
                status = Bc250SdmaEmitIbSegment(Buffer, BufferDwords, CursorDwords,
                    RingPosBase, IbAddress, prev, offset, Vmid,
                    LastIndirectEndDwords);
                if (status != BC250_SDMA_OK) return status;
            }
            status = Bc250SdmaEmitFenceUc(Buffer, BufferDwords, CursorDwords,
                Cross->GoAddress, Splits[i].Sdma1Token);
            if (status != BC250_SDMA_OK) return status;
            status = Bc250SdmaEmitCrossWait(Buffer, BufferDwords, CursorDwords,
                Cross->DoneAddress, Splits[i].Sdma1Token);
            if (status != BC250_SDMA_OK) return status;
            prev = Splits[i].Sdma1EndDwords;
            i = j;
            continue;
        }
        while (j < SplitCount && Splits[j].OffsetDwords == offset &&
               Splits[j].Sdma1EndDwords == 0u) {
            mask |= Splits[j].VmidMask;
            ++j;
        }
        if (offset > prev) {
            status = Bc250SdmaEmitIbSegment(Buffer, BufferDwords, CursorDwords,
                RingPosBase, IbAddress, prev, offset, Vmid,
                LastIndirectEndDwords);
            if (status != BC250_SDMA_OK) return status;
        }
        for (vmid = 1u; vmid < 16u; ++vmid) {
            if (((mask >> vmid) & 1u) == 0u) continue;
            status = Bc250SdmaEmitVmInvalidate(Buffer, BufferDwords,
                CursorDwords, BC250_SDMA_INVALIDATE_ENGINE, vmid, 0u);
            if (status != BC250_SDMA_OK) return status;
        }
        prev = offset;
        i = j;
    }
    if (prev < IbDwords) {
        status = Bc250SdmaEmitIbSegment(Buffer, BufferDwords, CursorDwords,
            RingPosBase, IbAddress, prev, IbDwords, Vmid,
            LastIndirectEndDwords);
        if (status != BC250_SDMA_OK) return status;
    }
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
