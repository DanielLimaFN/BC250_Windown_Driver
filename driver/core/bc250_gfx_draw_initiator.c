/*
 * bc250_gfx_draw_initiator.c - Contrato de Draw Initiator e Disparo de Desenho PM4 (B.3.3)
 *
 * Implementacao com validacao fail-closed (Lei 4), reuso mandatorio de emissores
 * PM4 canonicos (Bc250Pm4EmitDrawIndexAuto e Bc250Pm4EmitPacket2Nop) e verificacao estrita
 * de integridade de stream com suporte a buffer circular.
 *
 * Alvo: AMD BC-250 (GC 10.1.3 / Cyan Skillfish 2)
 */

#include "bc250_gfx_draw_initiator.h"

BC250_GFX_DRAW_STATUS Bc250GfxComputeDrawInitiator(
    uint32_t VertexCount,
    uint32_t DrawInitiator,
    BC250_GFX_DRAW_PACKET *PacketOut)
{
    if (PacketOut == NULL) {
        return BC250_GFX_DRAW_INVALID_ARGUMENT;
    }

    /* Validacao fail-closed de contagem de vertices */
    if (VertexCount == 0u) {
        return BC250_GFX_DRAW_INVALID_ARGUMENT;
    }

    /* Validacao fail-closed do modo de DrawInitiator */
    switch (DrawInitiator) {
    case BC250_GFX_DRAW_INITIATOR_POINTLIST:
    case BC250_GFX_DRAW_INITIATOR_AUTO_INDEX:
    case BC250_GFX_DRAW_INITIATOR_TRIANGLELIST:
    case BC250_GFX_DRAW_INITIATOR_RECTLIST:
        break;
    default:
        return BC250_GFX_DRAW_INVALID_ARGUMENT;
    }

    PacketOut->VertexCount = VertexCount;
    PacketOut->DrawInitiator = DrawInitiator;

    return BC250_GFX_DRAW_OK;
}

BC250_GFX_DRAW_STATUS Bc250GfxEmitDrawInitiatorStream(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    const BC250_GFX_DRAW_PACKET *Packet)
{
    BC250_PM4_STATUS pm4Status;

    if (RingBuffer == NULL || CursorDwords == NULL || Packet == NULL) {
        return BC250_GFX_DRAW_INVALID_ARGUMENT;
    }

    /* Pacote 1: DRAW_INDEX_AUTO (Header 0xC0012D00 + VertexCount + DrawInitiator, 3 DW) */
    pm4Status = Bc250Pm4EmitDrawIndexAuto(
        RingBuffer, RingDwords, CursorDwords,
        Packet->VertexCount, Packet->DrawInitiator);
    if (pm4Status != BC250_PM4_OK) {
        return (pm4Status == BC250_PM4_RING_FULL) ? BC250_GFX_DRAW_RING_FULL : BC250_GFX_DRAW_INVALID_ARGUMENT;
    }

    /* Pacote 2: Padding NOP Tipo 2 para alinhamento estrito a 4 dwords / 16 bytes (1 DW) */
    pm4Status = Bc250Pm4EmitPacket2Nop(RingBuffer, RingDwords, CursorDwords);
    if (pm4Status != BC250_PM4_OK) {
        return (pm4Status == BC250_PM4_RING_FULL) ? BC250_GFX_DRAW_RING_FULL : BC250_GFX_DRAW_INVALID_ARGUMENT;
    }

    return BC250_GFX_DRAW_OK;
}

BC250_GFX_DRAW_STATUS Bc250GfxVerifyDrawInitiatorStream(
    const uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t CursorDwords,
    const BC250_GFX_DRAW_PACKET *Packet)
{
    uint32_t mask;
    uint32_t startCursor;

    if (RingBuffer == NULL || Packet == NULL || RingDwords == 0 ||
        (RingDwords & (RingDwords - 1u)) != 0) {
        return BC250_GFX_DRAW_INVALID_ARGUMENT;
    }

    mask = RingDwords - 1u;
    startCursor = CursorDwords - BC250_GFX_DRAW_STREAM_TOTAL_DWORDS;

    /* Pacote 1: DRAW_INDEX_AUTO (3 dwords: Header 0xC0012D00 + VertexCount + DrawInitiator) */
    if (RingBuffer[(startCursor + 0u) & mask] != BC250_GFX_PM4_DRAW_INDEX_AUTO_HEADER) return BC250_GFX_DRAW_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 1u) & mask] != Packet->VertexCount) return BC250_GFX_DRAW_STREAM_MISMATCH;
    if (RingBuffer[(startCursor + 2u) & mask] != Packet->DrawInitiator) return BC250_GFX_DRAW_STREAM_MISMATCH;

    /* Pacote 2: Padding NOP Tipo 2 (1 dword: 0x80000000) */
    if (RingBuffer[(startCursor + 3u) & mask] != BC250_GFX_PM4_PACKET2_NOP) return BC250_GFX_DRAW_STREAM_MISMATCH;

    return BC250_GFX_DRAW_OK;
}

const char *Bc250GfxDrawStatusName(BC250_GFX_DRAW_STATUS Status)
{
    switch (Status) {
    case BC250_GFX_DRAW_OK:               return "OK";
    case BC250_GFX_DRAW_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case BC250_GFX_DRAW_RING_FULL:        return "RING_FULL";
    case BC250_GFX_DRAW_STREAM_MISMATCH:  return "STREAM_MISMATCH";
    default:                              return "UNKNOWN";
    }
}
