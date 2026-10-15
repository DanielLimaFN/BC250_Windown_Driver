#ifndef BC250_DCN_SCALE_CONTRACT_H
#define BC250_DCN_SCALE_CONTRACT_H

/*
 * C10 / ESC.1 (doc 765): modos de origem menores que o nativo, ampliados pelo
 * scaler do DPP0 (DSCL0) para o timing nativo herdado do GOP. O timing, o
 * clock, o PHY e o formato nao mudam; mudam o viewport e o pitch do HUBP0, o
 * DSCL0 e o fundo do MPCC0. Politica pura (sem registrador), testada por
 * driver/tests/dcn_scale_contract_test.c.
 *
 * Procedencia (P1, AMD MIT, copias locais em inc/linux_ref/dcn/):
 *   - sequencia de escrita: dcn10_dpp_dscl.c v6.18 (dpp1_dscl_set_scaler_manual_scale,
 *     set_manual_ratio_init, set_recout, set_scl_filter, set_scaler_filter);
 *   - line buffer do DCN 2.x: dcn20_dpp.c dscl2_calc_lb_num_partitions (formato
 *     float), dcn10_dpp_dscl.c dpp1_dscl_find_lb_memory_config, enum
 *     lb_memory_config de dc_spl_types.h v6.18;
 *   - filtro: dce_scl_filters.c v6.18 filter_4tap_64p_upscale (razao < 1);
 *   - taps: 1 na razao 1, 4 ao ampliar (dpp1_dscl_get_optimal_number_of_taps);
 *   - inits: (razao + taps + 1) / 2 truncado a 19 bits (dc_resource.c);
 *   - offsets: dcn_2_0_3_offset.h (BASE_IDX 2 -> (0x34C0 + mm) * 4) e mascaras
 *     de dcn_2_0_3_sh_mask.h.
 * O comportamento (lista de tamanhos, escalas oferecidas, pre-condicao do pipe)
 * segue o desenho do Nagi (docs/design/display-modes.md, estagio A); o codigo
 * e' nosso. Diferenca declarada: larguras so' multiplas de 64 (pitch linear da
 * primaria = largura * 4, alinhado a 256 bytes), e o line buffer e o fundo do
 * MPCC voltam ao GOP no teardown.
 */

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
typedef UINT16 bc250_u16_t;
#else
#include <stdint.h>
typedef uint16_t bc250_u16_t;
#endif

#define BC250_SCL_BYTE(mm) ((0x34C0u + (uint32_t)(mm)) * 4u)

#define BC250_SCL_REG_COEF_RAM_TAP_SELECT   BC250_SCL_BYTE(0x0CEAu)
#define BC250_SCL_REG_COEF_RAM_TAP_DATA     BC250_SCL_BYTE(0x0CEBu)
#define BC250_SCL_REG_SCL_MODE              BC250_SCL_BYTE(0x0CECu)
#define BC250_SCL_REG_TAP_CONTROL           BC250_SCL_BYTE(0x0CEDu)
#define BC250_SCL_REG_DSCL_CONTROL          BC250_SCL_BYTE(0x0CEEu)
#define BC250_SCL_REG_2TAP_CONTROL          BC250_SCL_BYTE(0x0CEFu)
#define BC250_SCL_REG_H_RATIO               BC250_SCL_BYTE(0x0CF1u)
#define BC250_SCL_REG_H_INIT                BC250_SCL_BYTE(0x0CF2u)
#define BC250_SCL_REG_H_RATIO_C             BC250_SCL_BYTE(0x0CF3u)
#define BC250_SCL_REG_H_INIT_C              BC250_SCL_BYTE(0x0CF4u)
#define BC250_SCL_REG_V_RATIO               BC250_SCL_BYTE(0x0CF5u)
#define BC250_SCL_REG_V_INIT                BC250_SCL_BYTE(0x0CF6u)
#define BC250_SCL_REG_V_INIT_BOT            BC250_SCL_BYTE(0x0CF7u)
#define BC250_SCL_REG_V_RATIO_C             BC250_SCL_BYTE(0x0CF8u)
#define BC250_SCL_REG_V_INIT_C              BC250_SCL_BYTE(0x0CF9u)
#define BC250_SCL_REG_V_INIT_BOT_C          BC250_SCL_BYTE(0x0CFAu)
#define BC250_SCL_REG_BLACK_OFFSET          BC250_SCL_BYTE(0x0CFBu)
#define BC250_SCL_REG_AUTOCAL               BC250_SCL_BYTE(0x0CFDu)
#define BC250_SCL_REG_RECOUT_START          BC250_SCL_BYTE(0x0D02u)
#define BC250_SCL_REG_RECOUT_SIZE           BC250_SCL_BYTE(0x0D03u)
#define BC250_SCL_REG_MPC_SIZE              BC250_SCL_BYTE(0x0D04u)
#define BC250_SCL_REG_LB_DATA_FORMAT        BC250_SCL_BYTE(0x0D05u)
#define BC250_SCL_REG_LB_MEMORY_CTRL        BC250_SCL_BYTE(0x0D06u)
#define BC250_SCL_REG_MEM_PWR_CTRL          BC250_SCL_BYTE(0x0D08u)
#define BC250_SCL_REG_HUBP0_VIEWPORT_START  BC250_SCL_BYTE(0x05E9u)
#define BC250_SCL_REG_HUBP0_VIEWPORT_DIM    BC250_SCL_BYTE(0x05EAu)
#define BC250_SCL_REG_HUBPREQ0_PITCH        BC250_SCL_BYTE(0x0607u)
#define BC250_SCL_REG_MPCC0_BG_R_CR         BC250_SCL_BYTE(0x127Au)
#define BC250_SCL_REG_MPCC0_BG_G_Y          BC250_SCL_BYTE(0x127Bu)
#define BC250_SCL_REG_MPCC0_BG_B_CB         BC250_SCL_BYTE(0x127Cu)
#define BC250_SCL_REG_OTG0_MASTER_LOCK      BC250_SCL_BYTE(0x1B8Bu)

