#include "../core/bc250_adapter_info_contract.h"

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

static BC250_ADAPTER_CAPABILITIES g_testCaps;
static BC250_NODE_INFO g_testNodeInfo;

static int Bc250RunAdapterInfoContractTest(void)
{
    uint32_t nodeCount = 0;
    uint32_t isBusy = 0;
    uint32_t pendingDwords = 0;

    memset(&g_testCaps, 0, sizeof(g_testCaps));
    memset(&g_testNodeInfo, 0, sizeof(g_testNodeInfo));

    /* 1. Test Capabilities */
    CHECK(Bc250GetAdapterCapabilities(&g_testCaps) == BC250_ADAPTER_INFO_OK);
    CHECK(g_testCaps.NodeCount == 2u);
    /*
     * 34 bits, nao 48: a hierarquia de 2 niveis cobre 12 + 9 + 13 (docs/03).
     * O valor literal fica aqui de proposito -- se alguem mexer na geometria,
     * este teste tem de ser reescrito conscientemente, nao seguir junto.
     */
    CHECK(g_testCaps.GpuVaBits == 34u);
    CHECK(g_testCaps.MaxVaAddress == 0x00000003FFFFFFFFull);
    CHECK(g_testCaps.PageSizeBytes == 4096u);
    CHECK(g_testCaps.VramBarIndex == 0u);
    CHECK(g_testCaps.DoorbellBarIndex == 2u);
    CHECK(g_testCaps.MmioBarIndex == 5u);
    CHECK(g_testCaps.SdmaEngineCount == 1u);
    CHECK(g_testCaps.ComputeEngineCount == 1u);

    /* 2. Test Node Count */
    CHECK(Bc250GetNodeCount(&nodeCount) == BC250_ADAPTER_INFO_OK);
    CHECK(nodeCount == 2u);

    /* 3. Test Node 0 (SDMA) */
    CHECK(Bc250GetNodeInfo(BC250_ADAPTER_NODE_SDMA, &g_testNodeInfo) == BC250_ADAPTER_INFO_OK);
    CHECK(g_testNodeInfo.NodeIndex == 0u);
    CHECK(g_testNodeInfo.EngineType == BC250_ENGINE_TYPE_SDMA);
    CHECK(g_testNodeInfo.SupportsGpuVa == 1u);
    CHECK(g_testNodeInfo.SupportsIoMmu == 1u);
    CHECK(g_testNodeInfo.ContextStatusReporting == 1u);
    CHECK(g_testNodeInfo.MaxSubmissionDwords == 1024u * 1024u);

    /* 4. Test Node 1 (Compute MEC) */
    CHECK(Bc250GetNodeInfo(BC250_ADAPTER_NODE_COMPUTE, &g_testNodeInfo) == BC250_ADAPTER_INFO_OK);
    CHECK(g_testNodeInfo.NodeIndex == 1u);
    CHECK(g_testNodeInfo.EngineType == BC250_ENGINE_TYPE_COMPUTE);
    CHECK(g_testNodeInfo.SupportsGpuVa == 1u);
    CHECK(g_testNodeInfo.SupportsIoMmu == 1u);
    CHECK(g_testNodeInfo.ContextStatusReporting == 1u);
    CHECK(g_testNodeInfo.MaxSubmissionDwords == 16384u);

    /* 5. Test Engine Status Query */
    /* Idle case */
    CHECK(Bc250QueryEngineStatus(0u, 10u, 10u, &isBusy, &pendingDwords) == BC250_ADAPTER_INFO_OK);
    CHECK(isBusy == 0u);
    CHECK(pendingDwords == 0u);

    /* Busy case */
    CHECK(Bc250QueryEngineStatus(1u, 10u, 30u, &isBusy, &pendingDwords) == BC250_ADAPTER_INFO_OK);
    CHECK(isBusy == 1u);
    CHECK(pendingDwords == 20u);

    /* 6. Test Error Handling */
    CHECK(Bc250GetAdapterCapabilities(NULL) == BC250_ADAPTER_INFO_BAD_ARGUMENT);
    CHECK(Bc250GetNodeCount(NULL) == BC250_ADAPTER_INFO_BAD_ARGUMENT);
    CHECK(Bc250GetNodeInfo(2u, &g_testNodeInfo) == BC250_ADAPTER_INFO_INVALID_NODE);
    CHECK(Bc250QueryEngineStatus(2u, 0, 0, &isBusy, &pendingDwords) == BC250_ADAPTER_INFO_INVALID_NODE);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunAdapterInfoContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunAdapterInfoContractTest() ? 0 : 1;
}
#endif
