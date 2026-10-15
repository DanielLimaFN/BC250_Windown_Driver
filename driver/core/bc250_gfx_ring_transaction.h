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
/* HDP.1 (28/09, doc 519): 0x3FC4 e' a INVALIDACAO de leitura do HDP, nao o
 * flush de escrita; o nome antigo (..._HDP_MEM_COHERENCY_FLUSH_CNTL) era falso.
 * Os dois usos sao lacos de espera em que a CPU LE a fence: invalidar e' o
 * certo ali, entao so' o nome mudou. */
#define BC250_GFX_REG_HDP_READ_CACHE_INVALIDATE 0x00003FC4u
#define BC250_GFX_REG_CP_RB_WPTR_DELAY       0x00008704u

/*
 * RLC_SAFE_MODE -- mmRLC_SAFE_MODE 0x4C05 idx1 -> (0xA000 + 0x4C05) * 4 = 0x3B014 (docs/03:1455).
 * Permite isolar o RLC antes de emitir soft-reset no CP ou reconfigurar aneis, evitando colisao no crossbar
 * e falha UTCL1 XNACK no SDMA.
 */
#define BC250_REG_RLC_SAFE_MODE              0x0003B014u
#define BC250_RLC_SAFE_MODE_CMD_MASK         0x00000001u
#define BC250_RLC_SAFE_MODE_MESSAGE_SHIFT    1u
#define BC250_RLC_SAFE_MODE_ENTER_CMD        0x00000003u /* CMD=1, MESSAGE=1 */
#define BC250_RLC_SAFE_MODE_EXIT_CMD         0x00000001u /* CMD=1, MESSAGE=0 */

/*
 * I.RLC / I.IB1 (doc 630) -- somente leitura. Procedencia P1:
 * inc/linux_ref/gc_10_1_0_offset.h, todos BASE_IDX 1 -> (0xA000 + reg) * 4.
 *   mmRLC_CNTL       0x4C00 -> 0x3B000   (RLC_CNTL__RLC_ENABLE_F32_MASK = 0x1, sh_mask)
 *   mmRLC_STAT       0x4C04 -> 0x3B010
 *   mmCP_IB2_BASE_LO 0x20CF -> 0x3033C, _HI 0x20D0 -> 0x30340, mmCP_IB2_BUFSZ 0x20D1 -> 0x30344
 *   (o CP_IB1_* ja' e' lido no [TDR_GFX_PACKET]; defines mais abaixo neste arquivo)
 * Motivo: o RLC nunca confirmou safe mode (0 de 41) e o pfp_direct_load
 * devolve RLC_CNTL ao valor de antes (0). A RADV encadeia IB2 dentro do IB1.
 */
#define BC250_REG_RLC_CNTL                   0x0003B000u
#define BC250_RLC_CNTL_ENABLE_F32_MASK       0x00000001u
#define BC250_REG_RLC_STAT                   0x0003B010u
#define BC250_GFX_REG_CP_IB2_BASE_LO         0x0003033Cu
#define BC250_GFX_REG_CP_IB2_BASE_HI         0x00030340u
#define BC250_GFX_REG_CP_IB2_BUFSZ           0x00030344u