#define BC250_SCL_MODE_DSCL_MODE_MASK           0x00000007u
#define BC250_SCL_MODE_COEF_RAM_SELECT          0x00000100u
#define BC250_SCL_MODE_COEF_RAM_SELECT_CURRENT  0x00001000u
#define BC250_SCL_MODE_CHROMA_COEF_MODE         0x00010000u
#define BC250_SCL_DSCL_MODE_444_BYPASS          0u   /* enum dscl_mode_sel */
#define BC250_SCL_DSCL_MODE_444_RGB_ENABLE      1u
#define BC250_SCL_2TAP_FIELDS_MASK              0x07110711u
#define BC250_SCL_COEF_FILTER_LUMA_VERT         0u   /* enum dcn10_coef_filter_type_sel */
#define BC250_SCL_COEF_FILTER_LUMA_HORZ         1u
#define BC250_SCL_COEF_FILTER_TYPE_SHIFT        16u
#define BC250_SCL_COEF_EVEN_EN                  0x00008000u
#define BC250_SCL_COEF_ODD_EN                   0x80000000u
#define BC250_SCL_COEF_ODD_SHIFT                16u
#define BC250_SCL_COEF_MASK                     0x00003FFFu
#define BC250_SCL_RATIO_MASK                    0x07FFFFFFu
#define BC250_SCL_INIT_FRAC_MASK                0x00FFFFFFu
#define BC250_SCL_INIT_INT_SHIFT                24u
#define BC250_SCL_INIT_INT_MASK                 0x0F000000u
#define BC250_SCL_LB_MEMORY_CONFIG_MASK         0x00000003u
#define BC250_SCL_LB_MAX_PARTITIONS_SHIFT       8u
#define BC250_SCL_LUT_MEM_PWR_FORCE_MASK        0x00000003u
#define BC250_SCL_XY_LOW_MASK                   0x00003FFFu   /* largura / X */
#define BC250_SCL_XY_HIGH_SHIFT                 16u
#define BC250_SCL_PITCH_MASK                    0x00003FFFu
#define BC250_SCL_MASTER_LOCK                   0x00000001u

#define BC250_SCL_NUM_PHASES        64u
#define BC250_SCL_FILTER_PHASES     (BC250_SCL_NUM_PHASES / 2u + 1u)   /* 33: a tabela guarda meia simetria */
#define BC250_SCL_FILTER_TAPS       4u
#define BC250_SCL_FILTER_WORDS      (BC250_SCL_FILTER_PHASES * BC250_SCL_FILTER_TAPS / 2u)  /* 66 escritas */

/* dce_scl_filters.c v6.18, filter_4tap_64p_upscale[132] (AMD, MIT). Extraido
 * por script do arquivo local inc/linux_ref/dcn/dce_scl_filters.c
 * (sha256 60fb9714...), nao digitado. */
