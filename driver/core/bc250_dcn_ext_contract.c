#include "bc250_dcn_ext_contract.h"

/* DCN.2 (doc 713). Proveniencia campo a campo no header. */

uint32_t Bc250DcnField(uint32_t Read, uint32_t Mask, uint32_t Value)
{
    return (Read & ~Mask) | (Value & Mask);
}

/* ---- 1. Timing --------------------------------------------------------------- */

uint32_t Bc250DcnTimingDecode(
    uint32_t HTotalReg, uint32_t HBlankStartEnd, uint32_t HSyncA, uint32_t HSyncCntl,
    uint32_t VTotalReg, uint32_t VBlankStartEnd, uint32_t VSyncA, uint32_t VSyncCntl,
    uint32_t ExpectedWidth, uint32_t ExpectedHeight, BC250_DCN_TIMING *Out)
{
    uint32_t refuse = 0u;
    uint32_t hTotal = (HTotalReg & BC250_DCN2_TOTAL_MASK) + 1u;
    uint32_t vTotal = (VTotalReg & BC250_DCN2_TOTAL_MASK) + 1u;
    uint32_t hBlankStart = HBlankStartEnd & BC250_DCN2_START_MASK;
    uint32_t hBlankEnd = (HBlankStartEnd & BC250_DCN2_END_MASK) >> BC250_DCN2_END_SHIFT;
    uint32_t vBlankStart = VBlankStartEnd & BC250_DCN2_START_MASK;
    uint32_t vBlankEnd = (VBlankStartEnd & BC250_DCN2_END_MASK) >> BC250_DCN2_END_SHIFT;
    uint32_t hSyncStart = HSyncA & BC250_DCN2_START_MASK;
    uint32_t hSyncEnd = (HSyncA & BC250_DCN2_END_MASK) >> BC250_DCN2_END_SHIFT;
    uint32_t vSyncStart = VSyncA & BC250_DCN2_START_MASK;
    uint32_t vSyncEnd = (VSyncA & BC250_DCN2_END_MASK) >> BC250_DCN2_END_SHIFT;

    if (Out == 0) return 0xFFFFFFFFu;
    Out->HTotal = hTotal;
    Out->VTotal = vTotal;
    /* optc1_program_timing: blank_start = total - front porch;
     * blank_end = blank_start - ativo (sem bordas). */
    Out->HActive = (hBlankStart > hBlankEnd) ? hBlankStart - hBlankEnd : 0u;
    Out->VActive = (vBlankStart > vBlankEnd) ? vBlankStart - vBlankEnd : 0u;
    Out->HFrontPorch = (hTotal > hBlankStart) ? hTotal - hBlankStart : 0u;
    Out->VFrontPorch = (vTotal > vBlankStart) ? vTotal - vBlankStart : 0u;
    Out->HSyncWidth = (hSyncEnd > hSyncStart) ? hSyncEnd - hSyncStart : 0u;
    Out->VSyncWidth = (vSyncEnd > vSyncStart) ? vSyncEnd - vSyncStart : 0u;
    /* POL = 0 para sincronismo positivo (optc1_program_timing). */
    Out->HSyncPositive = ((HSyncCntl & BC250_DCN2_SYNC_POL_MASK) == 0u) ? 1u : 0u;
    Out->VSyncPositive = ((VSyncCntl & BC250_DCN2_SYNC_POL_MASK) == 0u) ? 1u : 0u;
    Out->RefreshMilliHz = 0u;
    Out->PixelRateHz = 0u;

    if (hTotal <= Out->HActive || vTotal <= Out->VActive ||
        Out->HActive == 0u || Out->VActive == 0u) {
        refuse |= BC250_DCN_TIMING_REFUSE_TOTAL;
    }
    if (Out->HActive != ExpectedWidth || Out->VActive != ExpectedHeight) {
        refuse |= BC250_DCN_TIMING_REFUSE_ACTIVE;
    }
    if (Out->HSyncWidth == 0u || Out->VSyncWidth == 0u ||
        Out->HFrontPorch + Out->HSyncWidth > hTotal - Out->HActive ||
        Out->VFrontPorch + Out->VSyncWidth > vTotal - Out->VActive) {
        refuse |= BC250_DCN_TIMING_REFUSE_SYNC;
    }
    return refuse;
}

