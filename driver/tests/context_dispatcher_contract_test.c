#include "../core/bc250_context_dispatcher_contract.h"

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

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) return 1; } while (0)
#endif

#define TEST_RING_SIZE 256u

static uint32_t g_dispatchRing[TEST_RING_SIZE];
static BC250_GPU_CONTEXT g_sdmaContext;
static BC250_GPU_CONTEXT g_computeContext;
static BC250_SUBMIT_COMMAND_DESC g_submitDesc;

static int Bc250RunContextDispatcherContractTest(void)
{
    uint32_t cursor = 0;

    memset(g_dispatchRing, 0, sizeof(g_dispatchRing));
    memset(&g_sdmaContext, 0, sizeof(g_sdmaContext));
    memset(&g_computeContext, 0, sizeof(g_computeContext));
    memset(&g_submitDesc, 0, sizeof(g_submitDesc));

    /* 1. Test Context Creation */
    CHECK(Bc250ContextCreate(
        &g_sdmaContext, 1u, BC250_ENGINE_TYPE_SDMA, 0u,
        0x0000080000000000ull, 128u) == BC250_DISPATCH_OK);
    CHECK(g_sdmaContext.IsActive == 1u);
    CHECK(g_sdmaContext.EngineType == BC250_ENGINE_TYPE_SDMA);
    CHECK(g_sdmaContext.NodeOrdinal == 0u);

    CHECK(Bc250ContextCreate(
        &g_computeContext, 2u, BC250_ENGINE_TYPE_COMPUTE, 1u,
        0x0000080000001000ull, 256u) == BC250_DISPATCH_OK);
    CHECK(g_computeContext.IsActive == 1u);
    CHECK(g_computeContext.EngineType == BC250_ENGINE_TYPE_COMPUTE);
    CHECK(g_computeContext.NodeOrdinal == 1u);

    /* 2. Test SDMA Submission */
    cursor = 0;
    memset(&g_submitDesc, 0, sizeof(g_submitDesc));
    g_submitDesc.ContextId = 1u;
    g_submitDesc.EngineType = BC250_ENGINE_TYPE_SDMA;
    g_submitDesc.CopySrcGpuAddress = 0x0000000001000000ull;
    g_submitDesc.CopyDstGpuAddress = 0x0000000002000000ull;
    g_submitDesc.CopySizeBytes = 4096u;
    g_submitDesc.FenceGpuAddress = 0x000000000FF00000ull;
    g_submitDesc.FenceSequence = 0xBC250001u;

    CHECK(Bc250ContextBuildSdmaSubmission(
        &g_sdmaContext, &g_submitDesc, g_dispatchRing, TEST_RING_SIZE, &cursor) == BC250_DISPATCH_OK);
    CHECK(cursor == 11u); /* 7 dwords copy + 4 dwords fence */
    CHECK(g_dispatchRing[0] == 0x00000001u); /* COPY_LINEAR */
    CHECK(g_dispatchRing[1] == 4095u);
    CHECK(g_dispatchRing[7] == 0x00030005u); /* FENCE, MTYPE UC */
    CHECK(g_dispatchRing[10] == 0xBC250001u);

    /* 3. Test Compute Submission */
    cursor = 0;
    memset(g_dispatchRing, 0, sizeof(g_dispatchRing));
    memset(&g_submitDesc, 0, sizeof(g_submitDesc));
    g_submitDesc.ContextId = 2u;
    g_submitDesc.EngineType = BC250_ENGINE_TYPE_COMPUTE;
    g_submitDesc.CommandBufferGpuAddress = 0x0000080000004000ull;
    g_submitDesc.CommandBufferSizeDwords = 32u;
    g_submitDesc.FenceGpuAddress = 0x0000080000005000ull;
    g_submitDesc.FenceSequence = 0xBC250002u;

    CHECK(Bc250ContextBuildComputeSubmission(
        &g_computeContext, &g_submitDesc, g_dispatchRing, TEST_RING_SIZE, &cursor) == BC250_DISPATCH_OK);
    CHECK(cursor == 11u); /* 4 dwords IB + 7 dwords RELEASE_MEM */
    CHECK(g_dispatchRing[0] == 0xC0033F00u); /* IT_INDIRECT_BUFFER (count 3, op 0x3F) */
    CHECK(g_dispatchRing[1] == 0x00004000u); /* Addr Lo */
    CHECK(g_dispatchRing[2] == 0x00000800u); /* Addr Hi */
    CHECK(g_dispatchRing[3] == 32u);         /* Size Dwords | (VMID 0 << 24) */
    CHECK(g_dispatchRing[4] == 0xC0064900u); /* IT_RELEASE_MEM (count 6, op 0x49) */

    /* 4. Test Error Handling */
    /* Mismatched engine / context ID */
    g_submitDesc.ContextId = 1u; /* Mismatched to compute context 2 */
    CHECK(Bc250ContextValidateSubmission(&g_computeContext, &g_submitDesc) == BC250_DISPATCH_INVALID_ENGINE);
    g_submitDesc.ContextId = 2u;

    /* Misaligned fence */
    g_submitDesc.FenceGpuAddress = 0x0000080000005001ull;
    CHECK(Bc250ContextValidateSubmission(&g_computeContext, &g_submitDesc) == BC250_DISPATCH_MISALIGNED_ADDRESS);
    g_submitDesc.FenceGpuAddress = 0x0000080000005000ull;

    /* Destroy context and test inactive validation */
    CHECK(Bc250ContextDestroy(&g_computeContext) == BC250_DISPATCH_OK);
    CHECK(Bc250ContextValidateSubmission(&g_computeContext, &g_submitDesc) == BC250_DISPATCH_CONTEXT_INACTIVE);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunContextDispatcherContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunContextDispatcherContractTest() ? 0 : 1;
}
#endif
