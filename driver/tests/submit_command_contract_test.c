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

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) return 1; } while (0)
#endif

#define TEST_SUBMIT_RING_DWORDS 256u

static uint32_t g_testRing[TEST_SUBMIT_RING_DWORDS];
static BC250_SUBMISSION_REQUEST g_testReq;
static BC250_SUBMISSION_RESULT g_testResult;

static int Bc250RunSubmitCommandContractTest(void)
{
    uint32_t cursor = 0;

    memset(g_testRing, 0, sizeof(g_testRing));
    memset(&g_testReq, 0, sizeof(g_testReq));
    memset(&g_testResult, 0, sizeof(g_testResult));

    /* 1. Test SDMA Submission (Node 0) */
    g_testReq.NodeIndex = BC250_ADAPTER_NODE_SDMA;
    g_testReq.ContextId = 1u;
    g_testReq.DmaBufferGpuVa = 0x0000000010000000ull;
    g_testReq.DmaBufferSizeBytes = 4096u;
    g_testReq.FenceGpuVa = 0x0000000020000000ull;
    g_testReq.FenceValue = 101ull;
    g_testReq.RingIndex = 0u;

    cursor = 0;
    CHECK(Bc250BuildSdmaRingSubmission(
        &g_testReq, g_testRing, TEST_SUBMIT_RING_DWORDS, &cursor, &g_testResult) == BC250_SUBMISSION_OK);

    CHECK(cursor == 4u);
    CHECK(g_testResult.EmittedDwords == 4u);
    CHECK(g_testResult.UpdatedWptr == 4u);
    CHECK(g_testResult.IsSuccessful == 1u);
    CHECK(g_testRing[0] == (BC250_SDMA_OP_FENCE | (3u << 16)));
    CHECK(g_testRing[1] == 0x20000000u);
    CHECK(g_testRing[3] == 101u);

    /* 2. Test Compute MEC Submission (Node 1) */
    g_testReq.NodeIndex = BC250_ADAPTER_NODE_COMPUTE;
    g_testReq.ContextId = 2u;
    g_testReq.DmaBufferGpuVa = 0x0000000010000000ull;
    g_testReq.DmaBufferSizeBytes = 4096u;
    g_testReq.FenceGpuVa = 0x0000000020000000ull;
    g_testReq.FenceValue = 202ull;
    g_testReq.RingIndex = 1u;

    CHECK(Bc250BuildComputeRingSubmission(
        &g_testReq, g_testRing, TEST_SUBMIT_RING_DWORDS, &cursor, &g_testResult) == BC250_SUBMISSION_OK);

    CHECK(cursor == 16u); /* cursor inicial 4 + IB 4 + RELEASE_MEM 8 */
    CHECK(g_testResult.EmittedDwords == 12u);
    CHECK(g_testResult.UpdatedWptr == 16u);
    CHECK(g_testResult.IsSuccessful == 1u);

    /* Check PM4 Indirect Buffer Packet (DW 4..7) */
    CHECK(g_testRing[4] == 0xC0023F00u); /* IT_INDIRECT_BUFFER */
    CHECK(g_testRing[5] == 0x10000000u); /* Base lo */
    CHECK(g_testRing[7] == 0x00800400u); /* 4096 / 4 dwords + VALID bit 23 */

    /* Check PM4 Release Mem Packet (DW 8..15) */
    CHECK(g_testRing[8] == 0xC0064900u); /* IT_RELEASE_MEM */
    CHECK(g_testRing[11] == 0x20000000u); /* Fence addr lo */
    CHECK(g_testRing[13] == 202u);        /* Fence data lo */
    CHECK(g_testRing[15] == 0u);          /* ctxid/reserved */

    /* 3. Test Unified Router */
    cursor = 0;
    g_testReq.NodeIndex = BC250_ADAPTER_NODE_SDMA;
    CHECK(Bc250SubmitCommand(
        &g_testReq, g_testRing, TEST_SUBMIT_RING_DWORDS, &cursor, &g_testResult) == BC250_SUBMISSION_OK);
    CHECK(cursor == 4u);

    /* 4. Test Error Handling */
    /* Misaligned DMA buffer address */
    g_testReq.DmaBufferGpuVa = 0x0000000010000003ull;
    CHECK(Bc250ValidateSubmissionRequest(&g_testReq) == BC250_SUBMISSION_MISALIGNED_ADDRESS);
    g_testReq.DmaBufferGpuVa = 0x0000000010000000ull;

    /* Misaligned Fence address */
    g_testReq.FenceGpuVa = 0x0000000020000002ull;
    CHECK(Bc250ValidateSubmissionRequest(&g_testReq) == BC250_SUBMISSION_MISALIGNED_ADDRESS);
    g_testReq.FenceGpuVa = 0x0000000020000000ull;

    /* Invalid Node */
    g_testReq.NodeIndex = 2u;
    CHECK(Bc250SubmitCommand(
        &g_testReq, g_testRing, TEST_SUBMIT_RING_DWORDS, &cursor, &g_testResult) == BC250_SUBMISSION_INVALID_NODE);

    /* Null Argument */
    CHECK(Bc250SubmitCommand(NULL, g_testRing, 256u, &cursor, &g_testResult) == BC250_SUBMISSION_BAD_ARGUMENT);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunSubmitCommandContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunSubmitCommandContractTest() ? 0 : 1;
}
#endif
