#include "bc250_observable_shader.h"

/*
 * gfx1010, little-endian instruction dwords.  Generated/disassembled with
 * AMD clang/LLVM from ROCm 7.2.1 (hipclang-rocm-70201), target gfx1010.
 *
 *   v_mov_b32_e32 v0, 0
 *   v_mov_b32_e32 v1, 0xbc250c01
 *   global_store_dword v0, v1, s[0:1]
 *   s_waitcnt vmcnt(0)
 *   s_endpgm
 *
 * s[0:1] is populated by COMPUTE_USER_DATA_0/1 and is the only input.
 */
static const uint32_t g_Bc250ObservableShader[BC250_OBSERVABLE_SHADER_DWORDS] = {
    0x7E000280u,
    0x7E0202FFu,
    BC250_DISPATCH_RESULT_VALUE,
    0xDC708000u,
    0x00000100u,
    0xBF8C3F70u,
    BC250_OPCODE_S_ENDPGM
};

/*
 * RSRC1: VGPRS=0, SGPRS=0, FLOAT_MODE=0xF0, DX10_CLAMP=1, IEEE_MODE=1,
 * WGP_MODE=1.  RSRC2: USER_SGPR=2, no scratch/LDS/trap/TGID inputs.
 */
static const BC250_OBSERVABLE_SHADER_MANIFEST g_Bc250ObservableManifest = {
    10u,
    1u,
    3u,
    BC250_WAVE32,
    2u,
    2u,
    2u,
    0x20AF0000u,
    0x00000004u,
    BC250_DISPATCH_RESULT_VALUE,
    BC250_OBSERVABLE_SHADER_DWORDS,
    g_Bc250ObservableShader,
    BC250_OBSERVABLE_SHADER_SHA256_HEX,
    "AMD clang/LLVM ROCm 7.2.1; --offload-arch=gfx1010"
};

const BC250_OBSERVABLE_SHADER_MANIFEST *Bc250GetObservableShaderManifest(void)
{
    return &g_Bc250ObservableManifest;
}

int Bc250ValidateObservableShaderBlob(
    const uint32_t *ShaderDwords,
    uint32_t ShaderDwordCount)
{
    uint32_t i;

    if (ShaderDwords == NULL ||
        ShaderDwordCount != BC250_OBSERVABLE_SHADER_DWORDS) {
        return 0;
    }

    for (i = 0u; i < BC250_OBSERVABLE_SHADER_DWORDS; ++i) {
        if (ShaderDwords[i] != g_Bc250ObservableShader[i]) return 0;
    }
    return 1;
}

int Bc250ValidateObservableShaderManifest(
    const BC250_OBSERVABLE_SHADER_MANIFEST *Manifest)
{
    /*
     * GC10.1 COMPUTE_PGM_RSRC1 fields (gc_10_1_0_sh_mask.h):
     *   VGPRS[5:0]=0 (minimum allocation), SGPRS[9:6]=0,
     *   FLOAT_MODE[19:12]=0xf0, DX10_CLAMP[21]=1,
     *   IEEE_MODE[23]=1 and WGP_MODE[29]=1.
     * COMPUTE_PGM_RSRC2 USER_SGPR[5:1]=2 and all other fields are zero.
     */
    if (Manifest == NULL ||
        Manifest->TargetGfxMajor != 10u ||
        Manifest->TargetGfxMinor != 1u ||
        Manifest->TargetGfxStepping != 3u ||
        Manifest->WaveSize != BC250_WAVE32 ||
        Manifest->NumSgprs != 2u ||
        Manifest->NumVgprs != 2u ||
        Manifest->UserSgprs != 2u ||
        Manifest->PgmRsrc1 != 0x20AF0000u ||
        Manifest->PgmRsrc2 != 0x00000004u ||
        Manifest->ResultValue != BC250_DISPATCH_RESULT_VALUE ||
        Manifest->Sha256Hex == NULL ||
        Manifest->CompilerProvenance == NULL) {
        return 0;
    }

    return Bc250ValidateObservableShaderBlob(
        Manifest->ShaderDwords, Manifest->ShaderDwordCount);
}
