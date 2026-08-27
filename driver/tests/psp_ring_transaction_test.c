#include "../core/bc250_psp_ring_transaction.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);
static unsigned long g_failureLine;
#define CHECK(x) do { if (!(x)) { g_failureLine = __LINE__; return 0; } } while (0)

typedef struct FIXTURE {
    uint32_t C64, C67, C69, C70, C71;
    uint32_t Writes;
    uint32_t LeaveCommandStale;
} FIXTURE;

static uint32_t *Reg(FIXTURE *f, uint32_t o)
{
    switch (o) {
    case BC250_PSP_RING_C2PMSG_64: return &f->C64;
    case BC250_PSP_RING_C2PMSG_67: return &f->C67;
    case BC250_PSP_RING_C2PMSG_69: return &f->C69;
    case BC250_PSP_RING_C2PMSG_70: return &f->C70;
    case BC250_PSP_RING_C2PMSG_71: return &f->C71;
    default: return 0;
    }
}
static uint32_t Read32(void *c, uint32_t o, uint32_t *v)
{
    uint32_t *r = Reg((FIXTURE *)c, o); if (!r || !v) return 0; *v = *r; return 1;
}
static uint32_t Write32(void *c, uint32_t o, uint32_t v)
{
    FIXTURE *f=(FIXTURE *)c; uint32_t *r=Reg(f,o); if(!r)return 0; *r=v; ++f->Writes;
    if (o == BC250_PSP_RING_C2PMSG_64 && !f->LeaveCommandStale)
        f->C64 = BC250_PSP_RING_ACK_VALUE;
    return 1;
}
static void Stall(void *c, uint32_t us) { (void)c; (void)us; }

static int Run(void)
{
    FIXTURE f = { BC250_PSP_RING_TOS_READY, 0, 0, 0, 0, 0, 0 };
    BC250_PSP_RING_INTERFACE io = { Read32, Write32, Stall, &f, BC250_PSP_RING_IDENTITY };
    BC250_PSP_RING_EVIDENCE e;
    CHECK(Bc250RunPspRingTransaction(0x12345000ull, &io, 10u, &e) == BC250_PSP_RING_OK);
    CHECK(e.CreatePassed && e.DestroyPassed && e.RestorePassed && e.PhysicalReady);
    CHECK(e.CreatePolls == 1u && e.DestroyPolls == 1u);
    CHECK(f.C67 == 0u && f.C69 == 0u && f.C70 == 0u && f.C71 == 0u);
    f.C69 = 1u;
    CHECK(Bc250RunPspRingTransaction(0x12345000ull, &io, 10u, &e) ==
        BC250_PSP_RING_INHERITED_ACTIVE);
    f.C69 = 0u;
    f.LeaveCommandStale = 1u;
    CHECK(Bc250RunPspRingTransaction(0x12345000ull, &io, 2u, &e) ==
        BC250_PSP_RING_DESTROY_TIMEOUT);
    CHECK(e.CreatePassed == 0u && e.DestroyPassed == 0u &&
        e.PhysicalReady == 0u);
    CHECK(e.CreateAck == BC250_PSP_RING_CREATE_KM);
    return 1;
}
void mainCRTStartup(void) { ExitProcess(Run() ? 0u : g_failureLine); }
