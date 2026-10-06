#include "../core/bc250_sdma1_bringup.h"
#include "../core/bc250_sdma_packet_builder.h"

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    unsigned char *d = (unsigned char *)Destination;
    while (Length--) {
        *d++ = (unsigned char)Value;
    }
    return Destination;
}

#pragma function(memcpy)
void * __cdecl memcpy(void *Destination, const void *Source, size_t Length)
{
    unsigned char *d = (unsigned char *)Destination;
    const unsigned char *s = (const unsigned char *)Source;
    while (Length--) {
        *d++ = *s++;
    }
    return Destination;
}

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

static unsigned long g_failureLine;
#define CHECK(x) do { if (!(x)) { g_failureLine = __LINE__; return 0; } } while (0)

/* Janela do SDMA1: 0x6000..0x67FF. */
#define WIN_BASE 0x6000u
#define WIN_DW   (0x800u / 4u)
#define RING_GPU 0x000E0000ull
#define WORK_GPU 0x000E1000ull

typedef struct FIXTURE {
    uint32_t Regs[WIN_DW];
    uint32_t Ring[BC250_SDMA1_RING_BYTES / 4u];
    uint32_t Work[1024];
    uint32_t Execute;          /* 1 = o "silicio" executa o anel */
    uint32_t DropWrite;        /* offset cuja escrita some */
    uint32_t IgnoreHaltWrite;  /* o F32 nao para */
    uint32_t ClkWrites;
    uint32_t MaxClk;           /* maior valor ja escrito no CLK_CTRL */
} FIXTURE;

static FIXTURE g_f;
static BC250_SDMA1_EVIDENCE g_ev;

static uint32_t *Reg(uint32_t Offset)
{
    if ((Offset & 3u) != 0u || Offset < WIN_BASE || Offset >= WIN_BASE + WIN_DW * 4u) return 0;
    return &g_f.Regs[(Offset - WIN_BASE) / 4u];
}

/* Executa os pacotes entre RPTR e WPTR (so' FENCE e NOP). */
static void Run(void)
{
    uint32_t *rb = Reg(BC250_SDMA1_REG_RB_CNTL);
    uint32_t *f32 = Reg(BC250_SDMA1_REG_F32_CNTL);
    uint32_t *rptr = Reg(BC250_SDMA1_REG_RB_RPTR);
    uint32_t *wptr = Reg(BC250_SDMA1_REG_RB_WPTR);
    uint32_t base = *Reg(BC250_SDMA1_REG_RB_BASE);
    uint32_t dw;
    if (!g_f.Execute || (*rb & BC250_SDMA1_RB_ENABLE) == 0u || (*f32 & BC250_SDMA1_F32_HALT) != 0u) return;
    if ((uint64_t)base << 8 != RING_GPU) return; /* anel no lugar errado: nada executa */
    dw = *rptr / 4u;
    while (dw < *wptr / 4u) {
        uint32_t h = g_f.Ring[dw];
        if ((h & 0xFFu) == BC250_SDMA_OP_FENCE) {
            uint64_t a = (uint64_t)g_f.Ring[dw + 1u] | ((uint64_t)g_f.Ring[dw + 2u] << 32);
            if (a >= WORK_GPU && a < WORK_GPU + 4096u) g_f.Work[(a - WORK_GPU) / 4u] = g_f.Ring[dw + 3u];
            dw += 4u;
        } else {
            dw += 1u;
        }
    }
    *rptr = *wptr;
    g_f.Work[BC250_SDMA1_WORK_RPTR_OFFSET / 4u] = *rptr;
}

static uint32_t Read32(void *Context, uint32_t Offset, uint32_t *Value)
{
    uint32_t *r = Reg(Offset);
    (void)Context;
    if (r == 0 || Value == 0) return 0u;
    *Value = *r;
    return 1u;
}

static uint32_t Write32(void *Context, uint32_t Offset, uint32_t Value)
{
    uint32_t *r = Reg(Offset);
    (void)Context;
    if (r == 0) return 0u;
    if (Offset == g_f.DropWrite) return 1u;
    if (Offset == BC250_SDMA1_REG_F32_CNTL && g_f.IgnoreHaltWrite) Value &= ~BC250_SDMA1_F32_HALT;
    if (Offset == BC250_SDMA1_REG_CLK_CTRL) {
        ++g_f.ClkWrites;
        if (Value > g_f.MaxClk) g_f.MaxClk = Value;
    }
    *r = Value;
    return 1u;
}

static void Stall(void *Context, uint32_t Us) { (void)Context; (void)Us; Run(); }
static void Barrier(void *Context) { (void)Context; }