/*
 * RLC.1 e GFXRST.1 (doc 642) -- gfx_v10_0_rlc_resume e gfx_v10_0_soft_reset.
 * Procedencia P1: inc/linux_ref/gc_10_1_0_offset.h e gc_10_1_0_sh_mask.h.
 * BASE_IDX 1 -> (0xA000 + reg) * 4; BASE_IDX 0 -> (0x1260 + reg) * 4.
 *   mmRLC_PG_CNTL        0x4C43 idx1 -> 0x3B10C
 *   mmRLC_CGCG_CGLS_CTRL 0x4C49 idx1 -> 0x3B124
 *   mmRLC_SPM_MC_CNTL    0x4C71 idx1 -> 0x3B1C4 (RLC_SPM_VMID_MASK 0xF)
 *   mmRLC_GPM_STAT       0x4E6E idx1 -> 0x3B9B8 (RLC_EXEC_ROM_CODE_MASK 0x00400000)
 *   mmGRBM_STATUS2       0x0DA2 idx0 -> 0x08008 (RLC_BUSY_MASK 0x01000000)
 *   mmCP_MEC_CNTL        0x0E2D idx0 -> 0x08234 (MEC_ME1_HALT 0x40000000, MEC_ME2_HALT 0x10000000)
 *   CP_ME_CNTL: ME_HALT 0x10000000, PFP_HALT 0x04000000, CE_HALT 0x01000000
 *   GRBM_SOFT_RESET: SOFT_RESET_RLC 0x00000004
 *   GRBM_STATUS: PA 0x02000000 SC 0x01000000 BCI 0x00800000 SX 0x00100000
 *     TA 0x00004000 DB 0x04000000 CB 0x40000000 GDS 0x00008000
 *     SPI 0x00400000 GE_BUSY_NO_DMA 0x00010000; CP 0x20000000 CP_COHERENCY 0x10000000
 * RLC_PG_CNTL bit 23 (0x800000): handshake RLC/SMU, que o rlc_start limpa
 * quando amdgpu_pp_feature_mask tem GFXOFF (default 0xfff7bfff).
 */
#define BC250_REG_RLC_PG_CNTL                0x0003B10Cu
#define BC250_REG_RLC_CGCG_CGLS_CTRL         0x0003B124u
#define BC250_REG_RLC_SPM_MC_CNTL            0x0003B1C4u
#define BC250_RLC_SPM_MC_CNTL_VMID_MASK      0x0000000Fu
#define BC250_REG_RLC_GPM_STAT               0x0003B9B8u
#define BC250_RLC_GPM_STAT_EXEC_ROM_MASK     0x00400000u
#define BC250_RLC_PG_CNTL_SMU_HANDSHAKE_OFF  0x00800000u
#define BC250_GFX_REG_CP_MEC_CNTL            0x00008234u
#define BC250_CP_MEC_CNTL_ME1_HALT           0x40000000u
#define BC250_CP_MEC_CNTL_ME2_HALT           0x10000000u
#define BC250_CP_ME_CNTL_ME_HALT             0x10000000u
#define BC250_CP_ME_CNTL_PFP_HALT            0x04000000u
#define BC250_CP_ME_CNTL_CE_HALT             0x01000000u
#define BC250_GRBM_SOFT_RESET_RLC_MASK       0x00000004u
#define BC250_GRBM_STATUS2_RLC_BUSY          0x01000000u
#define BC250_GRBM_STATUS_GFX_PIPE_BUSY      (0x02000000u | 0x01000000u | \
    0x00800000u | 0x00100000u | 0x00004000u | 0x04000000u | 0x40000000u | \
    0x00008000u | 0x00400000u | 0x00010000u)
#define BC250_GRBM_STATUS_CP_BUSY_ANY        (0x20000000u | 0x10000000u)
/*
 * C05 (doc 764): gfx_v10_0_ring_soft_recovery (inc/linux_ref/gfx_v10_0.c:8753).
 * mmSQ_CMD = 0x111B, BASE_IDX 0 -> (0x1260 + 0x111B) * 4 = 0x8DEC (regcalc do
 * Nagi confere). Campos de gc_10_1_0_sh_mask.h: CMD [3:0], MODE [6:4],
 * CHECK_VMID [7], VM_ID [31:28]. CMD 3 = mata a onda, MODE 1 = broadcast.
 */
#define BC250_GFX_REG_SQ_CMD                 0x00008DECu
#define BC250_SQ_CMD_KILL_BROADCAST          (0x3u | (0x1u << 4))
#define BC250_SQ_CMD_CHECK_VMID              0x00000080u
#define BC250_SQ_CMD_VM_ID_SHIFT             28u

