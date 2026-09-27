#ifndef BC250_SUBMIT_COMMAND_CONTRACT_H
#define BC250_SUBMIT_COMMAND_CONTRACT_H

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

#include "bc250_adapter_info_contract.h"
#include "bc250_sdma_packet_builder.h"
#include "bc250_pm4_packet_builder.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum BC250_SUBMISSION_STATUS {
    BC250_SUBMISSION_OK = 0,
    BC250_SUBMISSION_BAD_ARGUMENT,
    BC250_SUBMISSION_INVALID_NODE,
    BC250_SUBMISSION_MISALIGNED_ADDRESS,
    BC250_SUBMISSION_RING_FULL,
    BC250_SUBMISSION_PAYLOAD_TOO_LARGE
} BC250_SUBMISSION_STATUS;

typedef struct BC250_SUBMISSION_REQUEST {
    uint32_t NodeIndex;
    uint32_t ContextId;
    uint64_t DmaBufferGpuVa;
    uint32_t DmaBufferSizeBytes;
    uint64_t FenceGpuVa;
    uint64_t FenceValue;
    uint32_t RingIndex;
} BC250_SUBMISSION_REQUEST;

typedef struct BC250_SUBMISSION_RESULT {
    uint32_t EmittedDwords;
    uint32_t UpdatedWptr;
    uint32_t DoorbellOffset;
    uint32_t DoorbellValue;
    uint32_t IsSuccessful;
} BC250_SUBMISSION_RESULT;

BC250_SUBMISSION_STATUS Bc250ValidateSubmissionRequest(
    const BC250_SUBMISSION_REQUEST *Request);

BC250_SUBMISSION_STATUS Bc250BuildSdmaRingSubmission(
    const BC250_SUBMISSION_REQUEST *Request,
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    BC250_SUBMISSION_RESULT *Result);

BC250_SUBMISSION_STATUS Bc250BuildComputeRingSubmission(
    const BC250_SUBMISSION_REQUEST *Request,
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    BC250_SUBMISSION_RESULT *Result);

BC250_SUBMISSION_STATUS Bc250SubmitCommand(
    const BC250_SUBMISSION_REQUEST *Request,
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    BC250_SUBMISSION_RESULT *Result);

const char *Bc250SubmissionStatusName(BC250_SUBMISSION_STATUS Status);

/*
 * =========================================================================
 * A13 (07/09) -- quem executou o que, sem eufemismo.
 *
 * MOTIVO, medido na corrida 2026-09-07 16:19: o log tem 14 linhas
 * "IB EXECUTADO: DMA buffer de %u bytes em VA=..." e, no mesmo log, 2
 * "PLANO A DIRECT FILL" + 12 "PLANO A SYNC NOP" -- ou seja 14 = 2 + 12 e
 * ZERO INDIRECT. Em nenhuma das 14 a GPU leu o DMA buffer entregue pelo
 * dxgkrnl: os dois PLANO A reimplementam a operacao no ANEL. A linha de
 * sucesso era comum aos tres ramos, entao afirmava execucao de IB onde nao
 * houve IB. Isso e' falso sucesso (Lei 4) e escondia o defeito do INDIRECT.
 *
 * Consequencia pratica que este contrato tambem resolve: como os dois
 * PLANO A tem PRECEDENCIA, o ramo INDIRECT nao chega a ser exercitado no
 * perfil ativo -- e por isso a telemetria IB_QUEUE_* da faixa A12 nao
 * escreveu uma linha na corrida 16:19.
 *
 * B3 (07/09, depois de 17:43 e 18:16): o INDIRECT passou a ser o DEFAULT e os
 * PLANO A ficaram atras de PlanoAFallbackEnabled, default 0 -- ver o campo.
 * =========================================================================
 */

typedef enum BC250_SUBMIT_EXEC_PATH {
    /* Nada aplicavel: silicio nao tocado, submissao recusada. */
    BC250_SUBMIT_PATH_REFUSED = 0,
    /* A GPU busca o DMA buffer do dxgkrnl pela GPUVA. O unico IB de verdade. */
    BC250_SUBMIT_PATH_INDIRECT = 1,
    /* PLANO A: fill reimplementado em pacotes do anel. A GPU nao le o buffer. */
    BC250_SUBMIT_PATH_RING_DIRECT_FILL = 2,
    /* PLANO A: NOP/fence reimplementados no anel. A GPU nao le o buffer. */
    BC250_SUBMIT_PATH_RING_SYNC_NOP = 3
} BC250_SUBMIT_EXEC_PATH;

typedef struct BC250_SUBMIT_PATH_INPUT {
    /* Ha' parametros de fill pendentes publicados pelo BuildPagingBuffer. */
    uint32_t HasPendingFill;
    uint32_t FillBytes;
    /* Endereco fisico do destino ja' resolvido; 0 = nao resolvido. */
    uint64_t DestPhysicalStart;
    uint32_t DmaBufferSizeBytes;
    /* A GPUVA do proprio DMA buffer resolve sob VMID8. */
    uint32_t IbGpuVaResolves;
    /*
     * B3 (07/09) -- POLARIDADE INVERTIDA. Antes o campo era IndirectOnly e o
     * default do driver era "PLANO A com precedencia"; agora o default e' o
     * INDIRECT e os PLANO A so' existem se alguem os pedir explicitamente.
     *
     * MOTIVO: em 17:43 e 18:16 o INDIRECT executou o DMA buffer real do
     * dxgkrnl com o tripe completo (5 canarios substituidos). Enquanto os
     * PLANO A tiverem precedencia, um INDIRECT que falhe fica ESCONDIDO atras
     * de uma reimplementacao que funciona -- fallback disfarcado de sucesso.
     *
     * 0 (default) = somente INDIRECT; GPUVA que nao resolve vira RECUSA.
     * 1           = restaura a precedencia antiga, para bisseccao.
     */
    uint32_t PlanoAFallbackEnabled;
} BC250_SUBMIT_PATH_INPUT;

/*
 * Default (PlanoAFallbackEnabled == 0): somente INDIRECT.
 * Com PlanoAFallbackEnabled == 1, reproduz exatamente a precedencia que rodou
 * em producao ate 07/09 -- DIRECT FILL, depois SYNC NOP, depois INDIRECT.
 */
BC250_SUBMIT_EXEC_PATH Bc250SelectSubmitExecPath(
    const BC250_SUBMIT_PATH_INPUT *Input);

/*
 * TRUE somente quando a GPU de fato le o DMA buffer entregue pelo dxgkrnl --
 * isto e', o unico caso em que o log pode dizer "IB EXECUTADO".
 */
uint32_t Bc250SubmitPathExecutesDmaBuffer(BC250_SUBMIT_EXEC_PATH Path);

/* Nome estavel para log e evidencia. */
const char *Bc250SubmitPathName(BC250_SUBMIT_EXEC_PATH Path);

#ifdef __cplusplus
}
#endif

#endif
