#ifndef BC250_GCVM_SDMA_VMID_TRANSACTION_H
#define BC250_GCVM_SDMA_VMID_TRANSACTION_H

/*
 * bc250_gcvm_sdma_vmid_transaction.h
 *
 * Contrato offline do incremento 1.17.0.0:
 *   primeira prova estrutural ponta-a-ponta de que a infraestrutura
 *   Windows (GCVM/SDMA/Fence) consegue encadear traducao de VA -> PA
 *   e copiar bytes via endereco de GPU, sem qualquer MMIO.
 *
 * NAO escreve em MMIO, ring, VRAM, PSP ou qualquer registrador.
 * Nao aloca memoria de kernel. NAO e prova fisica.
 *
 * O objetivo do contrato e servir de andar abaixo do ensaio fisico
 * do candidato 1.17.0.0: ele valida (1) o plano de page table,
 * (2) a traducao VA -> PA, (3) o pacote SDMA COPY LINEAR,
 * (4) o pacote SDMA FENCE, (5) o estado de ring consumido, e
 * (6) a verificacao estrutural do byte que a GPU "escreveria"
 *     no destino (no stub, comparamos o que o transporte
 *     "consumiu" como se fosse o dado real).
 *
 * Politica fail-closed: qualquer argumento invalido, VA fora
 * do plano, PTE nao VALID, ring overflow, ou destino sem espaco
 * suficiente retorna BAD_ARGUMENT / FAULT e NUNCA emite pacotes.
 */

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
typedef UINT8  uint8_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#include "bc250_gcvm_contract.h"
#include "bc250_sdma_packet_builder.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Limites duros do contrato offline. */
#define BC250_GSVT_MAX_PAGE_COUNT   1024u   /* 1024 * 4 KiB = 4 MiB */
#define BC250_GSVT_RING_DWORDS      512u    /* >= maior pacote SDMA */
#define BC250_GSVT_FENCE_SEQUENCE   0xB250u /* "BC250" sentinela */

/* Identidade do contrato (anti-cross-driver). */
#define BC250_GSVT_IDENTITY 0x47535654u /* "GSVT" */

/* VMID reservado para a transacao (espelha 1.16.0.0 inerte). */
#define BC250_GSVT_VMID 8u

typedef enum BC250_GSVT_STATUS {
    BC250_GSVT_OK = 0,
    BC250_GSVT_BAD_ARGUMENT,
    BC250_GSVT_NULL_INTERFACE,
    BC250_GSVT_IDENTITY_MISMATCH,
    BC250_GSVT_PLAN_INVALID,
    BC250_GSVT_PTE_INVALID,
    BC250_GSVT_TRANSLATION_FAULT,
    BC250_GSVT_RING_OVERFLOW,
    BC250_GSVT_COPY_TOO_LARGE,
    BC250_GSVT_POPULATE_FAILED,
    BC250_GSVT_DESTINATION_TOO_SMALL
} BC250_GSVT_STATUS;

typedef struct BC250_GSVT_DESTINATION_BUFFER {
    uint8_t *Bytes;
    uint32_t Capacity;
} BC250_GSVT_DESTINATION_BUFFER;

typedef uint32_t (*BC250_GSVT_FILL_TEST_PATTERN)(
    uint64_t PageIndex,
    uint32_t ByteOffset,
    void *Context);

typedef struct BC250_GSVT_REQUEST {
    /* Faixa logica a ser traduzida (4 KiB aligned). */
    uint64_t VaStart;
    uint32_t PageCount;
    uint32_t PteFlags;

    /* Endereco fisico (4 KiB aligned) onde o contrato ira
     * materializar a tabela de paginas. */
    uint64_t PageTablePhysical;

    /* Buffer de PTEs owned (PageCount * 8 bytes). */
    uint64_t *PteBuffer;
    uint32_t PteBufferDwords;

    /* Buffer de ring SDMA (>= BC250_GSVT_RING_DWORDS). */
    uint32_t *RingBuffer;
    uint32_t RingDwords;

    /* Destino "fisico" da copia (stub offline). */
    BC250_GSVT_DESTINATION_BUFFER Destination;

    /* Funcao que produz o padrao que a GPU "escreveria" para
     * o pageIndex/offset. Nulo => padrao deterministico interno
     * (PageIndex*0x100 + ByteOffset). */
    BC250_GSVT_FILL_TEST_PATTERN FillPattern;
    void *FillContext;
} BC250_GSVT_REQUEST;

/* Evidencia persistida apos uma execucao bem-sucedida. */
typedef struct BC250_GSVT_EVIDENCE {
    uint32_t VmId;
    uint32_t PageCount;
    uint64_t VaStart;
    uint64_t VaEnd;
    uint64_t PageTablePhysical;
    uint32_t CopyDwords;
    uint32_t FenceDwords;
    uint32_t TotalRingDwords;
    uint32_t FinalWptr;
    uint32_t FirstTranslatedPaLow;
    uint32_t LastTranslatedPaLow;
    uint32_t DestinationBytesWritten;
    uint32_t PatternMismatch;
    uint32_t TranslationFaults;
    uint32_t OfflineContractReady;
} BC250_GSVT_EVIDENCE;

BC250_GSVT_STATUS Bc250RunGcvmSdmaVmidContract(
    const BC250_GSVT_REQUEST *Request,
    BC250_GSVT_EVIDENCE *Evidence);

const char *Bc250GsvtStatusName(BC250_GSVT_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
