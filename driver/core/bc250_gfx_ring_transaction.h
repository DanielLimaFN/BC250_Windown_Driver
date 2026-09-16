#ifndef BC250_GFX_RING_TRANSACTION_H
#define BC250_GFX_RING_TRANSACTION_H

#include "bc250_gfx_ring_contract.h"
#include "bc250_gfx_sh_reg_packet.h"
#include "bc250_pm4_packet_builder.h"
#include "bc250_gfx_viewport_scissor.h"
#include "bc250_gfx_primitive_assembler.h"
#include "bc250_gfx_draw_initiator.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * =========================================================================
 * B.1 -- TRANSACAO FISICA DO ANEL GRAFICO, sob a Lei 5.
 *
 *   [PREFLIGHT] -> [SNAPSHOT] -> [APPLY] -> [READBACK] -> [PROVA] -> [ROLLBACK]
 *
 * O contrato irmao (bc250_gfx_ring_contract.h) e' aritmetica pura. Este aqui
 * toca registrador -- por funcao ponteiro, para que a suite offline exercite
 * a sequencia INTEIRA contra um MMIO falso, sem placa.
 *
 * NADA AQUI FOI VALIDADO EM HARDWARE.
 *
 * -------------------------------------------------------------------------
 * O LIMITE DELIBERADO DESTA ETAPA, E POR QUE ELE EXISTE
 *
 * A Lei 7 permite apenas offsets catalogados em docs/03 com fonte citada. O
 * bloco "CP ring GFX" do catalogo cobre a CONFIGURACAO do anel (base, cntl,
 * writeback, shadow, ponteiros, doorbell). Ele NAO cobre os registradores que
 * o unico prototipo do repositorio usa para LIGAR a engine -- CP_RB0_ACTIVE,
 * CP_MAX_CONTEXT, CP_RB_VMID, CP_RB_WPTR_DELAY e os quatro *_IC_OP_CNTL.
 *
 * Esses nomes EXISTEM em gc_10_1_0_sh_mask.h (conferido), mas o header de
 * OFFSETS (gc_10_1_0_offset.h) nao esta neste repositorio. Inverter a formula
 * do catalogo a partir dos bytes que o prototipo usa seria lavar H em P1: o
 * proprio byte veio de fonte desconhecida.
 *
 * Entao esta transacao CONFIGURA e PROVA a configuracao por readback, e
 * RECUSA POR NOME a partida da engine, devolvendo
 * BC250_GFX_RING_TXN_NOT_STARTABLE com o registrador que falta. Quando o
 * header chegar, basta promover a classe da entrada na tabela.
 *
 * Isso e' fail-closed de proveniencia, nao limitacao de codigo.
 * =========================================================================
 */

#define BC250_GFX_RING_TXN_IDENTITY 0x47523142u /* GR1B */

/* Offsets BAR5 do bloco "CP ring GFX" de docs/03 (classes P1/P0). */
#define BC250_GFX_REG_CP_RB0_RPTR            0x00008700u
#define BC250_GFX_REG_CP_RB_WPTR_POLL_CNTL   0x00008708u
#define BC250_GFX_REG_CP_ME_CNTL             0x000086D8u
#define BC250_GFX_REG_CP_RB0_BASE            0x0000C100u
#define BC250_GFX_REG_CP_RB0_CNTL            0x0000C104u
#define BC250_GFX_REG_CP_RB0_RPTR_ADDR_LO    0x0000C10Cu
#define BC250_GFX_REG_CP_RB0_RPTR_ADDR_HI    0x0000C110u
#define BC250_GFX_REG_CP_RB0_WPTR            0x0000C150u
#define BC250_GFX_REG_CP_RB0_WPTR_HI         0x0000C154u
#define BC250_GFX_REG_CP_RB0_BASE_HI         0x0000C2C4u
#define BC250_GFX_REG_CP_RB_WPTR_POLL_ADDR_LO 0x0000C3ACu
#define BC250_GFX_REG_CP_RB_WPTR_POLL_ADDR_HI 0x0000C3B0u
#define BC250_GFX_REG_CP_RB_DOORBELL_CONTROL 0x0000C3B4u

/*
 * CATALOGADO EM 14/09 -- classe P1.
 *
 * Ate esta data o offset nao tinha fonte e o APPLY parava nele. Com
 * gc_10_1_0_offset.h vendorizado e PROVADO contra nove enderecos que esta
 * placa ja confirmou (tools/import-gc-offset-header.py), mmCP_RB0_ACTIVE =
 * 0x1F40 idx0 -> BAR5 0x0C680. O valor coincide com o que o prototipo usava,
 * mas agora tem procedencia em vez de coincidencia.
 */
