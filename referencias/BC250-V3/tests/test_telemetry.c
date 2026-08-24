/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_telemetry.c

Abstract:
    Teste 2: telemetria do SMU.

    Fase 0: reescrito. A leitura em si sempre foi honesta — o KMD usa a tabela
    de metricas do SMU por DMA e zera os campos em vez de inventar valores.
    O que faltava era o teste VERIFICAR isso.

    Duas coisas novas:

      1. Rejeita explicitamente telemetria de origem fabricada. Se Flags nao
         indicar tabela SMU (1) nem sonda fisica SMN (2), o dado nao vale.

      2. T6 - variancia. Amostra em serie e exige que pelo menos um sensor
         VARIE. Um valor constante em todas as amostras e' indistinguivel de
         uma constante codificada, e nao serve como prova 8 da regra
         ("clock, potencia e temperatura reagindo a' carga").

--*/

#include "test_verdict.h"

#define AMOSTRAS 12

int main(void)
{
    D3DKMT_HANDLE h;
    BC250_WDDM_ESCAPE_TELEMETRY t = { 0 };
    ULONG tempMin = 0xFFFFFFFFu, tempMax = 0;
    ULONG powMin  = 0xFFFFFFFFu, powMax  = 0;
    ULONG clkMin  = 0xFFFFFFFFu, clkMax  = 0;
    ULONG flags = 0;
    int i, lidas = 0;

    BcInicio("Teste 2: Telemetria do SMU (com verificacao de variancia)");

    h = BcAbrirAdaptadorVivo();
    if (h == 0) {
        BcBloquear("Adaptador WDDM indisponivel ou nao responde. Se um teste\n"
                   "            anterior estourou o TDR, o handle esta morto: reinicie\n"
                   "            a bateria. Nao ha' medicao valida neste estado.");
        return BcVeredito();
    }

    printf("--- Amostragem ---\n");
    BcCriterio("Origem dos dados deve ser tabela SMU (Flags=1) ou sonda SMN\n"
               "            (Flags=2), e pelo menos um sensor deve variar entre amostras.");

    for (i = 0; i < AMOSTRAS; i++) {
        memset(&t, 0, sizeof(t));
        t.Header.CommandId = BC250_ESCAPE_CMD_QUERY_TELEMETRY;
        t.Header.InSize  = sizeof(t);
        t.Header.OutSize = sizeof(t);
        if (!CallBc250Escape(h, &t, sizeof(t))) continue;

        lidas++;
        flags = t.Flags;

        if (t.GfxTemperatureC < tempMin) tempMin = t.GfxTemperatureC;
        if (t.GfxTemperatureC > tempMax) tempMax = t.GfxTemperatureC;
        if (t.PowerMilliwatts < powMin)  powMin  = t.PowerMilliwatts;
        if (t.PowerMilliwatts > powMax)  powMax  = t.PowerMilliwatts;
        if (t.GfxClockMhz < clkMin)      clkMin  = t.GfxClockMhz;
        if (t.GfxClockMhz > clkMax)      clkMax  = t.GfxClockMhz;

        if (i < 4 || i == AMOSTRAS - 1) {
            BcMedido("[%02d] GFX %u C  SoC %u C  VDDGFX %u mV  "
                     "GFXCLK %u  SOCCLK %u  MEMCLK %u  PPT %u mW",
                     i + 1, t.GfxTemperatureC, t.SocTemperatureC, t.GfxVoltageMv,
                     t.GfxClockMhz, t.SocClockMhz, t.MemClockMhz, t.PowerMilliwatts);
        }
        Sleep(120);
    }

    if (lidas == 0) {
        BcBloquear("Nenhuma leitura de telemetria retornou.");
        return BcVeredito();
    }

    printf("\n--- Variancia observada em %d amostras ---\n", lidas);
    BcMedido("Temperatura GFX : %u .. %u C     (delta %u)",
             tempMin, tempMax, tempMax - tempMin);
    BcMedido("Potencia PPT    : %u .. %u mW    (delta %u)",
             powMin, powMax, powMax - powMin);
    BcMedido("Clock GFX       : %u .. %u MHz   (delta %u)",
             clkMin, clkMax, clkMax - clkMin);
    BcMedido("Origem dos dados: %s",
             flags == 1 ? "tabela de metricas do SMU (DMA)" :
             flags == 2 ? "sonda fisica SMN (diodo TSI)" :
                          "DESCONHECIDA / nao lida do silicio");

    printf("\n--- Avaliacao ---\n");
    BcProva(flags == 1 || flags == 2,
            "Telemetria vem do silicio (tabela SMU ou sonda SMN)");
    BcProva((tempMax - tempMin) > 0 || (powMax - powMin) > 0,
            "T6: pelo menos um sensor varia entre amostras (nao e' constante)");
    BcProva(clkMax > 0,
            "Clock GFX lido do SMU e' diferente de zero");

    if ((tempMax - tempMin) == 0 && (powMax - powMin) == 0) {
        printf("\n            Diagnostico: nenhum sensor variou. Isso e'\n");
        printf("            indistinguivel de valores codificados. Nao serve como\n");
        printf("            prova 8 da regra BC-250.\n");
    }

    return BcVeredito();
}