uint32_t Bc250DcnRefreshFromScan(uint32_t Frames, int32_t VertDelta, uint32_t VTotal,
    uint64_t Elapsed100ns)
{
    int64_t lines;
    if (VTotal == 0u || Elapsed100ns == 0ull) return 0u;
    lines = (int64_t)Frames * (int64_t)VTotal + (int64_t)VertDelta;
    if (lines <= 0) return 0u;
    /* mHz = linhas / VTotal / (Elapsed100ns * 1e-7) * 1000 */
    return (uint32_t)(((uint64_t)lines * 10000000000ull) / ((uint64_t)VTotal * Elapsed100ns));
}

uint32_t Bc250DcnTimingApplyRefresh(BC250_DCN_TIMING *Timing, uint32_t RefreshMilliHz)
{
    if (Timing == 0) return BC250_DCN_TIMING_REFUSE_REFRESH;
    if (RefreshMilliHz < 24000u || RefreshMilliHz > 240000u) {
        return BC250_DCN_TIMING_REFUSE_REFRESH;
    }
    Timing->RefreshMilliHz = RefreshMilliHz;
    Timing->PixelRateHz = ((uint64_t)RefreshMilliHz * (uint64_t)Timing->HTotal *
        (uint64_t)Timing->VTotal + 500ull) / 1000ull;
    return BC250_DCN_TIMING_OK;
}

/* ---- 6. EDID ------------------------------------------------------------------- */

void Bc250DcnEncodeDtd(const BC250_DCN_TIMING *Timing, uint32_t HSizeMm, uint32_t VSizeMm,
    uint8_t Dtd[18])
{
    uint32_t i, clock10k, hBlank, vBlank;
    {
        volatile uint8_t *clear = Dtd;
        for (i = 0u; i < 18u; ++i) clear[i] = 0u;
    }
    if (Timing == 0) return;
    clock10k = (uint32_t)((Timing->PixelRateHz + 5000ull) / 10000ull);
    if (clock10k > 0xFFFFu) clock10k = 0xFFFFu;
    hBlank = Timing->HTotal - Timing->HActive;
    vBlank = Timing->VTotal - Timing->VActive;
    Dtd[0] = (uint8_t)(clock10k & 0xFFu);
    Dtd[1] = (uint8_t)((clock10k >> 8) & 0xFFu);
    Dtd[2] = (uint8_t)(Timing->HActive & 0xFFu);
    Dtd[3] = (uint8_t)(hBlank & 0xFFu);
    Dtd[4] = (uint8_t)((((Timing->HActive >> 8) & 0x0Fu) << 4) | ((hBlank >> 8) & 0x0Fu));
    Dtd[5] = (uint8_t)(Timing->VActive & 0xFFu);
    Dtd[6] = (uint8_t)(vBlank & 0xFFu);
    Dtd[7] = (uint8_t)((((Timing->VActive >> 8) & 0x0Fu) << 4) | ((vBlank >> 8) & 0x0Fu));
    Dtd[8] = (uint8_t)(Timing->HFrontPorch & 0xFFu);
    Dtd[9] = (uint8_t)(Timing->HSyncWidth & 0xFFu);
    Dtd[10] = (uint8_t)(((Timing->VFrontPorch & 0x0Fu) << 4) | (Timing->VSyncWidth & 0x0Fu));
    Dtd[11] = (uint8_t)((((Timing->HFrontPorch >> 8) & 0x3u) << 6) |
        (((Timing->HSyncWidth >> 8) & 0x3u) << 4) |
        (((Timing->VFrontPorch >> 4) & 0x3u) << 2) |
        ((Timing->VSyncWidth >> 4) & 0x3u));
    Dtd[12] = (uint8_t)(HSizeMm & 0xFFu);
    Dtd[13] = (uint8_t)(VSizeMm & 0xFFu);
    Dtd[14] = (uint8_t)((((HSizeMm >> 8) & 0x0Fu) << 4) | ((VSizeMm >> 8) & 0x0Fu));
    Dtd[15] = 0u;
    Dtd[16] = 0u;
    /* Digital separado (bits 4-3 = 11) com as polaridades medidas. */
    Dtd[17] = (uint8_t)(0x18u | (Timing->VSyncPositive ? 0x04u : 0u) |
        (Timing->HSyncPositive ? 0x02u : 0u));
}