#define BC250_GFX_REG_CP_RB0_ACTIVE          0x0000C680u
#define BC250_GFX_REG_CP_MAX_CONTEXT         0x0000C2B8u
#define BC250_GFX_REG_CP_RB_VMID             0x0000C144u
#define BC250_GFX_REG_HDP_MEM_COHERENCY_FLUSH_CNTL 0x00003FC4u
#define BC250_GFX_REG_CP_RB_WPTR_DELAY       0x00008704u

/*
 * DIAGNOSTICO EM TEMPO REAL E TELEMETRIA DE HARDWARE -- classe P1 (docs/03 e gc_10_1_0).
 */
#define BC250_GFX_REG_GRBM_STATUS            0x00008010u
#define BC250_GFX_REG_CP_STAT                0x00008680u
#define BC250_GFX_REG_CP_PFP_INSTR_PNTR      0x00008694u
#define BC250_GFX_REG_CP_ME_INSTR_PNTR       0x00008698u

/*
 * ENDERECOS PROIBIDOS -- o prototipo escreve nestes achando que sao os
 * *_IC_OP_CNTL. Medido contra o header: os reais estao em BASE_IDX 1
 * (0x3E10C/0x3E11C/0x3E12C/0x3E13C). As constantes existem para o portao
 * poder proibi-las por nome.
 */
#define BC250_GFX_REG_WRONG_PFP_IC_OP_CNTL   0x0000C4D0u
#define BC250_GFX_REG_WRONG_ME_IC_OP_CNTL    0x0000C4D4u
#define BC250_GFX_REG_WRONG_CE_IC_OP_CNTL    0x0000C4D8u
#define BC250_GFX_REG_WRONG_CPC_IC_OP_CNTL   0x0000C4DCu

typedef uint32_t (*BC250_GFX_RING_READ32)(
    void *Context, uint32_t ByteOffset, uint32_t *Value);
typedef uint32_t (*BC250_GFX_RING_WRITE32)(
    void *Context, uint32_t ByteOffset, uint32_t Value);
typedef void (*BC250_GFX_RING_STALL)(void *Context, uint32_t Microseconds);

typedef struct BC250_GFX_RING_INTERFACE {
    BC250_GFX_RING_READ32 Read32;
    BC250_GFX_RING_WRITE32 Write32;
    BC250_GFX_RING_STALL Stall;
    void *Context;
    uint32_t Identity;
} BC250_GFX_RING_INTERFACE;

typedef enum BC250_GFX_RING_TXN_STATUS {
    BC250_GFX_RING_TXN_OK = 0,
    BC250_GFX_RING_TXN_BAD_ARGUMENT,
    /* O plano nao passou em Bc250GfxRingValidatePlan. */
    BC250_GFX_RING_TXN_PLAN_REJECTED,
    /* Uma leitura do preflight falhou: nao ha snapshot, nao ha rollback. */
    BC250_GFX_RING_TXN_PREFLIGHT_FAILED,
    /* Uma escrita falhou no meio do APPLY. */
    BC250_GFX_RING_TXN_WRITE_FAILED,
    /* O readback nao devolveu o que foi escrito. */
    BC250_GFX_RING_TXN_READBACK_MISMATCH,
    /* Configuracao aplicada e conferida, mas o anel nao ficou ATIVO no
     * readback de CP_RB0_ACTIVE. */
    BC250_GFX_RING_TXN_NOT_STARTABLE,
    /* O rollback nao restaurou o estado do snapshot. */
    BC250_GFX_RING_TXN_ROLLBACK_FAILED,
    /* Valor de poll herdado do SDMA barrado antes de qualquer escrita. */
    BC250_GFX_RING_TXN_POLL_CNTL_BORROWED,
    /* Anel nao esta ativo para submissao (Apply nao confirmou RB0_ACTIVE). */
    BC250_GFX_RING_TXN_NOT_ACTIVE,
    /* Readback do poison da fence na memoria falhou antes do disparo. */
    BC250_GFX_RING_TXN_FENCE_POISON_FAILED,
    /* Submissao excedeu o limite de polling sem alcancar o tripe da Lei 2. */
    BC250_GFX_RING_TXN_SUBMIT_TIMEOUT,
    /* Tripe da Lei 2 falhou (divergencia de dados ou fence invalida). */
    BC250_GFX_RING_TXN_TRIPOD_FAILED
} BC250_GFX_RING_TXN_STATUS;

