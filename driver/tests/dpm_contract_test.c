/*
 * Suite do governador de clock C08 / DPM.1 (doc 765). Politica pura:
 * subida pela carga, descida com histerese, teto termico, sem oscilacao.
 */
#include "../core/bc250_dpm_contract.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

static int g_failureLine = 0;
#define CHECK(cond) do { if (!(cond)) { g_failureLine = __LINE__; return 0; } } while (0)

/* Uma janela de 100 ms com uma demanda constante (MHz de trabalho). */
static uint32_t Tick(BC250_DPM_STATE *s, uint32_t demandMhz, uint32_t tctl)
{
    uint32_t busy = demandMhz >= s->ClockMhz ? 1000u : (demandMhz * 1000u) / s->ClockMhz;
    uint32_t want = Bc250DpmStep(s, busy, tctl, 100u);
    Bc250DpmCommit(s, want);
    return want;
}

static int Run(void)
{
    BC250_DPM_STATE s;
    uint32_t i, d, changes, last;

    /* Faixa: o teto pedido e' preso em 1000..2000 e no passo de 100. */
    Bc250DpmInit(&s, 2400u, 1500u);
    CHECK(s.MaxMhz == 2000u && s.ClockMhz == 1500u && s.CapMhz == 2000u);
    Bc250DpmInit(&s, 1850u, 1500u);
    CHECK(s.MaxMhz == 1800u);
    Bc250DpmInit(&s, 500u, 1500u);
    CHECK(s.MaxMhz == 1000u && s.ClockMhz == 1000u);

    /* Carga cheia sobe direto (1500 * 1000 / 800 = 1875 -> 1900), nunca acima do teto. */
    Bc250DpmInit(&s, 2000u, 1500u);
    CHECK(Tick(&s, 3000u, 60000u) == 1900u);
    CHECK(Tick(&s, 3000u, 60000u) == 2000u);
    CHECK(Tick(&s, 3000u, 60000u) == 2000u);

    /* Ociosa desce um nivel a cada 200 ms ate o piso, nunca abaixo de 1000. */
    for (i = 0u; i < 60u; ++i) (void)Tick(&s, 0u, 60000u);
    CHECK(s.ClockMhz == 1000u);

    /* Sem oscilacao: para cada demanda constante, depois de assentar, nenhuma troca. */
    for (d = 100u; d <= 2400u; d += 50u) {
        Bc250DpmInit(&s, 2000u, 1500u);
        for (i = 0u; i < 100u; ++i) (void)Tick(&s, d, 60000u);
        changes = 0u; last = s.ClockMhz;
        for (i = 0u; i < 200u; ++i) {
            uint32_t w = Tick(&s, d, 60000u);
            if (w != last) ++changes;
            last = w;
        }
        CHECK(changes == 0u);
        /* E o clock assentado da conta da demanda quando pode. */
        if (d * 1000u / 800u <= 2000u) CHECK(s.ClockMhz * 900u > d * 1000u || s.ClockMhz == 2000u);
    }

    /* 90 C: teto no piso na hora, mesmo com carga cheia. */
    Bc250DpmInit(&s, 2000u, 1800u);
    CHECK(Tick(&s, 3000u, 90500u) == 1000u);
    /* 88 C: um nivel abaixo na hora e mais um a cada 500 ms, sem subir. */
    Bc250DpmInit(&s, 2000u, 1800u);
    CHECK(Tick(&s, 3000u, 88000u) == 1700u);
    for (i = 0u; i < 4u; ++i) CHECK(Tick(&s, 3000u, 88000u) == 1700u);
    CHECK(Tick(&s, 3000u, 88000u) == 1600u);
    /* 84 C: teto parado; 80 C: sobe um nivel por segundo. */
    for (i = 0u; i < 20u; ++i) CHECK(Tick(&s, 3000u, 84000u) == 1600u);
    for (i = 0u; i < 9u; ++i) (void)Tick(&s, 3000u, 80000u);
    CHECK(s.CapMhz == 1600u);
    (void)Tick(&s, 3000u, 80000u);
    CHECK(s.CapMhz == 1700u);

    /* Sem Tctl: teto no ponto do firmware. */
    Bc250DpmInit(&s, 2000u, 1500u);
    CHECK(Tick(&s, 3000u, 0u) == 1500u);
    CHECK(s.CapMhz == 1500u);
    return 1;
}

void __stdcall mainCRTStartup(void)
{
    ExitProcess(Run() ? 0u : (unsigned long)g_failureLine);
}
