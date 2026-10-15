#include "../core/bc250_mmhub_gart.h"

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    unsigned char *d = (unsigned char *)Destination;
    while (Length--) {
        *d++ = (unsigned char)Value;
    }
    return Destination;
}

/* O /O2 reconhece o laco de copia do snapshot e emite memcpy (sem CRT aqui). */
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

/* Janela do MMHUB: 0x69000..0x6AFFF. */
#define MMHUB_WINDOW_BASE 0x69000u
#define MMHUB_WINDOW_DW   (0x2000u / 4u)

typedef struct FIXTURE {
    uint32_t Registers[MMHUB_WINDOW_DW];
    uint32_t Writes[MMHUB_WINDOW_DW];
    uint32_t FlushResult;
    uint32_t FlushCalls;
    uint32_t FailReadbackOffset; /* le o inverso depois de escrito */
    uint32_t FailWriteOffset;
} FIXTURE;

static FIXTURE g_fixture;
static uint32_t g_snapshot[MMHUB_WINDOW_DW];
static BC250_MMHUB_GART_EVIDENCE g_ev;

static uint32_t Slot(uint32_t Offset, uint32_t *Index)
{
    if ((Offset & 3u) != 0u || Offset < MMHUB_WINDOW_BASE ||
        Offset >= MMHUB_WINDOW_BASE + MMHUB_WINDOW_DW * 4u) return 0u;
    *Index = (Offset - MMHUB_WINDOW_BASE) / 4u;
    return 1u;
}

static uint32_t Read32(void *Context, uint32_t Offset, uint32_t *Value)
{
    FIXTURE *f = (FIXTURE *)Context;
    uint32_t i;
    if (Value == 0 || !Slot(Offset, &i)) return 0u;
    *Value = f->Registers[i];
    if (f->FailReadbackOffset == Offset && f->Writes[i] == 1u) *Value = ~*Value;
    return 1u;
}

static uint32_t Write32(void *Context, uint32_t Offset, uint32_t Value)
{
    FIXTURE *f = (FIXTURE *)Context;
    uint32_t i;
    if (!Slot(Offset, &i)) return 0u;
    if (f->FailWriteOffset == Offset) return 0u;
    /* O silicio limpa os gatilhos do L2_CNTL2. */
    if (Offset == BC250_MMHUB_VM_L2_CNTL2) Value &= ~BC250_MMHUB_L2_CNTL2_TRIGGERS;
    f->Registers[i] = Value;
    ++f->Writes[i];
    return 1u;
}

static uint32_t Flush(void *Context)
{
    FIXTURE *f = (FIXTURE *)Context;
    ++f->FlushCalls;
    return f->FlushResult;
}

static uint32_t Get(uint32_t Offset)
{
    uint32_t i = 0u;
    (void)Slot(Offset, &i);
    return g_fixture.Registers[i];
}

static void Put(uint32_t Offset, uint32_t Value)
{
    uint32_t i = 0u;
    (void)Slot(Offset, &i);
    g_fixture.Registers[i] = Value;
}

/* Estado medido na Corrida 229 (doc 610) + valores sem foto em padrao conhecido. */
static void Reset(void)
{
    uint32_t i;
    memset(&g_fixture, 0, sizeof(g_fixture));
    for (i = 0u; i < MMHUB_WINDOW_DW; ++i) g_fixture.Registers[i] = 0x5A5A0000u | i;
    Put(BC250_MMHUB_VM_L2_CNTL, 0x00080602u);
    Put(BC250_MMHUB_VM_CONTEXT0_CNTL, 0x007FFE80u);
    Put(BC250_MMHUB_VM_CONTEXT0_PT_BASE_LO32, 0u);
    Put(BC250_MMHUB_VM_CONTEXT0_PT_BASE_HI32, 0u);
    Put(BC250_MMHUB_VM_CONTEXT0_PT_START_LO32, 0u);
    Put(BC250_MMHUB_VM_CONTEXT0_PT_END_LO32, 0u);
    Put(BC250_MMHUB_MC_VM_FB_LOCATION_BASE, 0x0000F400u);
    Put(BC250_MMHUB_MC_VM_FB_LOCATION_TOP, 0x0000F40Fu);
    Put(BC250_MMHUB_MC_VM_AGP_TOP, 0u);
    Put(BC250_MMHUB_MC_VM_AGP_BOT, 0u);
    Put(BC250_MMHUB_MC_VM_SYS_APERTURE_LOW, 0x003D0000u);
    Put(BC250_MMHUB_MC_VM_SYS_APERTURE_HIGH, 0x003D0400u);
    Put(BC250_MMHUB_MC_VM_SYS_APERTURE_DEF_LSB, 0u);
    Put(BC250_MMHUB_MC_VM_MX_L1_TLB_CNTL, 0x00000501u);
    g_fixture.FlushResult = 1u;
}

