/*
 * bc250_gfx_viewport_scissor.c - Implementacao do Contrato de Viewport e Scissor (B.3.1)
 *
 * Implementa os calculos matematicos IEEE-754 de escala e deslocamento,
 * a validacao fail-closed e a emissao dos pacotes PM4 SET_CONTEXT_REG (IT=0x69).
 *
 * Alvo: AMD BC-250 (GC 10.1.3 / Cyan Skillfish 2)
 */

#include "bc250_gfx_viewport_scissor.h"

static uint32_t Bc250FloatToDword(float Val)
{
    union {
        float f;
        uint32_t u;
    } cvt;
    cvt.f = Val;
    return cvt.u;
}

static bool Bc250FloatIsFinite(float Val)
{
    union {
        float f;
        uint32_t u;
    } cvt;
    cvt.f = Val;
    /* Expoente de 8 bits em [30:23] todo 1 indica NaN ou Infinito */
    return ((cvt.u & 0x7F800000u) != 0x7F800000u);
}

BC250_GFX_VPORT_STATUS Bc250GfxComputeViewportScissor(
    const BC250_GFX_VIEWPORT_DIMS *Dims,
    const BC250_GFX_SCISSOR_RECT *Scissor,
    BC250_GFX_VIEWPORT_SCISSOR_PACKET *PacketOut)
{
    float xScale, xOffset;
    float yScale, yOffset;
    float zScale, zOffset;

    if (Dims == NULL || Scissor == NULL || PacketOut == NULL) {
        return BC250_GFX_VPORT_INVALID_ARGUMENT;
    }

    /* Validacao de finitude IEEE-754 (rejeita NaN e Infinitos) */
    if (!Bc250FloatIsFinite(Dims->X) || !Bc250FloatIsFinite(Dims->Y) ||
        !Bc250FloatIsFinite(Dims->Width) || !Bc250FloatIsFinite(Dims->Height) ||
        !Bc250FloatIsFinite(Dims->MinDepth) || !Bc250FloatIsFinite(Dims->MaxDepth)) {
        return BC250_GFX_VPORT_INVALID_ARGUMENT;
    }

    /* Validacao de limites de Viewport */
    if (Dims->Width <= 0.0f || Dims->Height <= 0.0f ||
        Dims->Width > BC250_GFX_MAX_VIEWPORT_DIM || Dims->Height > BC250_GFX_MAX_VIEWPORT_DIM) {
        return BC250_GFX_VPORT_OUT_OF_BOUNDS;
    }

    /* Validacao de faixa de profundidade (Depth [0.0, 1.0]) */
    if (Dims->MinDepth < 0.0f || Dims->MaxDepth > 1.0f || Dims->MinDepth > Dims->MaxDepth) {
        return BC250_GFX_VPORT_OUT_OF_BOUNDS;
    }

    /* Validacao de limites do retangulo de Scissor */
    if (Scissor->Right < Scissor->Left || Scissor->Bottom < Scissor->Top ||
        Scissor->Right > BC250_GFX_MAX_SCISSOR_COORD || Scissor->Bottom > BC250_GFX_MAX_SCISSOR_COORD) {
        return BC250_GFX_VPORT_OUT_OF_BOUNDS;
    }

    /*
     * Calculo das escalas e offsets para NDC -> Screen Space
     * Xscreen = Xndc * XScale + XOffset
     * Yscreen = Yndc * YScale + YOffset
     * Zscreen = Zndc * ZScale + ZOffset
     */
    xScale = Dims->Width * 0.5f;
    xOffset = Dims->X + xScale;

    /* Convencao DirectX (Y-down): escala negativa e offset na borda superior */
    yScale = -Dims->Height * 0.5f;
    yOffset = Dims->Y + Dims->Height * 0.5f;

    zScale = Dims->MaxDepth - Dims->MinDepth;
    zOffset = Dims->MinDepth;

    /* Bloco 1: Transformacao de Viewport */
    PacketOut->VportXScale = Bc250FloatToDword(xScale);
    PacketOut->VportXOffset = Bc250FloatToDword(xOffset);
    PacketOut->VportYScale = Bc250FloatToDword(yScale);
    PacketOut->VportYOffset = Bc250FloatToDword(yOffset);
    PacketOut->VportZScale = Bc250FloatToDword(zScale);
    PacketOut->VportZOffset = Bc250FloatToDword(zOffset);

    /* Bloco 2: Rasterizer Mode e Viewport Transform Engine Control */
    PacketOut->SuScModeCntl = BC250_GFX_SU_SC_MODE_CNTL_DEFAULT;
    PacketOut->VteCntl = BC250_GFX_VTE_CNTL_DEFAULT;

    /* Bloco 3: Window Scissor */
    PacketOut->WindowScissorTl = (Scissor->Left & 0x7FFFu) |
                                 ((Scissor->Top & 0x7FFFu) << 16) |
                                 BC250_GFX_SCISSOR_OFFSET_DISABLE;
    PacketOut->WindowScissorBr = (Scissor->Right & 0x7FFFu) |
                                 ((Scissor->Bottom & 0x7FFFu) << 16);

    /* Bloco 4: Viewport Scissor 0 */
    PacketOut->VportScissor0Tl = (Scissor->Left & 0x7FFFu) |
                                 ((Scissor->Top & 0x7FFFu) << 16) |
                                 BC250_GFX_SCISSOR_OFFSET_DISABLE;
    PacketOut->VportScissor0Br = (Scissor->Right & 0x7FFFu) |
                                 ((Scissor->Bottom & 0x7FFFu) << 16);

    return BC250_GFX_VPORT_OK;
}

