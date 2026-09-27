#include "../umd/bc250_vkd3d_shader_adapter.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned int BuildDxbc(unsigned char *Bytes)
{
    BC250_DXBC_RAW_HEADER *header = (BC250_DXBC_RAW_HEADER *)Bytes;
    BC250_DXBC_RAW_CHUNK_HEADER *chunk;
    unsigned int *words;
    memset(Bytes, 0, 52);
    header->Magic = BC250_DXBC_MAGIC_DXBC;
    header->Version = 1;
    header->TotalSize = 52;
    header->ChunkCount = 1;
    ((unsigned int *)(Bytes + 32))[0] = 36;
    chunk = (BC250_DXBC_RAW_CHUNK_HEADER *)(Bytes + 36);
    chunk->FourCC = BC250_DXBC_FOURCC_SHDR;
    chunk->ChunkSize = 8;
    words = (unsigned int *)(Bytes + 44);
    words[0] = (1u << 16) | (4u << 4) | 1u;
    words[1] = 2u;
    return 52;
}

static void AssertClosed(const BC250_VKD3D_SHADER_ADAPTER *Adapter)
{
    assert(Adapter->Module == NULL);
    assert(Adapter->CompileProc == NULL);
    assert(Adapter->FreeCodeProc == NULL);
    assert(Adapter->GetVersionProc == NULL);
}

int main(void)
{
    BC250_VKD3D_SHADER_ADAPTER adapter;
    BC250_DXBC_SPIRV_TRANSLATOR translator;
    BC250_DXBC_SPIRV_MODULE module;
    unsigned char dxbc[52];
    wchar_t providerPath[MAX_PATH];
    wchar_t *separator;

    memset(&adapter, 0xA5, sizeof(adapter));
    memset(&translator, 0xA5, sizeof(translator));
    assert(!Bc250Vkd3dShaderAdapterOpen(NULL, &adapter));
    AssertClosed(&adapter);

    memset(&adapter, 0xA5, sizeof(adapter));
    assert(!Bc250Vkd3dShaderAdapterOpen(L"vkd3d-shader-1.dll", &adapter));
    AssertClosed(&adapter);

    memset(&adapter, 0xA5, sizeof(adapter));
    assert(!Bc250Vkd3dShaderAdapterOpen(
        L"C:\\BC250-DEFINITELY-MISSING\\vkd3d-shader-1.dll", &adapter));
    AssertClosed(&adapter);

    assert(!Bc250Vkd3dShaderAdapterGetTranslator(&adapter, &translator));
    Bc250Vkd3dShaderAdapterClose(&adapter);
    AssertClosed(&adapter);

    assert(GetModuleFileNameW(NULL, providerPath, MAX_PATH) != 0);
    separator = wcsrchr(providerPath, L'\\');
    assert(separator != NULL);
    wcscpy_s(separator + 1, MAX_PATH - (size_t)(separator + 1 - providerPath),
        L"bc250_vkd3d_mock.dll");
    assert(Bc250Vkd3dShaderAdapterOpen(providerPath, &adapter));
    assert(adapter.VersionMajor == 1 && adapter.VersionMinor == 99);
    assert(Bc250Vkd3dShaderAdapterGetTranslator(&adapter, &translator));
    BuildDxbc(dxbc);
    assert(Bc250DxbcTranslateToSpirv(dxbc, sizeof(dxbc), &translator, &module) ==
        BC250_DXBC_SPIRV_OK);
    assert(module.Stage == BC250_DXBC_SHADER_VERTEX);
    assert(module.SizeBytes == 20 && module.Words[0] == BC250_SPIRV_MAGIC);
    Bc250DxbcReleaseSpirv(&module);
    Bc250Vkd3dShaderAdapterClose(&adapter);
    AssertClosed(&adapter);

    puts("VKD3D SHADER ADAPTER: PASS (load, ABI, translate, ownership, fail-closed)");
    return 0;
}
