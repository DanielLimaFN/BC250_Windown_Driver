#ifndef BC250_SDMA_QUEUE_STATE_CONTRACT_H
#define BC250_SDMA_QUEUE_STATE_CONTRACT_H

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

#ifdef __cplusplus
extern "C" {
#endif

/*
 * =========================================================================
 * DECODIFICACAO DA TELEMETRIA DE FILA DA SDMA0 (07/09) -- somente leitura
 *
 * POR QUE ISTO EXISTE
 *
 * docs/168_HANDOFF_2026-09-05_INDIRECT_SDMA.md secao 5.3 arquivou leituras de
 * silicio do bloco SDMA0_GFX_IB_* e terminou dizendo, na secao 6 item 6:
 *
 *     "Registradores ainda nao lidos: SDMA0_GFX_IB_SUB_REMAIN, e a semantica
 *      de bits de CONTEXT_STATUS e SKIP_CNTL (o SKIP_CNTL vem nao-zero e
 *      variavel nas variantes de VMID 8: 0x3CB3, 0x3A, 0x2A, 0x1A -- NAO SEI
 *      O QUE SIGNIFICA E NAO INTERPRETEI)."
 *
 * A semantica estava no repositorio: inc/linux_ref/gc_10_1_0_sh_mask.h. As
 * mascaras abaixo sao copiadas dele com linha citada, classe P1.
 *
 * O QUE A DECODIFICACAO REVELOU -- e e' um fato novo, nao uma hipotese
 *
 * Os dois valores de CONTEXT_STATUS medidos em 05/09 diferem em UM bit, e e' o
 * bit que importa:
 *
 *     PHYS  (VMID 0, MC fisico)  0x0D = SELECTED | IDLE | EXPIRED
 *     EXEC  (VMID 8, traduzido)  0x09 = SELECTED |        EXPIRED
 *                                             ^^^^ IDLE = 0
 *
 * Na variante de VMID 8 a fila reporta NAO OCIOSA depois de o RPTR alcancar o
 * WPTR. Medicao (P0, doc 168) mais decodificacao (P1, header local).
 *
 * HIPOTESE (H) que isso sustenta: a engine consumiu o pacote do ANEL, latchou
 * IB_BASE e CMD_VMID, e parou DENTRO do IB -- nao terminou, por isso IDLE
 * continua 0. Aponta para "travou buscando/traduzindo o IB", nao para "nao
 * executou o pacote".
 *
 * NAO PROVADO, e registrado para nao ser esquecido: nao sei se EXPIRED=1 aqui
 * e' significativo ou se e' o estado normal de uma fila que ja drenou. E o
 * PHYS -- que foi a IDLE, ou seja TERMINOU -- tambem falhou a prova de bytes,
 * o que nao pode ser problema de traducao porque nao havia traducao. Sao DUAS
 * falhas diferentes, e este contrato nao decide nenhuma das duas: ele so faz a
 * telemetria da proxima corrida sair legivel em vez de crua.
 *
 * ESTE MODULO NAO ESCREVE REGISTRADOR NENHUM. E' aritmetica pura sobre valores
 * lidos, para poder ser testada offline.
 * =========================================================================
 */

/*
 * Offsets em BAR0. Proveniencia: docs/03_CATALOGO_DE_REGISTRADORES.md, bloco
 * "SDMA0_GFX_IB_* -- derivacao cercada por ancoras", classe P1. O bloco e'
 * consecutivo entre as duas ancoras ja catalogadas (IB_CNTL 0x04BA8 e
 * DOORBELL 0x04BC8), e a aritmetica 0x4980 + dw*4 fecha nos dois lados.
 */
#define BC250_SDMA0_GFX_IB_CNTL_OFFSET        0x04BA8u /* dw 0x008a */
#define BC250_SDMA0_GFX_IB_RPTR_OFFSET        0x04BACu /* dw 0x008b */
#define BC250_SDMA0_GFX_IB_OFFSET_OFFSET      0x04BB0u /* dw 0x008c */
#define BC250_SDMA0_GFX_IB_BASE_LO_OFFSET     0x04BB4u /* dw 0x008d */
#define BC250_SDMA0_GFX_IB_BASE_HI_OFFSET     0x04BB8u /* dw 0x008e */
#define BC250_SDMA0_GFX_IB_SIZE_OFFSET        0x04BBCu /* dw 0x008f */
#define BC250_SDMA0_GFX_SKIP_CNTL_OFFSET      0x04BC0u /* dw 0x0090 */
#define BC250_SDMA0_GFX_CONTEXT_STATUS_OFFSET 0x04BC4u /* dw 0x0091 */
#define BC250_SDMA0_GFX_IB_SUB_REMAIN_OFFSET  0x04C3Cu /* dw 0x00af */

/*
 * Campos. Proveniencia: inc/linux_ref/gc_10_1_0_sh_mask.h -- classe P1.
 * As linhas citadas sao daquele arquivo neste repositorio.
 */

/* SDMA0_GFX_IB_CNTL (linhas 941-948) */
#define BC250_SDMA_IB_CNTL_IB_ENABLE_MASK          0x00000001u
#define BC250_SDMA_IB_CNTL_IB_SWAP_ENABLE_MASK     0x00000010u
#define BC250_SDMA_IB_CNTL_SWITCH_INSIDE_IB_MASK   0x00000100u
#define BC250_SDMA_IB_CNTL_CMD_VMID_MASK           0x000F0000u
#define BC250_SDMA_IB_CNTL_CMD_VMID_SHIFT                  16u

/*
 * SDMA0_GFX_IB_RPTR e SDMA0_GFX_IB_OFFSET (linhas 950-954).
 * ATENCAO: o campo OFFSET comeca no SHIFT 2, nao em 0. O valor cru e' um
 * offset em BYTES cujos dois bits baixos sao sempre zero; o campo conta
 * DWORDS. Ler o cru como dwords da' quatro vezes o valor certo.
 */
#define BC250_SDMA_IB_RPTR_OFFSET_MASK             0x003FFFFCu
#define BC250_SDMA_IB_RPTR_OFFSET_SHIFT                     2u

/* SDMA0_GFX_IB_SIZE (linhas 962-963) */
#define BC250_SDMA_IB_SIZE_SIZE_MASK               0x000FFFFFu

/* SDMA0_GFX_SKIP_CNTL (linhas 965-966) */
#define BC250_SDMA_SKIP_CNTL_SKIP_COUNT_MASK       0x000FFFFFu

/* SDMA0_GFX_IB_SUB_REMAIN (linhas 1013-1014) */
#define BC250_SDMA_IB_SUB_REMAIN_REMAIN_DW_MASK    0x00003FFFu

/* SDMA0_GFX_CONTEXT_STATUS (linhas 968-983) */
#define BC250_SDMA_CONTEXT_STATUS_SELECTED_MASK        0x00000001u
#define BC250_SDMA_CONTEXT_STATUS_IDLE_MASK            0x00000004u
#define BC250_SDMA_CONTEXT_STATUS_EXPIRED_MASK         0x00000008u
#define BC250_SDMA_CONTEXT_STATUS_EXCEPTION_MASK       0x00000070u
#define BC250_SDMA_CONTEXT_STATUS_EXCEPTION_SHIFT               4u
#define BC250_SDMA_CONTEXT_STATUS_CTXSW_ABLE_MASK      0x00000080u
#define BC250_SDMA_CONTEXT_STATUS_CTXSW_READY_MASK     0x00000100u
#define BC250_SDMA_CONTEXT_STATUS_PREEMPTED_MASK       0x00000200u
#define BC250_SDMA_CONTEXT_STATUS_PREEMPT_DISABLE_MASK 0x00000400u

/* Uniao de todos os campos conhecidos de CONTEXT_STATUS: 0x000007FD.
 * O bit 1 nao tem nome no header -- e' por isso que existe UnknownBits. */
#define BC250_SDMA_CONTEXT_STATUS_KNOWN_MASK           0x000007FDu

/*
 * Valores MEDIDOS no silicio em 2026-09-05, arquivados em docs/168 secao 5.3.
 * Ficam aqui como gabarito do teste -- nao como configuracao.
 */
#define BC250_SDMA_CONTEXT_STATUS_MEASURED_PHYS_VMID0  0x0000000Du
#define BC250_SDMA_CONTEXT_STATUS_MEASURED_EXEC_VMID8  0x00000009u
#define BC250_SDMA_IB_CNTL_MEASURED_EXEC_VMID8         0x00080101u
#define BC250_SDMA_IB_CNTL_MEASURED_PHYS_VMID0         0x00000101u
#define BC250_SDMA_IB_RPTR_MEASURED_ALL_VARIANTS       0x00000020u

typedef struct BC250_SDMA_QUEUE_STATE {
    /* --- valores crus, como lidos --- */
    uint32_t RawIbCntl;
    uint32_t RawIbRptr;
    uint32_t RawIbOffset;
    uint32_t RawIbSize;
    uint32_t RawSkipCntl;
    uint32_t RawContextStatus;
    uint32_t RawIbSubRemain;
    uint64_t RawIbBase;

    /* --- IB_CNTL decodificado --- */
    uint32_t IbEnable;
    uint32_t IbSwapEnable;
    uint32_t SwitchInsideIb;
    uint32_t CmdVmid;

    /* --- ponteiros, em DWORDS (campo, nao cru) --- */
    uint32_t IbRptrDwords;
    uint32_t IbOffsetDwords;
    uint32_t IbSizeDwords;
    uint32_t SkipCount;
    uint32_t IbSubRemainDwords;

    /* --- CONTEXT_STATUS decodificado --- */
    uint32_t Selected;
    uint32_t Idle;
    uint32_t Expired;
    uint32_t Exception;
    uint32_t CtxSwAble;
    uint32_t CtxSwReady;
    uint32_t Preempted;
    uint32_t PreemptDisable;
    uint32_t UnknownBits;   /* bits de CONTEXT_STATUS sem nome no header */
} BC250_SDMA_QUEUE_STATE;

/*
 * Veredito sobre a fila. Deliberadamente NAO diz "o IB executou" -- essa
 * afirmacao exige o tripe da Lei 2, e nenhum registrador de status substitui
 * bytes conferidos no destino.
 */
typedef enum BC250_SDMA_QUEUE_VERDICT {
    /* Fila ociosa e sem excecao: drenou. NAO prova que escreveu. */
    BC250_SDMA_QUEUE_DRAINED = 0,
    /* Nao ociosa: ha' trabalho pendente ou a engine parou no meio. */
    BC250_SDMA_QUEUE_BUSY_OR_STALLED,
    /* EXCEPTION nao-zero: a engine reportou erro. */
    BC250_SDMA_QUEUE_EXCEPTION,
    /* Preempcao em curso. */
    BC250_SDMA_QUEUE_PREEMPTED,
    /* IB_ENABLE desligado: a engine nao busca IB nenhum. */
    BC250_SDMA_QUEUE_IB_DISABLED
} BC250_SDMA_QUEUE_VERDICT;

typedef enum BC250_SDMA_QUEUE_STATE_STATUS {
    BC250_SDMA_QUEUE_STATE_OK = 0,
    BC250_SDMA_QUEUE_STATE_BAD_ARGUMENT
} BC250_SDMA_QUEUE_STATE_STATUS;

BC250_SDMA_QUEUE_STATE_STATUS Bc250SdmaDecodeQueueState(
    uint32_t IbCntl,
    uint32_t IbRptr,
    uint32_t IbOffset,
    uint32_t IbSize,
    uint32_t SkipCntl,
    uint32_t ContextStatus,
    uint64_t IbBase,
    uint32_t IbSubRemain,
    BC250_SDMA_QUEUE_STATE *State);

BC250_SDMA_QUEUE_VERDICT Bc250SdmaClassifyQueueState(
    const BC250_SDMA_QUEUE_STATE *State);

const char *Bc250SdmaQueueVerdictName(BC250_SDMA_QUEUE_VERDICT Verdict);

/*
 * TRUE quando a fila apresenta o padrao especifico medido na variante de
 * VMID 8: o anel drenou (RPTR==WPTR, informado pelo chamador) mas a fila NAO
 * esta ociosa. E' o discriminante que separou EXEC de PHYS em 05/09.
 *
 * Nao e' veredito de defeito -- e' um rotulo para o log, para que a proxima
 * corrida nao precise ser re-decodificada a mao.
 */
uint32_t Bc250SdmaQueueLooksStalledInsideIb(
    const BC250_SDMA_QUEUE_STATE *State,
    uint32_t RingDrained);

#ifdef __cplusplus
}
#endif

#endif
