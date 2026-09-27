#include "../core/bc250_gfx_shader_contract.h"
#include "../core/bc250_observable_shader.h"

#include <string.h>

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#else
/* Falha devolve 0 porque main() faz `return Run() ? 0 : 1`: devolver 1 aqui
 * fazia a falha virar SUCESSO no build offline. Travado por
 * audit_offline_check_is_not_inverted. */
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#endif

/* Janela coerente com a geometria real do segmento 1 (base 0xA8C000). */
#define TEST_SHADER_VA 0x0000000000A8C000ull

/*
 * RSRC1 de referencia com os campos de POLITICA que o compilador escolhe --
 * FLOAT_MODE=0xF0, DX10_CLAMP, IEEE_MODE -- e os campos DERIVADOS zerados.
 * Espelha 0x20AF0000 do shader de compute, menos o WGP_MODE, que e' de
 * compute e nao existe em SPI_SHADER_PGM_RSRC1_{PS,VS}.
 */
#define TEST_RSRC1_POLICY 0x00AF0000u

static void FillPlan(BC250_GFX_SHADER_PLAN *Plan, uint32_t Stage)
{
    memset(Plan, 0, sizeof(*Plan));
    Plan->Stage         = Stage;
    Plan->GpuAddress    = TEST_SHADER_VA;
    Plan->CodeBytes     = 64u;
    Plan->NumVgprs      = 4u;
    Plan->NumSgprs      = 8u;
    Plan->UserSgprs     = 2u;
    Plan->FloatMode     = 0xF0u;
    Plan->WaveSize      = 32u;
    Plan->VgprCompCnt   = 0u;
    /* wave32: ceil(4/8)-1 = 0 em VGPRS; ceil(8/8)-1 = 0 em SGPRS */
    Plan->Rsrc1Manifest = TEST_RSRC1_POLICY;
    /* USER_SGPR=2 -> bits[5:1] = 2 -> 0x04 */
    Plan->Rsrc2Manifest = 0x00000004u;
}

/*
 * ANCORA QUE TORNA A REGRA H FALSIFICAVEL.
 *
 * bc250_observable_shader.c carrega um manifesto produzido por compilador
 * REAL (AMD clang/LLVM ROCm 7.2.1, --offload-arch=gfx1010) e o shader dele
 * JA EXECUTOU NO SILICIO. COMPUTE_PGM_RSRC1 e SPI_SHADER_PGM_RSRC1_{PS,VS}
 * tem VGPRS em [5:0] e SGPRS em [9:6] -- mesmas posicoes, mesma ISA, mesma
 * granularidade de alocacao.
 *
 * Se a minha regra de granularidade estiver errada, ela NAO reproduz os
 * campos que o compilador emitiu, e esta suite reprova. E' o unico anexo de
 * realidade que a regra tem enquanto nao houver corrida.
 *
 * RESSALVA DECLARADA: com NumVgprs=2 e NumSgprs=2 qualquer granularidade
 * >= 2 produz campo 0, entao esta ancora CONFIRMA mas nao DISCRIMINA a
 * escala. Ela e' fraca de proposito e esta anotada como fraca; quem a ler
 * como prova da granularidade esta lendo errado.
 */
static int TestGranularityAgainstRealCompilerManifest(void)
{
    const BC250_OBSERVABLE_SHADER_MANIFEST *m;
    uint32_t vgprField;
    uint32_t sgprField;
    uint32_t userSgprField;

    m = Bc250GetObservableShaderManifest();
    CHECK(m != NULL);
    CHECK(Bc250ValidateObservableShaderManifest(m) == 1);

    vgprField = (m->PgmRsrc1 & BC250_GFX_RSRC1_VGPRS_MASK) >>
                BC250_GFX_RSRC1_VGPRS_SHIFT;
    sgprField = (m->PgmRsrc1 & BC250_GFX_RSRC1_SGPRS_MASK) >>
                BC250_GFX_RSRC1_SGPRS_SHIFT;
    userSgprField = (m->PgmRsrc2 & BC250_GFX_RSRC2_USER_SGPR_MASK) >>
                    BC250_GFX_RSRC2_USER_SGPR_SHIFT;

    CHECK(Bc250GfxShaderEncodeVgprField(m->NumVgprs, 32u) == vgprField);
    CHECK(Bc250GfxShaderEncodeSgprField(m->NumSgprs) == sgprField);
    CHECK(userSgprField == m->UserSgprs);
    return 1;
}