int Bc250DcnDecodeDtd(const uint8_t Dtd[18], BC250_DCN_TIMING *Out)
{
    uint32_t clock10k = (uint32_t)Dtd[0] | ((uint32_t)Dtd[1] << 8);
    uint32_t hBlank, vBlank;
    if (Out == 0 || clock10k == 0u) return 0;
    Out->HActive = (uint32_t)Dtd[2] | (((uint32_t)Dtd[4] >> 4) << 8);
    hBlank = (uint32_t)Dtd[3] | (((uint32_t)Dtd[4] & 0x0Fu) << 8);
    Out->VActive = (uint32_t)Dtd[5] | (((uint32_t)Dtd[7] >> 4) << 8);
    vBlank = (uint32_t)Dtd[6] | (((uint32_t)Dtd[7] & 0x0Fu) << 8);
    Out->HTotal = Out->HActive + hBlank;
    Out->VTotal = Out->VActive + vBlank;
    Out->HFrontPorch = (uint32_t)Dtd[8] | ((((uint32_t)Dtd[11] >> 6) & 0x3u) << 8);
    Out->HSyncWidth = (uint32_t)Dtd[9] | ((((uint32_t)Dtd[11] >> 4) & 0x3u) << 8);
    Out->VFrontPorch = ((uint32_t)Dtd[10] >> 4) | ((((uint32_t)Dtd[11] >> 2) & 0x3u) << 4);
    Out->VSyncWidth = ((uint32_t)Dtd[10] & 0x0Fu) | (((uint32_t)Dtd[11] & 0x3u) << 4);
    Out->VSyncPositive = (Dtd[17] & 0x04u) ? 1u : 0u;
    Out->HSyncPositive = (Dtd[17] & 0x02u) ? 1u : 0u;
    Out->PixelRateHz = (uint64_t)clock10k * 10000ull;
    Out->RefreshMilliHz = (Out->HTotal != 0u && Out->VTotal != 0u) ?
        (uint32_t)((Out->PixelRateHz * 1000ull) / ((uint64_t)Out->HTotal * Out->VTotal)) : 0u;
    return 1;
}

