/* bc250_dcn_flip_contract.c -- DCN.1 (doc 710). Ver o cabecalho. */
#include "bc250_dcn_flip_contract.h"

uint32_t Bc250DcnFlipEvaluateGop(
    const BC250_DCN_GOP_STATE *State,
    uint32_t ModeWidth, uint32_t ModeHeight, uint32_t ModePitchBytes,
    BC250_DCN_FLIP_PLAN *OutPlan)
{
    uint32_t refuse = 0u;
    uint32_t width, height, pitchPixels;
    uint64_t gop;

    if (State == 0 || OutPlan == 0) return BC250_DCN_FLIP_REFUSE_MODE;
    OutPlan->GopAddress = 0u;
    OutPlan->Width = 0u;
    OutPlan->Height = 0u;
    OutPlan->PitchPixels = 0u;
    OutPlan->PixelFormat = 0u;
    OutPlan->BaseFlipControl = 0u;

    if (State->OtgControl == 0xFFFFFFFFu || State->PrimaryAddrLo == 0xFFFFFFFFu ||
        State->FlipControl == 0xFFFFFFFFu) {
        return BC250_DCN_FLIP_REFUSE_DEAD_BLOCK;
    }
    if (ModeWidth == 0u || ModeHeight == 0u || ModePitchBytes < ModeWidth * 4u ||
        (ModePitchBytes % 4u) != 0u) {
        refuse |= BC250_DCN_FLIP_REFUSE_MODE;
    }

    width = (State->ViewportDim & BC250_DCN_PRI_VIEWPORT_WIDTH_MASK);
    height = (State->ViewportDim & BC250_DCN_PRI_VIEWPORT_HEIGHT_MASK) >>
        BC250_DCN_PRI_VIEWPORT_HEIGHT_SHIFT;
    /* O campo PITCH do HUBP e' em pixels menos 1 (lido 2559 para 2560). */
    pitchPixels = (State->SurfacePitch & BC250_DCN_SURFACE_PITCH_MASK) + 1u;
    gop = ((uint64_t)(State->PrimaryAddrHi & BC250_DCN_PRIMARY_ADDR_HIGH_MASK) << 32) |
        (uint64_t)State->PrimaryAddrLo;

    if ((State->OtgControl & BC250_DCN_OTG_MASTER_EN_MASK) == 0u)
        refuse |= BC250_DCN_FLIP_REFUSE_OTG_OFF;
    if (width != ModeWidth || height != ModeHeight)
        refuse |= BC250_DCN_FLIP_REFUSE_VIEWPORT;
    if (pitchPixels * 4u != ModePitchBytes)
        refuse |= BC250_DCN_FLIP_REFUSE_PITCH;
    if ((State->SurfaceControl & BC250_DCN_PRIMARY_SURFACE_DCC_EN_MASK) != 0u)
        refuse |= BC250_DCN_FLIP_REFUSE_DCC;
    if ((State->FlipControl & BC250_DCN_SURFACE_UPDATE_LOCK_MASK) != 0u)
        refuse |= BC250_DCN_FLIP_REFUSE_SURFACE_LOCK;
    if ((State->MasterUpdateLock & BC250_DCN_OTG_MASTER_UPDATE_LOCK_MASK) != 0u)
        refuse |= BC250_DCN_FLIP_REFUSE_MASTER_LOCK;
    if (gop == 0u || (gop % BC250_DCN_SURFACE_ADDRESS_ALIGN) != 0u)
        refuse |= BC250_DCN_FLIP_REFUSE_GOP_ADDRESS;

    OutPlan->GopAddress = gop;
    OutPlan->Width = width;
    OutPlan->Height = height;
    OutPlan->PitchPixels = pitchPixels;
    OutPlan->PixelFormat = State->SurfaceConfig & BC250_DCN_SURFACE_PIXEL_FORMAT_MASK;
    OutPlan->BaseFlipControl = State->FlipControl;
    return refuse;
}

int Bc250DcnFlipAddressForVramOffset(
    uint64_t GopAddress, uint64_t ScanoutVramOffset,
    uint64_t SourceVramOffset, uint64_t SurfaceBytes, uint64_t VramBytes,
    uint64_t *OutAddress)
{
    uint64_t address;
    if (OutAddress == 0) return 0;
    *OutAddress = 0u;
    if (GopAddress == 0u || SurfaceBytes == 0u || VramBytes == 0u) return 0;
    if (ScanoutVramOffset > GopAddress) return 0;
    if (SourceVramOffset >= VramBytes || SurfaceBytes > VramBytes - SourceVramOffset) return 0;
    if ((SourceVramOffset % BC250_DCN_SURFACE_ADDRESS_ALIGN) != 0u) return 0;
    address = GopAddress - ScanoutVramOffset + SourceVramOffset;
    if (address < GopAddress - ScanoutVramOffset) return 0; /* estouro */
    if ((address >> 48) != 0u) return 0;
    *OutAddress = address;
    return 1;
}

uint32_t Bc250DcnFlipControlValue(uint32_t BaseFlipControl, int Immediate)
{
    /* Lei 6: (lido & ~mask) | (valor & mask). Nao tocar o lock nem o resto. */
    uint32_t value = Immediate ? BC250_DCN_SURFACE_FLIP_TYPE_MASK : 0u;
    return (BaseFlipControl & ~BC250_DCN_SURFACE_FLIP_TYPE_MASK) |
        (value & BC250_DCN_SURFACE_FLIP_TYPE_MASK);
}

int Bc250DcnFlipIsPending(
    uint32_t FlipControl, uint32_t EarliestInUseLo, uint32_t EarliestInUseHi,
    uint64_t RequestedAddress)
{
    uint64_t earliest = ((uint64_t)(EarliestInUseHi & BC250_DCN_PRIMARY_ADDR_HIGH_MASK) << 32) |
        (uint64_t)EarliestInUseLo;
    if ((FlipControl & BC250_DCN_SURFACE_FLIP_PENDING_MASK) != 0u) return 1;
    return earliest != RequestedAddress ? 1 : 0;
}

uint32_t Bc250DcnFlipSupersededBetween(uint32_t ResolvedSeq, uint32_t LatchedSeq)
{
    if (LatchedSeq <= ResolvedSeq) return 0u;
    return LatchedSeq - ResolvedSeq - 1u;
}

uint32_t Bc250DcnFrameDelta(uint32_t FromFrame, uint32_t ToFrame)
{
    return (ToFrame - FromFrame) & BC250_DCN_FRAME_COUNT_MASK;
}

uint32_t Bc250DcnLatchFrameBucket(uint32_t Frames)
{
    return Frames >= 3u ? 3u : Frames;
}

uint32_t Bc250DcnLatchUsBucket(uint32_t Microseconds)
{
    if (Microseconds < 16700u) return 0u;
    if (Microseconds < 33400u) return 1u;
    if (Microseconds < 50000u) return 2u;
    return 3u;
}

uint32_t Bc250DcnRefreshMilliHz(uint32_t Frames, uint64_t ElapsedMicroseconds)
{
    if (ElapsedMicroseconds == 0ull) return 0u;
    return (uint32_t)(((uint64_t)Frames * 1000000000ull) / ElapsedMicroseconds);
}

int Bc250DcnFlipLatchIsViaIrq(int FlipIrqDpcPendingConsumed, int LatchDetectedThisCall)
{
    return (FlipIrqDpcPendingConsumed != 0 && LatchDetectedThisCall != 0) ? 1 : 0;
}
