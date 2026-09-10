#ifndef BC250_SDMA_PACKET_BUILDER_H
#define BC250_SDMA_PACKET_BUILDER_H

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

#define BC250_SDMA_OP_NOP        0u
#define BC250_SDMA_OP_COPY       1u
#define BC250_SDMA_OP_INDIRECT   4u
#define BC250_SDMA_OP_WRITE      2u
#define BC250_SDMA_OP_FENCE      5u
#define BC250_SDMA_OP_TRAP       6u
#define BC250_SDMA_OP_SEM        7u
#define BC250_SDMA_OP_POLL_REGMEM 8u
#define BC250_SDMA_OP_CONST_FILL 11u
#define BC250_SDMA_OP_GCR_REQ    17u

#define BC250_SDMA_SUBOP_COPY_LINEAR 0u
#define BC250_SDMA_SUBOP_WRITE_LINEAR 0u

#define BC250_SDMA_FILL_SIZE_BYTE  0u
#define BC250_SDMA_FILL_SIZE_SHORT 1u
#define BC250_SDMA_FILL_SIZE_DWORD 2u

#define BC250_SDMA_COPY_MAX_BYTES 0x00400000u /* 4 MiB */
#define BC250_SDMA_FILL_MAX_BYTES 0x00400000u /* 4 MiB */

#define BC250_SDMA_DWORDS_NOP(Extra)  (1u + (Extra))
#define BC250_SDMA_DWORDS_COPY_LINEAR 7u
#define BC250_SDMA_DWORDS_WRITE_LINEAR_BASE 4u
#define BC250_SDMA_DWORDS_CONST_FILL  5u
#define BC250_SDMA_DWORDS_FENCE       4u
#define BC250_SDMA_DWORDS_GCR_REQ     5u
#define BC250_SDMA_DWORDS_TRAP        2u
#define BC250_SDMA_DWORDS_INDIRECT    6u

/*
 * SDMA_PKT_INDIRECT_HEADER_VMID ocupa os bits 19:16 e o endereco base do IB e'
 * mascarado com 0xFFFFFFE0 pelo upstream (sdma_v5_0_ring_emit_ib), portanto o
 * alinhamento exigido e' de 32 bytes.
 */
#define BC250_SDMA_IB_ALIGNMENT_BYTES 32u
#define BC250_SDMA_IB_PACKET_ALIGNMENT_DWORDS 8u
#define BC250_SDMA_DWORDS_POLL_REGMEM 6u

typedef enum BC250_SDMA_STATUS {
    BC250_SDMA_OK = 0,
    BC250_SDMA_BAD_ARGUMENT,
    BC250_SDMA_RING_FULL,
    BC250_SDMA_MISALIGNED,
    BC250_SDMA_PAYLOAD_TOO_LARGE,
    BC250_SDMA_OUT_OF_BOUNDS
} BC250_SDMA_STATUS;

BC250_SDMA_STATUS Bc250SdmaEmitNop(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t ExtraCount);

BC250_SDMA_STATUS Bc250SdmaEmitCopyLinear(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t SrcAddress,
    uint64_t DstAddress,
    uint32_t ByteCount);

BC250_SDMA_STATUS Bc250SdmaEmitWriteLinear(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t DstAddress,
    const uint32_t *Data,
    uint32_t DwordCount);

BC250_SDMA_STATUS Bc250SdmaEmitConstFill(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t DstAddress,
    uint32_t FillValue,
    uint32_t ByteCount);

BC250_SDMA_STATUS Bc250SdmaEmitFence(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t FenceAddress,
    uint32_t FenceSequence);

BC250_SDMA_STATUS Bc250SdmaEmitGcr(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords);

BC250_SDMA_STATUS Bc250SdmaEmitIndirect(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t IbAddress,
    uint32_t IbSizeDwords,
    uint64_t CsaAddress,
    uint32_t Vmid);

/*
 * SDMA 5.0 exige que o pacote INDIRECT de 6 DW termine em fronteira de
 * 8 DW. O padding e' inserido ANTES do pacote, exatamente como
 * sdma_v5_0_ring_emit_ib() do upstream.
 */
uint32_t Bc250SdmaIndirectPrePadDwords(uint32_t WptrDwords);

/* Completa o proprio conteudo da IB ate multiplo de 8 DW. */
uint32_t Bc250SdmaIbSizePadDwords(uint32_t IbSizeDwords);

BC250_SDMA_STATUS Bc250SdmaEmitTrap(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t IntContext);

BC250_SDMA_STATUS Bc250SdmaEmitPollRegMem(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t Address,
    uint32_t Value,
    uint32_t Mask,
    uint32_t Func,
    uint32_t MemPoll,
    uint32_t IntervalUs,
    uint32_t RetryCount);

BC250_SDMA_STATUS Bc250SdmaRingComputeAvailableSpace(
    uint32_t RingDwords,
    uint32_t RptrDwords,
    uint32_t WptrDwords,
    uint32_t *AvailableDwords);

BC250_SDMA_STATUS Bc250SdmaRingAdvanceWptr(
    uint32_t RingDwords,
    uint32_t CurrentWptrDwords,
    uint32_t EmittedDwords,
    uint32_t *NewWptrDwords);

const char *Bc250SdmaStatusName(BC250_SDMA_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
