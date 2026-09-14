#ifndef BC250_GFX_SHADER_CONTRACT_H
#define BC250_GFX_SHADER_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#include "bc250_isa_validator.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * =========================================================================
 * B.2 -- PROGRAMA DE SHADER GRAFICO (SPI_SHADER_PGM_*). Parede B, etapa 2.
 *
 * POR QUE ESTE MODULO EXISTE
 *
 * A B.1 provou no silicio que o anel grafico CP_RB0 liga e executa PM4
 * (corrida 14/09 13:54: RPTR==WPTR==31, fence 0xBC250123 sobre poison,
 * Mismatches=0). O anel executa, mas nao ha NENHUM programa de shader
 * carregado: os estagios VS e PS da placa nunca foram apontados para
 * codigo algum. Sem isso, DRAW_INDEX_AUTO nao tem o que rodar.
 *
 * Este contrato e' a parte PURA dessa etapa: codificacao dos campos de
 * SPI_SHADER_PGM_LO/HI/RSRC1/RSRC2, validacao do plano e montagem da
 * sequencia de SET_SH_REG. Ele nao toca em MMIO e nao aloca memoria.
 *
 * NADA AQUI FOI VALIDADO EM HARDWARE.
 *
 * -------------------------------------------------------------------------
 * PROVENIENCIA (Lei 7 / Lei 8)
 *
 * Enderecos -- inc/linux_ref/gc_10_1_0_offset.h, vendorizado e conferido
 * contra 9 ancoras confirmadas no silicio (docs/03). Classe P1:
 *
 *   SPI_SHADER_PGM_LO_PS    mm=0x19A8 BASE_IDX=0  BAR5 0x0B020  SH dw 0x008
 *   SPI_SHADER_PGM_HI_PS    mm=0x19A9 BASE_IDX=0  BAR5 0x0B024  SH dw 0x009
 *   SPI_SHADER_PGM_RSRC1_PS mm=0x19AA BASE_IDX=0  BAR5 0x0B028  SH dw 0x00A
 *   SPI_SHADER_PGM_RSRC2_PS mm=0x19AB BASE_IDX=0  BAR5 0x0B02C  SH dw 0x00B
 *   SPI_SHADER_USER_DATA_PS_0 mm=0x19AC BASE_IDX=0 BAR5 0x0B030 SH dw 0x00C
 *   SPI_SHADER_PGM_LO_VS    mm=0x19E8 BASE_IDX=0  BAR5 0x0B120  SH dw 0x048
 *   SPI_SHADER_PGM_HI_VS    mm=0x19E9 BASE_IDX=0  BAR5 0x0B124  SH dw 0x049
 *   SPI_SHADER_PGM_RSRC1_VS mm=0x19EA BASE_IDX=0  BAR5 0x0B128  SH dw 0x04A
 *   SPI_SHADER_PGM_RSRC2_VS mm=0x19EB BASE_IDX=0  BAR5 0x0B12C  SH dw 0x04B
 *   SPI_SHADER_USER_DATA_VS_0 mm=0x19EC BASE_IDX=0 BAR5 0x0B130 SH dw 0x04C
 *
 * Conferencia da conta: (0x1260 + 0x19A8) - 0x2C00 = 0x008, e
 * (0x1260 + 0x19A8) * 4 = 0x0B020, que e' o byte catalogado. Fecha nos dois
 * sentidos.
 *
 * Campos -- inc/linux_ref/gc_10_1_0_sh_mask.h. Classe P1:
 *   SPI_SHADER_PGM_RSRC1_{PS,VS}__VGPRS_MASK       0x0000003F
 *   SPI_SHADER_PGM_RSRC1_{PS,VS}__SGPRS_MASK       0x000003C0
 *   SPI_SHADER_PGM_RSRC1_{PS,VS}__FLOAT_MODE_MASK  0x000FF000
 *   SPI_SHADER_PGM_RSRC1_{PS,VS}__DX10_CLAMP_MASK  0x00200000
 *   SPI_SHADER_PGM_RSRC1_{PS,VS}__IEEE_MODE_MASK   0x00800000
 *   SPI_SHADER_PGM_RSRC1_VS__VGPR_COMP_CNT_MASK    0x03000000
 *   SPI_SHADER_PGM_RSRC1_PS__MEM_ORDERED_MASK      0x02000000
 *   SPI_SHADER_PGM_RSRC1_VS__MEM_ORDERED_MASK      0x08000000
 *   SPI_SHADER_PGM_RSRC2_{PS,VS}__SCRATCH_EN_MASK  0x00000001
 *   SPI_SHADER_PGM_RSRC2_{PS,VS}__USER_SGPR_MASK   0x0000003E
 *
 * ATENCAO -- MEM_ORDERED fica em bit DIFERENTE em PS (25) e VS (27). Nao ha
 * campo comum; quem generalizar os dois estagios numa unica formula erra.
 *
 * Divisao do endereco -- LO recebe addr[39:8] e HI recebe addr[47:40], isto
 * e', >>8 e >>40. Derivado de SPI_SHADER_PGM_HI_*__MEM_BASE_MASK ser de 8
 * bits (0xFF): se LO fosse o endereco cru, o campo enderecaria so' 40 bits,
 * e a VA desta geracao e' de 48. A MESMA divisao ja executou no silicio no
 * caminho de compute (bc250_compute_dispatch_contract.c:77), onde o shader
 * observavel rodou e gravou a fence. Classe P1, com precedente P0.
 *
 * -------------------------------------------------------------------------
 * O QUE NAO E' P1 E ESTA DECLARADO COMO H
 *
 * A granularidade de alocacao de VGPR/SGPR -- quantos registradores cada
 * unidade do campo representa -- NAO esta no sh_mask.h: o header da a
 * POSICAO do campo, nao a escala. A regra usada aqui (VGPR em blocos de 8
 * em wave32 e 4 em wave64; SGPR em blocos de 8) e' classe H.
 *
 * POR ISSO ELA NAO ESTA NO CAMINHO CRITICO. O valor que vai para o
 * registrador e' o do MANIFESTO -- produzido por compilador real, como o
 * blob de compute de bc250_observable_shader.c ja e' --, e o valor
 * calculado aqui serve para CONFERIR o manifesto. Divergencia falha
 * fechado. Assim a regra H e' FALSIFICAVEL em vez de assumida: se o
 * compilador discordar, o build reprova e quem esta errado sou eu, nao o
 * silicio.
 * =========================================================================
 */

