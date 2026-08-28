#include "../core/bc250_smu_telemetry_contract.h"

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
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#endif

static uint32_t g_c2pmsg66 = 0u;
static uint32_t g_c2pmsg67 = 0u;
static uint32_t g_c2pmsg82 = 0u;

static uint32_t MockSmuRead32(void *Context, uint32_t ByteOffset)
{
    (void)Context;
    if (ByteOffset == BC250_SMU_MMIO_C2PMSG_66) return g_c2pmsg66;
    if (ByteOffset == BC250_SMU_MMIO_C2PMSG_67) return g_c2pmsg67;
    if (ByteOffset == BC250_SMU_MMIO_C2PMSG_82) return g_c2pmsg82;
    return 0u;
}

static void MockSmuWrite32(void *Context, uint32_t ByteOffset, uint32_t Value)
{
    (void)Context;
    if (ByteOffset == BC250_SMU_MMIO_C2PMSG_66) g_c2pmsg66 = Value;
    if (ByteOffset == BC250_SMU_MMIO_C2PMSG_82) g_c2pmsg82 = Value;
    if (ByteOffset == BC250_SMU_MMIO_C2PMSG_67) {
        g_c2pmsg67 = Value;
        /* Simula comportamento do SMU no hardware */
        if (Value == BC250_PPSMC_MSG_TestMessage) {
            g_c2pmsg82 = g_c2pmsg82 + 1u; /* Retorna param + 1 */
            g_c2pmsg66 = BC250_SMU_RESP_OK;
        } else if (Value == BC250_PPSMC_MSG_GetSmuVersion) {
            g_c2pmsg82 = 0x00550003u; /* v55.0.3 */
            g_c2pmsg66 = BC250_SMU_RESP_OK;
        } else if (Value == BC250_PPSMC_MSG_GetDriverIfVersion) {
            g_c2pmsg82 = 0x00000008u; /* IF v8 */
            g_c2pmsg66 = BC250_SMU_RESP_OK;
        } else if (Value == BC250_PPSMC_MSG_GetGfxFrequency) {
            g_c2pmsg82 = 1800u; /* 1800 MHz */
            g_c2pmsg66 = BC250_SMU_RESP_OK;
        } else if (Value == BC250_PPSMC_MSG_GetGfxVid) {
            g_c2pmsg82 = 1050u; /* 1050 mV */
            g_c2pmsg66 = BC250_SMU_RESP_OK;
        } else {
            g_c2pmsg66 = BC250_SMU_RESP_CMD_REJECT;
        }
    }
}

static int Bc250RunSmuTelemetryTest(void)
{
    BC250_SMU_TRANSPORT transport;
    uint32_t val = 0u;

    transport.Read32 = MockSmuRead32;
    transport.Write32 = MockSmuWrite32;
    transport.StallUs = NULL;
    transport.Context = NULL;

    /* 1. TestMessage Handshake */
    CHECK(Bc250SmuTestHandshake(&transport, 42u, &val) == BC250_SMU_STATUS_OK);
    CHECK(val == 43u);

    /* 2. Version Queries */
    CHECK(Bc250SmuGetVersion(&transport, &val) == BC250_SMU_STATUS_OK);
    CHECK(val == 0x00550003u);

    CHECK(Bc250SmuGetDriverIfVersion(&transport, &val) == BC250_SMU_STATUS_OK);
    CHECK(val == 8u);

    /* 3. Frequency & Voltage Queries */
    CHECK(Bc250SmuGetGfxFrequency(&transport, &val) == BC250_SMU_STATUS_OK);
    CHECK(val == 1800u);

    CHECK(Bc250SmuGetGfxVid(&transport, &val) == BC250_SMU_STATUS_OK);
    CHECK(val == 1050u);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunSmuTelemetryTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunSmuTelemetryTest() ? 0 : 1;
}
#endif