/*
 * DIAGNOSTICO EM TEMPO REAL E TELEMETRIA DE HARDWARE -- classe P1 (docs/03 e gc_10_1_0).
 */
#define BC250_GFX_REG_GRBM_STATUS            0x00008010u
#define BC250_GFX_REG_CP_STAT                0x00008680u
#define BC250_GFX_REG_CP_PFP_INSTR_PNTR      0x00008694u
#define BC250_GFX_REG_CP_ME_INSTR_PNTR       0x00008698u

/*
 * =========================================================================
 * 19/09 -- DIAGNOSTICO DO PONTO DE PARADA DO CP GRAFICO. SOMENTE LEITURA.
 * =========================================================================
 * Por que estes offsets entram agora: a corrida 2026-09-19 09:06:34 parou o
 * anel grafico com GRBM_STATUS=0xA2613028 (GUI_ACTIVE, CP_BUSY, GE_BUSY,
 * SPI_BUSY, PA_BUSY, CB_CLEAN=1, DB_CLEAN=1) e o quiesce nao tinha como
 * dizer EM QUE PACOTE o ME parou nem se havia onda viva. O handoff daquela
 * corrida listou tres causas possiveis; nenhuma era decidivel pelos
 * registradores que liamos.
 *
 * Proveniencia (Lei 7): inc/linux_ref/gc_10_1_0_offset.h, convertido pela
 * regra de docs/03 -- BAR5 = (SEG[BASE_IDX] + mmREG) * 4, com
 * SEG[0]=0x1260 e SEG[1]=0xA000, a mesma tabela de
 * tools/import-gc-offset-header.py. As tres ancoras do importador
 * (mmCP_ME_CNTL->0x086D8, mmGRBM_STATUS->0x08010, mmCP_RB0_ACTIVE->0x0C680)
 * reproduzem, entao a conversao esta conferida nesta placa.
 *
 *   mmCP_ME_HEADER_DUMP    0x0F41 idx0 -> 0x08684   ultimo header PM4 do ME
 *   mmCP_PFP_HEADER_DUMP   0x0F42 idx0 -> 0x08688   ultimo header PM4 do PFP
 *   mmGRBM_STATUS_SE0      0x0DA5 idx0 -> 0x08014   busy por shader engine
 *   mmGRBM_STATUS_SE1      0x0DA6 idx0 -> 0x08018
 *   mmSQ_DEBUG_STS_GLOBAL  0x10A9 idx0 -> 0x08C24   ondas em voo (WAVE_LEVEL)
 *   mmCP_IB1_BASE_LO       0x20CC idx1 -> 0x30330   IB que o CP esta lendo
 *   mmCP_IB1_BASE_HI       0x20CD idx1 -> 0x30334
 *   mmCP_IB1_BUFSZ         0x20CE idx1 -> 0x30338   quanto do IB resta
 *
 * GRBM_STATUS2 (0x08008) e CP_BUSY_STAT (0x0867C) ja existiam na allowlist
 * de leitura; entram aqui so' para o quiesce poder nomea-los.
 *
 * NENHUM destes e' escrito em lugar nenhum, e o portao
 * audit_gfx_stall_probe_is_read_only existe para manter assim.
 * NAO VALIDADO EM HARDWARE.
 */