#define BC250_GFX_SH_REG_BASE_DW 0x2C00u

/* Deslocamentos relativos a BC250_GFX_SH_REG_BASE_DW (ver proveniencia). */
#define BC250_GFX_SH_PGM_LO_PS      0x008u
#define BC250_GFX_SH_PGM_HI_PS      0x009u
#define BC250_GFX_SH_PGM_RSRC1_PS   0x00Au
#define BC250_GFX_SH_PGM_RSRC2_PS   0x00Bu
#define BC250_GFX_SH_USER_DATA_PS_0 0x00Cu
#define BC250_GFX_SH_PGM_LO_VS      0x048u
#define BC250_GFX_SH_PGM_HI_VS      0x049u
#define BC250_GFX_SH_PGM_RSRC1_VS   0x04Au
#define BC250_GFX_SH_PGM_RSRC2_VS   0x04Bu
#define BC250_GFX_SH_USER_DATA_VS_0 0x04Cu

/* Mascaras do sh_mask.h, replicadas com o nome de origem no comentario. */
#define BC250_GFX_RSRC1_VGPRS_MASK      0x0000003Fu
#define BC250_GFX_RSRC1_SGPRS_MASK      0x000003C0u
#define BC250_GFX_RSRC1_FLOAT_MODE_MASK 0x000FF000u
#define BC250_GFX_RSRC1_DX10_CLAMP_MASK 0x00200000u
#define BC250_GFX_RSRC1_IEEE_MODE_MASK  0x00800000u
#define BC250_GFX_RSRC1_VS_VGPR_COMP_CNT_MASK 0x03000000u
#define BC250_GFX_RSRC1_PS_MEM_ORDERED_MASK   0x02000000u
#define BC250_GFX_RSRC1_VS_MEM_ORDERED_MASK   0x08000000u
#define BC250_GFX_RSRC2_SCRATCH_EN_MASK 0x00000001u
#define BC250_GFX_RSRC2_USER_SGPR_MASK  0x0000003Eu

#define BC250_GFX_RSRC1_VGPRS_SHIFT      0u
#define BC250_GFX_RSRC1_SGPRS_SHIFT      6u
#define BC250_GFX_RSRC1_FLOAT_MODE_SHIFT 12u
#define BC250_GFX_RSRC1_VS_VGPR_COMP_CNT_SHIFT 24u
#define BC250_GFX_RSRC2_USER_SGPR_SHIFT  1u

/*
 * O codigo de shader e' endereçado em unidades de 256 bytes (ver divisao
 * >>8 acima): endereco nao alinhado perde os 8 bits baixos SILENCIOSAMENTE.
 * O contrato recusa em vez de truncar.
 */
#define BC250_GFX_SHADER_ADDRESS_ALIGN 256u

/* Limite superior dos campos de 6 e 4 bits, conferido antes de deslocar. */
#define BC250_GFX_RSRC1_VGPRS_FIELD_MAX 0x3Fu
#define BC250_GFX_RSRC1_SGPRS_FIELD_MAX 0x0Fu
#define BC250_GFX_RSRC2_USER_SGPR_MAX   0x1Fu

typedef enum BC250_GFX_SHADER_STAGE {
    BC250_GFX_SHADER_STAGE_VS = 0,
    BC250_GFX_SHADER_STAGE_PS = 1
} BC250_GFX_SHADER_STAGE;