int Bc250DcnEdidBlockValid(const uint8_t Block[128])
{
    uint32_t i, sum = 0u;
    static const uint8_t header[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    for (i = 0u; i < 8u; ++i) if (Block[i] != header[i]) return 0;
    for (i = 0u; i < 128u; ++i) sum += Block[i];
    return ((sum & 0xFFu) == 0u) ? 1 : 0;
}

int Bc250DcnEdidFindTiming(const uint8_t Block[128], const BC250_DCN_TIMING *Timing)
{
    int k;
    if (Timing == 0 || !Bc250DcnEdidBlockValid(Block)) return -1;
    for (k = 0; k < 4; ++k) {
        BC250_DCN_TIMING t;
        uint64_t lo, hi;
        if (!Bc250DcnDecodeDtd(&Block[54 + 18 * k], &t)) continue;
        if (t.HActive != Timing->HActive || t.VActive != Timing->VActive ||
            t.HTotal != Timing->HTotal || t.VTotal != Timing->VTotal) continue;
        if (Timing->PixelRateHz != 0ull) {
            lo = Timing->PixelRateHz - Timing->PixelRateHz / 200ull;
            hi = Timing->PixelRateHz + Timing->PixelRateHz / 200ull;
            if (t.PixelRateHz < lo || t.PixelRateHz > hi) continue;
        }
        return k;
    }
    return -1;
}

/* ---- 4. Cursor ------------------------------------------------------------------ */

int Bc250DcnCursorConvert(uint32_t Flags, uint32_t Width, uint32_t Height, uint32_t Pitch,
    const uint8_t *Pixels, uint32_t *Dst64x64)
{
    uint32_t x, y;
    if (Pixels == 0 || Dst64x64 == 0 || Width == 0u || Height == 0u ||
        Width > BC250_DCN_CURSOR_MAX || Height > BC250_DCN_CURSOR_MAX) {
        return 0;
    }
    {
        /* volatile: o /O2 trocaria o laco por memset, ausente sem CRT. */
        volatile uint32_t *clear = Dst64x64;
        for (y = 0u; y < BC250_DCN_CURSOR_MAX * BC250_DCN_CURSOR_MAX; ++y) clear[y] = 0u;
    }
    if (Flags == BC250_DCN_POINTER_COLOR) {
        if (Pitch < Width * 4u) return BC250_DCN_CURSOR_REFUSED;
        for (y = 0u; y < Height; ++y) {
            const uint8_t *row = Pixels + (uint64_t)y * Pitch;
            for (x = 0u; x < Width; ++x) {
                Dst64x64[y * BC250_DCN_CURSOR_MAX + x] =
                    (uint32_t)row[4u * x] | ((uint32_t)row[4u * x + 1u] << 8) |
                    ((uint32_t)row[4u * x + 2u] << 16) | ((uint32_t)row[4u * x + 3u] << 24);
            }
        }
        return BC250_DCN_CURSOR_OK;
    }
    if (Flags == BC250_DCN_POINTER_MASKED_COLOR) {
        if (Pitch < Width * 4u) return BC250_DCN_CURSOR_REFUSED;
        for (y = 0u; y < Height; ++y) {
            const uint8_t *row = Pixels + (uint64_t)y * Pitch;
            for (x = 0u; x < Width; ++x) {
                uint32_t rgb = (uint32_t)row[4u * x] | ((uint32_t)row[4u * x + 1u] << 8) |
                    ((uint32_t)row[4u * x + 2u] << 16);
                uint32_t mask = row[4u * x + 3u];
                uint32_t px;
                if (mask == 0x00u) px = 0xFF000000u | rgb;          /* substitui: opaco */
                else if (mask == 0xFFu && rgb == 0u) px = 0u;       /* XOR com 0: transparente */
                else if (mask == 0xFFu) return BC250_DCN_CURSOR_NEEDS_XOR;
                else return BC250_DCN_CURSOR_REFUSED;               /* mascara so' 0 ou 0xFF */
                Dst64x64[y * BC250_DCN_CURSOR_MAX + x] = px;
            }
        }
        return BC250_DCN_CURSOR_OK;
    }
    if (Flags == BC250_DCN_POINTER_MONOCHROME) {
        if (Pitch < (Width + 7u) / 8u) return BC250_DCN_CURSOR_REFUSED;
        for (y = 0u; y < Height; ++y) {
            const uint8_t *andRow = Pixels + (uint64_t)y * Pitch;
            const uint8_t *xorRow = Pixels + (uint64_t)(y + Height) * Pitch;
            for (x = 0u; x < Width; ++x) {
                uint32_t a = (andRow[x >> 3] >> (7u - (x & 7u))) & 1u;
                uint32_t o = (xorRow[x >> 3] >> (7u - (x & 7u))) & 1u;
                uint32_t px;
                if (a == 1u && o == 0u) px = 0x00000000u;        /* transparente */
                else if (a == 0u && o == 0u) px = 0xFF000000u;   /* preto */
                else if (a == 0u) px = 0xFFFFFFFFu;              /* branco */
                else return BC250_DCN_CURSOR_NEEDS_XOR;          /* inverter: software */
                Dst64x64[y * BC250_DCN_CURSOR_MAX + x] = px;
            }
        }
        return BC250_DCN_CURSOR_OK;
    }
    return BC250_DCN_CURSOR_REFUSED;
}

void Bc250DcnCursorPosition(int32_t X, int32_t Y, uint32_t Visible, uint32_t Width,
    uint32_t Height, uint32_t ScreenW, uint32_t ScreenH, BC250_DCN_CURSOR_POS *Out)
{
    uint32_t px, py, hx = 0u, hy = 0u;
    if (Out == 0) return;
    Out->Enable = 0u;
    Out->PositionReg = 0u;
    Out->HotSpotReg = 0u;
    if (!Visible || Width == 0u || Height == 0u) return;
    if (X + (int32_t)Width <= 0 || Y + (int32_t)Height <= 0 ||
        X >= (int32_t)ScreenW || Y >= (int32_t)ScreenH) return;
    if (X < 0) { hx = (uint32_t)(-X); px = 0u; } else { px = (uint32_t)X; }
    if (Y < 0) { hy = (uint32_t)(-Y); py = 0u; } else { py = (uint32_t)Y; }
    if (hx > 0xFFu) hx = 0xFFu;
    if (hy > 0xFFu) hy = 0xFFu;
    Out->Enable = 1u;
    Out->PositionReg = ((px << BC250_DCN2_CURSOR_X_SHIFT) & BC250_DCN2_CURSOR_X_MASK) |
        (py & BC250_DCN2_CURSOR_Y_MASK);
    Out->HotSpotReg = (hx << BC250_DCN2_CURSOR_HOT_X_SHIFT) | (hy & BC250_DCN2_CURSOR_HOT_Y_MASK);
}

uint32_t Bc250DcnCursorControl(uint32_t Read, uint32_t Width, uint32_t Enable)
{
    uint32_t lpc = (Width <= 32u) ? BC250_DCN_CURSOR_LPC_16 : BC250_DCN_CURSOR_LPC_8;
    uint32_t v = Bc250DcnField(Read, BC250_DCN2_CURSOR_MODE_MASK,
        BC250_DCN_CURSOR_MODE_UN_PREMULT << BC250_DCN2_CURSOR_MODE_SHIFT);
    v = Bc250DcnField(v, BC250_DCN2_CURSOR_2X_MAGNIFY, 0u);
    v = Bc250DcnField(v, BC250_DCN2_CURSOR_PITCH_MASK,
        BC250_DCN_CURSOR_PITCH_64 << BC250_DCN2_CURSOR_PITCH_SHIFT);
    v = Bc250DcnField(v, BC250_DCN2_CURSOR_LPC_MASK, lpc << BC250_DCN2_CURSOR_LPC_SHIFT);
    return Bc250DcnField(v, BC250_DCN2_CURSOR_ENABLE, Enable ? BC250_DCN2_CURSOR_ENABLE : 0u);
}

/* ---- 2/3. CRC --------------------------------------------------------------------- */

uint32_t Bc250DcnCrcWindowX(uint32_t Width)
{
    return (0u & BC250_DCN2_START_MASK) |
        ((Width << BC250_DCN2_END_SHIFT) & BC250_DCN2_END_MASK);
}

uint32_t Bc250DcnCrcWindowY(uint32_t Height)
{
    return (0u & BC250_DCN2_START_MASK) |
        ((Height << BC250_DCN2_END_SHIFT) & BC250_DCN2_END_MASK);
}

uint32_t Bc250DcnCrcCntlEnable(uint32_t Read)
{
    uint32_t v = Bc250DcnField(Read, BC250_DCN2_CRC_CONT_EN, BC250_DCN2_CRC_CONT_EN);
    v = Bc250DcnField(v, BC250_DCN2_CRC0_SELECT_MASK, 0u);  /* UNION_WINDOW_A_B */
    return Bc250DcnField(v, BC250_DCN2_CRC_EN, BC250_DCN2_CRC_EN);
}

/* ---- 2. Fontes de interrupcao (DCN.3) ------------------------------------------------ */

uint32_t Bc250DcnIrqClassify(uint32_t SrcId, uint32_t ExtId)
{
    if (SrcId == BC250_IH_SRCID_DCN_OTG0_VSTARTUP) return BC250_DCN_IRQ_VSTARTUP;
    if (SrcId == BC250_IH_SRCID_DCN_OTG0_VUPDATE_NO_LOCK) return BC250_DCN_IRQ_VUPDATE;
    /* 0x1E e' compartilhada por varios eventos do OTG1; so' o ext_id 6 e' VLINE0. */
    if (SrcId == BC250_IH_SRCID_DCN_OTG0_VLINE0 && ExtId == BC250_IH_CTXID_DCN_OTG0_VLINE0)
        return BC250_DCN_IRQ_VLINE0;
    if (SrcId == BC250_IH_SRCID_DCN_HUBP0_FLIP) return BC250_DCN_IRQ_FLIP;
    return BC250_DCN_IRQ_NONE;
}

uint32_t Bc250DcnVline0Position(uint32_t StartLine, uint32_t VTotal)
{
    uint32_t end;
    if (VTotal == 0u || StartLine >= VTotal) return 0u;
    end = (StartLine + 1u < VTotal) ? StartLine + 1u : 0u;
    return (StartLine & BC250_DCN2_VINT0_LINE_START_MASK) |
        ((end << BC250_DCN2_VINT0_LINE_END_SHIFT) & BC250_DCN2_VINT0_LINE_END_MASK);
}

/* ---- 6. AUX ------------------------------------------------------------------------- */

uint32_t Bc250DcnAuxInitControl(uint32_t ReadControl, uint32_t HpdSel)
{
    uint32_t v = Bc250DcnField(ReadControl, BC250_DCN2_AUX_HPD_SEL_MASK,
        HpdSel << BC250_DCN2_AUX_HPD_SEL_SHIFT);
    return Bc250DcnField(v, BC250_DCN2_AUX_LS_READ_EN, 0u);
}

uint32_t Bc250DcnAuxInitRxControl0(uint32_t ReadRxControl0)
{
    return Bc250DcnField(ReadRxControl0, BC250_DCN2_AUX_RX_RECEIVE_WINDOW_MASK, 0u);
}

uint32_t Bc250DcnAuxComposeHeader(uint32_t Action, uint32_t Address, uint32_t Length,
    uint8_t Out[4])
{
    Out[0] = (uint8_t)((Action & 0xF0u) | ((Address >> 16) & 0x0Fu));
    Out[1] = (uint8_t)((Address >> 8) & 0xFFu);
    Out[2] = (uint8_t)(Address & 0xFFu);
    Out[3] = (uint8_t)((Length != 0u) ? (Length - 1u) & 0xFFu : 0u);
    return (Length != 0u) ? 4u : 3u;
}

uint32_t Bc250DcnAuxDecodeReply(uint32_t ReplyNibble)
{
    uint32_t native = ReplyNibble & 0x3u;
    uint32_t i2c = (ReplyNibble >> 2) & 0x3u;
    if (native == 1u) return BC250_AUX_REPLY_NACK;
    if (native == 2u) return BC250_AUX_REPLY_DEFER;
    if (native == 3u) return BC250_AUX_REPLY_INVALID;
    if (i2c == 1u) return BC250_AUX_REPLY_I2C_NACK;
    if (i2c == 2u) return BC250_AUX_REPLY_I2C_DEFER;
    if (i2c == 3u) return BC250_AUX_REPLY_INVALID;
    return BC250_AUX_REPLY_ACK;
}

/* ---- 7. D06 ------------------------------------------------------------------- */

void Bc250DcnIrqDestDecode(uint32_t OtgInterruptDest, uint32_t DchubInterruptDest2,
    BC250_DCN_IRQ_DEST *Out)
{
    if (Out == 0) return;
    Out->VstartupDest = (OtgInterruptDest & BC250_DCN2_OTG0_VSTARTUP_DEST_MASK) ? 1u : 0u;
    Out->VupdateNoLockDest = (OtgInterruptDest & BC250_DCN2_OTG0_VUPDATE_NO_LOCK_DEST_MASK) ? 1u : 0u;
    Out->Vline0Dest = (OtgInterruptDest & BC250_DCN2_OTG0_VLINE0_DEST_MASK) ? 1u : 0u;
    Out->FlipDest = (DchubInterruptDest2 & BC250_DCN2_DCHUB_HUBP0_FLIP_DEST_MASK) ? 1u : 0u;
}

/* ---- 8. D12 ------------------------------------------------------------------- */

uint32_t Bc250DcnAuxPadI2cModeOffValue(uint32_t Read)
{
    /* Lei 6: so' o bit 12 (DDC_PAD1_I2CMODE) vai a 0, como hw_ddc.c set_config. */
    return (Read & ~BC250_DCN2_GPIO_DDC_PAD1_I2CMODE) | (0u & BC250_DCN2_GPIO_DDC_PAD1_I2CMODE);
}

uint32_t Bc250DcnAuxPadHardwareModeValue(uint32_t Read)
{
    uint32_t mask = BC250_DCN2_GPIO_DDC1CLK_MASK | BC250_DCN2_GPIO_DDC1DATA_MASK |
        BC250_DCN2_GPIO_AUX_PAD1_MODE;
    /* Lei 6: CLK/DATA MASK vao a 0 (pino dirigido por hardware), AUX_PAD1_MODE
     * vai a 1 (pad em modo AUX); o resto do registrador e' preservado. */
    uint32_t value = BC250_DCN2_GPIO_AUX_PAD1_MODE;
    return (Read & ~mask) | (value & mask);
}

/* ---- 9. D19 ------------------------------------------------------------------- */

uint32_t Bc250DcnComputeVTotalForRefresh(
    uint32_t CurrentVTotal, uint32_t CurrentRefreshMilliHz,
    uint32_t DesiredRefreshMilliHz, uint32_t *OutNewVTotal)
{
    uint64_t newTotal;
    if (OutNewVTotal == 0) return 0xFFFFFFFFu;
    *OutNewVTotal = 0u;
    if (DesiredRefreshMilliHz < 24000u || DesiredRefreshMilliHz > 240000u) return BC250_DCN_VTOTAL_REFUSE_RANGE;
    if (CurrentVTotal == 0u || CurrentRefreshMilliHz == 0u) return BC250_DCN_VTOTAL_REFUSE_INPUT;
    if (DesiredRefreshMilliHz >= CurrentRefreshMilliHz) return BC250_DCN_VTOTAL_REFUSE_NOT_LOWER;
    /* new_total = current_total * current_refresh / desired_refresh, arredondado. */
    newTotal = ((uint64_t)CurrentVTotal * (uint64_t)CurrentRefreshMilliHz +
        (uint64_t)DesiredRefreshMilliHz / 2u) / (uint64_t)DesiredRefreshMilliHz;
    if (newTotal == 0u || newTotal > (uint64_t)BC250_DCN2_TOTAL_MASK) return BC250_DCN_VTOTAL_REFUSE_OVERFLOW;
    *OutNewVTotal = (uint32_t)newTotal;
    return 0u;
}

uint64_t Bc250DcnElapsedMs(int64_t StartQpc, int64_t NowQpc, int64_t FreqQpc)
{
    int64_t delta;
    if (FreqQpc <= 0) return 0ull;
    delta = NowQpc - StartQpc;
    if (delta <= 0) return 0ull;
    return (uint64_t)((delta * 1000LL) / FreqQpc);
}
