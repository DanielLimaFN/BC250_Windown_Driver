#ifndef BC250_DXBC_SPIRV_BRIDGE_H
#define BC250_DXBC_SPIRV_BRIDGE_H

#include "bc250_dxbc_parser.h"

#define BC250_SPIRV_MAGIC 0x07230203u
#define BC250_SPIRV_MAX_BYTES (16u * 1024u * 1024u)

typedef enum BC250_DXBC_SPIRV_STATUS {
    BC250_DXBC_SPIRV_OK = 0,
    BC250_DXBC_SPIRV_BAD_ARGUMENT,
    BC250_DXBC_SPIRV_INVALID_DXBC,
    BC250_DXBC_SPIRV_TRANSLATOR_FAILED,
    BC250_DXBC_SPIRV_INVALID_OUTPUT
} BC250_DXBC_SPIRV_STATUS;

typedef int (*BC250_DXBC_SPIRV_COMPILE_FN)(
    void *Context, const void *Dxbc, uint32_t DxbcBytes,
    const void **Spirv, uint32_t *SpirvBytes);
typedef void (*BC250_DXBC_SPIRV_FREE_FN)(void *Context, const void *Spirv);

typedef struct BC250_DXBC_SECTION_DESC {
    uint32_t Tag;
    const void *Data;
    uint32_t DataBytes;
} BC250_DXBC_SECTION_DESC;

typedef int (*BC250_DXBC_SERIALIZE_FN)(
    void *Context,
    size_t SectionCount,
    const BC250_DXBC_SECTION_DESC *Sections,
    const void **Dxbc,
    uint32_t *DxbcBytes);
typedef void (*BC250_DXBC_FREE_DXBC_FN)(void *Context, const void *Dxbc);

typedef struct BC250_DXBC_SIGNATURE_ENTRY {
    uint32_t SystemValue;
    uint32_t Register;
    uint8_t Mask;
} BC250_DXBC_SIGNATURE_ENTRY;

typedef struct BC250_DXBC_STAGE_SIGNATURES {
    const BC250_DXBC_SIGNATURE_ENTRY *InputSignatures;
    uint32_t NumInputSignatures;
    const BC250_DXBC_SIGNATURE_ENTRY *OutputSignatures;
    uint32_t NumOutputSignatures;
} BC250_DXBC_STAGE_SIGNATURES;

typedef struct BC250_DXBC_SPIRV_TRANSLATOR {
    void *Context;
    BC250_DXBC_SPIRV_COMPILE_FN Compile;
    BC250_DXBC_SPIRV_FREE_FN Free;
    BC250_DXBC_SERIALIZE_FN SerializeDxbc;
    BC250_DXBC_FREE_DXBC_FN FreeDxbc;
} BC250_DXBC_SPIRV_TRANSLATOR;

typedef struct BC250_DXBC_SPIRV_MODULE {
    const uint32_t *Words;
    uint32_t SizeBytes;
    BC250_DXBC_SHADER_TYPE Stage;
    BC250_DXBC_SPIRV_TRANSLATOR Owner;
} BC250_DXBC_SPIRV_MODULE;

BC250_DXBC_SPIRV_STATUS Bc250DxbcTranslateToSpirv(
    const void *Dxbc, uint32_t DxbcBytes,
    const BC250_DXBC_SPIRV_TRANSLATOR *Translator,
    BC250_DXBC_SPIRV_MODULE *Module);
BC250_DXBC_SPIRV_STATUS Bc250DxbcTranslateTokenStreamToSpirv(
    const uint32_t *ShaderCode,
    const BC250_DXBC_STAGE_SIGNATURES *Signatures,
    const BC250_DXBC_SPIRV_TRANSLATOR *Translator,
    BC250_DXBC_SPIRV_MODULE *Module);
void Bc250DxbcReleaseSpirv(BC250_DXBC_SPIRV_MODULE *Module);

#endif
