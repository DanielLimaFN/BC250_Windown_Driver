#ifndef BC250_PERSISTENT_RING_CONTRACT_H
#define BC250_PERSISTENT_RING_CONTRACT_H

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

#include "bc250_context_dispatcher_contract.h"
#include "bc250_pm4_packet_builder.h"
#include "bc250_sdma_packet_builder.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Anel PERSISTENTE de submissao (Fase P1).
 *
 * As transacoes provadas em silicio ate o candidato 3.43 sao de tiro unico:
 * programam a fila, submetem um lote, conferem e revertem tudo.  O caminho de
 * producao exige o oposto — a fila permanece ativa e cada
 * DxgkDdiSubmitCommandVirtual anexa um lote novo.  Este contrato e' a maquina
 * de estados desse anel, offline e testavel: cursores, espaco livre, quebra de
 * volta (wraparound) e contabilidade de fences.
 *
 * REGRA INEGOCIAVEL: um pacote NUNCA pode cruzar o fim do anel.  O CP para o
 * RPTR no dword anterior a um pacote incompleto (vide 3.40/1.41 e o comentario
 * em bc250_pfp_direct_load.c:1027).  Quando o lote nao cabe ate o fim, o plano
 * preenche o resto com PACKET2 (0x80000000) no CP ou SDMA NOP no SDMA e
 * recomeca no offset zero.
 */

#define BC250_PRING_MIN_SIZE_DWORDS   64u
#define BC250_PRING_MAX_SIZE_DWORDS   0x00100000u /* 4 MiB de anel */
#define BC250_PRING_ALIGNMENT_BYTES   256u

/* IB(4) + RELEASE_MEM(8) */
#define BC250_PRING_COMPUTE_PAYLOAD_DWORDS \
    (BC250_PM4_DWORDS_INDIRECT_BUFFER + BC250_PM4_DWORDS_RELEASE_MEM)

/* INDIRECT(6) + FENCE(4) + TRAP(2) */
#define BC250_PRING_SDMA_PAYLOAD_DWORDS \
    (BC250_SDMA_DWORDS_INDIRECT + BC250_SDMA_DWORDS_FENCE + BC250_SDMA_DWORDS_TRAP)

typedef enum BC250_PRING_STATUS {
    BC250_PRING_OK = 0,
    BC250_PRING_BAD_ARGUMENT,
    BC250_PRING_INVALID_ENGINE,
    BC250_PRING_INVALID_RING_SIZE,
    BC250_PRING_MISALIGNED_ADDRESS,
    BC250_PRING_NOT_ACTIVE,
    BC250_PRING_RING_FULL,
    BC250_PRING_FENCE_REGRESSION,
    BC250_PRING_EMIT_FAILED,
    BC250_PRING_CURSOR_BUSY
} BC250_PRING_STATUS;

typedef enum BC250_PRING_COMPLETION_MODE {
    BC250_PRING_COMPLETION_POLL = 0,
    BC250_PRING_COMPLETION_INTERRUPT = 1
} BC250_PRING_COMPLETION_MODE;

typedef struct BC250_PERSISTENT_RING {
    uint32_t EngineType;        /* BC250_ENGINE_TYPE */
    uint32_t NodeOrdinal;
    uint32_t Vmid;
    uint64_t RingGpuAddress;
    uint32_t RingSizeDwords;    /* potencia de dois */
    uint64_t FenceGpuAddress;
    uint64_t CsaGpuAddress;     /* somente SDMA; 0 quando ausente */

    uint32_t WptrDwords;
    uint32_t RptrShadowDwords;

    uint64_t LastSubmittedFence;
    uint64_t LastRetiredFence;

    uint32_t SubmissionCount;
    uint32_t RetiredCount;
    uint32_t WrapCount;
    uint32_t PadDwordsTotal;
    uint32_t RingFullRejections;
    uint32_t CompletionMode;    /* BC250_PRING_COMPLETION_MODE */
    uint32_t IsActive;
} BC250_PERSISTENT_RING;

typedef struct BC250_PRING_SUBMISSION_PLAN {
    uint32_t StartWptrDwords;   /* onde o payload comeca (apos o padding) */
    uint32_t PadDwords;
    uint32_t PayloadDwords;
    uint32_t TotalDwords;
    uint32_t NewWptrDwords;
    uint32_t Wrapped;
    uint32_t AvailableBefore;
    uint64_t FenceValue;
    uint64_t IbGpuAddress;
    uint32_t IbSizeDwords;
} BC250_PRING_SUBMISSION_PLAN;

BC250_PRING_STATUS Bc250PersistentRingInit(
    BC250_PERSISTENT_RING *Ring,
    uint32_t EngineType,
    uint32_t NodeOrdinal,
    uint64_t RingGpuAddress,
    uint32_t RingSizeDwords,
    uint64_t FenceGpuAddress,
    uint64_t CsaGpuAddress,
    uint32_t Vmid,
    uint32_t CompletionMode);

BC250_PRING_STATUS Bc250PersistentRingAvailableDwords(
    const BC250_PERSISTENT_RING *Ring,
    uint32_t *AvailableDwords);

/*
 * Reconcilia o dominio logico com cursores FISICOS medidos depois que a HQD
 * foi ativada/reentrada. So e' legal com zero fence em voo e anel fisico
 * drenado. Os ponteiros crus podem carregar bits de volta; a saida e'
 * reduzida pela potencia de dois do anel. Nao escreve registrador nem fence.
 */
BC250_PRING_STATUS Bc250PersistentRingReconcileDrainedCursor(
    BC250_PERSISTENT_RING *Ring,
    uint32_t PhysicalRptrRaw,
    uint32_t PhysicalWptrRaw,
    uint32_t *LogicalWptrBefore,
    uint32_t *ReconciledCursor);

/*
 * Planeja o lote sem tocar no anel.  Nao muta Ring: o avanco so acontece em
 * Bc250PersistentRingCommit, depois de a emissao ter sido bem sucedida.
 */
BC250_PRING_STATUS Bc250PersistentRingPlanSubmission(
    const BC250_PERSISTENT_RING *Ring,
    uint64_t IbGpuAddress,
    uint32_t IbSizeDwords,
    uint64_t FenceValue,
    BC250_PRING_SUBMISSION_PLAN *Plan);

/*
 * Escreve padding + payload no buffer mapeado do anel.  RingBuffer aponta para
 * o dword 0 do anel e tem exatamente Ring->RingSizeDwords dwords.
 */
BC250_PRING_STATUS Bc250PersistentRingEmitSubmission(
    const BC250_PERSISTENT_RING *Ring,
    const BC250_PRING_SUBMISSION_PLAN *Plan,
    uint32_t *RingBuffer,
    uint32_t RingDwords);

BC250_PRING_STATUS Bc250PersistentRingCommit(
    BC250_PERSISTENT_RING *Ring,
    const BC250_PRING_SUBMISSION_PLAN *Plan);

/*
 * Contabiliza a conclusao observada na VRAM.  A GPU grava 32 bits, portanto a
 * comparacao e' feita nos 32 bits baixos contra a janela de fences em voo.
 */
BC250_PRING_STATUS Bc250PersistentRingRetire(
    BC250_PERSISTENT_RING *Ring,
    uint32_t ObservedFenceLow32,
    uint32_t RptrDwords,
    uint32_t *RetiredNow);

BC250_PRING_STATUS Bc250PersistentRingIsFenceComplete(
    const BC250_PERSISTENT_RING *Ring,
    uint64_t FenceValue,
    uint32_t *IsComplete);

const char *Bc250PersistentRingStatusName(BC250_PRING_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