typedef struct BC250_GFX_RING_TXN_EVIDENCE {
    /* SNAPSHOT -- o que a placa tinha antes de qualquer escrita nossa. */
    uint32_t CntlBefore;
    uint32_t BaseBefore;
    uint32_t BaseHiBefore;
    uint32_t RptrBefore;
    uint32_t WptrBefore;
    uint32_t PollCntlBefore;
    uint32_t MeCntlBefore;
    uint32_t SnapshotTaken;

    /* APPLY + READBACK. */
    uint32_t CntlPlanned;
    uint32_t CntlReadback;
    uint32_t BaseReadback;
    uint32_t BaseHiReadback;
    uint32_t ApplyReadbackPassed;

    /* Decodificacao do que a placa tinha, para o log dizer algo util. */
    BC250_GFX_RING_CNTL_FIELDS FieldsBefore;
    BC250_GFX_RING_CNTL_FIELDS FieldsPlanned;

    /*
     * Estado das micro-engines. MEDIDO em 14/09: o boot entrega
     * CE_HALT=PFP_HALT=ME_HALT=1. Configurar o anel com elas paradas produz
     * escrita aceita, readback conferindo e ZERO pacote consumido -- o falso
     * sucesso que a Lei 4 proibe. Por isso o estado entra na evidencia, e nao
     * fica implicito.
     */
    BC250_GFX_ME_CNTL_FIELDS MeCntlFields;
    uint32_t MicroEnginesHalted;

    /* Desalt, quando o plano o pede. MeCntlAfter e' o readback REAL. */
    uint32_t MeCntlPlanned;
    uint32_t MeCntlAfter;
    uint32_t UnhaltAttempted;
    uint32_t UnhaltPassed;

    /* Partida do anel. RB0_ACTIVE ganhou procedencia em 14/09. */
    uint32_t RingActivateAttempted;
    uint32_t RingActivateReadback;
    uint32_t RingActivatePassed;

    /* ROLLBACK. */
    uint32_t RollbackAttempted;
    uint32_t RollbackPassed;

    /* Contadores, para o log separar leitura de escrita. */
    uint32_t Reads;
    uint32_t Writes;
} BC250_GFX_RING_TXN_EVIDENCE;

/*
 * Somente leitura. Pode rodar em qualquer corrida sem alterar a placa, e e' a
 * primeira medicao do anel grafico que este projeto teria. Preenche o
 * snapshot e a decodificacao de CP_RB0_CNTL.
 *
 * Plan == NULL faz MEDICAO PURA: fotografa o anel sem propor configuracao.
 * Nesse modo CntlPlanned fica 0 e o APPLY recusa, entao medir nunca escorrega
 * para escrever. Plano PRESENTE e invalido e' recusado ANTES de qualquer
 * leitura -- propor errado e' defeito, nao ausencia de proposta.
 */
BC250_GFX_RING_TXN_STATUS Bc250GfxRingTxnPreflight(
    const BC250_GFX_RING_INTERFACE *Interface,
    const BC250_GFX_RING_PLAN *Plan,
    BC250_GFX_RING_TXN_EVIDENCE *Evidence);

/*
 * Escreve a configuracao do anel e confere por readback. NAO liga a engine:
 * devolve BC250_GFX_RING_TXN_NOT_STARTABLE nomeando o registrador sem
 * proveniencia. Exige que o preflight tenha tirado o snapshot -- sem ele nao
 * ha como voltar, e aplicar sem rota de volta viola a Lei 5.
 */
BC250_GFX_RING_TXN_STATUS Bc250GfxRingTxnApply(
    const BC250_GFX_RING_INTERFACE *Interface,
    const BC250_GFX_RING_PLAN *Plan,
    BC250_GFX_RING_TXN_EVIDENCE *Evidence);

/* Restaura o snapshot e confere por readback. */
BC250_GFX_RING_TXN_STATUS Bc250GfxRingTxnRollback(
    const BC250_GFX_RING_INTERFACE *Interface,
    BC250_GFX_RING_TXN_EVIDENCE *Evidence);