static void Io(BC250_GCVM_GLOBAL_MMU_INTERFACE *io)
{
    memset(io, 0, sizeof(*io));
    io->Read32 = Read32;
    io->Write32 = Write32;
    io->Flush = Flush;
    io->Context = &g_fixture;
    io->Identity = BC250_MMHUB_GART_IDENTITY;
}

static int Run(void)
{
    BC250_MMHUB_GART_CONFIG config;
    BC250_GCVM_GLOBAL_MMU_INTERFACE io;
    uint32_t i;

    /* Offsets conferidos contra mmhub_2_0_0_offset.h e a foto da 229. */
    CHECK(BC250_MMHUB_VM_CONTEXT0_CNTL == 0x69B00u);
    CHECK(BC250_MMHUB_VM_L2_CNTL == 0x69A00u);
    CHECK(BC250_MMHUB_MC_VM_MX_L1_TLB_CNTL == 0x6A1CCu);
    CHECK(BC250_MMHUB_MC_VM_AGP_BASE == 0x6A1C0u);
    CHECK(BC250_MMHUB_VM_INV_ENG17_REQ == 0x69BD0u);
    CHECK(BC250_MMHUB_VM_INV_ENG17_ACK == 0x69C18u);
    CHECK(BC250_MMHUB_VM_INV_ENG17_SEM == 0x69B88u);

    /* O FB e o FB_OFFSET nunca entram no conjunto de escrita. */
    CHECK(!Bc250MmhubGartIsWritable(BC250_MMHUB_MC_VM_FB_LOCATION_BASE));
    CHECK(!Bc250MmhubGartIsWritable(BC250_MMHUB_MC_VM_FB_LOCATION_TOP));
    CHECK(!Bc250MmhubGartIsWritable(0x6A15Cu)); /* MC_VM_FB_OFFSET */
    CHECK(!Bc250MmhubGartIsWritable(BC250_MMHUB_VM_INV_ENG17_ACK));
    CHECK(Bc250MmhubGartIsWritable(BC250_MMHUB_VM_INV_ENG17_REQ));
    CHECK(Bc250MmhubGartIsWritable(BC250_MMHUB_VM_CONTEXT0_CNTL));

    config.TablePa = 0x46FF02000ull; /* D2: 0x460000000 + 0x0FF02000 */
    config.DummyPa = 0x46FFFF000ull;

    /* 1. Caminho bom com o estado da 229. */
    Reset();
    Io(&io);
    for (i = 0u; i < MMHUB_WINDOW_DW; ++i) g_snapshot[i] = g_fixture.Registers[i];
    CHECK(Bc250MmhubGartProgram(&config, &io, &g_ev) == BC250_GCVM_GLOBAL_MMU_OK);
    CHECK(g_ev.ProgramPassed && g_ev.ReadbackPassed && g_ev.TlbFlushPassed);
    CHECK(g_fixture.FlushCalls == 1u);
    CHECK(Get(BC250_MMHUB_VM_CONTEXT0_PT_BASE_LO32) == 0x6FF02001u);
    CHECK(Get(BC250_MMHUB_VM_CONTEXT0_PT_BASE_HI32) == 0x4u);
    CHECK(Get(BC250_MMHUB_VM_CONTEXT0_PT_END_LO32) == 0x1FFu);
    CHECK(Get(BC250_MMHUB_VM_CONTEXT0_CNTL) == 0x007FFE01u);   /* ENABLE=1, RETRY=0, faults mantidos */
    CHECK(Get(BC250_MMHUB_MC_VM_MX_L1_TLB_CNTL) == 0x00001D59u); /* igual ao alvo do GFXHUB */
    CHECK(Get(BC250_MMHUB_VM_L2_CNTL) == 0x00080E01u);          /* igual ao alvo do GFXHUB */
    CHECK(Get(BC250_MMHUB_MC_VM_AGP_BOT) == 0x00FFFFFFu);       /* AGP fechada */
    CHECK(Get(BC250_MMHUB_MC_VM_AGP_TOP) == 0u);
    CHECK(Get(BC250_MMHUB_MC_VM_SYS_APERTURE_LOW) == 0x003D0000u);
    CHECK(Get(BC250_MMHUB_MC_VM_SYS_APERTURE_HIGH) == 0x003D03FFu);
    CHECK(Get(BC250_MMHUB_MC_VM_SYS_APERTURE_DEF_LSB) == 0x0046FFFFu);
    CHECK(Get(BC250_MMHUB_VM_L2_PROT_FAULT_DEF_LO32) == 0x0046FFFFu);
    CHECK((Get(BC250_MMHUB_VM_L2_PROT_FAULT_CNTL) & 0x1FFCu) == 0x1FFCu);
    CHECK((Get(BC250_MMHUB_VM_L2_CNTL3) & 0x000F803Fu) == ((6u << 15) | 9u));
    CHECK((Get(BC250_MMHUB_VM_L2_CNTL4) & 0xC0u) == 0u);
    CHECK((Get(BC250_MMHUB_VM_L2_CNTL5) & 0x1Fu) == 0u);
    CHECK(Get(BC250_MMHUB_VM_INV_ENG17_RANGE_HI32) == 0x1Fu);
    /* FB intocado. */
    CHECK(Get(BC250_MMHUB_MC_VM_FB_LOCATION_BASE) == 0x0000F400u);
    CHECK(Get(BC250_MMHUB_MC_VM_FB_LOCATION_TOP) == 0x0000F40Fu);
    /* So' o conjunto declarado foi escrito; REQ/SEM ficam com o Flush (fora daqui). */
    for (i = 0u; i < MMHUB_WINDOW_DW; ++i) {
        if (g_fixture.Writes[i] != 0u) {
            CHECK(Bc250MmhubGartRegisterIndex(MMHUB_WINDOW_BASE + i * 4u) < BC250_MMHUB_GART_REG_COUNT);
        }
    }

    /* 2. Restore devolve tudo, contexto 0 desligado, e flusha. */
    CHECK(Bc250MmhubGartRestore(&io, &g_ev) == BC250_GCVM_GLOBAL_MMU_OK);
    CHECK(g_ev.RestorePassed == 1u);
    CHECK(g_fixture.FlushCalls == 2u);
    for (i = 0u; i < MMHUB_WINDOW_DW; ++i) {
        uint32_t off = MMHUB_WINDOW_BASE + i * 4u;
        if (off == BC250_MMHUB_VM_L2_CNTL2) {
            CHECK((g_fixture.Registers[i] & ~3u) == (g_snapshot[i] & ~3u));
        } else {
            CHECK(g_fixture.Registers[i] == g_snapshot[i]);
        }
    }

    /* 3. Readback divergente: rollback devolve o snapshot e nao declara OK. */
    Reset();
    Io(&io);
    g_fixture.FailReadbackOffset = BC250_MMHUB_MC_VM_MX_L1_TLB_CNTL;
    CHECK(Bc250MmhubGartProgram(&config, &io, &g_ev) == BC250_GCVM_GLOBAL_MMU_READBACK_FAILED);
    CHECK(g_ev.ReadbackPassed == 0u && g_ev.FailureStep == 5u);
    CHECK(Get(BC250_MMHUB_VM_CONTEXT0_CNTL) == 0x007FFE80u);
    CHECK(Get(BC250_MMHUB_MC_VM_AGP_BOT) == 0u);

    /* 4. Flush falha: rollback, contexto 0 desligado de novo. */
    Reset();
    Io(&io);
    g_fixture.FlushResult = 0u;
    CHECK(Bc250MmhubGartProgram(&config, &io, &g_ev) == BC250_GCVM_GLOBAL_MMU_TLB_FAILED);
    CHECK(g_ev.TlbFlushPassed == 0u && g_ev.RollbackPassed == 0u); /* o flush do rollback tambem falha */
    CHECK(Get(BC250_MMHUB_VM_CONTEXT0_CNTL) == 0x007FFE80u);
    CHECK(Get(BC250_MMHUB_VM_L2_CNTL) == 0x00080602u);

    /* 5. Escrita recusada no meio: nada depois dela e' escrito; o antes volta. */
    Reset();
    Io(&io);
    g_fixture.FailWriteOffset = BC250_MMHUB_VM_L2_CNTL;
    CHECK(Bc250MmhubGartProgram(&config, &io, &g_ev) == BC250_GCVM_GLOBAL_MMU_WRITE_FAILED);
    CHECK(g_ev.FailureStep == 4u);
    CHECK(Get(BC250_MMHUB_VM_CONTEXT0_CNTL) == 0x007FFE80u);
    { uint32_t k = 0u; (void)Slot(BC250_MMHUB_VM_CONTEXT0_CNTL, &k); CHECK(g_fixture.Writes[k] == 0u); }
    CHECK(Get(BC250_MMHUB_MC_VM_MX_L1_TLB_CNTL) == 0x00000501u);

    /* 6. Recusas de argumento: identidade do GFXHUB, tabela desalinhada, FB zerado. */
    Reset();
    Io(&io);
    io.Identity = BC250_GCVM_GLOBAL_MMU_IDENTITY;
    CHECK(Bc250MmhubGartProgram(&config, &io, &g_ev) == BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT);
    Io(&io);
    config.TablePa = 0x46FF02800ull;
    CHECK(Bc250MmhubGartProgram(&config, &io, &g_ev) == BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT);
    config.TablePa = 0x46FF02000ull;
    Put(BC250_MMHUB_MC_VM_FB_LOCATION_BASE, 0u);
    CHECK(Bc250MmhubGartProgram(&config, &io, &g_ev) == BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT);
    for (i = 0u; i < MMHUB_WINDOW_DW; ++i) CHECK(g_fixture.Writes[i] == 0u);
    /* Restore sem snapshot recusa. */
    memset(&g_ev, 0, sizeof(g_ev));
    CHECK(Bc250MmhubGartRestore(&io, &g_ev) == BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT);
    return 1;
}

void mainCRTStartup(void)
{
    ExitProcess(Run() ? 0u : g_failureLine);
}
