/*
 * Suite do scaler C10 / ESC.1 (doc 765): lista de modos, escalas aceitas,
 * plano (recout, razoes, inits, taps, line buffer) e as palavras do filtro.
 * Nativo da BC-250 medido: 2560x1080 (Corridas 250/271).
 */
#include "../core/bc250_dcn_scale_contract.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

static int g_failureLine = 0;
#define CHECK(cond) do { if (!(cond)) { g_failureLine = __LINE__; return 0; } } while (0)

static int Run(void)
{
    uint32_t list[BC250_SCALE_MAX_MODES][2];
    uint32_t n, i;
    BC250_SCALE_PLAN p;

    /* Offsets conferidos contra dcn_2_0_3_offset.h (BASE_IDX 2). */
    CHECK(BC250_SCL_REG_SCL_MODE == 0x106B0u);
    CHECK(BC250_SCL_REG_COEF_RAM_TAP_DATA == 0x106ACu);
    CHECK(BC250_SCL_REG_RECOUT_SIZE == 0x1070Cu);
    CHECK(BC250_SCL_REG_HUBP0_VIEWPORT_DIM == 0x0EAA8u);
    CHECK(BC250_SCL_REG_HUBPREQ0_PITCH == 0x0EB1Cu);
    CHECK(BC250_SCL_REG_MPCC0_BG_R_CR == 0x11CE8u);
    CHECK(BC250_SCL_REG_OTG0_MASTER_LOCK == 0x1412Cu);

    /* Tabela extraida: primeira fase e' a identidade (0, 1.0, 0, 0). */
    CHECK(g_Bc250SclFilter4Tap64pUpscale[0] == 0x0000u && g_Bc250SclFilter4Tap64pUpscale[1] == 0x1000u);
    CHECK(Bc250ScaleCoefWord(0) == (0x0000u | BC250_SCL_COEF_EVEN_EN | (0x1000u << 16) | BC250_SCL_COEF_ODD_EN));

    /* Lista: nativo primeiro, so' larguras multiplas de 64 que cabem em 2560x1080. */
    n = Bc250ScaleBuildModeList(2560u, 1080u, list, BC250_SCALE_MAX_MODES);
    CHECK(n == 9u);   /* 2560x1080 + os 8 comuns (todos com largura multipla de 64 e altura <= 1080) */
    CHECK(list[0][0] == 2560u && list[0][1] == 1080u);
    CHECK(list[1][0] == 1920u && list[1][1] == 1080u);
    for (i = 0u; i < n; ++i) CHECK(list[i][0] % 64u == 0u && list[i][0] <= 2560u && list[i][1] <= 1080u);

    /* Escalas: nativo aceita todas; menor nunca identidade. */
    CHECK(Bc250ScaleSupports(2560u, 1080u, 2560u, 1080u, BC250_SCALE_IDENTITY));
    CHECK(!Bc250ScaleSupports(1920u, 1080u, 2560u, 1080u, BC250_SCALE_IDENTITY));
    CHECK(Bc250ScaleSupports(1920u, 1080u, 2560u, 1080u, BC250_SCALE_ASPECT));

    /* 1:1 e' bypass. */
    CHECK(Bc250ScaleComputePlan(2560u, 1080u, 2560u, 1080u, BC250_SCALE_IDENTITY, &p));
    CHECK(p.Bypass == 1u && p.DsclMode == 0u && p.RecoutW == 2560u && p.TapsH == 1u);

    /* 1920x1080 proporcional em 2560x1080: limitado pela altura -> 1920x1080
     * centrado em x=320, 1:1 (barras pretas dos lados), bypass. */
    CHECK(Bc250ScaleComputePlan(1920u, 1080u, 2560u, 1080u, BC250_SCALE_ASPECT, &p));
    CHECK(p.RecoutW == 1920u && p.RecoutH == 1080u && p.RecoutX == 320u && p.RecoutY == 0u);
    CHECK(p.Bypass == 1u && p.RatioH19 == (1u << 19));

    /* 1920x1080 esticado: razao H 0.75, V 1.0 -> 4 taps H, 1 tap V. */
    CHECK(Bc250ScaleComputePlan(1920u, 1080u, 2560u, 1080u, BC250_SCALE_STRETCHED, &p));
    CHECK(p.RecoutW == 2560u && p.RecoutH == 1080u && p.RecoutX == 0u);
    CHECK(p.RatioH19 == 393216u && p.RatioV19 == 524288u);          /* 0.75 e 1.0 em u3.19 */
    CHECK(p.TapsH == 4u && p.TapsV == 1u && p.Bypass == 0u && p.DsclMode == 1u);
    CHECK(p.InitH19 == (393216u + (5u << 19)) / 2u);                 /* (0.75 + 5) / 2 = 2.875 */
    CHECK(Bc250ScaleInitReg(p.InitH19) == ((((p.InitH19 & 0x7FFFFu) << 5)) | (2u << 24)));
    CHECK(Bc250ScaleRatioReg(p.RatioH19) == (393216u << 5));
    CHECK(Bc250ScaleTapControl(4u, 1u) == 0x3030u);

    /* 1280x720 proporcional: largura limita? 1280/720 = 16:9 em 2560x1080 -> 1920x1080 centrado. */
    CHECK(Bc250ScaleComputePlan(1280u, 720u, 2560u, 1080u, BC250_SCALE_ASPECT, &p));
    CHECK(p.RecoutW == 1920u && p.RecoutH == 1080u && p.RecoutX == 320u);
    CHECK(p.TapsH == 4u && p.TapsV == 4u);
    /* line buffer: linha de 1280 -> 214 palavras; config 1 = 970/214 = 4 particoes >= 4 taps. */
    CHECK(p.LbMemoryConfig == 1u);

    /* 1920x1080 esticado com 4 taps V (forcando V): linha 1920 -> 320; c1 = 3 < 4, c2 = 4 -> config 2. */
    CHECK(Bc250ScaleLbPartitions(1920u, 1u) == 3u && Bc250ScaleLbPartitions(1920u, 2u) == 4u);

    /* Centralizado: 1:1 dentro, bypass, recout no meio. */
    CHECK(Bc250ScaleComputePlan(1024u, 768u, 2560u, 1080u, BC250_SCALE_CENTERED, &p));
    CHECK(p.Bypass == 1u && p.RecoutW == 1024u && p.RecoutX == 768u && p.RecoutY == 156u);

    /* Recusas: identidade num tamanho menor, origem maior que o nativo. */
    CHECK(!Bc250ScaleComputePlan(1920u, 1080u, 2560u, 1080u, BC250_SCALE_IDENTITY, &p));
    CHECK(!Bc250ScaleComputePlan(3840u, 2160u, 2560u, 1080u, BC250_SCALE_STRETCHED, &p));
    return 1;
}

void __stdcall mainCRTStartup(void)
{
    ExitProcess(Run() ? 0u : (unsigned long)g_failureLine);
}
