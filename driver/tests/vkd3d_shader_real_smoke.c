#include "../umd/bc250_vkd3d_shader_adapter.h"
#include <stdio.h>
#include <stdlib.h>

static void *ReadShaderFile(const wchar_t *Path, unsigned int *Size)
{
    FILE *file = NULL;
    long length;
    void *data;
    if (_wfopen_s(&file, Path, L"rb") || !file) return NULL;
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0 ||
        (unsigned long)length > 0xfffffffful || fseek(file, 0, SEEK_SET)) {
        fclose(file); return NULL;
    }
    data = malloc((size_t)length);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data); fclose(file); return NULL;
    }
    fclose(file); *Size = (unsigned int)length; return data;
}

static uint32_t DecodeSignatureChunk(const BC250_DXBC_CHUNK *Chunk,
    BC250_DXBC_SIGNATURE_ENTRY *Entries, uint32_t MaxEntries)
{
    const uint8_t *data;
    uint32_t count, i;
    if (!Chunk || !Chunk->Data || Chunk->DataSize < 8u || !Entries) return 0;
    data = (const uint8_t *)Chunk->Data;
    count = *(const uint32_t *)(data + 0);
    if (count > MaxEntries) count = MaxEntries;
    if (8u + count * 24u > Chunk->DataSize) return 0;
    for (i = 0; i < count; ++i) {
        const uint8_t *entry = data + 8u + i * 24u;
        Entries[i].SystemValue = *(const uint32_t *)(entry + 8u);
        Entries[i].Register = *(const uint32_t *)(entry + 16u);
        Entries[i].Mask = entry[20];
    }
    return count;
}

static int TranslateOne(const wchar_t *Path, BC250_DXBC_SHADER_TYPE Expected,
    const BC250_DXBC_SPIRV_TRANSLATOR *Translator)
{
    BC250_DXBC_SPIRV_MODULE module;
    unsigned int size = 0;
    void *dxbc = ReadShaderFile(Path, &size);
    BC250_DXBC_SPIRV_STATUS status;
    if (!dxbc) return 0;
    status = Bc250DxbcTranslateToSpirv(dxbc, size, Translator, &module);
    if (status != BC250_DXBC_SPIRV_OK) {
        fwprintf(stderr, L"translation failed for %ls: bridge status=%u\n",
            Path, (unsigned int)status);
        free(dxbc);
        return 0;
    }
    if (module.Stage != Expected) {
        fwprintf(stderr, L"stage mismatch for %ls: got=%u expected=%u\n",
            Path, (unsigned int)module.Stage, (unsigned int)Expected);
        Bc250DxbcReleaseSpirv(&module);
        free(dxbc);
        return 0;
    }
    wprintf(L"TRANSLATED %ls: DXBC=%u SPIR-V=%u Stage=%u\n",
        Path, size, module.SizeBytes, (unsigned int)module.Stage);
    Bc250DxbcReleaseSpirv(&module);

    {
        BC250_DXBC_CONTAINER container;
        if (Bc250DxbcParse(dxbc, size, &container) == BC250_DXBC_OK &&
            container.ShaderChunk && container.ShaderChunk->Data &&
            container.ShaderChunk->DataSize >= 8u) {
            BC250_DXBC_STAGE_SIGNATURES sigs;
            BC250_DXBC_SIGNATURE_ENTRY inSig[8], outSig[8];
            BC250_DXBC_SPIRV_MODULE tokenModule;
            BC250_DXBC_SPIRV_STATUS tokenStatus;
            const uint32_t *shaderCode = (const uint32_t *)container.ShaderChunk->Data;
            memset(&sigs, 0, sizeof(sigs));
            sigs.NumInputSignatures = DecodeSignatureChunk(container.IsgnChunk, inSig, 8);
            sigs.InputSignatures = sigs.NumInputSignatures ? inSig : NULL;
            sigs.NumOutputSignatures = DecodeSignatureChunk(container.OsgnChunk, outSig, 8);
            sigs.OutputSignatures = sigs.NumOutputSignatures ? outSig : NULL;

            tokenStatus = Bc250DxbcTranslateTokenStreamToSpirv(
                shaderCode, &sigs, Translator, &tokenModule);
            if (tokenStatus != BC250_DXBC_SPIRV_OK) {
                fwprintf(stderr, L"token stream translation failed for %ls: status=%u\n",
                    Path, (unsigned int)tokenStatus);
                free(dxbc);
                return 0;
            }
            wprintf(L"TOKEN STREAM TRANSLATED %ls: Tokens=%u SPIR-V=%u Stage=%u\n",
                Path, shaderCode[1], tokenModule.SizeBytes, (unsigned int)tokenModule.Stage);
            Bc250DxbcReleaseSpirv(&tokenModule);
        }
    }

    free(dxbc);
    return 1;
}

int wmain(int argc, wchar_t **argv)
{
    BC250_VKD3D_SHADER_ADAPTER adapter;
    BC250_DXBC_SPIRV_TRANSLATOR translator;
    int ok;
    if (argc != 4) {
        fwprintf(stderr, L"usage: %ls ABSOLUTE_DLL VS_DXBC PS_DXBC\n", argv[0]);
        return 2;
    }
    if (!Bc250Vkd3dShaderAdapterOpen(argv[1], &adapter)) {
        fwprintf(stderr, L"failed to load provider: %ls (Win32=%lu)\n",
            argv[1], GetLastError());
        return 3;
    }
    printf("PROVIDER Version=%u.%u Text=%s\n",
        adapter.VersionMajor, adapter.VersionMinor, adapter.VersionText);
    if (!Bc250Vkd3dShaderAdapterGetTranslator(&adapter, &translator)) {
        Bc250Vkd3dShaderAdapterClose(&adapter); return 4;
    }
    ok = TranslateOne(argv[2], BC250_DXBC_SHADER_VERTEX, &translator) &&
        TranslateOne(argv[3], BC250_DXBC_SHADER_PIXEL, &translator);
    Bc250Vkd3dShaderAdapterClose(&adapter);
    if (!ok) return 5;
    puts("VKD3D SHADER REAL SMOKE: PASS");
    return 0;
}
