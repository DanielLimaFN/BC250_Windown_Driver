#include "../core/bc250_compute_mqd_contract.h"

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

static BC250_GFX10_COMPUTE_MQD g_testMqd;
static BC250_COMPUTE_MQD_PROPERTIES g_testProps;

static int Bc250RunComputeMqdContractTest(void)
{
    uint32_t available = 0;
    uint32_t wptr = 0;

    memset(&g_testMqd, 0, sizeof(g_testMqd));
    memset(&g_testProps, 0, sizeof(g_testProps));

    /* 1. Setup valid properties */
    g_testProps.MqdGpuAddress = 0x0000080000000000ull;
    g_testProps.HqdBaseGpuAddress = 0x0000080000001000ull;
    g_testProps.QueueSizeDwords = 16384u; /* 64 KiB */
    g_testProps.EopGpuAddress = 0x0000080000002000ull;
    g_testProps.EopSizeBytes = 4096u;
    g_testProps.RptrGpuAddress = 0x0000080000003000ull;
    g_testProps.WptrGpuAddress = 0x0000080000003004ull;
    g_testProps.UseDoorbell = 1u;
    g_testProps.DoorbellIndex = 0x20u;
    g_testProps.PipePriority = 0u;
    g_testProps.QueuePriority = 0u;
    g_testProps.HqdActive = 1u;

    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_OK);
    CHECK(g_testMqd.Header == BC250_MQD_HEADER_MAGIC);
    CHECK(g_testMqd.ComputePipelinestatEnable == 0x00000001u);
    CHECK(g_testMqd.ComputeStaticThreadMgmtSe0 == 0xFFFFFFFFu);
    CHECK(g_testMqd.ComputeMiscReserved == 0x00000003u);
    CHECK(g_testMqd.CpHqdEopBaseAddrLo == (uint32_t)((0x0000080000002000ull >> 8) & 0xFFFFFFFFull));
    CHECK(g_testMqd.CpHqdPqBaseLo == (uint32_t)((0x0000080000001000ull >> 8) & 0xFFFFFFFFull));
    CHECK((g_testMqd.CpHqdPqControl & 0x1Fu) == 13u); /* 16384 -> order 14, 14-1 = 13 */
    CHECK((g_testMqd.CpHqdPqDoorbellControl & (1u << 31)) != 0);
    CHECK(g_testMqd.CpHqdActive == 1u);

    /* 2. Validate MQD */
    CHECK(Bc250ComputeMqdValidate(&g_testMqd, &g_testProps) == BC250_MQD_OK);

    /* 3. Test queue available space and wptr advancement */
    CHECK(Bc250ComputeQueueComputeAvailableSpace(
        g_testProps.QueueSizeDwords, 0, 0, &available) == BC250_MQD_OK);
    CHECK(available == g_testProps.QueueSizeDwords - 1u);

    CHECK(Bc250ComputeQueueAdvanceWptr(
        g_testProps.QueueSizeDwords, 0, 12u, &wptr) == BC250_MQD_OK);
    CHECK(wptr == 12u);

    /* Test wraparound */
    CHECK(Bc250ComputeQueueAdvanceWptr(
        g_testProps.QueueSizeDwords, g_testProps.QueueSizeDwords - 4u, 8u, &wptr) == BC250_MQD_OK);
    CHECK(wptr == 4u);

    /* 4. Test error cases */
    /* Null pointers */
    CHECK(Bc250ComputeMqdInit(NULL, &g_testMqd) == BC250_MQD_BAD_ARGUMENT);
    CHECK(Bc250ComputeMqdInit(&g_testProps, NULL) == BC250_MQD_BAD_ARGUMENT);

    /* Invalid queue sizes */
    g_testProps.QueueSizeDwords = 100u; /* Not power of 2 */
    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_INVALID_QUEUE_SIZE);

    g_testProps.QueueSizeDwords = 32u; /* Below minimum 64 */
    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_INVALID_QUEUE_SIZE);
    g_testProps.QueueSizeDwords = 16384u; /* Restore */

    /* Invalid EOP size */
    g_testProps.EopSizeBytes = 128u; /* Below minimum 256 */
    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_INVALID_EOP_SIZE);
    g_testProps.EopSizeBytes = 4096u; /* Restore */

    /* Misaligned GPU address */
    g_testProps.MqdGpuAddress = 0x0000080000000004ull; /* Not 256-byte aligned */
    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_MISALIGNED_ADDRESS);
    g_testProps.MqdGpuAddress = 0x0000080000000000ull; /* Restore */

    g_testProps.HqdBaseGpuAddress = 0x0000080000001080ull; /* Not 256-byte aligned */
    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_MISALIGNED_ADDRESS);
    g_testProps.HqdBaseGpuAddress = 0x0000080000001000ull; /* Restore */

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunComputeMqdContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunComputeMqdContractTest() ? 0 : 1;
}
#endif
