#include "../core/bc250_psp_load_fw_transaction.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);
typedef unsigned long long size_t;
#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    unsigned char *p=(unsigned char *)Destination;
    while(Length--!=0u)*p++=(unsigned char)Value;
    return Destination;
}
static unsigned long g_failureLine;
#define CHECK(x) do { if (!(x)) { g_failureLine = __LINE__; return 0; } } while (0)

typedef struct FIXTURE {
    uint32_t C64, C67, C69, C70, C71, Checksum;
    volatile uint32_t *Fence;
    uint32_t CompleteFence;
} FIXTURE;
static uint32_t g_ring[BC250_PSP_RING_BYTES/4u];
static uint32_t g_cmd[BC250_PSP_LOAD_FW_COMMAND_BYTES/4u];

static uint32_t *Reg(FIXTURE *f, uint32_t o)
{
    switch (o) {
    case BC250_PSP_RING_C2PMSG_64: return &f->C64;
    case BC250_PSP_RING_C2PMSG_67: return &f->C67;
    case BC250_PSP_RING_C2PMSG_69: return &f->C69;
    case BC250_PSP_RING_C2PMSG_70: return &f->C70;
    case BC250_PSP_RING_C2PMSG_71: return &f->C71;
    case BC250_SDMA0_UCODE_CHECKSUM: return &f->Checksum;
    default: return 0;
    }
}
static uint32_t Read32(void *c, uint32_t o, uint32_t *v)
{
    uint32_t *r=Reg((FIXTURE *)c,o); if(!r||!v)return 0; *v=*r; return 1;
}
static uint32_t Write32(void *c, uint32_t o, uint32_t v)
{
    FIXTURE *f=(FIXTURE *)c; uint32_t *r=Reg(f,o); if(!r)return 0; *r=v;
    if(o==BC250_PSP_RING_C2PMSG_64) f->C64=v|BC250_PSP_RING_TOS_READY;
    if(o==BC250_PSP_RING_C2PMSG_67 && f->CompleteFence) {
        *f->Fence=1u; f->Checksum=0x34u;
    }
    return 1;
}
static void Stall(void *c,uint32_t us){(void)c;(void)us;}
static void Barrier(void *c){(void)c;}

static int Run(void)
{
    volatile uint32_t fence=0u;
    FIXTURE f={BC250_PSP_RING_TOS_READY,0,0,0,0,0,&fence,1};
    BC250_PSP_LOAD_FW_INTERFACE io={Read32,Write32,Stall,Barrier,&f,
        BC250_PSP_LOAD_FW_IDENTITY};
    BC250_PSP_LOAD_FW_EVIDENCE e;
    CHECK(Bc250RunPspLoadFirmwareTransaction(0x400100000ull,g_ring,0x400200000ull,g_cmd,
        0x400300000ull,&fence,0x400400000ull,33536u,BC250_PSP_LOAD_FW_SDMA0_TYPE,
        &io,10u,&e)==BC250_PSP_LOAD_FW_OK);
    CHECK(e.CreatePassed&&e.FencePassed&&e.ResponsePassed&&e.ChecksumPassed&&
        e.DestroyPassed&&e.RestorePassed&&e.PhysicalReady);
    CHECK(e.ChecksumAfter==0x34u&&e.FenceObserved==1u&&f.C67==0u);
    f.Checksum=0u; fence=0u;
    CHECK(Bc250RunPspLoadFirmwareTransaction(0x400100000ull,g_ring,0x400200000ull,g_cmd,
        0x400300000ull,&fence,0x400400000ull,25088u,BC250_PSP_LOAD_FW_RLC_TYPE,
        &io,10u,&e)==BC250_PSP_LOAD_FW_OK);
    CHECK(e.CreatePassed&&e.FencePassed&&e.ResponsePassed&&
        !e.ChecksumPassed&&e.DestroyPassed&&e.RestorePassed&&e.PhysicalReady);
    CHECK(e.FirmwareType==BC250_PSP_LOAD_FW_RLC_TYPE&&e.FirmwareBytes==25088u);
    f.CompleteFence=0u; f.Checksum=0u; fence=0u;
    CHECK(Bc250RunPspLoadFirmwareTransaction(0x400100000ull,g_ring,0x400200000ull,g_cmd,
        0x400300000ull,&fence,0x400400000ull,33536u,BC250_PSP_LOAD_FW_SDMA0_TYPE,
        &io,2u,&e)==BC250_PSP_LOAD_FW_FENCE_TIMEOUT);
    CHECK(!e.PhysicalReady&&e.DestroyPassed&&e.RestorePassed);
    return 1;
}
void mainCRTStartup(void){ExitProcess(Run()?0u:g_failureLine);}
