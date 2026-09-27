/*
 * bc250_gfx_viewport_scissor.h - Contrato de Viewport, Scissor e Contexto PM4 (B.3.1)
 *
 * Define o contrato matematico e de empacotamento PM4 Type-3 (SET_CONTEXT_REG, IT=0x69)
 * para transformacao de coordenadas de tela, recorte e rasterizacao no GFX10.
 *
 * Registradores envolvidos (base 0xA000, BASE_IDX=1):
 *   PA_CL_VPORT_XSCALE..ZOFFSET (0x010F..0x0114, 6 dwords contiguos)
 *   PA_SU_SC_MODE_CNTL          (0x0205, 1 dword)
 *   PA_CL_VTE_CNTL              (0x0206, 1 dword)
 *   PA_SC_WINDOW_SCISSOR_TL/BR  (0x0081..0x0082, 2 dwords contiguos)
 *   PA_SC_VPORT_SCISSOR_0_TL/BR (0x0094..0x0095, 2 dwords contiguos)
 *
 * Alvo: AMD BC-250 (GC 10.1.3 / Cyan Skillfish 2)
 */

#ifndef BC250_GFX_VIEWPORT_SCISSOR_H
#define BC250_GFX_VIEWPORT_SCISSOR_H

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
 * Opcode e Headers PM4 Canonicos para Registradores de Contexto (IT=0x69)
 */
#define BC250_GFX_PM4_SET_CONTEXT_REG_OPCODE        0x69u
#define BC250_GFX_PM4_SET_CTX_VPORT_HEADER_6DW      0xC0066900u /* Header: count = 7 - 1 = 6 (1 offset + 6 regs) */
#define BC250_GFX_PM4_SET_CTX_RASTER_HEADER_2DW     0xC0026900u /* Header: count = 3 - 1 = 2 (1 offset + 2 regs) */
#define BC250_GFX_PM4_SET_CTX_WIN_SCISSOR_HEADER_2DW 0xC0026900u /* Header: count = 3 - 1 = 2 (1 offset + 2 regs) */
#define BC250_GFX_PM4_SET_CTX_VP_SCISSOR_HEADER_2DW  0xC0026900u /* Header: count = 3 - 1 = 2 (1 offset + 2 regs) */

/* Contagem de dwords por bloco e total de stream */
#define BC250_GFX_VPORT_TRANSFORM_DWORDS            6u
#define BC250_GFX_VPORT_RASTER_DWORDS               2u
#define BC250_GFX_VPORT_WIN_SCISSOR_DWORDS          2u
#define BC250_GFX_VPORT_VP_SCISSOR_DWORDS           2u

/*
 * Total emitido no anel pelo stream completo:
 * Bloco 1 (VPORT):       1 header + 1 reg + 6 dados = 8 dwords
 * Bloco 2 (RASTER/VTE):  1 header + 1 reg + 2 dados = 4 dwords
 * Bloco 3 (WIN_SCISSOR): 1 header + 1 reg + 2 dados = 4 dwords
 * Bloco 4 (VP_SCISSOR):  1 header + 1 reg + 2 dados = 4 dwords
 * Total = 8 + 4 + 4 + 4 = exatamente 20 dwords (alinhado a 4 dwords / 16 bytes).
 */
#define BC250_GFX_VPORT_STREAM_TOTAL_DWORDS         20u

/*
 * Valores Canonicos de Registradores de Controle
 */
#define BC250_GFX_VTE_CNTL_DEFAULT                  0x0000043Fu /* VPORT X/Y/Z SCALE/OFFSET ENA + VTX_XY_FMT + VTX_Z_FMT */
#define BC250_GFX_SU_SC_MODE_CNTL_DEFAULT           0x00000000u /* Sem culling, modo solido, facing padrao */
#define BC250_GFX_SCISSOR_OFFSET_DISABLE            0x80000000u /* Bit 31: WINDOW_OFFSET_DISABLE */