typedef struct BC250_GFX_RING_SUBMIT_SH_REG_PLAN {
    uint32_t *RingCpuAddress;
    uint32_t RingDwords;
    uint32_t *WptrPollCpuAddress;
    volatile const uint32_t *RptrWbCpuAddress;
    volatile uint32_t *FenceCpuAddress;
    uint64_t FenceGpuVa;
    uint32_t FencePoisonValue;
    uint32_t FenceExpectedValue;
    const BC250_GFX_SHADER_REGS *VsRegs;
    const BC250_GFX_SHADER_REGS *PsRegs;
    uint32_t MaxPollIterations;
    uint32_t StallMicroseconds;
} BC250_GFX_RING_SUBMIT_SH_REG_PLAN;

typedef struct BC250_GFX_RING_SUBMIT_SH_REG_EVIDENCE {
    uint32_t InitialWptrDwords;
    uint32_t TargetWptrDwords;
    uint32_t EmittedDwords;
    uint32_t FencePoisonPlanted;
    uint32_t FencePoisonReadback;
    uint32_t FenceObserved;
    uint32_t RptrObservedMmio;
    uint32_t RptrObservedWb;
    BC250_TRIPOD_RPTR_SOURCE RptrSource;
    BC250_TRIPOD_VERDICT TripodVerdict;
    uint32_t PollsCompleted;
    uint32_t SubmitCompleted;
} BC250_GFX_RING_SUBMIT_SH_REG_EVIDENCE;

#define BC250_GFX_PM4_FENCE_WRITE_DATA_DWORDS 5u
#define BC250_GFX_PM4_SUBMIT_MIN_DWORDS \
    (BC250_GFX_PM4_PIPELINE_SHADER_TOTAL_DWORDS + BC250_GFX_PM4_FENCE_WRITE_DATA_DWORDS + 3u)

/*
 * B.2.4 -- Submissao de pacotes SET_SH_REG (VS + PS) e fence WRITE_DATA no anel grafico CP_RB0.
 *
 * Exige que Bc250GfxRingTxnApply tenha ativado o anel (RingActivatePassed == 1u).
 * Planta poison na fence com readback proof antes da emissao, emite os pacotes
 * via Bc250GfxShRegEmitPipeline e Bc250Pm4EmitWriteData (DST_SEL=5 Memory Direct MC),
 * avanca CP_RB0_WPTR, polla RPTR e afere o tripe da Lei 2 (RPTR==WPTR, fence sobre poison).
 */
BC250_GFX_RING_TXN_STATUS Bc250GfxRingTxnSubmitShReg(
    const BC250_GFX_RING_INTERFACE *Interface,
    const BC250_GFX_RING_PLAN *RingPlan,
    const BC250_GFX_RING_TXN_EVIDENCE *ApplyEvidence,
    const BC250_GFX_RING_SUBMIT_SH_REG_PLAN *SubmitPlan,
    BC250_GFX_RING_SUBMIT_SH_REG_EVIDENCE *SubmitEvidence);

#define BC250_GFX_3D_DRAW_SH_DWORDS          12u
#define BC250_GFX_3D_DRAW_COLOR_DWORDS        8u
#define BC250_GFX_3D_DRAW_TARGET_MASK_DWORDS  4u
#define BC250_GFX_3D_DRAW_VPORT_DWORDS       20u
#define BC250_GFX_3D_DRAW_PRIM_DWORDS         8u
#define BC250_GFX_3D_DRAW_INIT_DWORDS         4u
#define BC250_GFX_3D_DRAW_FENCE_DWORDS        8u
#define BC250_GFX_3D_DRAW_STREAM_TOTAL_DWORDS 64u

typedef enum BC250_GFX_3D_ISOLATION_STAGE {
    BC250_GFX_ISOLATION_STAGE_DEFAULT   = 0,
    BC250_GFX_ISOLATION_STAGE_4_1_RT    = 1,
    BC250_GFX_ISOLATION_STAGE_4_2_VPORT = 2,
    BC250_GFX_ISOLATION_STAGE_4_3_PRIM  = 3,
    BC250_GFX_ISOLATION_STAGE_4_4_DRAW  = 4,
    BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT = 5
} BC250_GFX_3D_ISOLATION_STAGE;

