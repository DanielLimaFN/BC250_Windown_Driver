/*
 * Suite do dreno do latch de fault (Corrida 56).
 *
 * Os numeros saem da Corrida 55: CNTL=0x3FFFFFFC com Clear=0 e
 * AceitaAtualizacoes=0, e Status=0x0014115D no primeiro fault.
 */
#include "../core/bc250_fault_latch_contract.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

static int g_failureLine = 0;
#define CHECK(cond) do { if (!(cond)) { g_failureLine = __LINE__; return 0; } } while (0)

#define CNTL_MEDIDO 0x3FFFFFFCu

/* ---------------------------------------------------------------- fake --- */
typedef struct FAKE {
    uint32_t Cntl;
    uint32_t Fila[8];      /* faults que o silicio entregara, em ordem */
    uint32_t FilaLen;
    uint32_t FilaPos;
    uint32_t Latched;      /* valor atualmente no STATUS */
    uint32_t ClearFunciona;
    uint32_t CntlLegivel;
    uint32_t RestauraQuebrada;
    uint32_t Escritas;
} FAKE;

static uint32_t FakeRead(void *ctx, uint32_t off, uint32_t *val)
{
    FAKE *f = (FAKE *)ctx;
    if (off == BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL) {
        if (f->CntlLegivel == 0u) return 0u;
        *val = f->RestauraQuebrada ? (f->Cntl ^ 0x4u) : f->Cntl;
        return 1u;
    }
    if (off == BC250_REG_GCVM_L2_PROTECTION_FAULT_STATUS) {
        *val = f->Latched;
        return 1u;
    }
    if (off == BC250_REG_GCVM_L2_PROTECTION_FAULT_ADDR_LO32) {
        *val = 0x2464E2u; return 1u;
    }
    if (off == BC250_REG_GCVM_L2_PROTECTION_FAULT_ADDR_HI32) {
        *val = 0u; return 1u;
    }
    return 0u;
}

static uint32_t FakeWrite(void *ctx, uint32_t off, uint32_t val)
{
    FAKE *f = (FAKE *)ctx;
    if (off != BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL) return 0u;
    ++f->Escritas;
    f->Cntl = val;
    if ((val & BC250_FAULT_CNTL_CLEAR_STATUS_ADDR_BIT) != 0u &&
        f->ClearFunciona != 0u) {
        /* o CLEAR libera o latch; o proximo da fila entra, se houver */
        f->Latched = (f->FilaPos < f->FilaLen) ? f->Fila[f->FilaPos++] : 0u;
    }
    return 1u;
}

static void FakeInit(FAKE *f)
{
    uint32_t i;
    f->Cntl = CNTL_MEDIDO;
    for (i = 0u; i < 8u; ++i) f->Fila[i] = 0u;
    f->FilaLen = 0u;
    f->FilaPos = 0u;
    f->Latched = 0u;
    f->ClearFunciona = 1u;
    f->CntlLegivel = 1u;
    f->RestauraQuebrada = 0u;
    f->Escritas = 0u;
}

static void IoInit(BC250_FAULT_LATCH_IO *io, FAKE *f)
{
    io->Context = f;
    io->Read32 = FakeRead;
    io->Write32 = FakeWrite;
}