static int TestEncodingBoundaries(void)
{
    /* Zero VGPR ainda ocupa um bloco: campo 0, nao "menos um". */
    CHECK(Bc250GfxShaderEncodeVgprField(0u, 32u) == 0u);
    CHECK(Bc250GfxShaderEncodeVgprField(1u, 32u) == 0u);
    CHECK(Bc250GfxShaderEncodeVgprField(8u, 32u) == 0u);
    CHECK(Bc250GfxShaderEncodeVgprField(9u, 32u) == 1u);
    CHECK(Bc250GfxShaderEncodeVgprField(16u, 32u) == 1u);

    /* wave64 usa granulo 4 -- a mesma contagem da campo DIFERENTE. */
    CHECK(Bc250GfxShaderEncodeVgprField(8u, 64u) == 1u);
    CHECK(Bc250GfxShaderEncodeVgprField(9u, 64u) == 2u);
    CHECK(Bc250GfxShaderEncodeVgprField(8u, 32u) !=
          Bc250GfxShaderEncodeVgprField(8u, 64u));

    CHECK(Bc250GfxShaderEncodeSgprField(0u) == 0u);
    CHECK(Bc250GfxShaderEncodeSgprField(8u) == 0u);
    CHECK(Bc250GfxShaderEncodeSgprField(9u) == 1u);
    return 1;
}

static int TestStageRegOffsetsMatchCatalog(void)
{
    uint32_t off;

    CHECK(Bc250GfxShaderStageRegOffset(
              (uint32_t)BC250_GFX_SHADER_STAGE_VS, &off) ==
          BC250_GFX_SHADER_OK);
    CHECK(off == 0x048u);
    /* (0x1260 + 0x19E8) - 0x2C00 = 0x048, e (0x1260+0x19E8)*4 = 0x0B120 */
    CHECK(((0x1260u + 0x19E8u) - BC250_GFX_SH_REG_BASE_DW) == off);
    CHECK(((0x1260u + 0x19E8u) * 4u) == 0x0B120u);

    CHECK(Bc250GfxShaderStageRegOffset(
              (uint32_t)BC250_GFX_SHADER_STAGE_PS, &off) ==
          BC250_GFX_SHADER_OK);
    CHECK(off == 0x008u);
    CHECK(((0x1260u + 0x19A8u) - BC250_GFX_SH_REG_BASE_DW) == off);
    CHECK(((0x1260u + 0x19A8u) * 4u) == 0x0B020u);

    /* Os quatro registradores de cada estagio sao contiguos -- e' o que
     * autoriza um unico SET_SH_REG de 4 valores. */
    CHECK(BC250_GFX_SH_PGM_HI_PS    == BC250_GFX_SH_PGM_LO_PS + 1u);
    CHECK(BC250_GFX_SH_PGM_RSRC1_PS == BC250_GFX_SH_PGM_LO_PS + 2u);
    CHECK(BC250_GFX_SH_PGM_RSRC2_PS == BC250_GFX_SH_PGM_LO_PS + 3u);
    CHECK(BC250_GFX_SH_PGM_HI_VS    == BC250_GFX_SH_PGM_LO_VS + 1u);
    CHECK(BC250_GFX_SH_PGM_RSRC1_VS == BC250_GFX_SH_PGM_LO_VS + 2u);
    CHECK(BC250_GFX_SH_PGM_RSRC2_VS == BC250_GFX_SH_PGM_LO_VS + 3u);

    CHECK(Bc250GfxShaderStageRegOffset(7u, &off) ==
          BC250_GFX_SHADER_BAD_ARGUMENT);
    CHECK(Bc250GfxShaderStageRegOffset(
              (uint32_t)BC250_GFX_SHADER_STAGE_VS, NULL) ==
          BC250_GFX_SHADER_BAD_ARGUMENT);
    return 1;
}

