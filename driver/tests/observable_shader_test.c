#include "../core/bc250_observable_shader.h"

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) return 1; } while (0)
#endif

static int Bc250RunObservableShaderTest(void)
{
    const BC250_OBSERVABLE_SHADER_MANIFEST *manifest;
    uint32_t mutated[BC250_OBSERVABLE_SHADER_DWORDS];
    uint32_t i;

    manifest = Bc250GetObservableShaderManifest();
    CHECK(manifest != NULL);
    CHECK(Bc250ValidateObservableShaderManifest(manifest) != 0);
    CHECK(manifest->TargetGfxMajor == 10u);
    CHECK(manifest->TargetGfxMinor == 1u);
    CHECK(manifest->TargetGfxStepping == 3u);
    CHECK(manifest->WaveSize == BC250_WAVE32);
    CHECK(manifest->UserSgprs == 2u);
    CHECK(manifest->ResultValue == BC250_DISPATCH_RESULT_VALUE);
    CHECK(manifest->ShaderDwordCount == BC250_OBSERVABLE_SHADER_DWORDS);
    CHECK(manifest->ShaderDwords[0] == 0x7E000280u);
    CHECK(manifest->ShaderDwords[1] == 0x7E0202FFu);
    CHECK(manifest->ShaderDwords[2] == BC250_DISPATCH_RESULT_VALUE);
    CHECK(manifest->ShaderDwords[3] == 0xDC708000u);
    CHECK(manifest->ShaderDwords[4] == 0x00000100u);
    CHECK(manifest->ShaderDwords[5] == 0xBF8C3F70u);
    CHECK(manifest->ShaderDwords[6] == BC250_OPCODE_S_ENDPGM);
    CHECK(Bc250ValidateObservableShaderBlob(
        manifest->ShaderDwords, manifest->ShaderDwordCount) != 0);

    for (i = 0u; i < BC250_OBSERVABLE_SHADER_DWORDS; ++i) {
        mutated[i] = manifest->ShaderDwords[i];
    }
    mutated[3] ^= 1u;
    CHECK(Bc250ValidateObservableShaderBlob(
        mutated, BC250_OBSERVABLE_SHADER_DWORDS) == 0);
    CHECK(Bc250ValidateObservableShaderBlob(
        manifest->ShaderDwords, BC250_OBSERVABLE_SHADER_DWORDS - 1u) == 0);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunObservableShaderTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunObservableShaderTest() ? 0 : 1;
}
#endif