static const bc250_u16_t g_Bc250SclFilter4Tap64pUpscale[132] = {
    0x0000u, 0x1000u, 0x0000u, 0x0000u,
    0x3FDCu, 0x0FFCu, 0x0028u, 0x0000u,
    0x3FB4u, 0x0FF8u, 0x0054u, 0x0000u,
    0x3F94u, 0x0FE8u, 0x0084u, 0x0000u,
    0x3F74u, 0x0FDCu, 0x00B4u, 0x3FFCu,
    0x3F58u, 0x0FC4u, 0x00E8u, 0x3FFCu,
    0x3F3Cu, 0x0FACu, 0x0120u, 0x3FF8u,
    0x3F24u, 0x0F90u, 0x0158u, 0x3FF4u,
    0x3F0Cu, 0x0F70u, 0x0194u, 0x3FF0u,
    0x3EF8u, 0x0F4Cu, 0x01D0u, 0x3FECu,
    0x3EE8u, 0x0F20u, 0x0210u, 0x3FE8u,
    0x3ED8u, 0x0EF4u, 0x0254u, 0x3FE0u,
    0x3ECCu, 0x0EC4u, 0x0298u, 0x3FD8u,
    0x3EC0u, 0x0E90u, 0x02DCu, 0x3FD4u,
    0x3EB8u, 0x0E58u, 0x0324u, 0x3FCCu,
    0x3EB0u, 0x0E20u, 0x036Cu, 0x3FC4u,
    0x3EACu, 0x0DE4u, 0x03B8u, 0x3FB8u,
    0x3EA8u, 0x0DA4u, 0x0404u, 0x3FB0u,
    0x3EA4u, 0x0D60u, 0x0454u, 0x3FA8u,
    0x3EA4u, 0x0D1Cu, 0x04A4u, 0x3F9Cu,
    0x3EA4u, 0x0CD8u, 0x04F4u, 0x3F90u,
    0x3EA8u, 0x0C88u, 0x0548u, 0x3F88u,
    0x3EACu, 0x0C3Cu, 0x059Cu, 0x3F7Cu,
    0x3EB0u, 0x0BF0u, 0x05F0u, 0x3F70u,
    0x3EB8u, 0x0BA0u, 0x0644u, 0x3F64u,
    0x3EBCu, 0x0B54u, 0x0698u, 0x3F58u,
    0x3EC4u, 0x0B00u, 0x06F0u, 0x3F4Cu,
    0x3ECCu, 0x0AACu, 0x0748u, 0x3F40u,
    0x3ED8u, 0x0A54u, 0x07A0u, 0x3F34u,
    0x3EE0u, 0x0A04u, 0x07F8u, 0x3F24u,
    0x3EECu, 0x09ACu, 0x0850u, 0x3F18u,
    0x3EF8u, 0x0954u, 0x08A8u, 0x3F0Cu,
    0x3F00u, 0x08FCu, 0x0900u, 0x3F04u,
};

/* Escalas do caminho (D3DKMDT_VIDPN_PRESENT_PATH_SCALING, d3dkmdt.h). */
#define BC250_SCALE_IDENTITY     1u
#define BC250_SCALE_CENTERED     2u
#define BC250_SCALE_STRETCHED    3u
#define BC250_SCALE_ASPECT       4u

#define BC250_SCALE_MAX_MODES    9u

/* Tamanhos comuns de desktop, do maior para o menor. So' entram os que cabem
 * no nativo e tem largura multipla de 64. */
static const uint32_t g_Bc250ScaleCommonSizes[][2] = {
    { 1920u, 1080u }, { 1600u, 900u }, { 1280u, 1024u }, { 1280u, 960u },
    { 1280u, 800u }, { 1280u, 720u }, { 1024u, 768u }, { 640u, 480u }
};

/* Lista de modos: o nativo primeiro, depois os comuns que cabem. Devolve o
 * numero de tamanhos (<= BC250_SCALE_MAX_MODES). */
