#include "bc250_gfx_shader_contract.h"

/*
 * B.2 -- parte PURA do programa de shader grafico. Nao toca em MMIO, nao
 * aloca memoria e nao depende de estado global. Ver a proveniencia completa
 * no cabecalho.
 *
 * NADA AQUI FOI VALIDADO EM HARDWARE.
 */

#define BC250_GFX_SHADER_VA_BITS 48u

static uint32_t Bc250GfxShaderCeilDiv(uint32_t Value, uint32_t Granule)
{
    if (Granule == 0u) {
        return 0u;
    }
    return (Value + Granule - 1u) / Granule;
}

uint32_t Bc250GfxShaderEncodeVgprField(uint32_t NumVgprs, uint32_t WaveSize)
{
    uint32_t granule;
    uint32_t blocks;

    /*
     * REGRA H -- ver o cabecalho. O sh_mask.h da a posicao do campo, nao a
     * escala. Esta funcao NAO decide o valor que vai ao silicio: ela produz
     * o valor de CONFERENCIA contra o manifesto do compilador.
     */
    granule = (WaveSize == 64u) ? BC250_GFX_VGPR_GRANULE_WAVE64
                                : BC250_GFX_VGPR_GRANULE_WAVE32;

    blocks = Bc250GfxShaderCeilDiv(NumVgprs, granule);
    if (blocks == 0u) {
        /*
         * Zero VGPR nao e' "zero blocos": o campo e' blocos-1, e um shader
         * que nao usa VGPR ainda ocupa a alocacao minima de um bloco.
         */
        blocks = 1u;
    }
    return blocks - 1u;
}

uint32_t Bc250GfxShaderEncodeSgprField(uint32_t NumSgprs)
{
    uint32_t blocks;

    blocks = Bc250GfxShaderCeilDiv(NumSgprs, BC250_GFX_SGPR_GRANULE);
    if (blocks == 0u) {
        blocks = 1u;
    }
    return blocks - 1u;
}

BC250_GFX_SHADER_STATUS Bc250GfxShaderStageRegOffset(
    uint32_t Stage,
    uint32_t *RegOffsetDw)
{
    if (RegOffsetDw == NULL) {
        return BC250_GFX_SHADER_BAD_ARGUMENT;
    }

    if (Stage == (uint32_t)BC250_GFX_SHADER_STAGE_VS) {
        *RegOffsetDw = BC250_GFX_SH_PGM_LO_VS;
        return BC250_GFX_SHADER_OK;
    }
    if (Stage == (uint32_t)BC250_GFX_SHADER_STAGE_PS) {
        *RegOffsetDw = BC250_GFX_SH_PGM_LO_PS;
        return BC250_GFX_SHADER_OK;
    }
    return BC250_GFX_SHADER_BAD_ARGUMENT;
}

BC250_GFX_SHADER_STATUS Bc250GfxShaderValidateBlob(
    const uint32_t *CodeDwords,
    uint32_t CodeDwordCount,
    const BC250_GFX_SHADER_PLAN *Plan)
{
    BC250_SHADER_RESOURCE_CONFIG config;
    BC250_SHADER_VALIDATION_RESULT result;
    BC250_ISA_STATUS isa;

    if (CodeDwords == NULL || Plan == NULL) {
        return BC250_GFX_SHADER_BAD_ARGUMENT;
    }
    if (CodeDwordCount == 0u) {
        return BC250_GFX_SHADER_EMPTY_CODE;
    }

    config.WaveSize = (Plan->WaveSize == 64u) ? BC250_WAVE64 : BC250_WAVE32;
    config.NumSgprs = Plan->NumSgprs;
    config.NumVgprs = Plan->NumVgprs;
    config.LdsSizeBytes = 0u;
    config.ScratchSizeBytes = 0u;

    result.InstructionCount = 0u;
    result.HasEndPgm = 0u;
    result.IsValid = 0u;

    /* FONTE UNICA da regra de ISA -- ver o cabecalho. */
    isa = Bc250ValidateComputeShaderIsa(
        CodeDwords, CodeDwordCount, &config, &result);

    if (isa == BC250_ISA_MISSING_ENDPGM) {
        return BC250_GFX_SHADER_NO_ENDPGM;
    }
    if (isa == BC250_ISA_EMPTY_SHADER) {
        return BC250_GFX_SHADER_EMPTY_CODE;
    }
    if (isa == BC250_ISA_EXCEEDS_SGPR_LIMIT ||
        isa == BC250_ISA_EXCEEDS_VGPR_LIMIT ||
        isa == BC250_ISA_EXCEEDS_LDS_LIMIT) {
        return BC250_GFX_SHADER_FIELD_OVERFLOW;
    }
    if (isa != BC250_ISA_OK || result.IsValid == 0u) {
        return BC250_GFX_SHADER_BAD_ARGUMENT;
    }

    return BC250_GFX_SHADER_OK;
}

