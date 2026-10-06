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
#define BC250_SDMA_SUBOP_COPY_LINEAR_SUB_WIND 4u
#define BC250_SDMA_SUBOP_WRITE_LINEAR 0u

#define BC250_SDMA_FILL_SIZE_BYTE  0u
#define BC250_SDMA_FILL_SIZE_SHORT 1u
#define BC250_SDMA_FILL_SIZE_DWORD 2u

/*
 * SDMA.4M (28/09, Corridas 135 e 151): as DUAS travas do SDMA0 que viraram
 * BugCheck 0x10E Arg1=4 ("Rotation failed in a must succeed path") pararam
 * dentro de um IB de paginacao com UM pacote CONST_FILL de EXATAMENTE 4 MiB
 * (count=0x3FFFFF). Mesa (ac_cmdbuf_sdma.c, ac_emit_sdma_constant_fill), com o
 * MESMO formato de pacote (fill_size=2, contagem em bytes-1), limita o fill do
 * SDMA < 6.0 a BITFIELD64_MASK(22) & ~0x3 = 0x3FFFFC e nunca programa 0x3FFFFF;
 * e limita a copia linear do SDMA < 5.2 a SDMA_V2_0_COPY_MAX_BYTES = 0x3FFF00
 * (sid.h). A BC-250 e' SDMA 5.0. Proveniencia: P1 (Mesa local); a ligacao com a
 * trava e' H FRACA: as 2 travas pararam num fill de 4 MiB, mas 333-379 fills
 * de 4 MiB por corrida passaram sem trava nas Corridas 146/149/150. Todo chamador fatia por estes
 * limites, entao o efeito e' so' o tamanho das fatias.
 */
#define BC250_SDMA_COPY_MAX_BYTES 0x003FFF00u /* Mesa SDMA_V2_0_COPY_MAX_BYTES */
#define BC250_SDMA_FILL_MAX_BYTES 0x003FFFFCu /* Mesa: MASK(22) & ~3 */

#define BC250_SDMA_DWORDS_NOP(Extra)  (1u + (Extra))
#define BC250_SDMA_DWORDS_COPY_LINEAR 7u
#define BC250_SDMA_DWORDS_COPY_LINEAR_SUB_WIND 13u
#define BC250_SDMA_DWORDS_WRITE_LINEAR_BASE 4u
#define BC250_SDMA_DWORDS_CONST_FILL  5u
#define BC250_SDMA_DWORDS_FENCE       4u
#define BC250_SDMA_DWORDS_FENCE64     5u
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

/*
 * SDMA 5.0 COPY_LINEAR_SUB_WIND para superficies RGBA/BGRA lineares de
 * 32 bits. Layout transcrito de navi10_sdma_pkt_open.h do Linux upstream:
 * https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/amdgpu/navi10_sdma_pkt_open.h
 * A localizacao dos campos e' P1 (header upstream). Interpretar pitch, slice
 * e retangulo como contagens de elementos menos um e' HIPOTESE (H) ate a
 * primeira corrida no silicio; o header gerado nao documenta a semantica.
 * Veredito fisico: NAO VALIDADA EM HARDWARE.
 */
BC250_SDMA_STATUS Bc250SdmaEmitCopyLinearSubWindow32(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t SrcAddress,
    uint32_t SrcPitchBytes,
    uint64_t DstAddress,
    uint32_t DstPitchBytes,
    uint32_t DstX,
    uint32_t DstY,
    uint32_t Width,
    uint32_t Height);

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

BC250_SDMA_STATUS Bc250SdmaEmitFence64(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t FenceAddress,
    uint64_t FenceValue);

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

/*
 * 4c (doc 567) -- escrita de registrador e invalidacao de TLB PELO SDMA, em
 * ordem com o trabalho da GPU, como o Linux faz no anel do kernel.
 *
 * Proveniencia (P1, copias locais em referencias/linux_upstream_2026-09-29/,
 * SHA-256 no doc 567):
 *   - navi10_sdma_pkt_open.h: SDMA_OP_SRBM_WRITE = 14; HEADER_BYTE_EN nos
 *     bits 31:28; ADDR no dw1 (18 bits, offset de registrador em DWORDS);
 *     POLL_REGMEM: FUNC 30:28, MEM_POLL 31, DW5 INTERVAL 15:0 e
 *     RETRY_COUNT 27:16.
 *   - sdma_v5_0.c: emit_wreg (SRBM_WRITE com BYTE_EN=0xF), emit_reg_wait
 *     (POLL_REGMEM FUNC=3 "igual", MEM_POLL=0, endereco = reg << 2,
 *     RETRY 0xFFF, INTERVAL 10) e emit_reg_write_reg_wait (escreve REQ,
 *     espera um ciclo em REQ com ref=0/mask=0, espera ACK & mask == mask).
 *   - gmc_v10_0.c: emit_flush_gpu_tlb; no GFXHUB o Linux NAO usa o semaforo.
 *   - amdgpu_gmc.c: amdgpu_gmc_allocate_vm_inv_eng da um engine por anel
 *     (bitmap 0x1FFF3; 17 fica para o flush de GART pela CPU).
 *
 * Engines nossos: 17 = CPU por MMIO, 0 = anel grafico (DX.32), 1 = SDMA0.
 * Offsets absolutos em DWORDS (GC_BASE seg0 = 0x1260, gc_10_1_0_offset.h):
 *   REQ_ENGn = 0x1260 + 0x1643 + n   (ENG1 = 0x28A4, byte 0xA290)
 *   ACK_ENGn = 0x1260 + 0x1655 + n   (ENG1 = 0x28B6, byte 0xA2D8)
 */
