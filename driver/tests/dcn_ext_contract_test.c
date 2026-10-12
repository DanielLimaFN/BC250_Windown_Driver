/* dcn_ext_contract_test.c -- DCN.2 (doc 713): timing, EDID, cursor, CRC, AUX.
 * O timing e' o do OTG0 medido na placa (H_TOTAL 2784, V_TOTAL 1111, blank
 * 224/31, sync 48+32 / 3+5) e o refresh o da Corrida 271 (59,99 Hz). */
#include "../core/bc250_dcn_ext_contract.h"

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);
#endif

#define CHECK(cond, msg) do { if (!(cond)) { (void)(msg); return __LINE__; } } while (0)

static uint32_t g_Cursor[BC250_DCN_CURSOR_MAX * BC250_DCN_CURSOR_MAX];
static uint8_t g_Edid[128];

static int Bc250RunDcnExtContractTest(void)
{
    BC250_DCN_TIMING t, back;
    BC250_DCN_CURSOR_POS pos;
    uint8_t dtd[18], hdr[4];
    uint32_t refuse, mhz, i, sum;

    /* 0. Lei 6. */
    CHECK(Bc250DcnField(0xFFFF0000u, 0x000000FFu, 0x12345678u) == 0xFFFF0078u, "lei 6");

    /* 1. Timing do OTG (inverso de optc1_program_timing). */
    refuse = Bc250DcnTimingDecode(2783u, (176u << 16) | 2736u, (32u << 16) | 0u, 0u,
        1110u, (28u << 16) | 1108u, (5u << 16) | 0u, 0u, 2560u, 1080u, &t);
    CHECK(refuse == BC250_DCN_TIMING_OK, "timing medido aceito");
    CHECK(t.HTotal == 2784u && t.VTotal == 1111u, "totais");
    CHECK(t.HActive == 2560u && t.VActive == 1080u, "ativo");
    CHECK(t.HFrontPorch == 48u && t.HSyncWidth == 32u, "porch/sync H");
    CHECK(t.VFrontPorch == 3u && t.VSyncWidth == 5u, "porch/sync V");
    CHECK(t.HSyncPositive == 1u && t.VSyncPositive == 1u, "POL 0 = positivo");
    refuse = Bc250DcnTimingDecode(2783u, (176u << 16) | 2736u, (32u << 16), 1u,
        1110u, (28u << 16) | 1108u, (5u << 16), 1u, 1920u, 1080u, &back);
    CHECK((refuse & BC250_DCN_TIMING_REFUSE_ACTIVE) != 0u, "modo diferente do GOP recusa");
    CHECK(back.HSyncPositive == 0u && back.VSyncPositive == 0u, "POL 1 = negativo");
    refuse = Bc250DcnTimingDecode(2783u, (176u << 16) | 2736u, 0u, 0u,
        1110u, (28u << 16) | 1108u, (5u << 16), 0u, 2560u, 1080u, &back);
    CHECK((refuse & BC250_DCN_TIMING_REFUSE_SYNC) != 0u, "sync zero recusa");

    /* Refresh pela varredura: 12 quadros + 100 linhas em 200 ms. */
    mhz = Bc250DcnRefreshFromScan(12u, 100, 1111u, 2000000ull);
    CHECK(mhz == 60450u, "refresh pela varredura");
    CHECK(Bc250DcnRefreshFromScan(12u, -1111 * 12, 1111u, 2000000ull) == 0u, "zero linhas");
    CHECK(Bc250DcnRefreshFromScan(1u, 0, 1111u, 0ull) == 0u, "sem tempo");
    CHECK(Bc250DcnTimingApplyRefresh(&t, 59994u) == BC250_DCN_TIMING_OK, "aplica 59,994");
    CHECK(t.PixelRateHz == 185562882ull, "clock de pixel derivado");
    CHECK(Bc250DcnTimingApplyRefresh(&back, 5000u) == BC250_DCN_TIMING_REFUSE_REFRESH, "5 Hz recusa");

    /* 6. DTD ida e volta, e busca no bloco EDID. */
    Bc250DcnEncodeDtd(&t, 677u, 286u, dtd);
    CHECK(Bc250DcnDecodeDtd(dtd, &back) == 1, "decodifica");
    CHECK(back.HActive == 2560u && back.VActive == 1080u && back.HTotal == 2784u &&
        back.VTotal == 1111u, "DTD ida e volta: geometria");
    CHECK(back.HFrontPorch == 48u && back.HSyncWidth == 32u && back.VFrontPorch == 3u &&
        back.VSyncWidth == 5u, "DTD ida e volta: sync");
    CHECK(back.PixelRateHz == 185560000ull, "DTD em 10 kHz");
    for (i = 0u; i < 128u; ++i) g_Edid[i] = 0u;
    g_Edid[0] = 0x00; for (i = 1u; i <= 6u; ++i) g_Edid[i] = 0xFF; g_Edid[7] = 0x00;
    for (i = 0u; i < 18u; ++i) g_Edid[54u + 18u + i] = dtd[i];   /* no 2o descritor */
    sum = 0u; for (i = 0u; i < 127u; ++i) sum += g_Edid[i];
    g_Edid[127] = (uint8_t)((0x100u - (sum & 0xFFu)) & 0xFFu);
    CHECK(Bc250DcnEdidBlockValid(g_Edid) == 1, "checksum");
    CHECK(Bc250DcnEdidFindTiming(g_Edid, &t) == 1, "acha o DTD do OTG");
    back = t; back.HTotal = 2720u;
    CHECK(Bc250DcnEdidFindTiming(g_Edid, &back) == -1, "total diferente nao casa");
    back = t; back.PixelRateHz = 173611437ull;
    CHECK(Bc250DcnEdidFindTiming(g_Edid, &back) == -1, "clock 56,13 Hz nao casa");
    g_Edid[127] ^= 1u;
    CHECK(Bc250DcnEdidBlockValid(g_Edid) == 0 && Bc250DcnEdidFindTiming(g_Edid, &t) == -1,
        "checksum errado recusa");

    /* 4. Cursor. */
    {
        static const uint8_t color[2 * 8] = {
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
            0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x01 };
        /* 4x2 monocromatico, pitch 1: AND depois XOR (bits por coluna 0..3). */
        static const uint8_t mono[4] = { 0xA0, 0x50, 0x00, 0x30 };
        CHECK(Bc250DcnCursorConvert(BC250_DCN_POINTER_COLOR, 2u, 2u, 8u, color, g_Cursor) == 1, "cor");
        CHECK(g_Cursor[0] == 0x44332211u && g_Cursor[1] == 0x88776655u &&
            g_Cursor[64] == 0xCCBBAA99u && g_Cursor[65] == 0x01FFEEDDu && g_Cursor[2] == 0u, "ARGB copiado");
        CHECK(Bc250DcnCursorConvert(BC250_DCN_POINTER_MONOCHROME, 4u, 2u, 1u, mono, g_Cursor) == 1, "mono");
        /* linha 0: AND 1010, XOR 0000 -> transp, preto, transp, preto */
        CHECK(g_Cursor[0] == 0u && g_Cursor[1] == 0xFF000000u && g_Cursor[2] == 0u &&
            g_Cursor[3] == 0xFF000000u, "mono linha 0");
        /* linha 1: AND 0101, XOR 0011 -> preto, transp, branco, inverter->branco */
        CHECK(g_Cursor[64] == 0xFF000000u && g_Cursor[65] == 0u && g_Cursor[66] == 0xFFFFFFFFu &&
            g_Cursor[67] == 0xFFFFFFFFu, "mono linha 1");
        CHECK(Bc250DcnCursorConvert(BC250_DCN_POINTER_COLOR, 65u, 2u, 260u, color, g_Cursor) == 0, "65 px recusa");
        CHECK(Bc250DcnCursorConvert(0x4u, 2u, 2u, 8u, color, g_Cursor) == 0, "masked color recusa");
        CHECK(Bc250DcnCursorConvert(BC250_DCN_POINTER_COLOR, 2u, 2u, 4u, color, g_Cursor) == 0, "pitch curto recusa");
    }
    Bc250DcnCursorPosition(100, 200, 1u, 32u, 32u, 2560u, 1080u, &pos);
    CHECK(pos.Enable == 1u && pos.PositionReg == ((100u << 16) | 200u) && pos.HotSpotReg == 0u, "posicao");
    Bc250DcnCursorPosition(-5, -7, 1u, 32u, 32u, 2560u, 1080u, &pos);
    CHECK(pos.Enable == 1u && pos.PositionReg == 0u && pos.HotSpotReg == ((5u << 16) | 7u), "recorte");
    Bc250DcnCursorPosition(-40, 10, 1u, 32u, 32u, 2560u, 1080u, &pos);
    CHECK(pos.Enable == 0u, "fora pela esquerda");
    Bc250DcnCursorPosition(2560, 10, 1u, 32u, 32u, 2560u, 1080u, &pos);
    CHECK(pos.Enable == 0u, "fora pela direita");
    Bc250DcnCursorPosition(10, 10, 0u, 32u, 32u, 2560u, 1080u, &pos);
    CHECK(pos.Enable == 0u, "invisivel");
    CHECK(Bc250DcnCursorControl(0u, 64u, 1u) == (0x1u | (3u << 8) | (3u << 24)), "controle 64 px");
    CHECK(Bc250DcnCursorControl(0x10u, 32u, 0u) == ((3u << 8) | (4u << 24)), "controle 32 px, sem lupa");

    /* 3. CRC. */
    CHECK(Bc250DcnCrcWindowX(2560u) == (2560u << 16), "janela X");
    CHECK(Bc250DcnCrcWindowY(1080u) == (1080u << 16), "janela Y");
    CHECK(Bc250DcnCrcCntlEnable(0x00700000u) == 0x11u, "CRC continuo, UNION_A_B");

    /* 6. AUX. */
    CHECK(Bc250DcnAuxComposeHeader(BC250_AUX_ACTION_DP_READ, 0x00000u, 16u, hdr) == 4u &&
        hdr[0] == 0x90u && hdr[1] == 0u && hdr[2] == 0u && hdr[3] == 15u, "DPCD 0x000 x16");
    CHECK(Bc250DcnAuxComposeHeader(BC250_AUX_ACTION_I2C_WRITE_MOT, BC250_EDID_I2C_ADDRESS, 1u, hdr) == 4u &&
        hdr[0] == 0x40u && hdr[2] == 0x50u && hdr[3] == 0u, "I2C MOT 0x50");
    CHECK(Bc250DcnAuxComposeHeader(BC250_AUX_ACTION_I2C_READ, BC250_EDID_I2C_ADDRESS, 0u, hdr) == 3u &&
        hdr[0] == 0x10u, "parada sem MOT");
    CHECK(Bc250DcnAuxDecodeReply(0x0u) == BC250_AUX_REPLY_ACK, "ack");
    CHECK(Bc250DcnAuxDecodeReply(0x1u) == BC250_AUX_REPLY_NACK, "nack");
    CHECK(Bc250DcnAuxDecodeReply(0x2u) == BC250_AUX_REPLY_DEFER, "defer");
    CHECK(Bc250DcnAuxDecodeReply(0x4u) == BC250_AUX_REPLY_I2C_NACK, "i2c nack");
    CHECK(Bc250DcnAuxDecodeReply(0x8u) == BC250_AUX_REPLY_I2C_DEFER, "i2c defer");
    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess((unsigned long)Bc250RunDcnExtContractTest());
}
#else
int main(void) { return Bc250RunDcnExtContractTest(); }
#endif
