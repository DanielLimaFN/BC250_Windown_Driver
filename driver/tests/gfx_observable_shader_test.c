#include "../core/bc250_gfx_observable_shader.h"

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    unsigned char *bytes = (unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#else
#include <string.h>
#include <stdio.h>
/* Falha devolve 0 porque main() faz `return Run() ? 0 : 1`: devolver 1
 * aqui fazia a falha virar SUCESSO no build offline.
 * Travado por audit_offline_check_is_not_inverted. */
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#endif

static int StringEquals(const char *A, const char *B)
{
    if (A == NULL || B == NULL) return 0;
    while (*A && *B) {
        if (*A != *B) return 0;
        ++A;
        ++B;
    }
    return (*A == *B);
}

#define TEST_SHADER_VA 0x0000000000A8C000ull

static int Bc250RunGfxObservableShaderTest(void)
{
    const BC250_GFX_OBSERVABLE_STAGE_MANIFEST *vs;
    const BC250_GFX_OBSERVABLE_STAGE_MANIFEST *ps;
    BC250_GFX_SHADER_PLAN vsPlan;
    BC250_GFX_SHADER_PLAN psPlan;
    BC250_GFX_SHADER_REGS vsRegs;
    BC250_GFX_SHADER_REGS psRegs;
    uint32_t mutatedVs[BC250_GFX_OBSERVABLE_VS_DWORDS];
    uint32_t mutatedPs[BC250_GFX_OBSERVABLE_PS_DWORDS];
    uint32_t i;

    /* 1. Manifestos e validacao estrutural */
    vs = Bc250GetGfxObservableVsManifest();
    ps = Bc250GetGfxObservablePsManifest();

    CHECK(vs != NULL);
    CHECK(ps != NULL);

    CHECK(Bc250ValidateGfxObservableManifest(vs) != 0);
    CHECK(Bc250ValidateGfxObservableManifest(ps) != 0);

    /* 2. Propriedades de VS */
    CHECK(vs->Stage == (uint32_t)BC250_GFX_SHADER_STAGE_VS);
    CHECK(vs->TargetGfxMajor == 10u);
    CHECK(vs->TargetGfxMinor == 1u);
    CHECK(vs->TargetGfxStepping == 3u);
    CHECK(vs->WaveSize == 32u);
    CHECK(vs->NumVgprs == 4u);
    CHECK(vs->NumSgprs == 8u);
    CHECK(vs->UserSgprs == 0u);
    CHECK(vs->VgprCompCnt == 0u);
    CHECK(vs->PgmRsrc1 == 0x00AF0000u);
    CHECK(vs->PgmRsrc2 == 0x00000000u);
    CHECK(vs->ShaderDwordCount == BC250_GFX_OBSERVABLE_VS_DWORDS);
    CHECK(StringEquals(vs->Sha256Hex, BC250_GFX_OBSERVABLE_VS_SHA256_HEX));
    CHECK(StringEquals(vs->EpistemicClass, "H"));
    CHECK(StringEquals(vs->CompilerProvenance, BC250_GFX_OBSERVABLE_PROVENANCE_STR));

    /* VS termina em s_endpgm */
    CHECK(vs->ShaderDwords[BC250_GFX_OBSERVABLE_VS_DWORDS - 1u] == BC250_OPCODE_S_ENDPGM);
    /* VS instrucao EXP pos0: dword 5 e dword 6 */
    CHECK(vs->ShaderDwords[5] == 0xF80008CFu);
    CHECK(vs->ShaderDwords[6] == 0x03020100u);

    /* 3. Propriedades de PS */
    CHECK(ps->Stage == (uint32_t)BC250_GFX_SHADER_STAGE_PS);
    CHECK(ps->TargetGfxMajor == 10u);
    CHECK(ps->TargetGfxMinor == 1u);
    CHECK(ps->TargetGfxStepping == 3u);
    CHECK(ps->WaveSize == 32u);
    CHECK(ps->NumVgprs == 4u);
    CHECK(ps->NumSgprs == 8u);
    CHECK(ps->UserSgprs == 0u);
    CHECK(ps->VgprCompCnt == 0u);
    CHECK(ps->PgmRsrc1 == 0x00AF0000u);
    CHECK(ps->PgmRsrc2 == 0x00000000u);
    CHECK(ps->ShaderDwordCount == BC250_GFX_OBSERVABLE_PS_DWORDS);
    CHECK(StringEquals(ps->Sha256Hex, BC250_GFX_OBSERVABLE_PS_SHA256_HEX));
    CHECK(StringEquals(ps->EpistemicClass, "H"));
    CHECK(StringEquals(ps->CompilerProvenance, BC250_GFX_OBSERVABLE_PROVENANCE_STR));

    /* PS termina em s_endpgm */
    CHECK(ps->ShaderDwords[BC250_GFX_OBSERVABLE_PS_DWORDS - 1u] == BC250_OPCODE_S_ENDPGM);
    /* PS instrucao EXP mrt0 vm done: dword 6 e dword 7 */
    CHECK(ps->ShaderDwords[6] == 0xF800180Fu);
    CHECK(ps->ShaderDwords[7] == 0x03020100u);

    /* 4. Integracao com o contrato de shader puro (B.2.1) */
    memset(&vsPlan, 0, sizeof(vsPlan));
    memset(&psPlan, 0, sizeof(psPlan));
    memset(&vsRegs, 0, sizeof(vsRegs));
    memset(&psRegs, 0, sizeof(psRegs));

    CHECK(Bc250PopulateGfxShaderPlanFromObservable(vs, TEST_SHADER_VA, &vsPlan) != 0);
    CHECK(Bc250GfxShaderValidateBlob(vs->ShaderDwords, vs->ShaderDwordCount, &vsPlan) == BC250_GFX_SHADER_OK);
    CHECK(Bc250GfxShaderComputeRegs(&vsPlan, &vsRegs) == BC250_GFX_SHADER_OK);
    CHECK(vsRegs.Rsrc1 == vs->PgmRsrc1);
    CHECK(vsRegs.Rsrc1Computed == vs->PgmRsrc1);
    CHECK(vsRegs.Rsrc2 == vs->PgmRsrc2);
    CHECK(vsRegs.Rsrc2Computed == vs->PgmRsrc2);
    CHECK(vsRegs.PgmLo == (uint32_t)(TEST_SHADER_VA >> 8));

    CHECK(Bc250PopulateGfxShaderPlanFromObservable(ps, TEST_SHADER_VA, &psPlan) != 0);
    CHECK(Bc250GfxShaderValidateBlob(ps->ShaderDwords, ps->ShaderDwordCount, &psPlan) == BC250_GFX_SHADER_OK);
    CHECK(Bc250GfxShaderComputeRegs(&psPlan, &psRegs) == BC250_GFX_SHADER_OK);
    CHECK(psRegs.Rsrc1 == ps->PgmRsrc1);
    CHECK(psRegs.Rsrc1Computed == ps->PgmRsrc1);
    CHECK(psRegs.Rsrc2 == ps->PgmRsrc2);
    CHECK(psRegs.Rsrc2Computed == ps->PgmRsrc2);
    CHECK(psRegs.PgmLo == (uint32_t)(TEST_SHADER_VA >> 8));

    /* 5. Casos negativos: mutacoes de blob e validacoes */
    /* Falha fechada de Bc250PopulateGfxShaderPlanFromObservable (D4) */
    CHECK(Bc250PopulateGfxShaderPlanFromObservable(NULL, TEST_SHADER_VA, &vsPlan) == 0);
    CHECK(vsPlan.CodeBytes == 0u);
    CHECK(vsPlan.NumVgprs == 0u);
    CHECK(Bc250PopulateGfxShaderPlanFromObservable(vs, TEST_SHADER_VA, NULL) == 0);
    for (i = 0u; i < BC250_GFX_OBSERVABLE_VS_DWORDS; ++i) {
        mutatedVs[i] = vs->ShaderDwords[i];
    }
    mutatedVs[0] ^= 1u;
    CHECK(Bc250ValidateGfxObservableBlob(vs->Stage, mutatedVs, vs->ShaderDwordCount) == 0);
    CHECK(Bc250ValidateGfxObservableBlob(vs->Stage, vs->ShaderDwords, vs->ShaderDwordCount - 1u) == 0);
    CHECK(Bc250ValidateGfxObservableBlob(99u, vs->ShaderDwords, vs->ShaderDwordCount) == 0);

    for (i = 0u; i < BC250_GFX_OBSERVABLE_PS_DWORDS; ++i) {
        mutatedPs[i] = ps->ShaderDwords[i];
    }
    mutatedPs[0] ^= 1u;
    CHECK(Bc250ValidateGfxObservableBlob(ps->Stage, mutatedPs, ps->ShaderDwordCount) == 0);
    CHECK(Bc250ValidateGfxObservableBlob(ps->Stage, ps->ShaderDwords, ps->ShaderDwordCount - 1u) == 0);

    /* Mutacao de endpgm no blob: contrato rejeita */
    mutatedVs[BC250_GFX_OBSERVABLE_VS_DWORDS - 1u] = 0xBF800000u;
    CHECK(Bc250GfxShaderValidateBlob(mutatedVs, vs->ShaderDwordCount, &vsPlan) == BC250_GFX_SHADER_NO_ENDPGM);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunGfxObservableShaderTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunGfxObservableShaderTest() ? 0 : 1;
}
#endif
