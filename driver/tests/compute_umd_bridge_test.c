#include "../core/bc250_isa_validator.h"
#include "../core/bc250_submit_command_contract.h"

#include <string.h>

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#pragma function(memcpy)
void * __cdecl memcpy(void *Destination, const void *Source, size_t Length)
{
    volatile unsigned char *dst = (volatile unsigned char *)Destination;
    const volatile unsigned char *src = (const volatile unsigned char *)Source;
    size_t index;
    for (index = 0; index < Length; ++index) dst[index] = src[index];
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) return 1; } while (0)
#endif

#define TEST_UMD_RING_DWORDS 256u

static uint32_t g_umdRing[TEST_UMD_RING_DWORDS];

/* Exemplos de shaders de computação GFX10 */
static const uint32_t g_validComputeShaderWave32[] = {
    0xC0000000u, /* s_load_dwordx2 */
    0x02000000u, /* v_add_u32 */
    0xDC000000u, /* global_store_dword */
    0xBF810000u  /* s_endpgm */
};

static const uint32_t g_invalidComputeShaderNoEndpgm[] = {
    0xC0000000u,
    0x02000000u,
    0xDC000000u,
    0x00000000u
};

static int Bc250RunComputeUmdBridgeTest(void)
{
    uint32_t cursor = 0;
    BC250_SHADER_RESOURCE_CONFIG config;
    BC250_SHADER_VALIDATION_RESULT result;
    BC250_SUBMISSION_REQUEST submitReq;
    BC250_SUBMISSION_RESULT submitRes;

    memset(g_umdRing, 0, sizeof(g_umdRing));
    memset(&config, 0, sizeof(config));
    memset(&result, 0, sizeof(result));

    /* 1. Test Valid Wave32 Compute Shader */
    config.WaveSize = BC250_WAVE32;
    config.NumSgprs = 16u;
    config.NumVgprs = 32u;
    config.LdsSizeBytes = 0u;
    config.ScratchSizeBytes = 0u;

    CHECK(Bc250ValidateComputeShaderIsa(
        g_validComputeShaderWave32, 4u, &config, &result) == BC250_ISA_OK);
    CHECK(result.IsValid == 1u);
    CHECK(result.HasEndPgm == 1u);
    CHECK(result.InstructionCount == 4u);

    /* 2. Test Valid Wave64 Compute Shader with LDS */
    config.WaveSize = BC250_WAVE64;
    config.NumSgprs = 64u;
    config.NumVgprs = 64u;
    config.LdsSizeBytes = 16384u;

    CHECK(Bc250ValidateComputeShaderIsa(
        g_validComputeShaderWave32, 4u, &config, &result) == BC250_ISA_OK);

    /* 3. Test Missing s_endpgm */
    CHECK(Bc250ValidateComputeShaderIsa(
        g_invalidComputeShaderNoEndpgm, 4u, &config, &result) == BC250_ISA_MISSING_ENDPGM);

    /* 4. Test Exceed SGPR Limit (> 108) */
    config.NumSgprs = 112u;
    CHECK(Bc250ValidateComputeShaderIsa(
        g_validComputeShaderWave32, 4u, &config, &result) == BC250_ISA_EXCEEDS_SGPR_LIMIT);
    config.NumSgprs = 16u;

    /* 5. Test Exceed VGPR Limit for Wave64 (> 128) */
    config.WaveSize = BC250_WAVE64;
    config.NumVgprs = 160u;
    CHECK(Bc250ValidateComputeShaderIsa(
        g_validComputeShaderWave32, 4u, &config, &result) == BC250_ISA_EXCEEDS_VGPR_LIMIT);
    config.NumVgprs = 32u;

    /* 6. Test Exceed LDS Limit (> 64KB) */
    config.LdsSizeBytes = 65536u + 1024u;
    CHECK(Bc250ValidateComputeShaderIsa(
        g_validComputeShaderWave32, 4u, &config, &result) == BC250_ISA_EXCEEDS_LDS_LIMIT);
    config.LdsSizeBytes = 0u;

    /* 7. Test Build IT_DISPATCH_DIRECT Packet */
    cursor = 0;
    CHECK(Bc250BuildComputeDispatchPacket(
        g_umdRing, TEST_UMD_RING_DWORDS, &cursor, 64u, 64u, 1u) == BC250_ISA_OK);
    CHECK(cursor == 5u);
    CHECK(g_umdRing[0] == BC250_PM4_HEADER_DISPATCH_DIRECT);
    CHECK(g_umdRing[1] == 64u);
    CHECK(g_umdRing[2] == 64u);
    CHECK(g_umdRing[3] == 1u);
    CHECK(g_umdRing[4] ==
        (BC250_DISPATCH_COMPUTE_SHADER_EN |
         BC250_DISPATCH_FORCE_START_000));

    /* 8. Test End-to-End Compute Dispatch Submission (UMD -> KMD Contract) */
    memset(&submitReq, 0, sizeof(submitReq));
    memset(&submitRes, 0, sizeof(submitRes));
    submitReq.NodeIndex = BC250_ADAPTER_NODE_COMPUTE;
    submitReq.ContextId = 1u;
    submitReq.DmaBufferGpuVa = 0x0000000050000000ull;
    submitReq.DmaBufferSizeBytes = 512u;
    submitReq.FenceGpuVa = 0x0000000060000000ull;
    submitReq.FenceValue = 1001ull;

    CHECK(Bc250SubmitCommand(
        &submitReq, g_umdRing, TEST_UMD_RING_DWORDS, &cursor, &submitRes) == BC250_SUBMISSION_OK);
    CHECK(submitRes.UpdatedWptr == cursor);
    CHECK(submitRes.EmittedDwords == 12u); /* 4 DW Indirect Buffer + 8 DW Release Mem */

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunComputeUmdBridgeTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunComputeUmdBridgeTest() ? 0 : 1;
}
#endif