static BC250_SDMA1_INTERFACE Iface(void)
{
    BC250_SDMA1_INTERFACE i;
    memset(&i, 0, sizeof(i));
    i.Read32 = Read32;
    i.Write32 = Write32;
    i.Stall = Stall;
    i.Barrier = Barrier;
    i.Context = &g_f;
    i.Identity = BC250_SDMA1_IDENTITY;
    return i;
}

static BC250_SDMA1_CONFIG Config(void)
{
    BC250_SDMA1_CONFIG c;
    memset(&c, 0, sizeof(c));
    c.RingCpu = g_f.Ring;
    c.RingGpu = RING_GPU;
    c.WorkCpu = g_f.Work;
    c.WorkGpu = WORK_GPU;
    c.FenceValue = 0x51D10001u;
    c.MaxPolls = 100u;
    return c;
}

/* Estado medido na Corrida 199/200: F32 parado, anel desligado, CLK do POST. */
static void Reset(void)
{
    memset(&g_f, 0, sizeof(g_f));
    *Reg(BC250_SDMA1_REG_UCODE_CHECKSUM) = 0x1234ABCDu;
    *Reg(BC250_SDMA1_REG_F32_CNTL) = BC250_SDMA1_F32_HALT;
    *Reg(BC250_SDMA1_REG_CLK_CTRL) = 0x000C0100u;
    *Reg(BC250_SDMA1_REG_CHICKEN_BITS) = 0x00400000u; /* bit fora da mascara deve sobreviver */
    *Reg(BC250_SDMA1_REG_WPTR_POLL_CNTL) = 0x00401000u;
    g_f.Execute = 1u;
}

