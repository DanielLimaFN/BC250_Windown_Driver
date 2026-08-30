#ifndef BC250_OBSERVABLE_SHADER_H
#define BC250_OBSERVABLE_SHADER_H

#include "bc250_compute_dispatch_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_OBSERVABLE_SHADER_DWORDS 7u
#define BC250_OBSERVABLE_SHADER_BYTES  (BC250_OBSERVABLE_SHADER_DWORDS * 4u)
#define BC250_OBSERVABLE_SHADER_SHA256_HEX \
    "b26df9cdf41dd53df889d6335790f25bdbbb12a17ca5f7b6241908994d72983b"

typedef struct BC250_OBSERVABLE_SHADER_MANIFEST {
    uint32_t TargetGfxMajor;
    uint32_t TargetGfxMinor;
    uint32_t TargetGfxStepping;
    BC250_WAVEFRONT_SIZE WaveSize;
    uint32_t NumSgprs;
    uint32_t NumVgprs;
    uint32_t UserSgprs;
    uint32_t PgmRsrc1;
    uint32_t PgmRsrc2;
    uint32_t ResultValue;
    uint32_t ShaderDwordCount;
    const uint32_t *ShaderDwords;
    const char *Sha256Hex;
    const char *CompilerProvenance;
} BC250_OBSERVABLE_SHADER_MANIFEST;

const BC250_OBSERVABLE_SHADER_MANIFEST *Bc250GetObservableShaderManifest(void);

/* Exact allowlist comparison. No arbitrary shader is accepted by this gate. */
int Bc250ValidateObservableShaderBlob(
    const uint32_t *ShaderDwords,
    uint32_t ShaderDwordCount);

/* Validates the complete fixed-function contract, including GC10.1 fields. */
int Bc250ValidateObservableShaderManifest(
    const BC250_OBSERVABLE_SHADER_MANIFEST *Manifest);

#ifdef __cplusplus
}
#endif

#endif
