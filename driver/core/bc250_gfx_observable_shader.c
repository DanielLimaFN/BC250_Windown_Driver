#include "bc250_gfx_observable_shader.h"

/*
 * =========================================================================
 * B.2.2 -- BLOBS E MANIFESTOS DE SHADER GRAFICO OBSERVAVEL (VS / PS)
 *
 * PROVENIENCIA: CLASSE H. Ver cabecalho para a declaracao formal.
 *
 * DETALHAMENTO DE INSTRUCOES (AMD RDNA 1.0 ISA / GFX10.1):
 *
 * 1. Vertex Shader (VS) -- 8 dwords:
 *    v_mov_b32_e32 v0, 0          -> 0x7E000280 (pos.x = 0.0)
 *    v_mov_b32_e32 v1, 0          -> 0x7E020280 (pos.y = 0.0)
 *    v_mov_b32_e32 v2, 0          -> 0x7E040280 (pos.z = 0.0)
 *    v_mov_b32_e32 v3, 0x3f800000 -> 0x7E0602FF, 0x3F800000 (pos.w = 1.0)
 *    exp pos0, v0, v1, v2, v3 done:
 *      dword 0: (0x3E << 26) | (1 << 11 [done]) | (12 << 4 [pos0]) | 0xF [en]
 *               = 0xF80008CF
 *      dword 1: v0 | (v1 << 8) | (v2 << 16) | (v3 << 24)
 *               = 0x03020100
 *    s_endpgm                     -> 0xBF810000 (fonte unica bc250_isa_validator.h)
 *
 * 2. Pixel Shader (PS) -- 9 dwords:
 *    v_mov_b32_e32 v0, 0x3f800000 -> 0x7E0002FF, 0x3F800000 (col.r = 1.0)
 *    v_mov_b32_e32 v1, 0          -> 0x7E020280 (col.g = 0.0)
 *    v_mov_b32_e32 v2, 0          -> 0x7E040280 (col.b = 0.0)
 *    v_mov_b32_e32 v3, 0x3f800000 -> 0x7E0602FF, 0x3F800000 (col.a = 1.0)
 *    exp mrt0, v0, v1, v2, v3 done vm:
 *      dword 0: (0x3E << 26) | (1 << 12 [vm]) | (1 << 11 [done]) | (0 << 4 [mrt0]) | 0xF [en]
 *               = 0xF800180F
 *      dword 1: v0 | (v1 << 8) | (v2 << 16) | (v3 << 24)
 *               = 0x03020100
 *    s_endpgm                     -> 0xBF810000
 *
 * NADA AQUI FOI VALIDADO EM HARDWARE.
 * =========================================================================
 */

static const uint32_t g_Bc250GfxObservableVsShader[BC250_GFX_OBSERVABLE_VS_DWORDS] = {
    0x7E000280u,
    0x7E020280u,
    0x7E040280u,
    0x7E0602FFu,
    0x3F800000u,
    0xF80008CFu,
    0x03020100u,
    BC250_OPCODE_S_ENDPGM
};

static const uint32_t g_Bc250GfxObservablePsShader[BC250_GFX_OBSERVABLE_PS_DWORDS] = {
    0x7E0002FFu,
    0x3F800000u,
    0x7E020280u,
    0x7E040280u,
    0x7E0602FFu,
    0x3F800000u,
    0xF800180Fu,
    0x03020100u,
    BC250_OPCODE_S_ENDPGM
};

static const BC250_GFX_OBSERVABLE_STAGE_MANIFEST g_Bc250GfxObservableVsManifest = {
    (uint32_t)BC250_GFX_SHADER_STAGE_VS,
    10u,
    1u,
    3u,
    32u,
    8u,
    4u,
    0u,
    0u,
    0xF0u,
    0x00AF0000u,
    0x00000000u,
    BC250_GFX_OBSERVABLE_VS_DWORDS,
    g_Bc250GfxObservableVsShader,
    BC250_GFX_OBSERVABLE_VS_SHA256_HEX,
    BC250_GFX_OBSERVABLE_EPISTEMIC_CLASS,
    BC250_GFX_OBSERVABLE_PROVENANCE_STR
};

static const BC250_GFX_OBSERVABLE_STAGE_MANIFEST g_Bc250GfxObservablePsManifest = {
    (uint32_t)BC250_GFX_SHADER_STAGE_PS,
    10u,
    1u,
    3u,
    32u,
    8u,
    4u,
    0u,
    0u,
    0xF0u,
    0x00AF0000u,
    0x00000000u,
    BC250_GFX_OBSERVABLE_PS_DWORDS,
    g_Bc250GfxObservablePsShader,
    BC250_GFX_OBSERVABLE_PS_SHA256_HEX,
    BC250_GFX_OBSERVABLE_EPISTEMIC_CLASS,
    BC250_GFX_OBSERVABLE_PROVENANCE_STR
};

