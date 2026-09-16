/*
 * bc250_gfx_draw_initiator.h - Contrato de Draw Initiator e Disparo de Desenho PM4 (B.3.3)
 *
 * Define o contrato e empacotamento PM4 Type-3 (DRAW_INDEX_AUTO, IT=0x2D)
 * para disparo de desenho de geometria sem indexacao no GFX10.
 *
 * Registrador associado (aperture CONTEXT 0xA000, BASE_IDX=1):
 *   VGT_DRAW_INITIATOR (mmREG 0x01FC -> SOC15 0xA1FC, CONTEXT offset 0x01FC, 1 dword)
 *
 * Alvo: AMD BC-250 (GC 10.1.3 / Cyan Skillfish 2)
 */

#ifndef BC250_GFX_DRAW_INITIATOR_H
#define BC250_GFX_DRAW_INITIATOR_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
typedef UINT8  uint8_t;
#endif
#ifndef __cplusplus
#ifndef bool
typedef unsigned char bool;
#define true 1
#define false 0
#endif
#endif
#else
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#endif

#include "bc250_pm4_packet_builder.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opcode e Headers PM4 Canonicos para DRAW_INDEX_AUTO (IT=0x2D) e PACKET2 NOP
 */
#define BC250_GFX_PM4_DRAW_INDEX_AUTO_OPCODE        0x2Du
#define BC250_GFX_PM4_PACKET2_NOP                   0x80000000u

/* Header PM4 DRAW_INDEX_AUTO: Count = 2 - 1 = 1 (1 IndexCount + 1 DrawInitiator) */
#define BC250_GFX_PM4_DRAW_INDEX_AUTO_HEADER        0xC0012D00u

/* Contagem de dwords do stream de Draw Initiator */
#define BC250_GFX_DRAW_INDEX_AUTO_DWORDS            3u /* Header + VertexCount + DrawInitiator */
#define BC250_GFX_DRAW_PADDING_DWORDS               1u /* Packet-2 NOP: 0x80000000u */
#define BC250_GFX_DRAW_STREAM_TOTAL_DWORDS          4u /* Total = 3 + 1 = 4 DW (16 bytes, alinhado a 16 bytes) */

/*
 * Modos de DrawInitiator e Source Select (gc_10_1_0_sh_mask.h L24268..L24276)
 */
#define BC250_GFX_DI_SRC_SEL_DMA                    0x00u /* Selecao por buffer de indice DMA */
#define BC250_GFX_DI_SRC_SEL_IMMEDIATE              0x01u /* Selecao por dados imediatos */
#define BC250_GFX_DI_SRC_SEL_AUTO_INDEX             0x02u /* Selecao automatica sequencial */

/* Modos canonicos aceitos pelo contrato */
#define BC250_GFX_DRAW_INITIATOR_POINTLIST          0x00000001u
#define BC250_GFX_DRAW_INITIATOR_AUTO_INDEX         0x00000002u /* Padrao GFX10 para non-indexed draw */
#define BC250_GFX_DRAW_INITIATOR_TRIANGLELIST       0x00000004u /* Compatibilidade UMD / legado */
#define BC250_GFX_DRAW_INITIATOR_RECTLIST           0x00000011u

/*
 * Status de Retorno do Draw Initiator
 */
typedef enum BC250_GFX_DRAW_STATUS {
    BC250_GFX_DRAW_OK = 0,
    BC250_GFX_DRAW_INVALID_ARGUMENT = 1,
    BC250_GFX_DRAW_RING_FULL = 2,
    BC250_GFX_DRAW_STREAM_MISMATCH = 3
} BC250_GFX_DRAW_STATUS;

/*
 * Pacote Compilado de Draw Initiator
 */
typedef struct BC250_GFX_DRAW_PACKET {
    uint32_t VertexCount;   /* Contagem de vertices / indices (minimo 1) */
    uint32_t DrawInitiator; /* Valor do registrador VGT_DRAW_INITIATOR */
} BC250_GFX_DRAW_PACKET;

/*
 * Funcoes de Contrato do Draw Initiator
 */
BC250_GFX_DRAW_STATUS Bc250GfxComputeDrawInitiator(
    uint32_t VertexCount,
    uint32_t DrawInitiator,
    BC250_GFX_DRAW_PACKET *PacketOut);

BC250_GFX_DRAW_STATUS Bc250GfxEmitDrawInitiatorStream(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    const BC250_GFX_DRAW_PACKET *Packet);

BC250_GFX_DRAW_STATUS Bc250GfxVerifyDrawInitiatorStream(
    const uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t CursorDwords,
    const BC250_GFX_DRAW_PACKET *Packet);

const char *Bc250GfxDrawStatusName(BC250_GFX_DRAW_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif /* BC250_GFX_DRAW_INITIATOR_H */
