/*
 * bc250_gfx_primitive_assembler.h - Contrato de Primitive Assembler e Topologia PM4 (B.3.2)
 *
 * Define o contrato e empacotamento PM4 Type-3 (SET_UCONFIG_REG, IT=0x79)
 * para configuracao de topologia de primitiva (DI_PT_TRILIST) e contagem de instancias no GFX10.
 *
 * Registradores envolvidos (base UCONFIG 0xC000, BASE_IDX=1):
 *   VGT_PRIMITIVE_TYPE (mmREG 0x2242 -> SOC15 0xC242, UCONFIG offset 0x0242, 1 dword)
 *   VGT_NUM_INSTANCES  (mmREG 0x224D -> SOC15 0xC24D, UCONFIG offset 0x024D, 1 dword)
 *
 * Alvo: AMD BC-250 (GC 10.1.3 / Cyan Skillfish 2)
 */

#ifndef BC250_GFX_PRIMITIVE_ASSEMBLER_H
#define BC250_GFX_PRIMITIVE_ASSEMBLER_H

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
 * Opcode e Headers PM4 Canonicos para Registradores UCONFIG (IT=0x79) e NOP (IT=0x10)
 */
#define BC250_GFX_PM4_SET_UCONFIG_REG_OPCODE        0x79u
#define BC250_GFX_PM4_NOP_OPCODE                    0x10u

/* Header PM4 SET_UCONFIG_REG: Count = 2 - 1 = 1 (1 reg offset + 1 valor) */
#define BC250_GFX_PM4_SET_UCONFIG_HEADER_1DW        0xC0017900u

/* Header PM4 NOP: Count = 1 - 1 = 0 (1 dado de padding) */
#define BC250_GFX_PM4_NOP_HEADER_1DW                0xC0001000u

/* Contagem de dwords do stream de Primitive Assembler */
#define BC250_GFX_PRIM_TYPE_DWORDS                  3u /* Header + Offset 0x0242 + Topology */
#define BC250_GFX_PRIM_INSTANCES_DWORDS             3u /* Header + Offset 0x024D + NumInstances */
#define BC250_GFX_PRIM_PADDING_DWORDS               2u /* Header NOP + 0x00000000 */
#define BC250_GFX_PRIM_STREAM_TOTAL_DWORDS          8u /* Total = 3 + 3 + 2 = 8 DW (32 bytes, alinhado a 16 bytes) */

/*
 * Topologias de Primitivas Suportadas (GFX10 VGT)
 */
#define BC250_GFX_PRIM_POINTLIST                    0x01u
#define BC250_GFX_PRIM_LINELIST                     0x02u
#define BC250_GFX_PRIM_LINESTRIP                    0x03u
#define BC250_GFX_PRIM_TRILIST                      0x04u /* DI_PT_TRILIST (Triangulo canonico) */
#define BC250_GFX_PRIM_TRISTRIP                     0x05u
#define BC250_GFX_PRIM_RECTLIST                     0x11u

/*
 * Status de Retorno do Primitive Assembler
 */
typedef enum BC250_GFX_PRIM_STATUS {
    BC250_GFX_PRIM_OK = 0,
    BC250_GFX_PRIM_INVALID_ARGUMENT = 1,
    BC250_GFX_PRIM_RING_FULL = 2,
    BC250_GFX_PRIM_STREAM_MISMATCH = 3
} BC250_GFX_PRIM_STATUS;

/*
 * Pacote Compilado de Primitive Assembler
 */
typedef struct BC250_GFX_PRIMITIVE_PACKET {
    uint32_t Topology;      /* Topologia VGT (e.g. BC250_GFX_PRIM_TRILIST = 0x04) */
    uint32_t NumInstances;  /* Contagem de instancias (minimo 1) */
} BC250_GFX_PRIMITIVE_PACKET;

/*
 * Funcoes de Contrato do Primitive Assembler
 */
BC250_GFX_PRIM_STATUS Bc250GfxComputePrimitiveAssembler(
    uint32_t Topology,
    uint32_t NumInstances,
    BC250_GFX_PRIMITIVE_PACKET *PacketOut);

BC250_GFX_PRIM_STATUS Bc250GfxEmitPrimitiveAssemblerStream(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    const BC250_GFX_PRIMITIVE_PACKET *Packet);

BC250_GFX_PRIM_STATUS Bc250GfxVerifyPrimitiveAssemblerStream(
    const uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t CursorDwords,
    const BC250_GFX_PRIMITIVE_PACKET *Packet);

const char *Bc250GfxPrimitiveStatusName(BC250_GFX_PRIM_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif /* BC250_GFX_PRIMITIVE_ASSEMBLER_H */
