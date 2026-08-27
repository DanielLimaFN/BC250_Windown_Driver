#include "../core/bc250_gcvm_vmid8_transaction.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

static unsigned long g_failureLine;
#define CHECK(x) do { if (!(x)) { g_failureLine = __LINE__; return 0; } } while (0)

typedef struct FIXTURE {
    uint32_t Registers[0xA500u / 4u];
    uint32_t FlushResult;
    uint32_t FlushCalls;
} FIXTURE;

static FIXTURE g_fixture;

static uint32_t Read32(void *Context, uint32_t Offset, uint32_t *Value)
{
    FIXTURE *f = (FIXTURE *)Context;
    if (Value == 0 || (Offset & 3u) != 0u || Offset >= 0xA500u) return 0u;
    *Value = f->Registers[Offset / 4u];
    return 1u;
}

static uint32_t Write32(void *Context, uint32_t Offset, uint32_t Value)
{
    FIXTURE *f = (FIXTURE *)Context;
    if ((Offset & 3u) != 0u || Offset >= 0xA500u) return 0u;
    f->Registers[Offset / 4u] = Value;
    return 1u;
}

static uint32_t Flush(void *Context)
{
    FIXTURE *f = (FIXTURE *)Context;
    ++f->FlushCalls;
    return f->FlushResult;
}

static int Run(void)
{
    FIXTURE *f = &g_fixture;
    BC250_GCVM_VMID8_INTERFACE io;
    BC250_GCVM_VMID8_EVIDENCE evidence;
    uint32_t original[6] = { 1u, 2u, 3u, 4u, 5u, 6u };
    const uint32_t offsets[6] = {
        BC250_GCVM_VMID8_PT_BASE_LO, BC250_GCVM_VMID8_PT_BASE_HI,
        BC250_GCVM_VMID8_PT_START_LO, BC250_GCVM_VMID8_PT_START_HI,
        BC250_GCVM_VMID8_PT_END_LO, BC250_GCVM_VMID8_PT_END_HI
    };
    uint32_t i;
    io.Read32 = Read32;
    io.Write32 = Write32;
    io.Flush = Flush;
    io.Context = f;
    io.Identity = BC250_GCVM_VMID8_IDENTITY;
    f->FlushResult = 1u;
    for (i = 0u; i < 6u; ++i) f->Registers[offsets[i] / 4u] = original[i];

    CHECK(Bc250RunGcvmVmid8InertTransaction(
        0x45FF10000ull, &io, &evidence) == BC250_GCVM_VMID8_OK);
    CHECK(evidence.PageTablePhysical == 0x45FF10000ull);
    CHECK(evidence.ProgramReadbackPassed == 1u);
    CHECK(evidence.TlbFlushPassed == 1u);
    CHECK(evidence.RestorePassed == 1u);
    CHECK(evidence.PhysicalReady == 1u);
    CHECK(f->FlushCalls == 1u);
    for (i = 0u; i < 6u; ++i) CHECK(f->Registers[offsets[i] / 4u] == original[i]);

    f->Registers[BC250_GCVM_VMID8_CNTL / 4u] = 1u;
    CHECK(Bc250RunGcvmVmid8InertTransaction(
        0x45FF10000ull, &io, &evidence) == BC250_GCVM_VMID8_CONTEXT_ENABLED);
    CHECK(f->FlushCalls == 1u);
    return 1;
}

void mainCRTStartup(void)
{
    ExitProcess(Run() ? 0u : g_failureLine);
}
