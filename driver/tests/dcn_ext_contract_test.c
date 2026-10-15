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
        /* 4x2 sem inversao: AND 1010/0101, XOR 0000/0010 (pitch 1). */
        static const uint8_t monoOk[4] = { 0xA0, 0x50, 0x00, 0x20 };
        /* cor mascarada 3x1: substitui, XOR com 0 (transparente), XOR com cor. */
        static const uint8_t masked[12] = {
            0x10, 0x20, 0x30, 0x00,  0x00, 0x00, 0x00, 0xFF,  0x01, 0x02, 0x03, 0xFF };
        static const uint8_t maskedBad[4] = { 0x10, 0x20, 0x30, 0x80 };
        CHECK(Bc250DcnCursorConvert(BC250_DCN_POINTER_MONOCHROME, 4u, 2u, 1u, monoOk, g_Cursor) == BC250_DCN_CURSOR_OK, "mono");
        /* linha 0: AND 1010, XOR 0000 -> transp, preto, transp, preto */
        CHECK(g_Cursor[0] == 0u && g_Cursor[1] == 0xFF000000u && g_Cursor[2] == 0u &&
            g_Cursor[3] == 0xFF000000u, "mono linha 0");
        /* linha 1: AND 0101, XOR 0010 -> preto, transp, branco, transp */
        CHECK(g_Cursor[64] == 0xFF000000u && g_Cursor[65] == 0u && g_Cursor[66] == 0xFFFFFFFFu &&
            g_Cursor[67] == 0u, "mono linha 1");
        /* O XOR fica nas linhas Height..2*Height-1 (VBoxMPWddm.cpp): a linha 2 do
         * buffer e' o XOR da linha 0, nao a AND de uma 3a linha. */
        CHECK(Bc250DcnCursorConvert(BC250_DCN_POINTER_MONOCHROME, 4u, 2u, 1u, mono, g_Cursor) == BC250_DCN_CURSOR_NEEDS_XOR,
            "mono com inversao fica no software");
        CHECK(Bc250DcnCursorConvert(BC250_DCN_POINTER_MASKED_COLOR, 2u, 1u, 8u, masked, g_Cursor) == BC250_DCN_CURSOR_OK,
            "mascarada sem XOR de cor");
        CHECK(g_Cursor[0] == 0xFF302010u && g_Cursor[1] == 0u, "mascarada: opaco e transparente");
        CHECK(Bc250DcnCursorConvert(BC250_DCN_POINTER_MASKED_COLOR, 3u, 1u, 12u, masked, g_Cursor) == BC250_DCN_CURSOR_NEEDS_XOR,
            "mascarada com XOR de cor fica no software");
        CHECK(Bc250DcnCursorConvert(BC250_DCN_POINTER_MASKED_COLOR, 1u, 1u, 4u, maskedBad, g_Cursor) == BC250_DCN_CURSOR_REFUSED,
            "mascara fora de 0/0xFF recusa");
        CHECK(Bc250DcnCursorConvert(BC250_DCN_POINTER_COLOR, 65u, 2u, 260u, color, g_Cursor) == BC250_DCN_CURSOR_REFUSED, "65 px recusa");
        CHECK(Bc250DcnCursorConvert(0x8u, 2u, 2u, 8u, color, g_Cursor) == BC250_DCN_CURSOR_REFUSED, "flag desconhecida recusa");
        CHECK(Bc250DcnCursorConvert(BC250_DCN_POINTER_COLOR, 2u, 2u, 4u, color, g_Cursor) == BC250_DCN_CURSOR_REFUSED, "pitch curto recusa");
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
    /* dcn10_aux_initialize: HPD_SEL no campo [22:20], LS_READ_EN 0, janela 0. */
    CHECK(Bc250DcnAuxInitControl(0x00000101u, 0u) == 0x00000001u, "aux init: LS_READ_EN 0, HPD 0");
    CHECK(Bc250DcnAuxInitControl(0x00700001u, 1u) == 0x00100001u, "aux init: HPD 1");
    CHECK(Bc250DcnAuxInitRxControl0(0x00000770u) == 0x00000070u, "aux init: janela 0");

    /* DCN.3: fontes do cliente DCE (to_dal_irq_source_dcn201). */
    CHECK(Bc250DcnIrqClassify(0x3Cu, 0u) == BC250_DCN_IRQ_VSTARTUP, "0x3C vstartup");
    CHECK(Bc250DcnIrqClassify(0x57u, 0u) == BC250_DCN_IRQ_VUPDATE, "0x57 vupdate");
    CHECK(Bc250DcnIrqClassify(0x1Eu, 6u) == BC250_DCN_IRQ_VLINE0, "0x1E/6 vline0");
    CHECK(Bc250DcnIrqClassify(0x1Eu, 3u) == BC250_DCN_IRQ_NONE, "0x1E/3 (trigger A) nao e' vline0");
    CHECK(Bc250DcnIrqClassify(0x4Fu, 0u) == BC250_DCN_IRQ_FLIP, "0x4F flip");
    CHECK(Bc250DcnIrqClassify(0x50u, 0u) == BC250_DCN_IRQ_NONE, "0x50 flip do HUBP1 nao e' nosso");
    /* VLINE0 no inicio do blank (1108 de 1111). */
    CHECK(Bc250DcnVline0Position(1108u, 1111u) == ((1109u << 16) | 1108u), "vline0 1108");
    CHECK(Bc250DcnVline0Position(1110u, 1111u) == 1110u, "vline0 na ultima linha: fim 0");
    CHECK(Bc250DcnVline0Position(1111u, 1111u) == 0u, "vline0 fora recusa");

    /* D06 (doc 716/718, so' leitura): decodificacao dos quatro bits de
     * destino. Valores sinteticos cobrindo os quatro independentes e as
     * combinacoes vistas no catalogo (todos ligados / todos desligados). */
    {
        BC250_DCN_IRQ_DEST dest;
        Bc250DcnIrqDestDecode(0u, 0u, &dest);
        CHECK(dest.VstartupDest == 0u && dest.VupdateNoLockDest == 0u &&
              dest.Vline0Dest == 0u && dest.FlipDest == 0u, "tudo zero decodifica zero");
        Bc250DcnIrqDestDecode(0x000902FFu, 1u, &dest);
        CHECK(dest.VstartupDest == 1u, "bit 16 (VSTARTUP) isolado");
        CHECK(dest.VupdateNoLockDest == 1u, "bit 19 (V_UPDATE_NO_LOCK) isolado");
        CHECK(dest.Vline0Dest == 1u, "bit 9 (VERTICAL_INTERRUPT0) isolado");
        CHECK(dest.FlipDest == 1u, "bit 0 do DCHUB_INTERRUPT_DEST2 isolado");
        Bc250DcnIrqDestDecode(0xFFFFFFFFu, 0xFFFFFFFEu, &dest);
        CHECK(dest.VstartupDest == 1u && dest.VupdateNoLockDest == 1u &&
              dest.Vline0Dest == 1u && dest.FlipDest == 0u,
              "flip isolado em 0 com o resto do OTG em 1 (o caso que importa: comparar flip contra os outros tres)");
        /* Out == NULL nao pode crashar (falha fechada, sem escrever nada). */
        Bc250DcnIrqDestDecode(0xFFFFFFFFu, 0xFFFFFFFFu, 0);
    }

    /* D12 (doc 716/718): Lei 6 no DC_GPIO_DDC1_MASK -- CLK/DATA MASK a 0,
     * AUX_PAD1_MODE a 1, resto preservado. */
    CHECK(Bc250DcnAuxPadHardwareModeValue(0u) == 0x00010000u, "registrador zerado: so' liga AUX_PAD1_MODE");
    CHECK(Bc250DcnAuxPadHardwareModeValue(0xFFFFFFFFu) == 0xFFFFFEFEu,
        "resto (MASK/PD_EN/RECV/STR/POL) preservado em 1; so' CLK_MASK e DATA_MASK (bits 0 e 8) vao a 0");
    CHECK(Bc250DcnAuxPadHardwareModeValue(0x00140001u) == 0x00150000u,
        "AUX_PAD1_MODE em 0 (DDC) e AUX1_POL em 1 preservado -- CLK_MASK (bit 0) zera, PAD (bit 16) liga, AUX1_POL continua");
    CHECK(Bc250DcnAuxPadHardwareModeValue(0x00010101u) == 0x00010000u,
        "ja em modo AUX (idempotente): CLK/DATA MASK zerados, PAD continua 1");
    /* C09 (doc 765): os valores que o amdgpu escreveu nesta placa (rastro E03). */
    CHECK(Bc250DcnAuxPadI2cModeOffValue(0x0003F55Au) == 0x0003E55Au,
        "AUX_CTRL_5 do POST 0x0003F55A vira 0x0003E55A, como o Linux");
    CHECK(Bc250DcnAuxPadI2cModeOffValue(0x0003E55Au) == 0x0003E55Au, "idempotente");
    CHECK(Bc250DcnAuxPadI2cModeOffValue(0xFFFFFFFFu) == 0xFFFFEFFFu, "so' o bit 12 muda");

    /* D19 (doc 716/720): so' diminuir refresh (aumentar V_TOTAL); numeros da
     * 271/273 (V_TOTAL=1111, ~60 Hz). */
    {
        uint32_t newVTotal = 0xFFFFFFFFu;
        uint32_t r = Bc250DcnComputeVTotalForRefresh(1111u, 60000u, 50000u, &newVTotal);
        CHECK(r == 0u, "60->50 Hz aceita");
        CHECK(newVTotal == 1333u, "1111*60000/50000 = 1333,2 -> 1333 arredondado");
        r = Bc250DcnComputeVTotalForRefresh(1111u, 60000u, 60000u, &newVTotal);
        CHECK(r == BC250_DCN_VTOTAL_REFUSE_NOT_LOWER, "mesmo refresh recusa (so' diminuir)");
        r = Bc250DcnComputeVTotalForRefresh(1111u, 60000u, 70000u, &newVTotal);
        CHECK(r == BC250_DCN_VTOTAL_REFUSE_NOT_LOWER, "aumentar refresh recusa (encolheria o front porch)");
        r = Bc250DcnComputeVTotalForRefresh(1111u, 60000u, 10000u, &newVTotal);
        CHECK(r == BC250_DCN_VTOTAL_REFUSE_RANGE, "10 Hz fora de [24,240] recusa");
        r = Bc250DcnComputeVTotalForRefresh(1111u, 60000u, 250000u, &newVTotal);
        CHECK(r == BC250_DCN_VTOTAL_REFUSE_RANGE, "250 Hz fora de [24,240] recusa");
        r = Bc250DcnComputeVTotalForRefresh(0u, 60000u, 50000u, &newVTotal);
        CHECK(r == BC250_DCN_VTOTAL_REFUSE_INPUT, "VTotal atual zero recusa");
        r = Bc250DcnComputeVTotalForRefresh(1111u, 0u, 50000u, &newVTotal);
        CHECK(r == BC250_DCN_VTOTAL_REFUSE_INPUT, "refresh atual zero recusa");
        r = Bc250DcnComputeVTotalForRefresh(0x7FFEu, 60000u, 24000u, &newVTotal);
        CHECK(r == BC250_DCN_VTOTAL_REFUSE_OVERFLOW, "V_TOTAL novo nao cabe em 15 bits recusa");
        r = Bc250DcnComputeVTotalForRefresh(1111u, 60000u, 59940u, &newVTotal);
        CHECK(r == 0u && newVTotal == 1112u, "60 -> 59,94 Hz (estilo NTSC): 1 linha extra");
    }

    /* D19b (doc 723/724): elapsed em ms, pura, fail-safe. */
    CHECK(Bc250DcnElapsedMs(0, 10000000, 10000000) == 1000u, "1x a frequencia = 1000 ms");
    CHECK(Bc250DcnElapsedMs(0, 2000000, 10000000) == 200u, "200 ms (uma chamada de Bc250Dcn2MeasureTiming)");
    CHECK(Bc250DcnElapsedMs(0, 4000000, 10000000) == 400u, "400 ms (as duas chamadas da prova de D19)");
    CHECK(Bc250DcnElapsedMs(100, 50, 10000000) == 0u, "Now antes de Start: 0, nunca negativo");
    CHECK(Bc250DcnElapsedMs(0, 1000, 0) == 0u, "frequencia zero: 0, nunca divide por zero");
    CHECK(Bc250DcnElapsedMs(0, 1000, -1) == 0u, "frequencia negativa: 0");
    CHECK(Bc250DcnElapsedMs(1000000000000LL, 1000000000000LL, 10000000) == 0u, "Start==Now: 0");
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