/* Limites arquiteturais para validacao fail-closed */
#define BC250_GFX_MAX_VIEWPORT_DIM                  16384.0f
#define BC250_GFX_MAX_SCISSOR_COORD                 16384u

typedef enum BC250_GFX_VPORT_STATUS {
    BC250_GFX_VPORT_OK = 0,
    BC250_GFX_VPORT_INVALID_ARGUMENT,
    BC250_GFX_VPORT_OUT_OF_BOUNDS,
    BC250_GFX_VPORT_RING_FULL,
    BC250_GFX_VPORT_STREAM_MISMATCH
} BC250_GFX_VPORT_STATUS;

/*
 * Dimensoes logicas da Viewport em ponto flutuante
 */
typedef struct BC250_GFX_VIEWPORT_DIMS {
    float X;
    float Y;
    float Width;
    float Height;
    float MinDepth;
    float MaxDepth;
} BC250_GFX_VIEWPORT_DIMS;

/*
 * Retangulo logico de corte (Scissor)
 */
typedef struct BC250_GFX_SCISSOR_RECT {
    uint32_t Left;
    uint32_t Top;
    uint32_t Right;
    uint32_t Bottom;
} BC250_GFX_SCISSOR_RECT;

/*
 * Pacote de registradores de contexto compilados para Viewport e Scissor
 */
typedef struct BC250_GFX_VIEWPORT_SCISSOR_PACKET {
    /* Bloco 1: Transformacao de Viewport (0x010F..0x0114) */
    uint32_t VportXScale;    /* float bitcast: Width / 2.0f */
    uint32_t VportXOffset;   /* float bitcast: X + Width / 2.0f */
    uint32_t VportYScale;    /* float bitcast: -Height / 2.0f (DirectX Y-down) */
    uint32_t VportYOffset;   /* float bitcast: Y + Height / 2.0f */
    uint32_t VportZScale;    /* float bitcast: MaxDepth - MinDepth */
    uint32_t VportZOffset;   /* float bitcast: MinDepth */

    /* Bloco 2: Controle de Raster e VTE (0x0205..0x0206) */
    uint32_t SuScModeCntl;   /* PA_SU_SC_MODE_CNTL */
    uint32_t VteCntl;        /* PA_CL_VTE_CNTL */

    /* Bloco 3: Window Scissor (0x0081..0x0082) */
    uint32_t WindowScissorTl;
    uint32_t WindowScissorBr;

    /* Bloco 4: Viewport Scissor 0 (0x0094..0x0095) */
    uint32_t VportScissor0Tl;
    uint32_t VportScissor0Br;
} BC250_GFX_VIEWPORT_SCISSOR_PACKET;

/*
 * Calcula e valida os registradores de Viewport e Scissor a partir das dimensoes.
 * Realiza validacao fail-closed contra NaN, Infinitos, dimensoes negativas e fora dos limites.
 */
BC250_GFX_VPORT_STATUS Bc250GfxComputeViewportScissor(
    const BC250_GFX_VIEWPORT_DIMS *Dims,
    const BC250_GFX_SCISSOR_RECT *Scissor,
    BC250_GFX_VIEWPORT_SCISSOR_PACKET *PacketOut);

/*
 * Emite a sequencia completa de 4 pacotes SET_CONTEXT_REG (20 dwords) no anel grafico.
 * Utiliza estritamente Bc250Pm4EmitSetContextReg garantindo suporte nativo a wrap-around circular.
 */
BC250_GFX_VPORT_STATUS Bc250GfxEmitViewportScissorStream(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    const BC250_GFX_VIEWPORT_SCISSOR_PACKET *Packet);

/*
 * Verifica a integridade dos 20 dwords emitidos no anel contra os valores esperados.
 */
BC250_GFX_VPORT_STATUS Bc250GfxVerifyViewportScissorStream(
    const uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t CursorDwords,
    const BC250_GFX_VIEWPORT_SCISSOR_PACKET *Packet);

const char *Bc250GfxVportStatusName(BC250_GFX_VPORT_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif /* BC250_GFX_VIEWPORT_SCISSOR_H */