#define BC250_SDMA_OP_SRBM_WRITE        14u
#define BC250_SDMA_DWORDS_SRBM_WRITE    3u
#define BC250_SDMA_DWORDS_REG_WAIT      BC250_SDMA_DWORDS_POLL_REGMEM
#define BC250_SDMA_DWORDS_VM_INVALIDATE \
    (BC250_SDMA_DWORDS_SRBM_WRITE + 2u * BC250_SDMA_DWORDS_REG_WAIT)

#define BC250_SDMA_REG_WAIT_RETRY_COUNT 0xFFFu /* sdma_v5_0_ring_emit_reg_wait */
#define BC250_SDMA_REG_WAIT_INTERVAL    10u    /* idem */
#define BC250_SDMA_POLL_FUNC_EQUAL      3u

#define BC250_SDMA_INVALIDATE_ENGINE    1u
#define BC250_GCVM_INV_ENG0_REQ_DWORD   (0x1260u + 0x1643u)
#define BC250_GCVM_INV_ENG0_ACK_DWORD   (0x1260u + 0x1655u)
#define BC250_GCVM_INV_ENG_COUNT        18u
/* Bits 19..23 de GCVM_INVALIDATE_ENG*_REQ: L2 PTEs, PDE0/1/2 e L1 PTEs
 * (gfxhub_v2_0_get_invalidate_req); o mesmo valor do flush por MMIO. */
#define BC250_GCVM_INV_REQ_ALL_LEVELS   0x00F80000u

BC250_SDMA_STATUS Bc250SdmaEmitSrbmWrite(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t RegisterDword,
    uint32_t Value);

/* POLL_REGMEM sobre registrador, identico a sdma_v5_0_ring_emit_reg_wait. */
BC250_SDMA_STATUS Bc250SdmaEmitRegWait(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t RegisterDword,
    uint32_t Reference,
    uint32_t Mask);

uint32_t Bc250GcvmInvalidateRequestValue(uint32_t Vmid, uint32_t FlushType);

/*
 * Invalidacao de UMA VMID do GFXHUB pelo SDMA, sequencia de
 * sdma_v5_0_ring_emit_reg_write_reg_wait. 15 dwords. Recusa VMID 0 (a raiz
 * do MEC/anel fisico) e engine 17 (reservado a CPU).
 */
BC250_SDMA_STATUS Bc250SdmaEmitVmInvalidate(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t Engine,
    uint32_t Vmid,
    uint32_t FlushType);

/*
 * 4c.4 (doc 575) -- IB DE PAGINACAO DIVIDIDO NO ANEL NOS PONTOS DE FlushTlb.
 *
 * Com PageTableUpdateMode=GPU_VIRTUAL o contrato poe no MESMO paging buffer
 * UpdatePageTable (PTE por WRITE_LINEAR), FlushTlb e a operacao que usa o
 * mapeamento novo (examples.md do WDDM). A invalidacao nao pode ir DENTRO do
 * IB: o SDMA para no SRBM_WRITE (P0, Corrida 205, doc 568). Por isso o IB e'
 * executado em PEDACOS, com a invalidacao no anel entre eles:
 *
 *   [flush(off=0)] [pad][INDIRECT seg0][GCR] [flush] [pad][INDIRECT seg1][GCR] ... [flush(off=fim)]
 *
 * Splits: OffsetDwords crescente, multiplo de 8 (base de IB alinhada a 32 B),
 * <= IbDwords; offsets iguais se fundem (OR das mascaras). Mascara vazia ou
 * com bit 0 e' recusada. RingPosBase e' a posicao ABSOLUTA no anel de
 * Buffer[0], porque o pre-pad do INDIRECT depende dela. A fence fica com o
 * chamador.
 */
#define BC250_SDMA_IB_SPLIT_MAX 32u /* doc 580: folga sobre o teto de 12 do UpdatePageTable */

typedef struct BC250_SDMA_IB_SPLIT {
    uint32_t OffsetDwords;
    uint32_t VmidMask;
    /*
     * P4.2 (doc 622): faixa [OffsetDwords, Sdma1EndDwords) do IB que roda no
     * SDMA1 (escrita de PTE), com troca de sinais entre os aneis; 0 = ponto
     * de divisao comum (invalidacao). Sdma1Token e' o valor da troca.
     */
    uint32_t Sdma1EndDwords;
    uint32_t Sdma1Token;
} BC250_SDMA_IB_SPLIT;

