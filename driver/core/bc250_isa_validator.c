#include "bc250_isa_validator.h"

#include <string.h>

BC250_ISA_STATUS Bc250ValidateComputeShaderIsa(
    const uint32_t *ShaderDwords,
    uint32_t ShaderDwordCount,
    const BC250_SHADER_RESOURCE_CONFIG *Config,
    BC250_SHADER_VALIDATION_RESULT *Result)
{
    uint32_t i;
    uint32_t hasEndPgm = 0;

    if (ShaderDwords == NULL || Config == NULL || Result == NULL) {
        return BC250_ISA_BAD_ARGUMENT;
    }

    if (ShaderDwordCount == 0) {
        return BC250_ISA_EMPTY_SHADER;
    }

    if (Config->NumSgprs > BC250_MAX_SGPRS_PER_SIMD) {
        return BC250_ISA_EXCEEDS_SGPR_LIMIT;
    }

    if (Config->WaveSize == BC250_WAVE32) {
        if (Config->NumVgprs > BC250_MAX_VGPRS_WAVE32) {
            return BC250_ISA_EXCEEDS_VGPR_LIMIT;
        }
    } else {
        if (Config->NumVgprs > BC250_MAX_VGPRS_WAVE64) {
            return BC250_ISA_EXCEEDS_VGPR_LIMIT;
        }
    }

    if (Config->LdsSizeBytes > BC250_MAX_LDS_BYTES_PER_WGP) {
        return BC250_ISA_EXCEEDS_LDS_LIMIT;
    }

    for (i = 0; i < ShaderDwordCount; ++i) {
        if (ShaderDwords[i] == BC250_OPCODE_S_ENDPGM) {
            hasEndPgm = 1;
            break;
        }
    }

    if (!hasEndPgm) {
        return BC250_ISA_MISSING_ENDPGM;
    }

    memset(Result, 0, sizeof(BC250_SHADER_VALIDATION_RESULT));
    Result->InstructionCount = ShaderDwordCount;
    Result->HasEndPgm = 1u;
    Result->IsValid = 1u;

    return BC250_ISA_OK;
}

BC250_ISA_STATUS Bc250BuildComputeDispatchPacket(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t DimX,
    uint32_t DimY,
    uint32_t DimZ)
{
    return Bc250BuildComputeDispatchPacketEx(
        RingBuffer,
        RingDwords,
        CursorDwords,
        DimX,
        DimY,
        DimZ,
        BC250_DISPATCH_COMPUTE_SHADER_EN |
        BC250_DISPATCH_FORCE_START_000);
}

BC250_ISA_STATUS Bc250BuildComputeDispatchPacketEx(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t DimX,
    uint32_t DimY,
    uint32_t DimZ,
    uint32_t Initiator)
{
    if (RingBuffer == NULL || CursorDwords == NULL) {
        return BC250_ISA_BAD_ARGUMENT;
    }

    if (DimX == 0 || DimY == 0 || DimZ == 0) {
        return BC250_ISA_BAD_ARGUMENT;
    }

    if ((Initiator & BC250_DISPATCH_COMPUTE_SHADER_EN) == 0u) {
        return BC250_ISA_BAD_ARGUMENT;
    }

    /* Header count=3 means FOUR payload dwords on PACKET3. */
    if (*CursorDwords + 5u > RingDwords) {
        return BC250_ISA_BUFFER_TOO_SMALL;
    }

    RingBuffer[(*CursorDwords)++] = BC250_PM4_HEADER_DISPATCH_DIRECT;
    RingBuffer[(*CursorDwords)++] = DimX;
    RingBuffer[(*CursorDwords)++] = DimY;
    RingBuffer[(*CursorDwords)++] = DimZ;
    RingBuffer[(*CursorDwords)++] = Initiator;

    return BC250_ISA_OK;
}

const char *Bc250IsaStatusName(BC250_ISA_STATUS Status)
{
    switch (Status) {
    case BC250_ISA_OK: return "ok";
    case BC250_ISA_BAD_ARGUMENT: return "bad_argument";
    case BC250_ISA_EMPTY_SHADER: return "empty_shader";
    case BC250_ISA_MISALIGNED_SHADER: return "misaligned_shader";
    case BC250_ISA_EXCEEDS_SGPR_LIMIT: return "exceeds_sgpr_limit";
    case BC250_ISA_EXCEEDS_VGPR_LIMIT: return "exceeds_vgpr_limit";
    case BC250_ISA_EXCEEDS_LDS_LIMIT: return "exceeds_lds_limit";
    case BC250_ISA_MISSING_ENDPGM: return "missing_endpgm";
    case BC250_ISA_BUFFER_TOO_SMALL: return "buffer_too_small";
    default: return "unknown";
    }
}
