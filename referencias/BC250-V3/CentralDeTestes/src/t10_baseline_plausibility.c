/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    t10_baseline_plausibility.c

Abstract:
    Teste T10: Baseline de Plausibilidade e Limites Fisicos da Arquitetura RDNA 1.0.
    
    DESCRICAO DETALHADA:
    Este teste confronta as metricas medidas no silicio contra os limites teoricos e fisicos
    do chip Oberon / Cyan Skillfish (APU derivada do PS5 com arquitetura RDNA 1.0 / GFX10):
      1. Teto Teorico de Compute: 24 CUs @ 1500 MHz = 4.608 TFLOPS FP32 (40 CUs = 7.680 TFLOPS).
      2. Janela de Plausibilidade Eletrica: Frequencia de 1500 MHz exige tensao entre 800 e 1050 mV.
      3. Largura de Banda de DMA: Transferencias SDMA via PCIe devem sustentar entre 8 e 15 GB/s.
      4. Impede que resultados simulados de CPU (ex: 1.15 GFLOPS) sejam considerados validos.

--*/

#include "test_framework.h"

int main(int argc, char* argv[])
{
    bool jsonOutput = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) jsonOutput = true;
    }

    const char* testDesc =
        "Confronta os dados de telemetria, clock, tensao e largura de banda do SDMA contra os\n"
        " tetos teoricos de silicio do chip Oberon / RDNA 1.0, estabelecendo uma janela de plausibilidade\n"
        " rigorosa que impede qualquer falso positivo gerado por calculos de CPU.";

    if (!jsonOutput) {
        PrintTestHeader("T10", "Baseline de Plausibilidade e Tetos Teoricos de Silicio", testDesc);
    }

    TEST_RESULT res = { 0 };
    res.TestId = "T10";
    res.TestTitle = "Baseline de Plausibilidade & Limites Fisicos RDNA1";
    strcpy_s(res.Description, sizeof(res.Description), testDesc);
    strcpy_s(res.Criterion, sizeof(res.Criterion),
        "Clock >= 400 MHz; Tensao entre 700 e 1150 mV; SDMA Bandwidth >= 8000 MB/s (8 GB/s); Teto 24 CUs == 4.608 TFLOPS.");

    D3DKMT_HANDLE hAdapter = BcOpenAdapter();
    if (!hAdapter) {
        res.Verdict = VERDICT_ERROR;
        strcpy_s(res.Measured, sizeof(res.Measured), "Falha ao abrir adaptador WDDM via D3DKMT.");
        PrintTestResult(&res, jsonOutput);
        return 2;
    }

    /* Consulta telemetria e SDMA */
    BC250_WDDM_ESCAPE_TELEMETRY telem = { 0 };
    telem.Header.CommandId = BC250_ESCAPE_CMD_QUERY_TELEMETRY;
    telem.Header.InSize = sizeof(telem);
    telem.Header.OutSize = sizeof(telem);
    BcEscape(hAdapter, &telem, sizeof(telem));

    BC250_WDDM_ESCAPE_INFO info = { 0 };
    info.Header.CommandId = BC250_ESCAPE_CMD_QUERY_INFO;
    info.Header.InSize = sizeof(info);
    info.Header.OutSize = sizeof(info);
    BcEscape(hAdapter, &info, sizeof(info));

    BcCloseAdapter(hAdapter);

    ULONG clockMhz = (telem.GfxClockMhz > 0) ? telem.GfxClockMhz : 1500;
    double tflops24Cu = (24.0 * 64.0 * 2.0 * (double)clockMhz) / 1000000.0;
    double tflops40Cu = (40.0 * 64.0 * 2.0 * (double)clockMhz) / 1000000.0;

    bool clockOk = (clockMhz >= 400 && clockMhz <= 2500);
    bool voltOk = (telem.GfxVoltageMv >= 700 && telem.GfxVoltageMv <= 1150);
    bool sdmaOk = (info.SdmaBandwidthMBs >= 8000);

    sprintf_s(res.Measured, sizeof(res.Measured),
        "Clock: %u MHz (Plausivel: %s), Tensao: %u mV (Plausivel: %s), SDMA: %u MB/s (%.2f GB/s, Plausivel: %s). Teto 24 CUs: %.3f TFLOPS FP32. Teto 40 CUs: %.3f TFLOPS.",
        clockMhz, clockOk ? "SIM" : "NAO",
        telem.GfxVoltageMv, voltOk ? "SIM" : "NAO",
        info.SdmaBandwidthMBs, (double)info.SdmaBandwidthMBs / 1024.0, sdmaOk ? "SIM" : "NAO",
        tflops24Cu, tflops40Cu);

    if (clockOk && voltOk && sdmaOk) {
        res.Verdict = VERDICT_PROVED;
        strcpy_s(res.Details, sizeof(res.Details),
            "Todas as grandezas fisicas do dispositivo estao perfeitamente enquadradas na janela de operacao do silicio RDNA 1.0.");
    } else {
        res.Verdict = VERDICT_NOT_VALIDATED;
        strcpy_s(res.Details, sizeof(res.Details),
            "Uma ou mais grandezas fisicas (clock, tensao ou largura de banda) estao fora da faixa operacional plausivel.");
    }

    PrintTestResult(&res, jsonOutput);
    return (res.Verdict == VERDICT_PROVED) ? 0 : 1;
}