static int TestAddressSplit(void)
{
    BC250_GFX_SHADER_PLAN plan;
    BC250_GFX_SHADER_REGS regs;

    FillPlan(&plan, (uint32_t)BC250_GFX_SHADER_STAGE_VS);
    memset(&regs, 0xEE, sizeof(regs));
    CHECK(Bc250GfxShaderComputeRegs(&plan, &regs) == BC250_GFX_SHADER_OK);
    CHECK(regs.PgmLo == (uint32_t)(TEST_SHADER_VA >> 8));
    CHECK(regs.PgmHi == (uint32_t)(TEST_SHADER_VA >> 40));
    CHECK(regs.Rsrc1 == plan.Rsrc1Manifest);
    CHECK(regs.Rsrc2 == plan.Rsrc2Manifest);

    /* Um endereco alto exercita o HI de verdade -- com LO cru o HI sairia
     * zero e o teste nao distinguiria as duas convencoes. */
    plan.GpuAddress = 0x0000123456789000ull;
    CHECK(Bc250GfxShaderComputeRegs(&plan, &regs) == BC250_GFX_SHADER_OK);
    CHECK(regs.PgmLo == 0x34567890u);
    CHECK(regs.PgmHi == 0x00000012u);
    CHECK(regs.PgmHi != 0u);
    return 1;
}

static int TestFailClosed(void)
{
    BC250_GFX_SHADER_PLAN plan;
    BC250_GFX_SHADER_REGS regs;

    /* desalinhado: perderia os 8 bits baixos em silencio */
    FillPlan(&plan, (uint32_t)BC250_GFX_SHADER_STAGE_VS);
    plan.GpuAddress = TEST_SHADER_VA + 0x80ull;
    CHECK(Bc250GfxShaderComputeRegs(&plan, &regs) ==
          BC250_GFX_SHADER_MISALIGNED);

    /* acima de 48 bits: nao cabe em LO+HI */
    FillPlan(&plan, (uint32_t)BC250_GFX_SHADER_STAGE_VS);
    plan.GpuAddress = 0x0001000000000000ull;
    CHECK(Bc250GfxShaderComputeRegs(&plan, &regs) ==
          BC250_GFX_SHADER_ADDRESS_OUT_OF_RANGE);

    /* codigo vazio */
    FillPlan(&plan, (uint32_t)BC250_GFX_SHADER_STAGE_VS);
    plan.CodeBytes = 0u;
    CHECK(Bc250GfxShaderComputeRegs(&plan, &regs) ==
          BC250_GFX_SHADER_EMPTY_CODE);

    /* VGPR_COMP_CNT no PS cai no bit de MEM_ORDERED: recusa, nao ignora */
    FillPlan(&plan, (uint32_t)BC250_GFX_SHADER_STAGE_PS);
    plan.VgprCompCnt = 1u;
    CHECK(Bc250GfxShaderComputeRegs(&plan, &regs) ==
          BC250_GFX_SHADER_BAD_ARGUMENT);

    /* wave size invalido */
    FillPlan(&plan, (uint32_t)BC250_GFX_SHADER_STAGE_VS);
    plan.WaveSize = 16u;
    CHECK(Bc250GfxShaderComputeRegs(&plan, &regs) ==
          BC250_GFX_SHADER_BAD_ARGUMENT);

    /* USER_SGPR maior que o campo de 5 bits */
    FillPlan(&plan, (uint32_t)BC250_GFX_SHADER_STAGE_VS);
    plan.UserSgprs = 32u;
    CHECK(Bc250GfxShaderComputeRegs(&plan, &regs) ==
          BC250_GFX_SHADER_FIELD_OVERFLOW);

    CHECK(Bc250GfxShaderComputeRegs(NULL, &regs) ==
          BC250_GFX_SHADER_BAD_ARGUMENT);
    FillPlan(&plan, (uint32_t)BC250_GFX_SHADER_STAGE_VS);
    CHECK(Bc250GfxShaderComputeRegs(&plan, NULL) ==
          BC250_GFX_SHADER_BAD_ARGUMENT);
    return 1;
}