BC250_GFX_VPORT_STATUS Bc250GfxEmitViewportScissorStream(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    const BC250_GFX_VIEWPORT_SCISSOR_PACKET *Packet)
{
    BC250_PM4_STATUS pm4Status;
    uint32_t vportRegs[6];
    uint32_t rasterRegs[2];
    uint32_t winScissorRegs[2];
    uint32_t vpScissorRegs[2];

    if (RingBuffer == NULL || CursorDwords == NULL || Packet == NULL) {
        return BC250_GFX_VPORT_INVALID_ARGUMENT;
    }

    /* Prepara Bloco 1: 6 dwords contiguos (PA_CL_VPORT_XSCALE..ZOFFSET) */
    vportRegs[0] = Packet->VportXScale;
    vportRegs[1] = Packet->VportXOffset;
    vportRegs[2] = Packet->VportYScale;
    vportRegs[3] = Packet->VportYOffset;
    vportRegs[4] = Packet->VportZScale;
    vportRegs[5] = Packet->VportZOffset;

    pm4Status = Bc250Pm4EmitSetContextReg(
        RingBuffer, RingDwords, CursorDwords,
        BC250_PM4_REG_PA_CL_VPORT_XSCALE,
        vportRegs, BC250_GFX_VPORT_TRANSFORM_DWORDS);
    if (pm4Status != BC250_PM4_OK) {
        return (pm4Status == BC250_PM4_RING_FULL) ? BC250_GFX_VPORT_RING_FULL : BC250_GFX_VPORT_INVALID_ARGUMENT;
    }

    /* Prepara Bloco 2: 2 dwords contiguos (PA_SU_SC_MODE_CNTL e PA_CL_VTE_CNTL) */
    rasterRegs[0] = Packet->SuScModeCntl;
    rasterRegs[1] = Packet->VteCntl;

    pm4Status = Bc250Pm4EmitSetContextReg(
        RingBuffer, RingDwords, CursorDwords,
        BC250_PM4_REG_PA_SU_SC_MODE_CNTL,
        rasterRegs, BC250_GFX_VPORT_RASTER_DWORDS);
    if (pm4Status != BC250_PM4_OK) {
        return (pm4Status == BC250_PM4_RING_FULL) ? BC250_GFX_VPORT_RING_FULL : BC250_GFX_VPORT_INVALID_ARGUMENT;
    }

    /* Prepara Bloco 3: 2 dwords contiguos (PA_SC_WINDOW_SCISSOR_TL e BR) */
    winScissorRegs[0] = Packet->WindowScissorTl;
    winScissorRegs[1] = Packet->WindowScissorBr;

    pm4Status = Bc250Pm4EmitSetContextReg(
        RingBuffer, RingDwords, CursorDwords,
        BC250_PM4_REG_PA_SC_WINDOW_SCISSOR_TL,
        winScissorRegs, BC250_GFX_VPORT_WIN_SCISSOR_DWORDS);
    if (pm4Status != BC250_PM4_OK) {
        return (pm4Status == BC250_PM4_RING_FULL) ? BC250_GFX_VPORT_RING_FULL : BC250_GFX_VPORT_INVALID_ARGUMENT;
    }

    /* Prepara Bloco 4: 2 dwords contiguos (PA_SC_VPORT_SCISSOR_0_TL e BR) */
    vpScissorRegs[0] = Packet->VportScissor0Tl;
    vpScissorRegs[1] = Packet->VportScissor0Br;

    pm4Status = Bc250Pm4EmitSetContextReg(
        RingBuffer, RingDwords, CursorDwords,
        BC250_PM4_REG_PA_SC_VPORT_SCISSOR_0_TL,
        vpScissorRegs, BC250_GFX_VPORT_VP_SCISSOR_DWORDS);
    if (pm4Status != BC250_PM4_OK) {
        return (pm4Status == BC250_PM4_RING_FULL) ? BC250_GFX_VPORT_RING_FULL : BC250_GFX_VPORT_INVALID_ARGUMENT;
    }

    return BC250_GFX_VPORT_OK;
}

