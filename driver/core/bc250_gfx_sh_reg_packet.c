#include "bc250_gfx_sh_reg_packet.h"

static void Bc250GfxShRegZeroMemory(void *Ptr, uint32_t SizeBytes)
{
    volatile unsigned char *p = (volatile unsigned char *)Ptr;
    uint32_t i;
    if (p == NULL) return;
    for (i = 0; i < SizeBytes; ++i) {
        p[i] = 0u;
    }
}

static uint32_t Bc250GfxShRegsValid(const BC250_GFX_SHADER_REGS *Regs)
{
    if (Regs == NULL) return 0u;
    /* PgmLo == 0 e' arquiteturalmente nulo/invalido como endereco de microcodigo.
     * Rsrc1 == 0 e' valido em shaders minimos ou sem configuracao estendida. */
    if (Regs->PgmLo == 0u) return 0u;
    return 1u;
}

BC250_GFX_SH_REG_STATUS Bc250GfxShRegEmitVs(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    const BC250_GFX_SHADER_REGS *VsRegs)
{
    uint32_t header;
    uint32_t vsValues[4];
    BC250_PM4_STATUS pm4Status;

    if (RingBuffer == NULL || CursorDwords == NULL || VsRegs == NULL) {
        return BC250_GFX_SH_REG_BAD_ARGUMENT;
    }

    if (!Bc250GfxShRegsValid(VsRegs)) {
        return BC250_GFX_SH_REG_INVALID_REGS;
    }

    if (RingDwords < BC250_GFX_PM4_SHADER_DWORDS_PER_STAGE) {
        return BC250_GFX_SH_REG_RING_FULL;
    }

    header = BC250_GFX_PM4_SET_SH_REG_HEADER_4DW;
    if ((header & BC250_GFX_SH_REG_COMPUTE_BIT_FORBIDDEN) != 0u) {
        return BC250_GFX_SH_REG_BAD_ARGUMENT;
    }

    vsValues[0] = VsRegs->PgmLo;
    vsValues[1] = VsRegs->PgmHi;
    vsValues[2] = VsRegs->Rsrc1;
    vsValues[3] = VsRegs->Rsrc2;

    pm4Status = Bc250Pm4EmitSetShReg(RingBuffer, RingDwords, CursorDwords,
                                     BC250_GFX_SH_PGM_LO_VS, vsValues, 4u);
    if (pm4Status == BC250_PM4_RING_FULL) {
        return BC250_GFX_SH_REG_RING_FULL;
    }
    if (pm4Status != BC250_PM4_OK) {
        return BC250_GFX_SH_REG_BAD_ARGUMENT;
    }

    return BC250_GFX_SH_REG_OK;
}

BC250_GFX_SH_REG_STATUS Bc250GfxShRegEmitPs(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    const BC250_GFX_SHADER_REGS *PsRegs)
{
    uint32_t header;
    uint32_t psValues[4];
    BC250_PM4_STATUS pm4Status;

    if (RingBuffer == NULL || CursorDwords == NULL || PsRegs == NULL) {
        return BC250_GFX_SH_REG_BAD_ARGUMENT;
    }

    if (!Bc250GfxShRegsValid(PsRegs)) {
        return BC250_GFX_SH_REG_INVALID_REGS;
    }

    if (RingDwords < BC250_GFX_PM4_SHADER_DWORDS_PER_STAGE) {
        return BC250_GFX_SH_REG_RING_FULL;
    }

    header = BC250_GFX_PM4_SET_SH_REG_HEADER_4DW;
    if ((header & BC250_GFX_SH_REG_COMPUTE_BIT_FORBIDDEN) != 0u) {
        return BC250_GFX_SH_REG_BAD_ARGUMENT;
    }

    psValues[0] = PsRegs->PgmLo;
    psValues[1] = PsRegs->PgmHi;
    psValues[2] = PsRegs->Rsrc1;
    psValues[3] = PsRegs->Rsrc2;

    pm4Status = Bc250Pm4EmitSetShReg(RingBuffer, RingDwords, CursorDwords,
                                     BC250_GFX_SH_PGM_LO_PS, psValues, 4u);
    if (pm4Status == BC250_PM4_RING_FULL) {
        return BC250_GFX_SH_REG_RING_FULL;
    }
    if (pm4Status != BC250_PM4_OK) {
        return BC250_GFX_SH_REG_BAD_ARGUMENT;
    }

    return BC250_GFX_SH_REG_OK;
}

