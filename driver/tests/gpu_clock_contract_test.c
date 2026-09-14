#include "../core/bc250_gpu_clock_contract.h"

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
/* Falha devolve 0 porque main() faz `return Run() ? 0 : 1`: devolver 1
 * aqui fazia a falha virar SUCESSO no build offline. Medido em 14/09
 * quebrando o emissor de proposito -- suite offline saiu EXIT=0 e a
 * kernel EXIT=1. Travado por audit_offline_check_is_not_inverted. */
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#endif

static BC250_GPU_CLOCK_CALIBRATION g_calibration;

static int Bc250RunGpuClockContractTest(void)
{
    memset(&g_calibration, 0, sizeof(g_calibration));

    /* 1. Test Node 0 (SDMA) Clock Calibration */
    CHECK(Bc250CalibrateGpuClock(
        BC250_ADAPTER_NODE_SDMA,
        0u,
        0x12345678u,
        0x00000001u,
        5000000ull,
        10000000ull,
        &g_calibration) == BC250_GPU_CLOCK_OK);

    CHECK(g_calibration.NodeIndex == BC250_ADAPTER_NODE_SDMA);
    CHECK(g_calibration.GpuTimestamp == 0x0000000112345678ull);
    CHECK(g_calibration.CpuTimestamp == 5000000ull);
    CHECK(g_calibration.GpuFrequencyHz == BC250_GPU_CLOCK_FREQUENCY_HZ);
    CHECK(g_calibration.CpuFrequencyHz == 10000000ull);
    CHECK(g_calibration.IsValid == 1u);

    /* 2. Test Node 1 (Compute MEC) Clock Calibration */
    memset(&g_calibration, 0, sizeof(g_calibration));
    CHECK(Bc250CalibrateGpuClock(
        BC250_ADAPTER_NODE_COMPUTE,
        0u,
        0xAABBCCDDu,
        0x00000002u,
        6000000ull,
        10000000ull,
        &g_calibration) == BC250_GPU_CLOCK_OK);

    CHECK(g_calibration.NodeIndex == BC250_ADAPTER_NODE_COMPUTE);
    CHECK(g_calibration.GpuTimestamp == 0x00000002AABBCCDDull);
    CHECK(g_calibration.CpuTimestamp == 6000000ull);

    /* 3. Test Error Handling */
    /* Invalid Node */
    CHECK(Bc250CalibrateGpuClock(
        2u, 0u, 0x1000u, 0x0u, 1000ull, 1000ull, &g_calibration) == BC250_GPU_CLOCK_INVALID_NODE);

    /* Read Glitch (0xFFFFFFFF) */
    CHECK(Bc250CalibrateGpuClock(
        0u, 0u, 0xFFFFFFFFu, 0xFFFFFFFFu, 1000ull, 1000ull, &g_calibration) == BC250_GPU_CLOCK_READ_GLITCH);

    /* Null Argument */
    CHECK(Bc250CalibrateGpuClock(
        0u, 0u, 0x1000u, 0x0u, 1000ull, 1000ull, NULL) == BC250_GPU_CLOCK_BAD_ARGUMENT);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunGpuClockContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunGpuClockContractTest() ? 0 : 1;
}
#endif
