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
/*
 * A13: era `return 1`, mas neste arquivo 1 significa SUCESSO (main devolve
 * `run() ? 0 : 1`), logo um CHECK falho em modo usuario era lido como PASS. A
 * suite compila com /DBC250_KERNEL_MODE, entao o defeito nunca mascarou uma
 * corrida -- mas ficar de pe' e' convite a repetir a classe de erro.
 */
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#endif

#define TEST_SUBMIT_RING_DWORDS 256u

static uint32_t g_testRing[TEST_SUBMIT_RING_DWORDS];
static BC250_SUBMISSION_REQUEST g_testReq;
static BC250_SUBMISSION_RESULT g_testResult;


/*
 * =====================================================================
 * A13/B3 -- trava da precedencia de caminho do SubmitCommandVirtual Node 0.
 * A precedencia dos PLANO A so' existe com PlanoAFallbackEnabled=1; o default
 * do driver e' somente INDIRECT.
 *
 * Gabarito medido na corrida 2026-09-07 16:19: 14 submissoes, 2 DIRECT FILL
 * + 12 SYNC NOP, ZERO INDIRECT -- e ainda assim 14 linhas "IB EXECUTADO".
 * =====================================================================
 */
static int TestSubmitPathPrecedenceWithFallbackEnabled(void)
{
    BC250_SUBMIT_PATH_INPUT in;

    /* As duas submissoes de fill da corrida 16:19, com o fallback LIGADO. */
    memset(&in, 0, sizeof(in));
    in.PlanoAFallbackEnabled = 1u;
    in.HasPendingFill = 1u;
    in.FillBytes = 11075584u;
    in.DestPhysicalStart = 0xF400AB0000ull;
    in.DmaBufferSizeBytes = 0x1000u;
    in.IbGpuVaResolves = 1u;
    CHECK(Bc250SelectSubmitExecPath(&in) == BC250_SUBMIT_PATH_RING_DIRECT_FILL);
    /* DIRECT FILL nao exige que a GPUVA do IB resolva. */
    in.IbGpuVaResolves = 0u;
    CHECK(Bc250SelectSubmitExecPath(&in) == BC250_SUBMIT_PATH_RING_DIRECT_FILL);
    /* Mas exige destino fisico resolvido e tamanho nao nulo. */
    in.DestPhysicalStart = 0ull;
    CHECK(Bc250SelectSubmitExecPath(&in) != BC250_SUBMIT_PATH_RING_DIRECT_FILL);
    in.DestPhysicalStart = 0xF400AB0000ull;
    in.FillBytes = 0u;
    CHECK(Bc250SelectSubmitExecPath(&in) != BC250_SUBMIT_PATH_RING_DIRECT_FILL);

    /* Os 12 buffers de 4 bytes do DWM/VidSch, com o fallback LIGADO. */
    memset(&in, 0, sizeof(in));
    in.PlanoAFallbackEnabled = 1u;
    in.DmaBufferSizeBytes = 4u;
    in.IbGpuVaResolves = 1u;
    CHECK(Bc250SelectSubmitExecPath(&in) == BC250_SUBMIT_PATH_RING_SYNC_NOP);

    /* Sem fill e sem 4 bytes: INDIRECT, se a GPUVA resolver. */
    memset(&in, 0, sizeof(in));
    in.PlanoAFallbackEnabled = 1u;
    in.DmaBufferSizeBytes = 0x1000u;
    in.IbGpuVaResolves = 1u;
    CHECK(Bc250SelectSubmitExecPath(&in) == BC250_SUBMIT_PATH_INDIRECT);
    in.IbGpuVaResolves = 0u;
    CHECK(Bc250SelectSubmitExecPath(&in) == BC250_SUBMIT_PATH_REFUSED);

    CHECK(Bc250SelectSubmitExecPath(NULL) == BC250_SUBMIT_PATH_REFUSED);
    return 1;
}

static int TestDefaultSuppressesPlanoA(void)
{
    BC250_SUBMIT_PATH_INPUT in;

    /*
     * B3: o mesmo fill da corrida 16:19 com o campo em ZERO -- isto e', o
     * DEFAULT do driver. Nao ha mais como cair no PLANO A sem pedir.
     */
    memset(&in, 0, sizeof(in));
    in.HasPendingFill = 1u;
    in.FillBytes = 11075584u;
    in.DestPhysicalStart = 0xF400AB0000ull;
    in.DmaBufferSizeBytes = 0x1000u;
    in.IbGpuVaResolves = 1u;
    CHECK(Bc250SelectSubmitExecPath(&in) == BC250_SUBMIT_PATH_INDIRECT);

    /* E o mesmo sync de 4 bytes. */
    memset(&in, 0, sizeof(in));
    in.DmaBufferSizeBytes = 4u;
    in.IbGpuVaResolves = 1u;
    CHECK(Bc250SelectSubmitExecPath(&in) == BC250_SUBMIT_PATH_INDIRECT);

    /*
     * Com a precedencia desligada e a GPUVA sem resolver, o resultado tem de
     * ser RECUSA -- nunca cair de volta no PLANO A. Fallback escondido e' o
     * defeito que esta faixa existe para matar.
     */
    in.IbGpuVaResolves = 0u;
    CHECK(Bc250SelectSubmitExecPath(&in) == BC250_SUBMIT_PATH_REFUSED);
    return 1;
}

static int TestOnlyIndirectExecutesTheDmaBuffer(void)
{
    /*
     * A regra que a corrida 16:19 violou 14 vezes: so' o INDIRECT autoriza a
     * frase "IB EXECUTADO".
     */
    CHECK(Bc250SubmitPathExecutesDmaBuffer(BC250_SUBMIT_PATH_INDIRECT) == 1u);
    CHECK(Bc250SubmitPathExecutesDmaBuffer(BC250_SUBMIT_PATH_RING_DIRECT_FILL) == 0u);
    CHECK(Bc250SubmitPathExecutesDmaBuffer(BC250_SUBMIT_PATH_RING_SYNC_NOP) == 0u);
    CHECK(Bc250SubmitPathExecutesDmaBuffer(BC250_SUBMIT_PATH_REFUSED) == 0u);

    CHECK(Bc250SubmitPathName(BC250_SUBMIT_PATH_INDIRECT)[0] != '\0');
    CHECK(Bc250SubmitPathName(BC250_SUBMIT_PATH_RING_DIRECT_FILL)[0] != '\0');
    CHECK(Bc250SubmitPathName(BC250_SUBMIT_PATH_RING_SYNC_NOP)[0] != '\0');
    CHECK(Bc250SubmitPathName(BC250_SUBMIT_PATH_REFUSED)[0] != '\0');
    return 1;
}

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

    /* A13 -- precedencia de caminho e rotulo de execucao. */
    CHECK(TestSubmitPathPrecedenceWithFallbackEnabled() == 1);
    CHECK(TestDefaultSuppressesPlanoA() == 1);
    CHECK(TestOnlyIndirectExecutesTheDmaBuffer() == 1);

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
