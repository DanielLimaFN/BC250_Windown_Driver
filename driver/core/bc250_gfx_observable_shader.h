#ifndef BC250_GFX_OBSERVABLE_SHADER_H
#define BC250_GFX_OBSERVABLE_SHADER_H

#include "bc250_gfx_shader_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * =========================================================================
 * B.2.2 -- BLOBS E MANIFESTOS DE SHADER GRAFICO OBSERVAVEL (VS / PS)
 *
 * PROVENIENCIA E RIGOR EPISTEMOLOGICO (Lei 1 / Lei 8)
 *
 * Declaracao de Classe: CLASSE H.
 *
 * Motivo: Nao ha compilador ROCm LLVM/Clang com target amdgcn disponivel no
 * ambiente de build local (host Windows sem toolchain ROCm). Os blobs
 * abaixo foram montados artesanalmente com a codificacao canonica de
 * instrucoes AMD RDNA 1.0 (GFX10 / GC 10.1.3), cruzados contra o codigo de
 * montagem de microcodigo do compilador Mesa ACO (aco_assembler.cpp).
 *
 * Qualquer tentativa de declarar estes blobs como P1 sem compilador
 * rastreavel constitui violacao direta da Lei 1.
 *
 * RESSALVA EPISTEMOLOGICA (D2): A conferencia entre este manifesto e a
 * Regra H e' CIRCULAR nesta etapa (Classe H), pois ambos foram escritos
 * pela mesma mao e raciocinio, sem compilador independente no host.
 * Garante consistencia interna do contrato, mas NAO prova hardware.
 *
 * DIVIDA DECLARADA B.2.2/D3: O Vertex Shader atual emite posicao constante
 * (0, 0, 0, 1.0). Isto e' suficiente para validar o binding de pipeline
 * e o SET_SH_REG no anel grafico na B.2.4 sem hang de hardware, mas
 * bloqueia rasterizacao na B.5 (tres vertices identicos formam triangulo de
 * area zero). O VS deve ser estendido na etapa B.3 (geometria) para ler
 * atributos de vertice via VGPR de entrada ou buffers em VRAM.
 *
 * NADA AQUI FOI VALIDADO EM HARDWARE.
 * =========================================================================
 */

#define BC250_GFX_OBSERVABLE_VS_DWORDS 8u
#define BC250_GFX_OBSERVABLE_VS_BYTES  (BC250_GFX_OBSERVABLE_VS_DWORDS * 4u)
#define BC250_GFX_OBSERVABLE_VS_SHA256_HEX \
    "219387cf4964de41ac24153b4669b8d1313ef23ecb63ce07a886db69434b9539"

#define BC250_GFX_OBSERVABLE_PS_DWORDS 9u
#define BC250_GFX_OBSERVABLE_PS_BYTES  (BC250_GFX_OBSERVABLE_PS_DWORDS * 4u)
#define BC250_GFX_OBSERVABLE_PS_SHA256_HEX \
    "f5787965e08da58c5bef2b296dee47dce32de9f62f8b7e93ec0c06a9bb2a16c2"

#define BC250_GFX_OBSERVABLE_PROVENANCE_STR \
    "Hand-assembled minimal gfx1010 ISA (AMD RDNA 1.0 architecture manual, Class H)"

#define BC250_GFX_OBSERVABLE_EPISTEMIC_CLASS "H"

typedef struct BC250_GFX_OBSERVABLE_STAGE_MANIFEST {
    uint32_t Stage;              /* BC250_GFX_SHADER_STAGE_VS ou PS */
    uint32_t TargetGfxMajor;     /* 10 */
    uint32_t TargetGfxMinor;     /* 1  */
    uint32_t TargetGfxStepping;  /* 3  */
    uint32_t WaveSize;           /* 32 */
    uint32_t NumSgprs;           /* 8  */
    uint32_t NumVgprs;           /* 4  */
    uint32_t UserSgprs;          /* 0  */
    uint32_t VgprCompCnt;        /* 0  */
    uint32_t FloatMode;          /* 0xF0 */
    uint32_t PgmRsrc1;           /* 0x00AF0000u */
    uint32_t PgmRsrc2;           /* 0x00000000u */
    uint32_t ShaderDwordCount;
    const uint32_t *ShaderDwords;
    const char *Sha256Hex;
    const char *EpistemicClass;
    const char *CompilerProvenance;
} BC250_GFX_OBSERVABLE_STAGE_MANIFEST;

const BC250_GFX_OBSERVABLE_STAGE_MANIFEST *Bc250GetGfxObservableVsManifest(void);
const BC250_GFX_OBSERVABLE_STAGE_MANIFEST *Bc250GetGfxObservablePsManifest(void);

int Bc250ValidateGfxObservableBlob(
    uint32_t Stage,
    const uint32_t *ShaderDwords,
    uint32_t ShaderDwordCount);

int Bc250ValidateGfxObservableManifest(
    const BC250_GFX_OBSERVABLE_STAGE_MANIFEST *Manifest);

/*
 * Popula o plano a partir do manifesto observavel.
 * Devolve 1 em sucesso. Em caso de erro (argumentos nulos ou manifesto
 * invalido), zera o plano e devolve 0 (falha fechada).
 */
int Bc250PopulateGfxShaderPlanFromObservable(
    const BC250_GFX_OBSERVABLE_STAGE_MANIFEST *Manifest,
    uint64_t GpuAddress,
    BC250_GFX_SHADER_PLAN *Plan);

#ifdef __cplusplus
}
#endif

#endif /* BC250_GFX_OBSERVABLE_SHADER_H */