#define BC250_GFX_REG_GRBM_STATUS2           0x00008008u
#define BC250_GFX_REG_GRBM_STATUS_SE0        0x00008014u
#define BC250_GFX_REG_GRBM_STATUS_SE1        0x00008018u
#define BC250_GFX_REG_CP_BUSY_STAT           0x0000867Cu
#define BC250_GFX_REG_CP_ME_HEADER_DUMP      0x00008684u
#define BC250_GFX_REG_CP_PFP_HEADER_DUMP     0x00008688u
#define BC250_GFX_REG_SQ_DEBUG_STS_GLOBAL    0x00008C24u
/* DX.32: mmCOMPUTE_VMID 0x1BB4 BASE_IDX 0 -> (0x1260+0x1BB4)*4; SOMENTE LEITURA. */
#define BC250_GFX_REG_COMPUTE_VMID           0x0000B850u
/* DX.33: mmCP_ME0_PIPE0_VMID 0x1DF2 BASE_IDX 0 -> (0x1260+0x1DF2)*4; campo VMID[3:0]; SOMENTE LEITURA. */
#define BC250_GFX_REG_CP_ME0_PIPE0_VMID      0x0000C148u
#define BC250_GFX_REG_CP_IB1_BASE_LO         0x00030330u
#define BC250_GFX_REG_CP_IB1_BASE_HI         0x00030334u
#define BC250_GFX_REG_CP_IB1_BUFSZ           0x00030338u

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

    /*
     * D0.4 -- CP_RB_WPTR_POLL_CNTL (0x08708), o canal que nunca foi escrito.
     *
     * Ate 18/09 este registrador era apenas LIDO no preflight (PollCntlBefore)
     * e submetido a Bc250GfxRingPollCntlIsBorrowed. A corrida 14:20:07 mostrou
     * por que isso importa: com a doorbell escrita 0 pelo APPLY e o MMIO de
     * CP_RB0_WPTR nao retendo o valor, nenhum canal de publicacao de WPTR
     * estava operante.
     *
     * AVISO QUE TEM DE VIAJAR COM O CAMPO: pelo header upstream este registrador
     * NAO TEM BIT DE ENABLE -- so' POLL_FREQUENCY[15:0] e IDLE_POLL_COUNT[31:16]
     * (gc_10_1_0_sh_mask.h). Programa-lo CONFIGURA a cadencia do poll; nao
     * "liga" o mecanismo. Ausencia de efeito na proxima corrida NAO refuta o
     * canal de doorbell, que e' o mecanismo provado nesta placa no lado do
     * compute.
     */
    uint32_t PollCntlPlanned;
    uint32_t PollCntlReadback;
    uint32_t PollCntlApplyAttempted;
    uint32_t PollCntlApplyPassed;

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

/*
 * =========================================================================
 * SONDA DE ONDA -- 19/09, depois da Corrida 35. LEITURA INDIRETA VIA SQ.
 * =========================================================================
 * A Corrida 35 mediu o que faltava e fechou o Desfecho 1 do docs/289:
 *   [TDR_GFX_WAVES] SQ_DEBUG_STS_GLOBAL=0x00030031 Busy=1
 *                   WaveLevelSA0=3 WaveLevelSA1=3
 * Seis ondas em voo que NAO retiram, com CB_CLEAN=1 nos dois shader engines
 * e CP_BUSY_STAT sem SemPolling, sem EopDoneBusy e sem CoherCntNeqZero.
 *
 * O contador diz QUANTAS ondas estao presas. Ele nao diz ONDE. Quem diz e' o
 * PC da onda, e o PC so' se alcanca pelo caminho INDIRETO do SQ:
 *
 *   1. GRBM_GFX_INDEX seleciona SE / SA / instancia (broadcast DESLIGADO)
 *   2. SQ_IND_INDEX recebe (ixSQ_WAVE_* << 16) | WAVE_ID
 *   3. SQ_IND_DATA devolve o registrador daquela onda
 *
 * Proveniencia (Lei 7), inc/linux_ref/gc_10_1_0_offset.h pela regra de docs/03
 * (BAR5 = (SEG[BASE_IDX] + mmREG) * 4, SEG[0]=0x1260, SEG[1]=0xA000):
 *   mmSQ_IND_INDEX    0x1118 idx0 -> 0x08DE0
 *   mmSQ_IND_DATA     0x1119 idx0 -> 0x08DE4
 *   mmGRBM_GFX_INDEX  0x2200 idx1 -> 0x30800   (ja em uso pelo caminho MEC)
 * Campos de SQ_IND_INDEX, gc_10_1_0_sh_mask.h:8112-8115:
 *   WAVE_ID 0x0000001F | WORKITEM_ID 0x000007E0 | AUTO_INCR 0x00000800
 *   INDEX   0xFFFF0000
 * Campos de GRBM_GFX_INDEX, mesmo header:27820-27825.
 *
 * ---------------------------------------------------------------------
 * POR QUE ESTA SONDA E' A UNICA COISA DESTA FAIXA QUE ESCREVE REGISTRADOR
 * ---------------------------------------------------------------------
 * Ela NAO e' somente leitura: selecionar onda exige escrever GRBM_GFX_INDEX e
 * SQ_IND_INDEX. Isso a separa da sonda de parada do CP (que le e pronto) e
 * traz dois riscos reais, tratados por construcao:
 *
 *   (a) GRBM_GFX_INDEX deixado fora de broadcast redireciona TODO acesso
 *       posterior a um unico SE -- inclusive o soft-reset que vem logo
 *       depois no quiesce. A transacao restaura broadcast SEMPRE, com
 *       readback, e declara em campo proprio se a restauracao falhou.
 *   (b) laco longo em DISPATCH_LEVEL foi a causa do 0x133 em 10/09 12:16.
 *       Por isso o orcamento de leituras e' FIXO e pequeno, conferido pelo
 *       contrato antes de qualquer escrita, e a varredura para assim que
 *       encontra ondas suficientes.
 *
 * Fail-closed em tres camadas: tunable GfxWaveProbe (default 0), exigencia de
 * que o SQ tenha reportado onda em voo, e orcamento que o plano nao pode
 * exceder. NAO VALIDADO EM HARDWARE.
 */