BC250_GFX_SHADER_STATUS Bc250GfxShaderComputeRegs(
    const BC250_GFX_SHADER_PLAN *Plan,
    BC250_GFX_SHADER_REGS *Regs)
{
    uint32_t vgprField;
    uint32_t sgprField;
    uint32_t userSgprField;
    uint32_t rsrc1;
    uint32_t rsrc2;

    if (Plan == NULL || Regs == NULL) {
        return BC250_GFX_SHADER_BAD_ARGUMENT;
    }
    if (Plan->Stage != (uint32_t)BC250_GFX_SHADER_STAGE_VS &&
        Plan->Stage != (uint32_t)BC250_GFX_SHADER_STAGE_PS) {
        return BC250_GFX_SHADER_BAD_ARGUMENT;
    }
    if (Plan->WaveSize != 32u && Plan->WaveSize != 64u) {
        return BC250_GFX_SHADER_BAD_ARGUMENT;
    }
    if (Plan->CodeBytes == 0u) {
        return BC250_GFX_SHADER_EMPTY_CODE;
    }

    /*
     * VGPR_COMP_CNT so' existe no VS. No PS aqueles dois bits sao outra
     * coisa (bit 25 e' MEM_ORDERED), entao aceitar o campo no PS seria
     * escrever um valor com significado diferente do pedido.
     */
    if (Plan->Stage == (uint32_t)BC250_GFX_SHADER_STAGE_PS &&
        Plan->VgprCompCnt != 0u) {
        return BC250_GFX_SHADER_BAD_ARGUMENT;
    }
    if (Plan->VgprCompCnt > 3u) {
        return BC250_GFX_SHADER_FIELD_OVERFLOW;
    }

    if ((Plan->GpuAddress % (uint64_t)BC250_GFX_SHADER_ADDRESS_ALIGN) != 0u) {
        return BC250_GFX_SHADER_MISALIGNED;
    }
    if ((Plan->GpuAddress >> BC250_GFX_SHADER_VA_BITS) != 0u) {
        return BC250_GFX_SHADER_ADDRESS_OUT_OF_RANGE;
    }

    vgprField = Bc250GfxShaderEncodeVgprField(Plan->NumVgprs, Plan->WaveSize);
    sgprField = Bc250GfxShaderEncodeSgprField(Plan->NumSgprs);
    userSgprField = Plan->UserSgprs;

    if (vgprField > BC250_GFX_RSRC1_VGPRS_FIELD_MAX ||
        sgprField > BC250_GFX_RSRC1_SGPRS_FIELD_MAX ||
        userSgprField > BC250_GFX_RSRC2_USER_SGPR_MAX) {
        return BC250_GFX_SHADER_FIELD_OVERFLOW;
    }

    /*
     * Lei 6. Partimos do manifesto do compilador e SUBSTITUIMOS somente os
     * campos que a regra deste modulo afirma derivar. Tudo o mais -- modo de
     * float, clamp, IEEE, ordenacao de memoria, scratch -- e' escolha do
     * compilador e passa intacto.
     */
    rsrc1 = (Plan->Rsrc1Manifest & ~BC250_GFX_RSRC1_VGPRS_MASK) |
            ((vgprField << BC250_GFX_RSRC1_VGPRS_SHIFT) &
             BC250_GFX_RSRC1_VGPRS_MASK);
    rsrc1 = (rsrc1 & ~BC250_GFX_RSRC1_SGPRS_MASK) |
            ((sgprField << BC250_GFX_RSRC1_SGPRS_SHIFT) &
             BC250_GFX_RSRC1_SGPRS_MASK);

    rsrc2 = (Plan->Rsrc2Manifest & ~BC250_GFX_RSRC2_USER_SGPR_MASK) |
            ((userSgprField << BC250_GFX_RSRC2_USER_SGPR_SHIFT) &
             BC250_GFX_RSRC2_USER_SGPR_MASK);

    Regs->Rsrc1Computed = rsrc1;
    Regs->Rsrc2Computed = rsrc2;

    /*
     * FALHA FECHADA. Se a regra H discordar do compilador, quem esta errado
     * sou eu -- e apontar a SPI para uma alocacao que o codigo nao respeita
     * produz leitura de VGPR alheio, nao mensagem de erro. Recusar offline
     * custa zero; descobrir no silicio custa uma corrida e possivelmente um
     * BugCheck.
     */
    if (rsrc1 != Plan->Rsrc1Manifest || rsrc2 != Plan->Rsrc2Manifest) {
        Regs->PgmLo = 0u;
        Regs->PgmHi = 0u;
        Regs->Rsrc1 = 0u;
        Regs->Rsrc2 = 0u;
        return BC250_GFX_SHADER_MANIFEST_MISMATCH;
    }

    Regs->PgmLo = (uint32_t)(Plan->GpuAddress >> 8);
    Regs->PgmHi = (uint32_t)(Plan->GpuAddress >> 40);
    Regs->Rsrc1 = Plan->Rsrc1Manifest;
    Regs->Rsrc2 = Plan->Rsrc2Manifest;

    return BC250_GFX_SHADER_OK;
}

const char *Bc250GfxShaderStatusName(BC250_GFX_SHADER_STATUS Status)
{
    switch (Status) {
    case BC250_GFX_SHADER_OK:                    return "OK";
    case BC250_GFX_SHADER_BAD_ARGUMENT:          return "BAD_ARGUMENT";
    case BC250_GFX_SHADER_MISALIGNED:            return "MISALIGNED";
    case BC250_GFX_SHADER_ADDRESS_OUT_OF_RANGE:  return "ADDRESS_OUT_OF_RANGE";
    case BC250_GFX_SHADER_FIELD_OVERFLOW:        return "FIELD_OVERFLOW";
    case BC250_GFX_SHADER_MANIFEST_MISMATCH:     return "MANIFEST_MISMATCH";
    case BC250_GFX_SHADER_EMPTY_CODE:            return "EMPTY_CODE";
    case BC250_GFX_SHADER_NO_ENDPGM:             return "NO_ENDPGM";
    default:                                     return "DESCONHECIDO";
    }
}
