#include "bc250_dxbc_spirv_bridge.h"
#include <string.h>

BC250_DXBC_SPIRV_STATUS Bc250DxbcTranslateToSpirv(
    const void *Dxbc, uint32_t DxbcBytes,
    const BC250_DXBC_SPIRV_TRANSLATOR *Translator,
    BC250_DXBC_SPIRV_MODULE *Module)
{
    BC250_DXBC_CONTAINER container;
    const void *output = NULL;
    uint32_t outputBytes = 0;
    const uint32_t *words;

    if (!Dxbc || !Translator || !Translator->Compile || !Translator->Free || !Module) {
        return BC250_DXBC_SPIRV_BAD_ARGUMENT;
    }
    memset(Module, 0, sizeof(*Module));
    if (Bc250DxbcParse(Dxbc, DxbcBytes, &container) != BC250_DXBC_OK) {
        return BC250_DXBC_SPIRV_INVALID_DXBC;
    }
    if (Translator->Compile(Translator->Context, Dxbc, DxbcBytes,
            &output, &outputBytes) != 0) {
        return BC250_DXBC_SPIRV_TRANSLATOR_FAILED;
    }
    if (!output || outputBytes < 20u || outputBytes > BC250_SPIRV_MAX_BYTES ||
        (outputBytes & 3u) != 0u) {
        if (output) Translator->Free(Translator->Context, output);
        return BC250_DXBC_SPIRV_INVALID_OUTPUT;
    }
    words = (const uint32_t *)output;
    if (words[0] != BC250_SPIRV_MAGIC ||
        words[1] < 0x00010000u || words[1] > 0x00010600u ||
        words[3] == 0u || words[4] != 0u) {
        Translator->Free(Translator->Context, output);
        return BC250_DXBC_SPIRV_INVALID_OUTPUT;
    }
    Module->Words = words;
    Module->SizeBytes = outputBytes;
    Module->Stage = container.ShaderType;
    Module->Owner = *Translator;
    return BC250_DXBC_SPIRV_OK;
}

void Bc250DxbcReleaseSpirv(BC250_DXBC_SPIRV_MODULE *Module)
{
    if (!Module) return;
    if (Module->Words && Module->Owner.Free) {
        Module->Owner.Free(Module->Owner.Context, Module->Words);
    }
    memset(Module, 0, sizeof(*Module));
}

static size_t Bc250LocalStrLen(const char *s)
{
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static void Bc250LocalMemCopy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;
    for (i = 0; i < n; ++i) d[i] = s[i];
}

static uint32_t Bc250DxbcBuildSignatureChunk(
    const BC250_DXBC_SIGNATURE_ENTRY *Entries,
    uint32_t NumEntries,
    int IsPixelShaderOutput,
    uint8_t *Buffer,
    uint32_t BufferCapacity)
{
    uint32_t headerSize;
    uint32_t stringOffset;
    uint32_t i;
    uint32_t totalSize;

    if (NumEntries == 0 || Buffer == NULL) return 0;
    headerSize = 8u + NumEntries * 24u;
    if (headerSize > BufferCapacity) return 0;

    *(uint32_t *)(Buffer + 0) = NumEntries;
    *(uint32_t *)(Buffer + 4) = 8u;

    stringOffset = headerSize;
    for (i = 0; i < NumEntries; ++i) {
        const char *name;
        size_t nameLen;
        uint32_t compType = 3u; /* float */
        uint32_t sysVal = Entries[i].SystemValue;
        uint8_t mask = Entries[i].Mask ? Entries[i].Mask : 0xFu;
        uint8_t *entry;

        if (IsPixelShaderOutput) {
            name = "SV_Target";
        } else {
            switch (sysVal) {
            case 1u: name = "SV_Position"; break;
            case 2u: name = "SV_ClipDistance"; break;
            case 3u: name = "SV_CullDistance"; break;
            case 4u: name = "SV_RenderTargetArrayIndex"; compType = 1u; break;
            case 5u: name = "SV_ViewportArrayIndex"; compType = 1u; break;
            case 6u: name = "SV_VertexID"; compType = 1u; break;
            case 7u: name = "SV_PrimitiveID"; compType = 1u; break;
            case 8u: name = "SV_InstanceID"; compType = 1u; break;
            case 9u: name = "SV_IsFrontFace"; compType = 1u; break;
            case 10u: name = "SV_SampleIndex"; compType = 1u; break;
            default: name = "TEXCOORD"; break;
            }
        }

        nameLen = Bc250LocalStrLen(name) + 1u;
        if (stringOffset + nameLen > BufferCapacity) return 0;

        Bc250LocalMemCopy(Buffer + stringOffset, name, nameLen);

        entry = Buffer + 8u + i * 24u;
        *(uint32_t *)(entry + 0) = stringOffset;
        *(uint32_t *)(entry + 4) = (sysVal == 0u) ? Entries[i].Register : 0u;
        *(uint32_t *)(entry + 8) = sysVal;
        *(uint32_t *)(entry + 12) = compType;
        *(uint32_t *)(entry + 16) = Entries[i].Register;
        entry[20] = mask;
        entry[21] = mask;
        entry[22] = 0;
        entry[23] = 0;

        stringOffset += (uint32_t)nameLen;
    }

    totalSize = (stringOffset + 3u) & ~3u;
    if (totalSize > BufferCapacity) return 0;
    while (stringOffset < totalSize) {
        Buffer[stringOffset++] = 0;
    }
    return totalSize;
}