#define BC250_GFX_REG_SQ_IND_INDEX           0x00008DE0u
#define BC250_GFX_REG_SQ_IND_DATA            0x00008DE4u
#define BC250_GFX_REG_GRBM_GFX_INDEX         0x00030800u

/* Broadcast em SE, SA e instancia -- o estado normal do seletor. */
#define BC250_GRBM_GFX_INDEX_BROADCAST_ALL   0xE0000000u

/* Enderecos indiretos ixSQ_WAVE_*, gc_10_1_0_offset.h:11188-11199. */
#define BC250_IX_SQ_WAVE_STATUS              0x0102u
#define BC250_IX_SQ_WAVE_TRAPSTS             0x0103u
#define BC250_IX_SQ_WAVE_IB_STS              0x0107u
#define BC250_IX_SQ_WAVE_PC_LO               0x0108u
#define BC250_IX_SQ_WAVE_PC_HI               0x0109u
#define BC250_IX_SQ_WAVE_INST_DW0            0x010Au
/* DX.34: ixSQ_WAVE_HW_ID2 0x0118 (gc_10_1_0_offset.h:11201); VM_ID em 27:24
 * (SQ_WAVE_HW_ID2__VM_ID_MASK 0x0F000000, sh_mask.h:42910) -- o VMID DA ONDA. */
#define BC250_IX_SQ_WAVE_HW_ID2              0x0118u
#define BC250_WAVE_HW_ID2_VM_ID(v)           (((v) >> 24) & 0xFu)
#define BC250_WAVE_HW_ID2_ME_ID(v)           (((v) >> 8) & 0x3u)
#define BC250_WAVE_HW_ID2_PIPE_ID(v)         (((v) >> 4) & 0x3u)
#define BC250_WAVE_HW_ID2_QUEUE_ID(v)        ((v) & 0xFu)

/* Tetos do orcamento. Numeros pequenos de proposito: ver risco (b) acima. */
#define BC250_WAVE_PROBE_MAX_READS           52u
#define BC250_WAVE_PROBE_MAX_REPORTED        4u
#define BC250_WAVE_PROBE_MAX_SLOTS           32u

