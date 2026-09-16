/*
 * bc250_gfx_primitive_assembler.c - Contrato de Primitive Assembler e Topologia PM4 (B.3.2)
 *
 * Implementacao com validacao fail-closed (Lei 4), reuso mandatorio de emissores
 * PM4 canonicos (Bc250Pm4EmitSetUconfigReg e Bc250Pm4EmitNop) e verificacao estrita
 * de integridade de stream com suporte a buffer circular.
 *
 * Alvo: AMD BC-250 (GC 10.1.3 / Cyan Skillfish 2)
 */

#include "bc250_gfx_primitive_assembler.h"

BC250_GFX_PRIM_STATUS Bc250GfxComputePrimitiveAssembler(
    uint32_t Topology,
    uint32_t NumInstances,
    BC250_GFX_PRIMITIVE_PACKET *PacketOut)
{
    if (PacketOut == NULL) {
        return BC250_GFX_PRIM_INVALID_ARGUMENT;
    }

    /* Validacao fail-closed de contagem de instancias */
    if (NumInstances == 0u) {
        return BC250_GFX_PRIM_INVALID_ARGUMENT;
    }

    /* Validacao fail-closed de topologia de primitiva */
    switch (Topology) {
    case BC250_GFX_PRIM_POINTLIST:
    case BC250_GFX_PRIM_LINELIST:
    case BC250_GFX_PRIM_LINESTRIP:
    case BC250_GFX_PRIM_TRILIST:
    case BC250_GFX_PRIM_TRISTRIP:
    case BC250_GFX_PRIM_RECTLIST:
        break;
    default:
        return BC250_GFX_PRIM_INVALID_ARGUMENT;
    }

    PacketOut->Topology = Topology;
    PacketOut->NumInstances = NumInstances;

    return BC250_GFX_PRIM_OK;
}

BC250_GFX_PRIM_STATUS Bc250GfxEmitPrimitiveAssemblerStream(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    const BC250_GFX_PRIMITIVE_PACKET *Packet)
{
    BC250_PM4_STATUS pm4Status;
    uint32_t topVal;
    uint32_t instVal;

    if (RingBuffer == NULL || CursorDwords == NULL || Packet == NULL) {
        return BC250_GFX_PRIM_INVALID_ARGUMENT;
    }

    /* Pacote 1: VGT_PRIMITIVE_TYPE (mmREG 0x2242 -> SOC15 0xC242, UCONFIG offset 0x0242, 3 DW) */
    topVal = Packet->Topology;
    pm4Status = Bc250Pm4EmitSetUconfigReg(
        RingBuffer, RingDwords, CursorDwords,
        BC250_PM4_REG_VGT_PRIMITIVE_TYPE,
        &topVal, 1u);
    if (pm4Status != BC250_PM4_OK) {
        return (pm4Status == BC250_PM4_RING_FULL) ? BC250_GFX_PRIM_RING_FULL : BC250_GFX_PRIM_INVALID_ARGUMENT;
    }

    /* Pacote 2: VGT_NUM_INSTANCES (mmREG 0x224D -> SOC15 0xC24D, UCONFIG offset 0x024D, 3 DW) */
    instVal = Packet->NumInstances;
    pm4Status = Bc250Pm4EmitSetUconfigReg(
        RingBuffer, RingDwords, CursorDwords,
        BC250_PM4_REG_VGT_NUM_INSTANCES,
        &instVal, 1u);
    if (pm4Status != BC250_PM4_OK) {
        return (pm4Status == BC250_PM4_RING_FULL) ? BC250_GFX_PRIM_RING_FULL : BC250_GFX_PRIM_INVALID_ARGUMENT;
    }

    /* Pacote 3: Padding NOP para alinhamento estrito a 4 dwords / 16 bytes (2 DW) */
    pm4Status = Bc250Pm4EmitNop(RingBuffer, RingDwords, CursorDwords, 1u);
    if (pm4Status != BC250_PM4_OK) {
        return (pm4Status == BC250_PM4_RING_FULL) ? BC250_GFX_PRIM_RING_FULL : BC250_GFX_PRIM_INVALID_ARGUMENT;
    }

    return BC250_GFX_PRIM_OK;
}

BC250_GFX_PRIM_STATUS Bc250GfxVerifyPrimitiveAssemblerStream(
    const uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t CursorDwords,
    const BC250_GFX_PRIMITIVE_PACKET *Packet)
{
    uint32_t mask;
    uint32_t startCursor;

    if (RingBuffer == NULL || Packet == NULL || RingDwords == 0 ||
        (RingDwords & (RingDwords - 1u)) != 0) {
        return BC250_GFX_PRIM_INVALID_ARGUMENT;
    }

    mask = RingDwords - 1u;
    startCursor = CursorDwords - BC250_GFX_PRIM_STREAM_TOTAL_DWORDS;

    /* Pacote 1: VGT_PRIMITIVE_TYPE (3 dwords: Header 0xC0017900 + Offset 0x0242 + Topology) */
    if (RingBuffer[(startCursor + 0u) & mask] != BC250_GFX_PM4_SET_UCONFIG_HEADER_1DW) return BC250_GFX_PRIM_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 1u) & mask] != 0x0242u) return BC250_GFX_PRIM_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 2u) & mask] != Packet->Topology) return BC250_GFX_PRIM_STREAM_MISMATCH;

    /* Pacote 2: VGT_NUM_INSTANCES (3 dwords: Header 0xC0017900 + Offset 0x024D + NumInstances) */
    if (RingBuffer[(startCursor + 3u) & mask] != BC250_GFX_PM4_SET_UCONFIG_HEADER_1DW) return BC250_GFX_PRIM_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 4u) & mask] != 0x024Du) return BC250_GFX_PRIM_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 5u) & mask] != Packet->NumInstances) return BC250_GFX_PRIM_STREAM_MISMATCH;

    /* Pacote 3: Padding NOP (2 dwords: Header 0xC0001000 + 0x00000000) */
    if (RingBuffer[(startCursor + 6u) & mask] != BC250_GFX_PM4_NOP_HEADER_1DW) return BC250_GFX_PRIM_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 7u) & mask] != 0x00000000u) return BC250_GFX_PRIM_STREAM_MISMATCH;

    return BC250_GFX_PRIM_OK;
}

const char *Bc250GfxPrimitiveStatusName(BC250_GFX_PRIM_STATUS Status)
{
    switch (Status) {
    case BC250_GFX_PRIM_OK:               return "OK";
    case BC250_GFX_PRIM_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case BC250_GFX_PRIM_RING_FULL:        return "RING_FULL";
    case BC250_GFX_PRIM_STREAM_MISMATCH:  return "STREAM_MISMATCH";
    default:                              return "UNKNOWN";
    }
}