static __inline uint32_t Bc250ScaleBuildModeList(uint32_t NativeW, uint32_t NativeH,
    uint32_t (*Out)[2], uint32_t Capacity)
{
    uint32_t n = 0u, i;
    if (Out == 0 || Capacity == 0u || NativeW == 0u || NativeH == 0u) return 0u;
    Out[n][0] = NativeW; Out[n][1] = NativeH; ++n;
    for (i = 0u; i < sizeof(g_Bc250ScaleCommonSizes) / sizeof(g_Bc250ScaleCommonSizes[0]) && n < Capacity; ++i) {
        uint32_t w = g_Bc250ScaleCommonSizes[i][0], h = g_Bc250ScaleCommonSizes[i][1];
        if (w > NativeW || h > NativeH || (w == NativeW && h == NativeH) || (w % 64u) != 0u) continue;
        Out[n][0] = w; Out[n][1] = h; ++n;
    }
    return n;
}

/* A escala vale para o tamanho? O nativo aceita todas (1:1 em todas); o menor
 * aceita centralizada, esticada e proporcional, nunca identidade. */
static __inline int Bc250ScaleSupports(uint32_t SrcW, uint32_t SrcH, uint32_t NativeW, uint32_t NativeH,
    uint32_t Scaling)
{
    if (SrcW == NativeW && SrcH == NativeH)
        return Scaling >= BC250_SCALE_IDENTITY && Scaling <= BC250_SCALE_ASPECT;
    return Scaling == BC250_SCALE_CENTERED || Scaling == BC250_SCALE_STRETCHED ||
        Scaling == BC250_SCALE_ASPECT;
}

typedef struct BC250_SCALE_PLAN {
    uint32_t SrcW, SrcH, DstW, DstH, Scaling;
    uint32_t RecoutX, RecoutY, RecoutW, RecoutH;
    uint32_t RatioH19, RatioV19;          /* u3.19 */
    uint32_t InitH19, InitV19;            /* u4.19 */
    uint32_t TapsH, TapsV;
    uint32_t DsclMode;
    uint32_t LbMemoryConfig;              /* enum lb_memory_config */
    uint32_t Bypass;                      /* 1 = razao 1 nos dois eixos */
} BC250_SCALE_PLAN;

/* dscl2_calc_lb_num_partitions (formato float: 6 pixels por palavra). */
static __inline uint32_t Bc250ScaleLbPartitions(uint32_t LineSize, uint32_t Config)
{
    uint32_t mem = Config == 1u ? 970u : (Config == 2u ? 1290u : 970u + 1290u + 484u);
    uint32_t line = (LineSize == 0u ? 1u : LineSize);
    uint32_t parts = mem / ((line + 5u) / 6u);
    return parts > 64u ? 64u : parts;
}

static __inline int Bc250ScaleLbValid(uint32_t CeilVRatio, uint32_t Parts, uint32_t VTaps)
{
    if (CeilVRatio > 2u) return VTaps + CeilVRatio <= Parts + 2u;
    return VTaps <= Parts;
}