BC250_DXBC_SPIRV_STATUS Bc250DxbcTranslateTokenStreamToSpirv(
    const uint32_t *ShaderCode,
    const BC250_DXBC_STAGE_SIGNATURES *Signatures,
    const BC250_DXBC_SPIRV_TRANSLATOR *Translator,
    BC250_DXBC_SPIRV_MODULE *Module)
{
    uint32_t versionToken, tokenCount, majorVer, shaderType;
    BC250_DXBC_SHADER_TYPE stage;
    uint32_t shaderTag;
    uint8_t isgnBuffer[1024];
    uint8_t osgnBuffer[1024];
    uint32_t isgnSize = 0, osgnSize = 0;
    BC250_DXBC_SECTION_DESC sections[4];
    size_t sectionCount = 0;
    const void *serializedDxbc = NULL;
    uint32_t serializedBytes = 0;
    BC250_DXBC_SPIRV_STATUS status;

    if (!ShaderCode || !Translator || !Translator->Compile ||
        !Translator->Free || !Translator->SerializeDxbc ||
        !Translator->FreeDxbc || !Module) {
        return BC250_DXBC_SPIRV_BAD_ARGUMENT;
    }

    versionToken = ShaderCode[0];
    tokenCount = ShaderCode[1];
    if (tokenCount < 2u) {
        return BC250_DXBC_SPIRV_INVALID_DXBC;
    }

    shaderType = (versionToken >> 16u) & 0xFFFFu;
    majorVer = (versionToken >> 4u) & 0xFu;

    switch (shaderType) {
    case 0u: stage = BC250_DXBC_SHADER_PIXEL; break;
    case 1u: stage = BC250_DXBC_SHADER_VERTEX; break;
    case 2u: stage = BC250_DXBC_SHADER_GEOMETRY; break;
    case 4u: stage = BC250_DXBC_SHADER_COMPUTE; break;
    default: stage = BC250_DXBC_SHADER_UNKNOWN; break;
    }

    if (stage == BC250_DXBC_SHADER_UNKNOWN) {
        return BC250_DXBC_SPIRV_INVALID_DXBC;
    }

    shaderTag = (majorVer >= 5u) ? BC250_DXBC_FOURCC_SHEX : BC250_DXBC_FOURCC_SHDR;

    if (Signatures && Signatures->NumInputSignatures > 0 && Signatures->InputSignatures) {
        isgnSize = Bc250DxbcBuildSignatureChunk(
            Signatures->InputSignatures, Signatures->NumInputSignatures,
            0, isgnBuffer, sizeof(isgnBuffer));
        if (isgnSize > 0) {
            sections[sectionCount].Tag = BC250_DXBC_FOURCC_ISGN;
            sections[sectionCount].Data = isgnBuffer;
            sections[sectionCount].DataBytes = isgnSize;
            sectionCount++;
        }
    }

    if (Signatures && Signatures->NumOutputSignatures > 0 && Signatures->OutputSignatures) {
        osgnSize = Bc250DxbcBuildSignatureChunk(
            Signatures->OutputSignatures, Signatures->NumOutputSignatures,
            (stage == BC250_DXBC_SHADER_PIXEL) ? 1 : 0,
            osgnBuffer, sizeof(osgnBuffer));
        if (osgnSize > 0) {
            sections[sectionCount].Tag = BC250_DXBC_FOURCC_OSGN;
            sections[sectionCount].Data = osgnBuffer;
            sections[sectionCount].DataBytes = osgnSize;
            sectionCount++;
        }
    }

    sections[sectionCount].Tag = shaderTag;
    sections[sectionCount].Data = ShaderCode;
    sections[sectionCount].DataBytes = tokenCount * (uint32_t)sizeof(uint32_t);
    sectionCount++;

    if (Translator->SerializeDxbc(Translator->Context, sectionCount, sections,
            &serializedDxbc, &serializedBytes) != 0) {
        return BC250_DXBC_SPIRV_TRANSLATOR_FAILED;
    }

    status = Bc250DxbcTranslateToSpirv(serializedDxbc, serializedBytes, Translator, Module);
    Translator->FreeDxbc(Translator->Context, serializedDxbc);
    return status;
}
