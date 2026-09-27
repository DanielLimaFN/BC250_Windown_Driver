#ifndef BC250_GFX_SH_REG_PACKET_H
#define BC250_GFX_SH_REG_PACKET_H

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

#include "bc250_gfx_shader_contract.h"
#include "bc250_pm4_packet_builder.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * =========================================================================
 * B.2.4 -- EMISSAO PM4 SET_SH_REG PARA SHADERS GRAFICOS (VS E PS).
 *
 * POR QUE ESTE MODULO EXISTE
 *
 * Apos o calculo dos registradores SPI_SHADER_PGM_* (B.2.1), a obtencao dos
 * blobs autenticados (B.2.2) e o upload com prova de readback na VRAM (B.2.3),
 * o anel grafico precisa ser alimentado com os comandos PM4 para carregar
 * os registradores na GPU.
 *
 * PROVENIENCIA DOS PACOTES PM4 TYPE-3 (Lei 7 / Lei 8)
 *
 * Formato do pacote TYPE-3:
 *   [31:30] = 0x3 (TYPE 3)
 *   [29:16] = Count - 1 (onde Count e' o numero total de dwords apos o header)
 *   [15:8]  = Opcode (0x76 para IT_SET_SH_REG)
 *   [7:0]   = Modificadores de Shader Type:
 *             Bit 1 = SHADER_TYPE_COMPUTE (usado APENAS pela MEC / Compute)
 *
 * INVARIANTE CRITICO (docs/272 secao 5.3):
 * No anel grafico (CP_RB0), o bit SHADER_TYPE_COMPUTE (0x02) NUNCA pode ser
 * emitido. Pacotes SET_SH_REG no anel grafico com esse bit ativo enderecam
 * registradores da MEC e causam travamento de pipeline ou erro de RCIU.
 * O header para 4 registradores consecutivos e':
 *   Count = 5 dwords (1 dword de offset + 4 dwords de registradores)
 *   Header = (3 << 30) | ((5 - 1) << 16) | (0x76 << 8) | 0 = 0xC0047600u
 * =========================================================================
 */

#define BC250_GFX_PM4_SET_SH_REG_OPCODE             0x76u
#define BC250_GFX_PM4_SET_SH_REG_HEADER_4DW         0xC0047600u
#define BC250_GFX_PM4_SHADER_DWORDS_PER_STAGE       6u
#define BC250_GFX_PM4_PIPELINE_SHADER_TOTAL_DWORDS  12u
#define BC250_GFX_SH_REG_COMPUTE_BIT_FORBIDDEN      0x02u

typedef enum BC250_GFX_SH_REG_STATUS {
    BC250_GFX_SH_REG_OK = 0,
    BC250_GFX_SH_REG_BAD_ARGUMENT = 1,
    BC250_GFX_SH_REG_RING_FULL = 2,
    BC250_GFX_SH_REG_INVALID_REGS = 3,
    BC250_GFX_SH_REG_STREAM_MISMATCH = 4
} BC250_GFX_SH_REG_STATUS;

typedef struct BC250_GFX_SH_REG_EMISSION_EVIDENCE {
    uint32_t VsEmittedDwords;
    uint32_t PsEmittedDwords;
    uint32_t TotalEmittedDwords;
    uint32_t VsHeaderEmitted;
    uint32_t PsHeaderEmitted;
    uint32_t EmissionSuccessful;
} BC250_GFX_SH_REG_EMISSION_EVIDENCE;

/*
 * Emite o pacote SET_SH_REG para os 4 registradores de Vertex Shader:
 *   Offset 0x048 (SPI_SHADER_PGM_LO_VS)
 *   Valores: PgmLo, PgmHi, Rsrc1, Rsrc2
 * Consome exatamente 6 dwords no ring.
 */
BC250_GFX_SH_REG_STATUS Bc250GfxShRegEmitVs(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    const BC250_GFX_SHADER_REGS *VsRegs);

/*
 * Emite o pacote SET_SH_REG para os 4 registradores de Pixel Shader:
 *   Offset 0x008 (SPI_SHADER_PGM_LO_PS)
 *   Valores: PgmLo, PgmHi, Rsrc1, Rsrc2
 * Consome exatamente 6 dwords no ring.
 */
BC250_GFX_SH_REG_STATUS Bc250GfxShRegEmitPs(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    const BC250_GFX_SHADER_REGS *PsRegs);

/*
 * Emite o par completo de pacotes SET_SH_REG na ordem canonica do pipeline (VS entao PS).
 * Consome exatamente 12 dwords no ring e preenche EvidenceOut.
 */
BC250_GFX_SH_REG_STATUS Bc250GfxShRegEmitPipeline(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    const BC250_GFX_SHADER_REGS *VsRegs,
    const BC250_GFX_SHADER_REGS *PsRegs,
    BC250_GFX_SH_REG_EMISSION_EVIDENCE *EvidenceOut);

/*
 * Verifica se um stream emitido no ring confere exatamente com as configuracoes
 * esperadas de VS e PS, sem qualquer bit de compute e sem deslocamentos incorretos.
 */
BC250_GFX_SH_REG_STATUS Bc250GfxShRegVerifyStream(
    const uint32_t *PacketStream,
    uint32_t DwordCount,
    const BC250_GFX_SHADER_REGS *VsRegs,
    const BC250_GFX_SHADER_REGS *PsRegs);

const char *Bc250GfxShRegStatusName(BC250_GFX_SH_REG_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif /* BC250_GFX_SH_REG_PACKET_H */
