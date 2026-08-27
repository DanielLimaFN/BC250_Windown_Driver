#include "../core/bc250_sdma_f32_transaction.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);
typedef unsigned long long size_t;
#pragma function(memset)
void * __cdecl memset(void *d,int v,size_t n){unsigned char*p=d;while(n--)*p++=(unsigned char)v;return d;}
static unsigned long g_line;
#define CHECK(x) do{if(!(x)){g_line=__LINE__;return 0;}}while(0)
typedef struct FIXTURE { uint32_t clk,cntl,f32,status2,reads,move,block; } FIXTURE;
static uint32_t *Reg(FIXTURE*f,uint32_t o){switch(o){case BC250_SDMA0_CLK_CTRL:return &f->clk;case BC250_SDMA0_CNTL:return &f->cntl;case BC250_SDMA0_F32_CNTL:return &f->f32;case BC250_SDMA0_STATUS2_REG:return &f->status2;default:return 0;}}
static uint32_t Read32(void*c,uint32_t o,uint32_t*v){FIXTURE*f=c;uint32_t*r=Reg(f,o);if(!r||!v)return 0;if(o==BC250_SDMA0_STATUS2_REG&&f->move&&f->reads++>0)f->status2+=4u;*v=*r;return 1;}
static uint32_t Write32(void*c,uint32_t o,uint32_t v){FIXTURE*f=c;uint32_t*r=Reg(f,o);if(!r||f->block)return 0;*r=v;return 1;}
static void Stall(void*c,uint32_t us){(void)c;(void)us;}
static int Run(void){FIXTURE f={0x10u,0x20u,0u,0x100u,0u,1u,0u};BC250_SDMA_F32_INTERFACE io={Read32,Write32,Stall,&f,BC250_SDMA_F32_IDENTITY};BC250_SDMA_F32_EVIDENCE e;CHECK(Bc250RunSdmaF32Transaction(&io,&e)==BC250_SDMA_F32_OK);CHECK(e.ApplyReadbackPassed&&e.PcTransitions>0&&e.RestorePassed&&e.PhysicalReady);CHECK(f.clk==0x10u&&f.cntl==0x20u&&f.f32==0u);f.move=0u;f.reads=0u;CHECK(Bc250RunSdmaF32Transaction(&io,&e)==BC250_SDMA_F32_PC_STATIC);CHECK(e.RestorePassed&&!e.PhysicalReady);return 1;}
void mainCRTStartup(void){ExitProcess(Run()?0u:g_line);}