typedef struct BC250_WAVE_PROBE_SLOT {
    uint32_t SeIndex;
    uint32_t SaIndex;
    uint32_t InstanceIndex;
    uint32_t WaveId;
} BC250_WAVE_PROBE_SLOT;

typedef struct BC250_WAVE_PROBE_PLAN {
    /* Ordem de visita. O chamador monta; o contrato nao inventa topologia. */
    BC250_WAVE_PROBE_SLOT Slots[BC250_WAVE_PROBE_MAX_SLOTS];
    uint32_t SlotCount;
    /* Quantas ondas o SQ disse estar em voo. Zero => a sonda nao roda. */
    uint32_t WavesInFlight;
    /* Teto de leituras de SQ_IND_DATA nesta passagem. */
    uint32_t ReadBudget;
} BC250_WAVE_PROBE_PLAN;

typedef struct BC250_WAVE_PROBE_FOUND {
    uint32_t SeIndex;
    uint32_t SaIndex;
    uint32_t InstanceIndex;
    uint32_t WaveId;
    uint32_t Status;
    uint32_t PcLo;
    uint32_t PcHi;
    uint32_t IbSts;
    uint32_t InstDw0;
    uint32_t TrapSts;
    uint32_t HwId2;
    uint32_t ValidMask; /* bit 0 STATUS; bits 1..5 PC_LO/HI, IB_STS, INST, TRAP; bit 6 HW_ID2 */
} BC250_WAVE_PROBE_FOUND;

typedef struct BC250_WAVE_PROBE_EVIDENCE {
    uint32_t SlotsVisited;
    uint32_t ReadsIssued;
    uint32_t WavesFound;
    BC250_WAVE_PROBE_FOUND Found[BC250_WAVE_PROBE_MAX_REPORTED];
    /* 1 = GRBM_GFX_INDEX voltou a broadcast e o readback confirmou. */
    uint32_t SelectorRestored;
    uint32_t SelectorReadback;
    /* 1 = alguma leitura de SQ_IND_DATA foi recusada pelo transporte. */
    uint32_t ReadRefused;
    uint32_t PoisonReads;
} BC250_WAVE_PROBE_EVIDENCE;

typedef enum BC250_WAVE_PROBE_STATUS {
    BC250_WAVE_PROBE_OK = 0,
    BC250_WAVE_PROBE_BAD_ARGUMENT,
    /* WavesInFlight == 0: nao ha onda para inspecionar, e nao se escreve nada. */
    BC250_WAVE_PROBE_NO_WAVES,
    /* Plano excede o orcamento ou declara mais slots do que o teto. */
    BC250_WAVE_PROBE_PLAN_REJECTED,
    /* Escrita de seletor falhou: aborta ANTES de ler qualquer coisa. */
    BC250_WAVE_PROBE_SELECT_FAILED,
    /* Varredura terminou, mas o broadcast NAO voltou -- o pior desfecho. */
    BC250_WAVE_PROBE_SELECTOR_STUCK
} BC250_WAVE_PROBE_STATUS;

uint32_t Bc250WaveProbeEncodeIndIndex(uint32_t IxAddress, uint32_t WaveId);
uint32_t Bc250WaveProbeEncodeGfxIndex(
    uint32_t SeIndex, uint32_t SaIndex, uint32_t InstanceIndex);
uint32_t Bc250WaveProbeStatusIsValid(uint32_t WaveStatus);
BC250_WAVE_PROBE_STATUS Bc250WaveProbeValidatePlan(const BC250_WAVE_PROBE_PLAN *Plan);
BC250_WAVE_PROBE_STATUS Bc250WaveProbeRun(
    const BC250_GFX_RING_INTERFACE *Interface,
    const BC250_WAVE_PROBE_PLAN *Plan,
    BC250_WAVE_PROBE_EVIDENCE *Evidence);
const char *Bc250WaveProbeStatusName(BC250_WAVE_PROBE_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif /* BC250_GFX_RING_TRANSACTION_H */
