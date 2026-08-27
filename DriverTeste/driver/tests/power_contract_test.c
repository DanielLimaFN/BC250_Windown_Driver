#include "../core/bc250_power_contract.h"

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

static BC250_POWER_CONTEXT g_powerCtx;

static int Bc250RunPowerContractTest(void)
{
    /* 1. Test Power Initialization */
    CHECK(Bc250PowerInitialize(&g_powerCtx) == BC250_POWER_OK);
    CHECK(g_powerCtx.CurrentPowerState == BC250_POWER_STATE_D0);
    CHECK(g_powerCtx.PreviousPowerState == BC250_POWER_STATE_UNSPECIFIED);
    CHECK(g_powerCtx.StablePowerStateEnabled == 0u);
    CHECK(g_powerCtx.GfxoffPermitted == 1u);
    CHECK(g_powerCtx.TransitionCount == 0u);

    /* 2. Test Transition D0 -> D3 (Suspend / Sleep) */
    CHECK(Bc250PowerSetState(&g_powerCtx, BC250_POWER_STATE_D3, 0u) == BC250_POWER_OK);
    CHECK(g_powerCtx.CurrentPowerState == BC250_POWER_STATE_D3);
    CHECK(g_powerCtx.PreviousPowerState == BC250_POWER_STATE_D0);
    CHECK(g_powerCtx.GfxoffPermitted == 0u);
    CHECK(g_powerCtx.TransitionCount == 1u);

    /* 3. Test Transition D3 -> D0 (Resume / Wakeup) */
    CHECK(Bc250PowerSetState(&g_powerCtx, BC250_POWER_STATE_D0, 0u) == BC250_POWER_OK);
    CHECK(g_powerCtx.CurrentPowerState == BC250_POWER_STATE_D0);
    CHECK(g_powerCtx.PreviousPowerState == BC250_POWER_STATE_D3);
    CHECK(g_powerCtx.GfxoffPermitted == 1u);
    CHECK(g_powerCtx.TransitionCount == 2u);

    /* 4. Test Stable Power State (Profiling) */
    CHECK(Bc250PowerSetStableState(&g_powerCtx, 1u) == BC250_POWER_OK);
    CHECK(g_powerCtx.StablePowerStateEnabled == 1u);
    CHECK(g_powerCtx.GfxoffPermitted == 0u);

    CHECK(Bc250PowerSetStableState(&g_powerCtx, 0u) == BC250_POWER_OK);
    CHECK(g_powerCtx.StablePowerStateEnabled == 0u);
    CHECK(g_powerCtx.GfxoffPermitted == 1u);

    /* 5. Test Error Handling */
    CHECK(Bc250PowerSetState(&g_powerCtx, 99u, 0u) == BC250_POWER_INVALID_STATE);
    CHECK(Bc250PowerSetState(NULL, BC250_POWER_STATE_D0, 0u) == BC250_POWER_BAD_ARGUMENT);
    CHECK(Bc250PowerSetStableState(NULL, 1u) == BC250_POWER_BAD_ARGUMENT);
    CHECK(Bc250PowerInitialize(NULL) == BC250_POWER_BAD_ARGUMENT);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunPowerContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunPowerContractTest() ? 0 : 1;
}
#endif