static int TestManifestMismatchIsFatal(void)
{
    BC250_GFX_SHADER_PLAN plan;
    BC250_GFX_SHADER_REGS regs;

    /*
     * O compilador declarou VGPRS=3 (32 VGPRs) mas o plano diz que o shader
     * usa 4. Se o contrato publicasse o manifesto sem conferir, o silicio
     * receberia uma alocacao coerente -- e o defeito ficaria invisivel ate
     * alguem mudar a contagem. Aqui ele e' FATAL.
     */
    FillPlan(&plan, (uint32_t)BC250_GFX_SHADER_STAGE_VS);
    plan.Rsrc1Manifest = TEST_RSRC1_POLICY | 0x03u;
    memset(&regs, 0xEE, sizeof(regs));
    CHECK(Bc250GfxShaderComputeRegs(&plan, &regs) ==
          BC250_GFX_SHADER_MANIFEST_MISMATCH);
    /* falha fechada: nada publicavel sobra na saida */
    CHECK(regs.PgmLo == 0u);
    CHECK(regs.PgmHi == 0u);
    CHECK(regs.Rsrc1 == 0u);
    CHECK(regs.Rsrc2 == 0u);
    /* mas o valor calculado FICA, para o log dizer de quanto foi a briga */
    CHECK(regs.Rsrc1Computed == TEST_RSRC1_POLICY);

    /* divergencia so' no RSRC2 tambem e' fatal */
    FillPlan(&plan, (uint32_t)BC250_GFX_SHADER_STAGE_VS);
    plan.Rsrc2Manifest = 0x00000006u; /* USER_SGPR=3, plano diz 2 */
    CHECK(Bc250GfxShaderComputeRegs(&plan, &regs) ==
          BC250_GFX_SHADER_MANIFEST_MISMATCH);

    /*
     * CONTRAPONTO -- sem ele a conferencia viraria peneira. Os campos de
     * POLITICA do compilador tem de passar INTACTOS: mudar FLOAT_MODE nao e'
     * divergencia, e' escolha dele.
     */
    FillPlan(&plan, (uint32_t)BC250_GFX_SHADER_STAGE_VS);
    plan.Rsrc1Manifest = TEST_RSRC1_POLICY | BC250_GFX_RSRC1_VS_MEM_ORDERED_MASK;
    CHECK(Bc250GfxShaderComputeRegs(&plan, &regs) == BC250_GFX_SHADER_OK);
    CHECK((regs.Rsrc1 & BC250_GFX_RSRC1_VS_MEM_ORDERED_MASK) != 0u);
    CHECK(regs.Rsrc1 == plan.Rsrc1Manifest);

    FillPlan(&plan, (uint32_t)BC250_GFX_SHADER_STAGE_VS);
    plan.Rsrc2Manifest = 0x00000004u | BC250_GFX_RSRC2_SCRATCH_EN_MASK;
    CHECK(Bc250GfxShaderComputeRegs(&plan, &regs) == BC250_GFX_SHADER_OK);
    CHECK((regs.Rsrc2 & BC250_GFX_RSRC2_SCRATCH_EN_MASK) != 0u);
    return 1;
}