static int RunAll(void)
{
    BC250_SDMA1_INTERFACE i;
    BC250_SDMA1_CONFIG c;

    /* 1. Sucesso: tripe (RPTR==WPTR, fence sobre poison) e relogio de volta ao POST. */
    Reset(); i = Iface(); c = Config();
    CHECK(Bc250Sdma1BringUp(&c, &i, &g_ev) == BC250_SDMA1_OK);
    CHECK(g_ev.PhysicalReady == 1u && g_ev.ProofPassed == 1u && g_ev.ReadbackPassed == 1u);
    CHECK(g_ev.RptrObserved == g_ev.WptrBytes && g_ev.WptrBytes == 64u);
    CHECK(g_ev.FenceObserved == 0x51D10001u);
    CHECK(g_f.Work[BC250_SDMA1_WORK_FENCE_OFFSET / 4u] == 0x51D10001u);
    CHECK(g_ev.ClkBefore == 0x000C0100u && g_ev.ClkRestored == 0x000C0100u);
    CHECK(*Reg(BC250_SDMA1_REG_CLK_CTRL) == 0x000C0100u);
    CHECK(g_f.MaxClk == (0x000C0100u | BC250_SDMA1_CLK_OVERRIDES)); /* forcado so' durante */
    CHECK((*Reg(BC250_SDMA1_REG_F32_CNTL) & BC250_SDMA1_F32_HALT) == 0u);
    CHECK((*Reg(BC250_SDMA1_REG_RB_CNTL) & BC250_SDMA1_RB_ENABLE) != 0u);
    CHECK((*Reg(BC250_SDMA1_REG_CHICKEN_BITS) & 0x00400000u) != 0u); /* Lei 6 */
    CHECK(*Reg(BC250_SDMA1_REG_RB_BASE) == (uint32_t)(RING_GPU >> 8));
    CHECK(*Reg(BC250_SDMA1_REG_RPTR_ADDR_LO) == (uint32_t)(WORK_GPU + BC250_SDMA1_WORK_RPTR_OFFSET));
    CHECK((*Reg(BC250_SDMA1_REG_DOORBELL) & BC250_SDMA1_DOORBELL_ENABLE) == 0u);
    /* Desligamento: anel, poll e writeback desligados, F32 parado. */
    CHECK(Bc250Sdma1Shutdown(&i, &g_ev) == BC250_SDMA1_OK && g_ev.SafeToFree == 1u);
    CHECK((*Reg(BC250_SDMA1_REG_RB_CNTL) & (BC250_SDMA1_RB_ENABLE | BC250_SDMA1_RPTR_WRITEBACK_ENABLE)) == 0u);
    CHECK((*Reg(BC250_SDMA1_REG_WPTR_POLL_CNTL) & BC250_SDMA1_POLL_ENABLE) == 0u);
    CHECK((*Reg(BC250_SDMA1_REG_F32_CNTL) & BC250_SDMA1_F32_HALT) != 0u);

    /* 2. Sem firmware: recusa antes de escrever. */
    Reset(); i = Iface(); c = Config();
    *Reg(BC250_SDMA1_REG_UCODE_CHECKSUM) = 0u;
    CHECK(Bc250Sdma1BringUp(&c, &i, &g_ev) == BC250_SDMA1_NO_FIRMWARE);
    CHECK(g_f.ClkWrites == 0u && (*Reg(BC250_SDMA1_REG_RB_CNTL) & BC250_SDMA1_RB_ENABLE) == 0u);

    /* 3. Anel ja ligado por outro: recusa sem tocar. */
    Reset(); i = Iface(); c = Config();
    *Reg(BC250_SDMA1_REG_RB_CNTL) = BC250_SDMA1_RB_ENABLE;
    CHECK(Bc250Sdma1BringUp(&c, &i, &g_ev) == BC250_SDMA1_INHERITED_ACTIVE);
    CHECK(g_f.ClkWrites == 0u);

    /* 4. Escrita perdida (RB_BASE): o readback pega, a engine desliga, o relogio volta. */
    Reset(); i = Iface(); c = Config();
    g_f.DropWrite = BC250_SDMA1_REG_RB_BASE;
    CHECK(Bc250Sdma1BringUp(&c, &i, &g_ev) == BC250_SDMA1_READBACK_FAILED);
    CHECK(g_ev.PhysicalReady == 0u && g_ev.SafeToFree == 1u);
    CHECK(*Reg(BC250_SDMA1_REG_CLK_CTRL) == 0x000C0100u);
    CHECK((*Reg(BC250_SDMA1_REG_F32_CNTL) & BC250_SDMA1_F32_HALT) != 0u);

    /* 5. A engine nao executa: timeout, fence no poison, desligada e liberavel. */
    Reset(); i = Iface(); c = Config();
    g_f.Execute = 0u;
    CHECK(Bc250Sdma1BringUp(&c, &i, &g_ev) == BC250_SDMA1_PROOF_TIMEOUT);
    CHECK(g_ev.FenceObserved == BC250_SDMA1_FENCE_POISON && g_ev.ProofPassed == 0u);
    CHECK(g_ev.ProofPolls == 100u && g_ev.SafeToFree == 1u);

    /* 6. F32 que nao para: o desligamento nao declara a memoria liberavel. */
    Reset(); i = Iface(); c = Config();
    g_f.Execute = 0u;
    g_f.IgnoreHaltWrite = 1u;
    CHECK(Bc250Sdma1BringUp(&c, &i, &g_ev) == BC250_SDMA1_SHUTDOWN_UNSAFE);
    CHECK(g_ev.SafeToFree == 0u);

    /* 7. Argumentos e lista de registradores. */
    Reset(); i = Iface(); c = Config();
    c.FenceValue = BC250_SDMA1_FENCE_POISON;
    CHECK(Bc250Sdma1BringUp(&c, &i, &g_ev) == BC250_SDMA1_BAD_ARGUMENT);
    c = Config(); c.RingGpu = RING_GPU + 0x10u;
    CHECK(Bc250Sdma1BringUp(&c, &i, &g_ev) == BC250_SDMA1_BAD_ARGUMENT);
    c = Config(); i.Identity = 0u;
    CHECK(Bc250Sdma1BringUp(&c, &i, &g_ev) == BC250_SDMA1_BAD_ARGUMENT);
    CHECK(Bc250Sdma1IsRegister(BC250_SDMA1_REG_F32_CNTL) == 1u);
    CHECK(Bc250Sdma1IsRegister(BC250_SDMA1_REG_RB_CNTL) == 1u);
    CHECK(Bc250Sdma1IsRegister(0x00004A28u) == 0u); /* SDMA0_F32_CNTL nao e' deste contrato */
    CHECK(Bc250Sdma1IsRegister(0x000061E8u) == 0u); /* POWER_CNTL fica fora */
    /* Offsets = SDMA0 + 0x600 dw (sdma_v5_0.c SDMA1_REG_OFFSET). */
    CHECK(BC250_SDMA1_REG_F32_CNTL == 0x00004A28u + 0x1800u);
    CHECK(BC250_SDMA1_REG_RB_CNTL == 0x00004B80u + 0x1800u);
    CHECK(BC250_SDMA1_REG_MINOR_PTR_UPDATE == 0x00004C54u + 0x1800u);
    return 1;
}

void mainCRTStartup(void)
{
    ExitProcess(RunAll() ? 0u : g_failureLine);
}