/*
 * P4.2 (doc 622): troca de sinais entre SDMA0 e SDMA1, na forma do Linux
 * (P1, sdma_v5_0.c): FENCE com MTYPE_UC (sdma_v5_0_ring_emit_fence) e
 * POLL_REGMEM de memoria, FUNC=3 "igual", mascara 0xFFFFFFFF, RETRY 0xFFF e
 * INTERVAL 4 (sdma_v5_0_ring_emit_pipeline_sync).
 *   SDMA0: ... IB ate o inicio da faixa | FENCE(Go = T) | POLL(Done == T) | ...
 *   SDMA1: POLL(Go == T) | IB da faixa (mesma VMID) | GCR | FENCE(Done = T)
 * Go e Done sao dwords em memoria que os dois aneis enxergam (VMID 0).
 */
typedef struct BC250_SDMA_CROSS_ENGINE {
    uint64_t GoAddress;
    uint64_t DoneAddress;
} BC250_SDMA_CROSS_ENGINE;

#define BC250_SDMA_CROSS_POLL_FUNC     3u      /* igual */
#define BC250_SDMA_CROSS_POLL_RETRY    0xFFFu  /* sdma_v5_0_ring_emit_pipeline_sync */
#define BC250_SDMA_CROSS_POLL_INTERVAL 4u
#define BC250_SDMA_DWORDS_CROSS_HANDOFF (BC250_SDMA_DWORDS_FENCE + BC250_SDMA_DWORDS_POLL_REGMEM)

/* Fim da faixa coberta pelo ponto: Sdma1EndDwords nas faixas, OffsetDwords nos pontos. */
uint32_t Bc250SdmaSplitEndDwords(const BC250_SDMA_IB_SPLIT *Split);

/*
 * Como Bc250SdmaEmitSplitIndirect, aceitando faixas do SDMA1: a faixa NAO
 * roda no SDMA0; no lugar dela vai o FENCE(Go) + POLL(Done). Cross NULL com
 * faixa presente = BAD_ARGUMENT.
 */
BC250_SDMA_STATUS Bc250SdmaEmitSplitIndirectCross(
    uint32_t *Buffer,
    uint32_t BufferDwords,
    uint32_t *CursorDwords,
    uint32_t RingPosBase,
    uint64_t IbAddress,
    uint32_t IbDwords,
    uint32_t Vmid,
    const BC250_SDMA_IB_SPLIT *Splits,
    uint32_t SplitCount,
    const BC250_SDMA_CROSS_ENGINE *Cross,
    uint32_t *LastIndirectEndDwords);

/* Lado do SDMA1 de uma faixa: POLL(Go == Token) | IB [First, End) | GCR | FENCE(Done = Token). */
BC250_SDMA_STATUS Bc250SdmaEmitSdma1RangeJob(
    uint32_t *Buffer,
    uint32_t BufferDwords,
    uint32_t *CursorDwords,
    uint32_t RingPosBase,
    const BC250_SDMA_CROSS_ENGINE *Cross,
    uint32_t Token,
    uint64_t IbAddress,
    uint32_t FirstDword,
    uint32_t EndDword,
    uint32_t Vmid);

#define BC250_SDMA_DWORDS_SDMA1_RANGE_JOB_WORST \
    (BC250_SDMA_DWORDS_POLL_REGMEM + (BC250_SDMA_IB_PACKET_ALIGNMENT_DWORDS - 1u) + \
     BC250_SDMA_DWORDS_INDIRECT + BC250_SDMA_DWORDS_GCR_REQ + BC250_SDMA_DWORDS_FENCE)

BC250_SDMA_STATUS Bc250SdmaEmitSplitIndirect(
    uint32_t *Buffer,
    uint32_t BufferDwords,
    uint32_t *CursorDwords,
    uint32_t RingPosBase,
    uint64_t IbAddress,
    uint32_t IbDwords,
    uint32_t Vmid,
    const BC250_SDMA_IB_SPLIT *Splits,
    uint32_t SplitCount,
    uint32_t *LastIndirectEndDwords);

/*
 * 4c.4 (doc 580): onde termina o proximo pedaco de anel de um IB dividido.
 * Devolve o indice (exclusivo) do ultimo ponto incluido a partir de First:
 * cresce enquanto PreDwords + pior caso + FENCE <= MaxDwords, inclui sempre
 * pelo menos um ponto e nunca corta numa faixa vazia do IB (ponto no mesmo
 * offset do inicio do pedaco).
 */
uint32_t Bc250SdmaSplitNextChunk(
    const BC250_SDMA_IB_SPLIT *Splits,
    uint32_t SplitCount,
    uint32_t First,
    uint32_t ChunkStartDwords,
    uint32_t PreDwords,
    uint32_t MaxDwords);

/* Pior caso de dwords de Bc250SdmaEmitSplitIndirect (para reservar o anel). */
uint32_t Bc250SdmaSplitIndirectWorstDwords(
    const BC250_SDMA_IB_SPLIT *Splits,
    uint32_t SplitCount);

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