static int TestMemOrderedBitDiffersPerStage(void)
{
    /*
     * PS usa o bit 25 e VS o 27. Quem generalizar os dois estagios numa
     * formula so' escreve VGPR_COMP_CNT achando que escreve MEM_ORDERED.
     */
    CHECK(BC250_GFX_RSRC1_PS_MEM_ORDERED_MASK !=
          BC250_GFX_RSRC1_VS_MEM_ORDERED_MASK);
    CHECK(BC250_GFX_RSRC1_PS_MEM_ORDERED_MASK == 0x02000000u);
    CHECK(BC250_GFX_RSRC1_VS_MEM_ORDERED_MASK == 0x08000000u);
    /* o bit de MEM_ORDERED do PS cai DENTRO de VGPR_COMP_CNT do VS */
    CHECK((BC250_GFX_RSRC1_PS_MEM_ORDERED_MASK &
           BC250_GFX_RSRC1_VS_VGPR_COMP_CNT_MASK) != 0u);
    return 1;
}

static int TestBlobValidationDelegates(void)
{
    BC250_GFX_SHADER_PLAN plan;
    uint32_t good[3];
    uint32_t bad[3];

    FillPlan(&plan, (uint32_t)BC250_GFX_SHADER_STAGE_PS);

    good[0] = 0x7E000280u;
    good[1] = 0xBF8C3F70u;
    good[2] = BC250_OPCODE_S_ENDPGM;
    CHECK(Bc250GfxShaderValidateBlob(good, 3u, &plan) == BC250_GFX_SHADER_OK);

    /* sem s_endpgm a wave nunca retorna -- hang de engine grafica */
    bad[0] = 0x7E000280u;
    bad[1] = 0x7E000280u;
    bad[2] = 0x7E000280u;
    CHECK(Bc250GfxShaderValidateBlob(bad, 3u, &plan) ==
          BC250_GFX_SHADER_NO_ENDPGM);

    CHECK(Bc250GfxShaderValidateBlob(good, 0u, &plan) ==
          BC250_GFX_SHADER_EMPTY_CODE);
    CHECK(Bc250GfxShaderValidateBlob(NULL, 3u, &plan) ==
          BC250_GFX_SHADER_BAD_ARGUMENT);
    CHECK(Bc250GfxShaderValidateBlob(good, 3u, NULL) ==
          BC250_GFX_SHADER_BAD_ARGUMENT);

    /* teto de VGPR: a fonte unica de ISA tem de reprovar */
    plan.NumVgprs = 4096u;
    CHECK(Bc250GfxShaderValidateBlob(good, 3u, &plan) !=
          BC250_GFX_SHADER_OK);
    return 1;
}

static int TestStatusNamesAreDistinct(void)
{
    CHECK(Bc250GfxShaderStatusName(BC250_GFX_SHADER_OK)[0] == 'O');
    CHECK(Bc250GfxShaderStatusName(BC250_GFX_SHADER_MANIFEST_MISMATCH)[0] == 'M');
    CHECK(Bc250GfxShaderStatusName((BC250_GFX_SHADER_STATUS)99)[0] == 'D');
    return 1;
}

static int Bc250RunGfxShaderSuite(void)
{
    if (!TestGranularityAgainstRealCompilerManifest()) return 0;
    if (!TestEncodingBoundaries()) return 0;
    if (!TestStageRegOffsetsMatchCatalog()) return 0;
    if (!TestAddressSplit()) return 0;
    if (!TestFailClosed()) return 0;
    if (!TestManifestMismatchIsFatal()) return 0;
    if (!TestMemOrderedBitDiffersPerStage()) return 0;
    if (!TestBlobValidationDelegates()) return 0;
    if (!TestStatusNamesAreDistinct()) return 0;
    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunGfxShaderSuite() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunGfxShaderSuite() ? 0 : 1;
}
#endif