typedef struct BC250_GFX_RING_SUBMIT_3D_DRAW_PLAN {
    uint32_t *RingCpuAddress;
    uint32_t RingDwords;
    uint32_t *WptrPollCpuAddress;
    volatile const uint32_t *RptrWbCpuAddress;
    volatile uint32_t *FenceCpuAddress;
    uint64_t FenceGpuVa;
    uint32_t FencePoisonValue;
    uint32_t FenceExpectedValue;
    uint32_t SkipDraw;
    uint32_t IsolationStage;
    const BC250_GFX_SHADER_REGS *VsRegs;
    const BC250_GFX_SHADER_REGS *PsRegs;
    uint64_t ColorTargetBaseGpuVa;
    uint32_t ColorTargetWidth;
    uint32_t ColorTargetHeight;
    uint32_t ColorTargetPitchBytes;
    uint32_t ColorTargetFormatIndex;
    uint32_t ColorTargetCompSwap;
    BC250_GFX_VIEWPORT_DIMS Viewport;
    BC250_GFX_SCISSOR_RECT Scissor;
    uint32_t PrimitiveTopology;
    uint32_t NumInstances;
    uint32_t VertexCount;
    uint32_t DrawInitiator;
    uint32_t MaxPollIterations;
    uint32_t StallMicroseconds;
} BC250_GFX_RING_SUBMIT_3D_DRAW_PLAN;

typedef struct BC250_GFX_RING_SUBMIT_3D_DRAW_EVIDENCE {
    uint32_t InitialWptrDwords;
    uint32_t TargetWptrDwords;
    uint32_t EmittedDwords;
    uint32_t FencePoisonPlanted;
    uint32_t FencePoisonReadback;
    uint32_t FenceObserved;
    uint32_t RptrObservedMmio;
    uint32_t RptrObservedWb;
    BC250_TRIPOD_RPTR_SOURCE RptrSource;
    BC250_TRIPOD_VERDICT TripodVerdict;
    uint32_t PollsCompleted;
    uint32_t SubmitCompleted;
    /* Telemetria de Hardware em Tempo Real */
    uint32_t GrbmStatus;
    uint32_t CpStat;
    uint32_t PfpPntr;
    uint32_t MePntr;
} BC250_GFX_RING_SUBMIT_3D_DRAW_EVIDENCE;

/*
 * B.4 -- Submissao de pipeline completo de renderizacao de triangulo 3D no anel grafico CP_RB0.
 *
 * Emite os 7 blocos estruturais do pipeline grafico GFX10 em uma transacao atomica de 64 dwords:
 *   1. Shaders VS + PS (SET_SH_REG 0x76)               -- 12 dwords
 *   2. Color Target 0 (SET_CONTEXT_REG 0x69)           --  8 dwords
 *   3. Target Mask (SET_CONTEXT_REG 0x69 + NOP pad)    --  4 dwords
 *   4. Viewport & Scissor (SET_CONTEXT_REG 0x69)       -- 20 dwords
 *   5. Primitive Assembler (SET_UCONFIG_REG 0x79)      --  8 dwords
 *   6. Draw Initiator (DRAW_INDEX_AUTO 0x2D + NOP pad) --  4 dwords
 *   7. Fence fisica VRAM (RELEASE_MEM 0x49 EOP 32-bit) --  8 dwords
 * Total = exatamente 64 dwords (256 bytes, estritamente alinhado a 16 bytes).
 *
 * Exige anel ativo (RingActivatePassed == 1u).
 * Injeta poison na fence com readback previo (Lei 2).
 * Dispara CP_RB0_WPTR e polla RPTR + fence sob o tripe estrito da Lei 2.
 */
BC250_GFX_RING_TXN_STATUS Bc250GfxRingTxnSubmit3DDraw(
    const BC250_GFX_RING_INTERFACE *Interface,
    const BC250_GFX_RING_PLAN *RingPlan,
    const BC250_GFX_RING_TXN_EVIDENCE *ApplyEvidence,
    const BC250_GFX_RING_SUBMIT_3D_DRAW_PLAN *SubmitPlan,
    BC250_GFX_RING_SUBMIT_3D_DRAW_EVIDENCE *SubmitEvidence);

/*
 * B.4 -- Verificacao de integridade e conformidade de stream PM4 de 64 dwords.
 */
BC250_GFX_RING_TXN_STATUS Bc250GfxVerify3DDrawStream(
    const uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t CursorDwords,
    const BC250_GFX_RING_SUBMIT_3D_DRAW_PLAN *SubmitPlan);

const char *Bc250GfxRingTxnStatusName(BC250_GFX_RING_TXN_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif /* BC250_GFX_RING_TRANSACTION_H */