BC250_GFX_SH_REG_STATUS Bc250GfxShRegEmitPipeline(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    const BC250_GFX_SHADER_REGS *VsRegs,
    const BC250_GFX_SHADER_REGS *PsRegs,
    BC250_GFX_SH_REG_EMISSION_EVIDENCE *EvidenceOut)
{
    uint32_t startCursor;
    BC250_GFX_SH_REG_STATUS status;

    if (EvidenceOut != NULL) {
        Bc250GfxShRegZeroMemory(EvidenceOut, (uint32_t)sizeof(*EvidenceOut));
    }

    if (RingBuffer == NULL || CursorDwords == NULL || VsRegs == NULL ||
        PsRegs == NULL || EvidenceOut == NULL) {
        return BC250_GFX_SH_REG_BAD_ARGUMENT;
    }

    if (!Bc250GfxShRegsValid(VsRegs) || !Bc250GfxShRegsValid(PsRegs)) {
        return BC250_GFX_SH_REG_INVALID_REGS;
    }

    if (RingDwords < BC250_GFX_PM4_PIPELINE_SHADER_TOTAL_DWORDS) {
        return BC250_GFX_SH_REG_RING_FULL;
    }

    startCursor = *CursorDwords;

    status = Bc250GfxShRegEmitVs(RingBuffer, RingDwords, CursorDwords, VsRegs);
    if (status != BC250_GFX_SH_REG_OK) {
        *CursorDwords = startCursor;
        return status;
    }

    status = Bc250GfxShRegEmitPs(RingBuffer, RingDwords, CursorDwords, PsRegs);
    if (status != BC250_GFX_SH_REG_OK) {
        *CursorDwords = startCursor;
        return status;
    }

    EvidenceOut->VsEmittedDwords = BC250_GFX_PM4_SHADER_DWORDS_PER_STAGE;
    EvidenceOut->PsEmittedDwords = BC250_GFX_PM4_SHADER_DWORDS_PER_STAGE;
    EvidenceOut->TotalEmittedDwords = BC250_GFX_PM4_PIPELINE_SHADER_TOTAL_DWORDS;
    EvidenceOut->VsHeaderEmitted = BC250_GFX_PM4_SET_SH_REG_HEADER_4DW;
    EvidenceOut->PsHeaderEmitted = BC250_GFX_PM4_SET_SH_REG_HEADER_4DW;
    EvidenceOut->EmissionSuccessful = 1u;

    return BC250_GFX_SH_REG_OK;
}

BC250_GFX_SH_REG_STATUS Bc250GfxShRegVerifyStream(
    const uint32_t *PacketStream,
    uint32_t DwordCount,
    const BC250_GFX_SHADER_REGS *VsRegs,
    const BC250_GFX_SHADER_REGS *PsRegs)
{
    if (PacketStream == NULL || VsRegs == NULL || PsRegs == NULL) {
        return BC250_GFX_SH_REG_BAD_ARGUMENT;
    }

    if (DwordCount != BC250_GFX_PM4_PIPELINE_SHADER_TOTAL_DWORDS) {
        return BC250_GFX_SH_REG_STREAM_MISMATCH;
    }

    /* 1. Verificacao do bloco do Vertex Shader */
    if (PacketStream[0] != BC250_GFX_PM4_SET_SH_REG_HEADER_4DW) {
        return BC250_GFX_SH_REG_STREAM_MISMATCH;
    }
    if ((PacketStream[0] & BC250_GFX_SH_REG_COMPUTE_BIT_FORBIDDEN) != 0u) {
        return BC250_GFX_SH_REG_STREAM_MISMATCH;
    }
    if (PacketStream[1] != BC250_GFX_SH_PGM_LO_VS) {
        return BC250_GFX_SH_REG_STREAM_MISMATCH;
    }
    if (PacketStream[2] != VsRegs->PgmLo ||
        PacketStream[3] != VsRegs->PgmHi ||
        PacketStream[4] != VsRegs->Rsrc1 ||
        PacketStream[5] != VsRegs->Rsrc2) {
        return BC250_GFX_SH_REG_STREAM_MISMATCH;
    }

    /* 2. Verificacao do bloco do Pixel Shader */
    if (PacketStream[6] != BC250_GFX_PM4_SET_SH_REG_HEADER_4DW) {
        return BC250_GFX_SH_REG_STREAM_MISMATCH;
    }
    if ((PacketStream[6] & BC250_GFX_SH_REG_COMPUTE_BIT_FORBIDDEN) != 0u) {
        return BC250_GFX_SH_REG_STREAM_MISMATCH;
    }
    if (PacketStream[7] != BC250_GFX_SH_PGM_LO_PS) {
        return BC250_GFX_SH_REG_STREAM_MISMATCH;
    }
    if (PacketStream[8] != PsRegs->PgmLo ||
        PacketStream[9] != PsRegs->PgmHi ||
        PacketStream[10] != PsRegs->Rsrc1 ||
        PacketStream[11] != PsRegs->Rsrc2) {
        return BC250_GFX_SH_REG_STREAM_MISMATCH;
    }

    return BC250_GFX_SH_REG_OK;
}

const char *Bc250GfxShRegStatusName(BC250_GFX_SH_REG_STATUS Status)
{
    switch (Status) {
    case BC250_GFX_SH_REG_OK:
        return "BC250_GFX_SH_REG_OK";
    case BC250_GFX_SH_REG_BAD_ARGUMENT:
        return "BC250_GFX_SH_REG_BAD_ARGUMENT";
    case BC250_GFX_SH_REG_RING_FULL:
        return "BC250_GFX_SH_REG_RING_FULL";
    case BC250_GFX_SH_REG_INVALID_REGS:
        return "BC250_GFX_SH_REG_INVALID_REGS";
    case BC250_GFX_SH_REG_STREAM_MISMATCH:
        return "BC250_GFX_SH_REG_STREAM_MISMATCH";
    default:
        return "BC250_GFX_SH_REG_UNKNOWN";
    }
}
