#ifndef BC250_GFX_RING_TRANSACTION_H
#define BC250_GFX_RING_TRANSACTION_H

#include "bc250_gfx_ring_contract.h"

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
#define BC250_GFX_REG_CP_RB_WPTR_DELAY       0x00008704u

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
    BC250_GFX_RING_TXN_POLL_CNTL_BORROWED
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

const char *Bc250GfxRingTxnStatusName(BC250_GFX_RING_TXN_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif /* BC250_GFX_RING_TRANSACTION_H */