const BC250_GFX_OBSERVABLE_STAGE_MANIFEST *Bc250GetGfxObservableVsManifest(void)
{
    return &g_Bc250GfxObservableVsManifest;
}

const BC250_GFX_OBSERVABLE_STAGE_MANIFEST *Bc250GetGfxObservablePsManifest(void)
{
    return &g_Bc250GfxObservablePsManifest;
}

int Bc250ValidateGfxObservableBlob(
    uint32_t Stage,
    const uint32_t *ShaderDwords,
    uint32_t ShaderDwordCount)
{
    const uint32_t *expected;
    uint32_t expectedCount;
    uint32_t i;

    if (ShaderDwords == NULL) {
        return 0;
    }

    if (Stage == (uint32_t)BC250_GFX_SHADER_STAGE_VS) {
        expected = g_Bc250GfxObservableVsShader;
        expectedCount = BC250_GFX_OBSERVABLE_VS_DWORDS;
    } else if (Stage == (uint32_t)BC250_GFX_SHADER_STAGE_PS) {
        expected = g_Bc250GfxObservablePsShader;
        expectedCount = BC250_GFX_OBSERVABLE_PS_DWORDS;
    } else {
        return 0;
    }

    if (ShaderDwordCount != expectedCount) {
        return 0;
    }

    for (i = 0u; i < expectedCount; ++i) {
        if (ShaderDwords[i] != expected[i]) {
            return 0;
        }
    }
    return 1;
}

int Bc250ValidateGfxObservableManifest(
    const BC250_GFX_OBSERVABLE_STAGE_MANIFEST *Manifest)
{
    if (Manifest == NULL ||
        (Manifest->Stage != (uint32_t)BC250_GFX_SHADER_STAGE_VS &&
         Manifest->Stage != (uint32_t)BC250_GFX_SHADER_STAGE_PS) ||
        Manifest->TargetGfxMajor != 10u ||
        Manifest->TargetGfxMinor != 1u ||
        Manifest->TargetGfxStepping != 3u ||
        Manifest->WaveSize != 32u ||
        Manifest->NumSgprs != 8u ||
        Manifest->NumVgprs != 4u ||
        Manifest->UserSgprs != 0u ||
        Manifest->VgprCompCnt != 0u ||
        Manifest->FloatMode != 0xF0u ||
        Manifest->PgmRsrc1 != 0x00AF0000u ||
        Manifest->PgmRsrc2 != 0x00000000u ||
        Manifest->Sha256Hex == NULL ||
        Manifest->EpistemicClass == NULL ||
        Manifest->CompilerProvenance == NULL) {
        return 0;
    }

    return Bc250ValidateGfxObservableBlob(
        Manifest->Stage,
        Manifest->ShaderDwords,
        Manifest->ShaderDwordCount);
}

int Bc250PopulateGfxShaderPlanFromObservable(
    const BC250_GFX_OBSERVABLE_STAGE_MANIFEST *Manifest,
    uint64_t GpuAddress,
    BC250_GFX_SHADER_PLAN *Plan)
{
    uint32_t i;
    unsigned char *raw;

    if (Plan != NULL) {
        raw = (unsigned char *)Plan;
        for (i = 0u; i < (uint32_t)sizeof(*Plan); ++i) {
            raw[i] = 0u;
        }
    }

    if (Manifest == NULL || Plan == NULL) {
        return 0;
    }

    if (Bc250ValidateGfxObservableManifest(Manifest) == 0) {
        return 0;
    }

    Plan->Stage         = Manifest->Stage;
    Plan->GpuAddress    = GpuAddress;
    Plan->CodeBytes     = Manifest->ShaderDwordCount * 4u;
    Plan->NumVgprs      = Manifest->NumVgprs;
    Plan->NumSgprs      = Manifest->NumSgprs;
    Plan->UserSgprs     = Manifest->UserSgprs;
    Plan->FloatMode     = Manifest->FloatMode;
    Plan->WaveSize      = Manifest->WaveSize;
    Plan->VgprCompCnt   = Manifest->VgprCompCnt;
    Plan->Rsrc1Manifest = Manifest->PgmRsrc1;
    Plan->Rsrc2Manifest = Manifest->PgmRsrc2;
    return 1;
}