typedef enum BC250_GFX_SHADER_STATUS {
    BC250_GFX_SHADER_OK = 0,
    BC250_GFX_SHADER_BAD_ARGUMENT,
    BC250_GFX_SHADER_MISALIGNED,
    BC250_GFX_SHADER_ADDRESS_OUT_OF_RANGE,
    BC250_GFX_SHADER_FIELD_OVERFLOW,
    BC250_GFX_SHADER_MANIFEST_MISMATCH,
    BC250_GFX_SHADER_EMPTY_CODE,
    BC250_GFX_SHADER_NO_ENDPGM
} BC250_GFX_SHADER_STATUS;

/*
 * Plano de um estagio. NumVgprs/NumSgprs sao contagens BRUTAS de
 * registradores usados pelo shader; a codificacao em campo e' derivada.
 * Rsrc1Manifest/Rsrc2Manifest sao os valores que o COMPILADOR declarou --
 * eles e' que vao para o silicio.
 */
typedef struct BC250_GFX_SHADER_PLAN {
    uint32_t Stage;          /* BC250_GFX_SHADER_STAGE_*            */
    uint64_t GpuAddress;     /* VA do blob, multiplo de 256         */
    uint32_t CodeBytes;
    uint32_t NumVgprs;
    uint32_t NumSgprs;
    uint32_t UserSgprs;
    uint32_t FloatMode;
    uint32_t WaveSize;       /* 32 ou 64                            */
    uint32_t VgprCompCnt;    /* somente VS: 0..3                    */
    uint32_t Rsrc1Manifest;
    uint32_t Rsrc2Manifest;
} BC250_GFX_SHADER_PLAN;

typedef struct BC250_GFX_SHADER_REGS {
    uint32_t PgmLo;
    uint32_t PgmHi;
    uint32_t Rsrc1;
    uint32_t Rsrc2;
    uint32_t Rsrc1Computed;  /* o que a regra H produziu, para o log */
    uint32_t Rsrc2Computed;
} BC250_GFX_SHADER_REGS;

/*
 * REGRA H -- granularidade de alocacao. Ver o bloco de proveniencia.
 * Wave32 aloca VGPR em blocos de 8; wave64 em blocos de 4. SGPR em 8.
 */
#define BC250_GFX_VGPR_GRANULE_WAVE32 8u
#define BC250_GFX_VGPR_GRANULE_WAVE64 4u
#define BC250_GFX_SGPR_GRANULE        8u

uint32_t Bc250GfxShaderEncodeVgprField(uint32_t NumVgprs, uint32_t WaveSize);
uint32_t Bc250GfxShaderEncodeSgprField(uint32_t NumSgprs);

/*
 * Valida o plano e devolve os quatro registradores. Os campos Rsrc1/Rsrc2
 * devolvidos sao os do MANIFESTO; Rsrc1Computed/Rsrc2Computed trazem o que
 * a regra H calculou. Se divergirem, devolve MANIFEST_MISMATCH e NAO
 * preenche nada -- falha fechada, porque publicar um RSRC que o compilador
 * nao reconhece e' apontar a SPI para uma alocacao que o codigo nao
 * respeita.
 */
BC250_GFX_SHADER_STATUS Bc250GfxShaderComputeRegs(
    const BC250_GFX_SHADER_PLAN *Plan,
    BC250_GFX_SHADER_REGS *Regs);

/*
 * Confere que o blob termina em s_endpgm e respeita os tetos de SGPR/VGPR.
 * Um shader sem s_endpgm nao devolve a wave, e a consequencia no silicio e'
 * hang de engine grafica -- caminho que hoje custa BugCheck 0x116/0x119.
 * Recusar offline e' barato.
 *
 * FONTE UNICA: delega a Bc250ValidateComputeShaderIsa (bc250_isa_validator.h).
 * A checagem -- s_endpgm no fim, alinhamento e tetos de registrador -- e' de
 * ISA, nao de estagio: o mesmo gfx1010 executa VS, PS e CS. Duplicar a
 * constante S_ENDPGM aqui criaria uma SEGUNDA verdade, que e' exatamente a
 * classe de defeito que ja custou A13, B4 e ContextAbi neste projeto.
 */
BC250_GFX_SHADER_STATUS Bc250GfxShaderValidateBlob(
    const uint32_t *CodeDwords,
    uint32_t CodeDwordCount,
    const BC250_GFX_SHADER_PLAN *Plan);

/*
 * Deslocamento do primeiro registrador do estagio, relativo ao SH_REG_BASE.
 * Os quatro registradores de cada estagio sao CONTIGUOS (LO, HI, RSRC1,
 * RSRC2), o que permite um unico SET_SH_REG de 4 valores por estagio.
 */
BC250_GFX_SHADER_STATUS Bc250GfxShaderStageRegOffset(
    uint32_t Stage,
    uint32_t *RegOffsetDw);

const char *Bc250GfxShaderStatusName(BC250_GFX_SHADER_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif /* BC250_GFX_SHADER_CONTRACT_H */
