#include "test_common.h"
typedef struct { BC250_ESCAPE_HEADER Header; ULONG Offset; ULONG Value; } REGE;
static D3DKMT_HANDLE g_h;
static ULONG RR(ULONG o){REGE r={0};r.Header.CommandId=BC250_ESCAPE_CMD_READ_REG;r.Header.InSize=sizeof(r);r.Header.OutSize=sizeof(r);r.Offset=o;
 if(!CallBc250Escape(g_h,&r,sizeof(r)))return 0xBADBAD00; return r.Value;}
static void RW(ULONG o,ULONG v){REGE r={0};r.Header.CommandId=BC250_ESCAPE_CMD_WRITE_REG;r.Header.InSize=sizeof(r);r.Header.OutSize=sizeof(r);r.Offset=o;r.Value=v;CallBc250Escape(g_h,&r,sizeof(r));}
static void stick(const char*n,ULONG o,ULONG v){ULONG b=RR(o);RW(o,v);ULONG a=RR(o);RW(o,b);
 printf("  %-34s antes=0x%08X  esc=0x%08X  lido=0x%08X  %s\n",n,b,v,a,(a==v)?"[GRAVOU]":"[IGNORADO]");}
int main(void){g_h=OpenBc250Adapter(); if(!g_h){printf("fail\n");return 1;}
 printf("--- Aperture 0xC1xx (nosso CP_RB0) vs 0xDAxx (alias GC ref) ---\n");
 ULONG a[]={0xC100,0xC104,0xC10C,0xC110,0xC150,0xC154,0xC2C4,0xDA60,0xDA64,0xDA68,0xDA6C,0xDA78,0xDA7C};
 for(int i=0;i<13;i++) printf("  0x%04X = 0x%08X\n",a[i],RR(a[i]));
 printf("\n--- Teste de escrita (write-stick) ---\n");
 stick("CP_RB0_CNTL nosso (0xC104)",0xC104,0x0000110B);
 stick("CP_RB0_WPTR nosso (0xC150)",0xC150,0x000000AA);
 stick("CP_RING0_CNTL ref (0xDA68)",0xDA68,0x0000110B);
 stick("CP_RING0_WPTR ref (0xDA78)",0xDA78,0x000000AA);
 stick("CP_RING0_BASE_LO ref(0xDA60)",0xDA60,0xDEADB000);
 printf("\n--- RPTR do CP em movimento? (0xDA6C, 8 amostras) ---\n");
 for(int i=0;i<8;i++){printf("  RPTR=0x%08X  WPTR=0x%08X  GRBM=0x%08X\n",RR(0xDA6C),RR(0xDA78),RR(0x3260));Sleep(60);}
 printf("\n--- CP_STAT candidatos ---\n");
 ULONG s[]={0x8684,0x8688,0x868C,0x8680,0x8034,0x32D4};
 for(int i=0;i<6;i++) printf("  0x%04X = 0x%08X\n",s[i],RR(s[i]));
 return 0;}