/* Plano completo; devolve 0 e nada preenchido de util se recusar. */
static __inline int Bc250ScaleComputePlan(uint32_t SrcW, uint32_t SrcH, uint32_t DstW, uint32_t DstH,
    uint32_t Scaling, BC250_SCALE_PLAN *P)
{
    uint32_t lineSize;
    if (P == 0) return 0;
    P->SrcW = SrcW; P->SrcH = SrcH; P->DstW = DstW; P->DstH = DstH; P->Scaling = Scaling;
    P->RecoutX = 0u; P->RecoutY = 0u; P->RecoutW = DstW; P->RecoutH = DstH;
    P->RatioH19 = 1u << 19; P->RatioV19 = 1u << 19;
    P->TapsH = 1u; P->TapsV = 1u; P->InitH19 = 0u; P->InitV19 = 0u;
    P->DsclMode = BC250_SCL_DSCL_MODE_444_BYPASS; P->LbMemoryConfig = 0u; P->Bypass = 1u;
    if (SrcW == 0u || SrcH == 0u || DstW == 0u || DstH == 0u || SrcW > DstW || SrcH > DstH ||
        SrcW > 0x3FFFu || SrcH > 0x3FFFu || !Bc250ScaleSupports(SrcW, SrcH, DstW, DstH, Scaling)) {
        return 0;
    }
    if (SrcW == DstW && SrcH == DstH) {
        return 1;   /* 1:1: bypass, sem filtro */
    }
    if (Scaling == BC250_SCALE_CENTERED) {
        P->RecoutW = SrcW; P->RecoutH = SrcH;
    } else if (Scaling == BC250_SCALE_ASPECT) {
        /* O maior retangulo com o aspecto da origem que cabe, centrado. */
        if ((uint64_t)SrcW * DstH >= (uint64_t)SrcH * DstW) {
            P->RecoutW = DstW;
            P->RecoutH = (uint32_t)(((uint64_t)SrcH * DstW) / SrcW);
        } else {
            P->RecoutH = DstH;
            P->RecoutW = (uint32_t)(((uint64_t)SrcW * DstH) / SrcH);
        }
    }
    P->RecoutX = (DstW - P->RecoutW) / 2u;
    P->RecoutY = (DstH - P->RecoutH) / 2u;
    /* Razao = origem / recout, truncada a 19 bits (dc_fixpt_truncate). */
    P->RatioH19 = (uint32_t)(((uint64_t)SrcW << 19) / P->RecoutW);
    P->RatioV19 = (uint32_t)(((uint64_t)SrcH << 19) / P->RecoutH);
    if (P->RatioH19 > (1u << 19) || P->RatioV19 > (1u << 19)) return 0;   /* so' amplia */
    P->TapsH = P->RatioH19 == (1u << 19) ? 1u : 4u;
    P->TapsV = P->RatioV19 == (1u << 19) ? 1u : 4u;
    P->Bypass = (P->TapsH == 1u && P->TapsV == 1u) ? 1u : 0u;
    P->DsclMode = P->Bypass ? BC250_SCL_DSCL_MODE_444_BYPASS : BC250_SCL_DSCL_MODE_444_RGB_ENABLE;
    P->InitH19 = (P->RatioH19 + ((P->TapsH + 1u) << 19)) / 2u;
    P->InitV19 = (P->RatioV19 + ((P->TapsV + 1u) << 19)) / 2u;
    /* Line buffer: a primeira configuracao que comporta os taps verticais. */
    lineSize = SrcW < P->RecoutW ? SrcW : P->RecoutW;
    if (Bc250ScaleLbValid(1u, Bc250ScaleLbPartitions(lineSize, 1u), P->TapsV)) P->LbMemoryConfig = 1u;
    else if (Bc250ScaleLbValid(1u, Bc250ScaleLbPartitions(lineSize, 2u), P->TapsV)) P->LbMemoryConfig = 2u;
    else P->LbMemoryConfig = 0u;
    return 1;
}

/* Valores de registrador derivados do plano (Lei 6 fica no chamador). */
static __inline uint32_t Bc250ScaleRatioReg(uint32_t Ratio19) { return (Ratio19 << 5) & BC250_SCL_RATIO_MASK; }
static __inline uint32_t Bc250ScaleInitReg(uint32_t Init19)
{
    return (((Init19 & 0x7FFFFu) << 5) & BC250_SCL_INIT_FRAC_MASK) |
        (((Init19 >> 19) << BC250_SCL_INIT_INT_SHIFT) & BC250_SCL_INIT_INT_MASK);
}
static __inline uint32_t Bc250ScaleXy(uint32_t Lo, uint32_t Hi)
{
    return (Lo & BC250_SCL_XY_LOW_MASK) | ((Hi & BC250_SCL_XY_LOW_MASK) << BC250_SCL_XY_HIGH_SHIFT);
}
static __inline uint32_t Bc250ScaleTapControl(uint32_t TapsH, uint32_t TapsV)
{
    /* V, H, V_C, H_C: taps - 1 em campos de 3 bits nos bits 0, 4, 8, 12. */
    return ((TapsV - 1u) & 7u) | (((TapsH - 1u) & 7u) << 4) |
        (((TapsV - 1u) & 7u) << 8) | (((TapsH - 1u) & 7u) << 12);
}
/* Palavra k (0..65) do filtro de 4 taps: par de taps da fase. */
static __inline uint32_t Bc250ScaleCoefWord(uint32_t K)
{
    uint32_t phase = K / 2u, pair = K % 2u;
    uint32_t even = g_Bc250SclFilter4Tap64pUpscale[phase * 4u + 2u * pair];
    uint32_t odd = g_Bc250SclFilter4Tap64pUpscale[phase * 4u + 2u * pair + 1u];
    return (even & BC250_SCL_COEF_MASK) | BC250_SCL_COEF_EVEN_EN |
        ((odd & BC250_SCL_COEF_MASK) << BC250_SCL_COEF_ODD_SHIFT) | BC250_SCL_COEF_ODD_EN;
}

#endif