BC250_GFX_VPORT_STATUS Bc250GfxVerifyViewportScissorStream(
    const uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t CursorDwords,
    const BC250_GFX_VIEWPORT_SCISSOR_PACKET *Packet)
{
    uint32_t mask;
    uint32_t startCursor;

    if (RingBuffer == NULL || Packet == NULL || RingDwords == 0 ||
        (RingDwords & (RingDwords - 1u)) != 0) {
        return BC250_GFX_VPORT_INVALID_ARGUMENT;
    }

    mask = RingDwords - 1u;
    startCursor = CursorDwords - BC250_GFX_VPORT_STREAM_TOTAL_DWORDS;

    /* Bloco 1: VPORT (8 dwords: Header + Offset 0x010F + 6 regs) */
    if (RingBuffer[(startCursor + 0u) & mask] != BC250_GFX_PM4_SET_CTX_VPORT_HEADER_6DW) return BC250_GFX_VPORT_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 1u) & mask] != 0x010Fu) return BC250_GFX_VPORT_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 2u) & mask] != Packet->VportXScale) return BC250_GFX_VPORT_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 3u) & mask] != Packet->VportXOffset) return BC250_GFX_VPORT_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 4u) & mask] != Packet->VportYScale) return BC250_GFX_VPORT_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 5u) & mask] != Packet->VportYOffset) return BC250_GFX_VPORT_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 6u) & mask] != Packet->VportZScale) return BC250_GFX_VPORT_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 7u) & mask] != Packet->VportZOffset) return BC250_GFX_VPORT_STREAM_MISMATCH;

    /* Bloco 2: RASTER/VTE (4 dwords: Header + Offset 0x0205 + 2 regs) */
    if (RingBuffer[(startCursor + 8u) & mask] != BC250_GFX_PM4_SET_CTX_RASTER_HEADER_2DW) return BC250_GFX_VPORT_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 9u) & mask] != 0x0205u) return BC250_GFX_VPORT_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 10u) & mask] != Packet->SuScModeCntl) return BC250_GFX_VPORT_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 11u) & mask] != Packet->VteCntl) return BC250_GFX_VPORT_STREAM_MISMATCH;

    /* Bloco 3: Window Scissor (4 dwords: Header + Offset 0x0081 + 2 regs) */
    if (RingBuffer[(startCursor + 12u) & mask] != BC250_GFX_PM4_SET_CTX_WIN_SCISSOR_HEADER_2DW) return BC250_GFX_VPORT_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 13u) & mask] != 0x0081u) return BC250_GFX_VPORT_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 14u) & mask] != Packet->WindowScissorTl) return BC250_GFX_VPORT_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 15u) & mask] != Packet->WindowScissorBr) return BC250_GFX_VPORT_STREAM_MISMATCH;

    /* Bloco 4: Viewport Scissor 0 (4 dwords: Header + Offset 0x0094 + 2 regs) */
    if (RingBuffer[(startCursor + 16u) & mask] != BC250_GFX_PM4_SET_CTX_VP_SCISSOR_HEADER_2DW) return BC250_GFX_VPORT_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 17u) & mask] != 0x0094u) return BC250_GFX_VPORT_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 18u) & mask] != Packet->VportScissor0Tl) return BC250_GFX_VPORT_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 19u) & mask] != Packet->VportScissor0Br) return BC250_GFX_VPORT_STREAM_MISMATCH;

    return BC250_GFX_VPORT_OK;
}

const char *Bc250GfxVportStatusName(BC250_GFX_VPORT_STATUS Status)
{
    switch (Status) {
    case BC250_GFX_VPORT_OK:               return "OK";
    case BC250_GFX_VPORT_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case BC250_GFX_VPORT_OUT_OF_BOUNDS:    return "OUT_OF_BOUNDS";
    case BC250_GFX_VPORT_RING_FULL:        return "RING_FULL";
    case BC250_GFX_VPORT_STREAM_MISMATCH:  return "STREAM_MISMATCH";
    default:                               return "UNKNOWN";
    }
}
