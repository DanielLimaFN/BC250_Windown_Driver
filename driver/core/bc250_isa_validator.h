#ifndef BC250_ISA_VALIDATOR_H
#define BC250_ISA_VALIDATOR_H

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

#define BC250_MAX_SGPRS_PER_SIMD     108u
#define BC250_MAX_VGPRS_WAVE32       256u
#define BC250_MAX_VGPRS_WAVE64       128u
#define BC250_MAX_LDS_BYTES_PER_WGP  (64u * 1024u)
#define BC250_OPCODE_S_ENDPGM        0xBF810000u
#define BC250_PM4_HEADER_DISPATCH_DIRECT 0xC0031500u
#define BC250_DISPATCH_COMPUTE_SHADER_EN 0x00000001u
#define BC250_DISPATCH_FORCE_START_000   0x00000004u
#define BC250_DISPATCH_CS_W32_EN         0x00008000u

typedef enum BC250_WAVEFRONT_SIZE {
    BC250_WAVE32 = 0,
    BC250_WAVE64 = 1
} BC250_WAVEFRONT_SIZE;

typedef enum BC250_ISA_STATUS {
    BC250_ISA_OK = 0,
    BC250_ISA_BAD_ARGUMENT,
    BC250_ISA_EMPTY_SHADER,
    BC250_ISA_MISALIGNED_SHADER,
    BC250_ISA_EXCEEDS_SGPR_LIMIT,
    BC250_ISA_EXCEEDS_VGPR_LIMIT,
    BC250_ISA_EXCEEDS_LDS_LIMIT,
    BC250_ISA_MISSING_ENDPGM,
    BC250_ISA_BUFFER_TOO_SMALL
} BC250_ISA_STATUS;

typedef struct BC250_SHADER_RESOURCE_CONFIG {
    BC250_WAVEFRONT_SIZE WaveSize;
    uint32_t NumSgprs;
    uint32_t NumVgprs;
    uint32_t LdsSizeBytes;
    uint32_t ScratchSizeBytes;
} BC250_SHADER_RESOURCE_CONFIG;

typedef struct BC250_SHADER_VALIDATION_RESULT {
    uint32_t InstructionCount;
    uint32_t HasEndPgm;
    uint32_t IsValid;
} BC250_SHADER_VALIDATION_RESULT;

BC250_ISA_STATUS Bc250ValidateComputeShaderIsa(
    const uint32_t *ShaderDwords,
    uint32_t ShaderDwordCount,
    const BC250_SHADER_RESOURCE_CONFIG *Config,
    BC250_SHADER_VALIDATION_RESULT *Result);

BC250_ISA_STATUS Bc250BuildComputeDispatchPacket(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t DimX,
    uint32_t DimY,
    uint32_t DimZ);

BC250_ISA_STATUS Bc250BuildComputeDispatchPacketEx(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t DimX,
    uint32_t DimY,
    uint32_t DimZ,
    uint32_t Initiator);

const char *Bc250IsaStatusName(BC250_ISA_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