static int Run(void)
{
    FAKE f;
    BC250_FAULT_LATCH_IO io;
    BC250_FAULT_LATCH_EVIDENCE ev;

    /* --- latch vazio: nada a drenar, e ainda assim restaura e prova --- */
    FakeInit(&f); IoInit(&io, &f);
    CHECK(Bc250FaultLatchDrain(&io, &ev) == BC250_FAULT_LATCH_OK);
    CHECK(ev.Drained == 0u);
    CHECK(ev.CntlBefore == CNTL_MEDIDO);
    CHECK(ev.RestorePassed == 1u);
    CHECK(f.Cntl == CNTL_MEDIDO);

    /* --- fila de tres: os TRES saem, em ordem --- */
    FakeInit(&f); IoInit(&io, &f);
    f.Latched = 0x0014115Du;             /* o medido na Corrida 55 */
    f.Fila[0] = 0x001013BDu; f.Fila[1] = 0x00201281u; f.FilaLen = 2u;
    CHECK(Bc250FaultLatchDrain(&io, &ev) == BC250_FAULT_LATCH_OK);
    CHECK(ev.Drained == 3u);
    CHECK(ev.Entries[0].Status == 0x0014115Du);
    CHECK(ev.Entries[1].Status == 0x001013BDu);
    CHECK(ev.Entries[2].Status == 0x00201281u);
    CHECK(ev.ClearAcked == 1u);
    CHECK(ev.ClearIneficaz == 0u);
    CHECK(ev.HitCeiling == 0u);
    /* MORE_FAULTS=1 esta no bit 0 de 0x0014115D */
    CHECK(ev.MoreFaultsSeen == 1u);
    CHECK(ev.RestorePassed == 1u && f.Cntl == CNTL_MEDIDO);

    /* --- CLEAR que nao pega: UM fault, nao oito --- */
    FakeInit(&f); IoInit(&io, &f);
    f.Latched = 0x0014115Du;
    f.ClearFunciona = 0u;
    CHECK(Bc250FaultLatchDrain(&io, &ev) == BC250_FAULT_LATCH_OK);
    CHECK(ev.Drained == 1u);          /* repeticao NAO conta como fault novo */
    CHECK(ev.ClearIneficaz == 1u);
    CHECK(ev.ClearAcked == 0u);
    CHECK(ev.RestorePassed == 1u);

    /* --- teto: para em MAX e DECLARA que pode haver mais --- */
    FakeInit(&f); IoInit(&io, &f);
    {
        uint32_t k;
        f.Latched = 0x00000101u;
        for (k = 0u; k < 8u; ++k) f.Fila[k] = 0x00000101u + k + 1u;
        f.FilaLen = 8u;
    }
    CHECK(Bc250FaultLatchDrain(&io, &ev) == BC250_FAULT_LATCH_OK);
    CHECK(ev.Drained == BC250_FAULT_LATCH_MAX_DRAIN);
    CHECK(ev.HitCeiling == 1u);

    /* --- CNTL ilegivel: NAO se escreve valor inventado --- */
    FakeInit(&f); IoInit(&io, &f);
    f.CntlLegivel = 0u;
    f.Latched = 0x0014115Du;
    CHECK(Bc250FaultLatchDrain(&io, &ev) == BC250_FAULT_LATCH_CNTL_UNREADABLE);
    CHECK(f.Escritas == 0u);          /* nenhuma escrita sem saber o valor base */
    CHECK(ev.Drained == 0u);

    /* --- restauracao que nao confere e FALHA declarada --- */
    FakeInit(&f); IoInit(&io, &f);
    f.RestauraQuebrada = 1u;
    CHECK(Bc250FaultLatchDrain(&io, &ev) == BC250_FAULT_LATCH_RESTORE_FAILED);
    CHECK(ev.RestorePassed == 0u);

    /* --- argumentos invalidos --- */
    CHECK(Bc250FaultLatchDrain(NULL, &ev) == BC250_FAULT_LATCH_BAD_ARGUMENT);
    CHECK(Bc250FaultLatchDrain(&io, NULL) == BC250_FAULT_LATCH_BAD_ARGUMENT);
    {
        BC250_FAULT_LATCH_IO vazio;
        vazio.Context = &f; vazio.Read32 = 0; vazio.Write32 = FakeWrite;
        CHECK(Bc250FaultLatchDrain(&vazio, &ev) == BC250_FAULT_LATCH_BAD_ARGUMENT);
    }

    return 1;
}

void __stdcall mainCRTStartup(void)
{
    ExitProcess(Run() ? 0u : (unsigned long)g_failureLine);
}
