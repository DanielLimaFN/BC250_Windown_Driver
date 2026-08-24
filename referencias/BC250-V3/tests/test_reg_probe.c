#include "test_common.h"

typedef struct _BC250_ESC_REG {
    BC250_ESCAPE_HEADER Header;
    ULONG Offset;
    ULONG Value;
} BC250_ESC_REG;

static D3DKMT_HANDLE g_h;

static ULONG RegRead(ULONG off) {
    BC250_ESC_REG r = {0};
    r.Header.CommandId = BC250_ESCAPE_CMD_READ_REG;
    r.Header.InSize = sizeof(r); r.Header.OutSize = sizeof(r);
    r.Offset = off;
    if (!CallBc250Escape(g_h, &r, sizeof(r))) return 0xBADBAD00;
    return r.Value;
}
static void RegWrite(ULONG off, ULONG val) {
    BC250_ESC_REG r = {0};
    r.Header.CommandId = BC250_ESCAPE_CMD_WRITE_REG;
    r.Header.InSize = sizeof(r); r.Header.OutSize = sizeof(r);
    r.Offset = off; r.Value = val;
    CallBc250Escape(g_h, &r, sizeof(r));
}

typedef struct { const char* name; ULONG off; } REG;

int main(void) {
    g_h = OpenBc250Adapter();
    if (!g_h) { printf("Falha ao abrir adaptador\n"); return 1; }
    printf("Adaptador: 0x%08X\n\n", g_h);

    REG ro[] = {
        {"GRBM_STATUS         (0x3260)", 0x3260},
        {"GRBM_STATUS2        (0x326C)", 0x326C},
        {"GRBM_GFX_INDEX      (0x34D0)", 0x34D0},
        {"GRBM_GFX_CNTL       (0x2022)", 0x2022},
        {"CP_SCRATCH          (0x32D4)", 0x32D4},
        {"SPI_PG_STATIC_WGP   (0x5C3C)", 0x5C3C},
        {"SPI_PG_MASK         (0x34FC)", 0x34FC},
        {"CC_GC_SHADER_ARRAY  (0x9C1C)", 0x9C1C},
        {"RLC_PG_ALWAYS_ON    (0x3D64)", 0x3D64},
        {"CP_ME_CNTL  (nosso 0x86D8)",   0x86D8},
        {"CP_ME_CNTL  (ref   0xC060)",   0xC060},
        {"CP_RB0_RPTR (nosso 0x8700)",   0x8700},
        {"CP_RING0_BASE_LO(ref 0xDA60)", 0xDA60},
        {"CP_RING0_CNTL   (ref 0xDA68)", 0xDA68},
        {"CP_RING0_RPTR   (ref 0xDA6C)", 0xDA6C},
        {"CP_RING0_WPTR   (ref 0xDA78)", 0xDA78},
        {"KIQ_BASE_LO     (ref 0xE060)", 0xE060},
        {"KIQ_RPTR        (ref 0xE06C)", 0xE06C},
        {"KIQ_WPTR        (ref 0xE078)", 0xE078},
        {"PSP_C2PMSG_64   (0x58200)",    0x58200},
        {"PSP_C2PMSG_81   (0x58244)",    0x58244},
        {"PSP_C2PMSG_35   (0x5818C)",    0x5818C},
        {"COMPUTE_DISPATCH_INIT(0x80E0)",0x80E0},
    };
    printf("--- [ Leitura de registradores ] ---\n");
    for (int i=0;i<(int)(sizeof(ro)/sizeof(ro[0]));i++)
        printf("  %-32s = 0x%08X\n", ro[i].name, RegRead(ro[i].off));

    printf("\n--- [ Teste de SCRATCH: MMIO alcanca o bloco GC? ] ---\n");
    ULONG scratchRegs[] = {0x32D4, 0x32D8, 0x30E0};
    for (int i=0;i<3;i++) {
        ULONG off = scratchRegs[i];
        ULONG before = RegRead(off);
        RegWrite(off, 0x4D585042);
        ULONG after = RegRead(off);
        RegWrite(off, before);
        printf("  0x%04X: antes=0x%08X  escrito=0x4D585042  lido=0x%08X  %s\n",
               off, before, after, (after==0x4D585042)?"[GRAVOU - GC ALCANCAVEL]":"[NAO GRAVOU]");
    }

    printf("\n--- [ Teste de escrita SPI_PG / CC (SOS-lock?) ] ---\n");
    ULONG spiB = RegRead(0x5C3C);
    RegWrite(0x5C3C, 0x1F);
    ULONG spiA = RegRead(0x5C3C);
    printf("  SPI_PG_STATIC_WGP(0x5C3C): antes=0x%08X escrito=0x1F lido=0x%08X %s\n",
           spiB, spiA, (spiA==0x1F)?"[DESBLOQUEADO]":"[SOS-LOCKED]");
    ULONG ccB = RegRead(0x9C1C);
    RegWrite(0x9C1C, 0x00000000);
    ULONG ccA = RegRead(0x9C1C);
    RegWrite(0x9C1C, ccB);
    printf("  CC_GC_SHADER_ARRAY(0x9C1C): antes=0x%08X escrito=0x0 lido=0x%08X %s\n",
           ccB, ccA, (ccA==0)?"[GRAVAVEL]":"[LOCKED/RO]");
    return 0;
}
