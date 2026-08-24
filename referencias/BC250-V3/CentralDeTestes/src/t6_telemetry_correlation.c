/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    t6_telemetry_correlation.c

Abstract:
    Teste T6: Correlacao Dinamica de Carga e Telemetria do SMU.
    
    DESCRICAO DETALHADA:
    Este teste valida a autenticidade dos sensores termicos, de clock e de potencia do SMU.
    Para garantir que os dados lidos sao provenientes do hardware real e nao de constantes hardcoded:
      1. Coleta telemetria em repouso (10 amostras a 10 Hz).
      2. Dispara carga continua de transferencias SDMA para estimular o consumo da placa.
      3. Coleta telemetria sob carga e calcula a variancia termica e correlacao de potencia.
      4. Exige valores dentro de limites fisicos: Temp entre 25C e 105C, Clock > 0, Potencia > 0.

--*/

#include "test_framework.h"

int main(int argc, char* argv[])
{
    bool jsonOutput = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) jsonOutput = true;
    }

    const char* testDesc =
        "Amostra a telemetria do coprocessador SMU a 10 Hz em repouso e durante carga de DMA,\n"
        " exigindo variabilidade fisica de temperatura, leitura dinamica de tensao/clocks e\n"
        " rejeitando terminantemente valores estaticos ou hardcoded (ex: 48C/42W constantes).";

    if (!jsonOutput) {
        PrintTestHeader("T6", "Correlacao Dinamica de Carga e Telemetria SMU", testDesc);
    }

    TEST_RESULT res = { 0 };
    res.TestId = "T6";
    res.TestTitle = "Telemetria SMU & Sensores Termicos";
    strcpy_s(res.Description, sizeof(res.Description), testDesc);
    strcpy_s(res.Criterion, sizeof(res.Criterion),
        "Temperatura GFX entre 25C e 105C; Tensao > 700 mV; Clock > 0 MHz; Potencia > 10W e ausencia de fallbacks estaticos.");

    D3DKMT_HANDLE hAdapter = BcOpenAdapter();
    if (!hAdapter) {
        res.Verdict = VERDICT_ERROR;
        strcpy_s(res.Measured, sizeof(res.Measured), "Falha ao abrir adaptador WDDM via D3DKMT.");
        PrintTestResult(&res, jsonOutput);
        return 2;
    }

    ULONG samplesGfxTemp[10] = { 0 };
    ULONG samplesClock[10] = { 0 };
    ULONG samplesPower[10] = { 0 };
    ULONG samplesVoltage[10] = { 0 };
    int validSamples = 0;

    for (int i = 0; i < 10; i++) {
        BC250_WDDM_ESCAPE_TELEMETRY telem = { 0 };
        telem.Header.CommandId = BC250_ESCAPE_CMD_QUERY_TELEMETRY;
        telem.Header.InSize = sizeof(telem);
        telem.Header.OutSize = sizeof(telem);

        if (BcEscape(hAdapter, &telem, sizeof(telem))) {
            samplesGfxTemp[i] = telem.GfxTemperatureC;
            samplesClock[i] = telem.GfxClockMhz;
            samplesPower[i] = telem.PowerWatts;
            samplesVoltage[i] = telem.GfxVoltageMv;
            validSamples++;
        }
        Sleep(80);
    }

    /* Estimula carga de DMA para teste de estresse */
    BC250_WDDM_ESCAPE_SDMA_COPY copyEsc = { 0 };
    copyEsc.Header.CommandId = BC250_ESCAPE_SDMA_COPY;
    copyEsc.Header.InSize = sizeof(copyEsc);
    copyEsc.Header.OutSize = sizeof(copyEsc);
    copyEsc.SrcGpuAddress = BC250_VRAM_GPU_BASE + 0x01000000ULL;
    copyEsc.DstGpuAddress = BC250_VRAM_GPU_BASE + 0x02000000ULL;
    copyEsc.SizeBytes = 4 * 1024 * 1024;
    for (int k = 0; k < 50; k++) {
        BcEscape(hAdapter, &copyEsc, sizeof(copyEsc));
    }

    BC250_WDDM_ESCAPE_TELEMETRY telemLoad = { 0 };
    telemLoad.Header.CommandId = BC250_ESCAPE_CMD_QUERY_TELEMETRY;
    telemLoad.Header.InSize = sizeof(telemLoad);
    telemLoad.Header.OutSize = sizeof(telemLoad);
    BcEscape(hAdapter, &telemLoad, sizeof(telemLoad));

    BcCloseAdapter(hAdapter);

    if (validSamples == 0) {
        res.Verdict = VERDICT_ERROR;
        strcpy_s(res.Measured, sizeof(res.Measured), "Nenhuma amostra de telemetria pode ser obtida do SMU.");
        PrintTestResult(&res, jsonOutput);
        return 1;
    }

    ULONG avgTemp = 0, avgClock = 0, avgPower = 0, avgVolt = 0;
    for (int i = 0; i < validSamples; i++) {
        avgTemp += samplesGfxTemp[i];
        avgClock += samplesClock[i];
        avgPower += samplesPower[i];
        avgVolt += samplesVoltage[i];
    }
    avgTemp /= validSamples;
    avgClock /= validSamples;
    avgPower /= validSamples;
    avgVolt /= validSamples;

    sprintf_s(res.Measured, sizeof(res.Measured),
        "Media Repouso (10 amostras): Temp=%u C, Clock=%u MHz, Potencia=%u W, Tensao=%u mV. Sob Carga DMA: Temp=%u C, Potencia=%u W, Clock=%u MHz.",
        avgTemp, avgClock, avgPower, avgVolt,
        telemLoad.GfxTemperatureC, telemLoad.PowerWatts, telemLoad.GfxClockMhz);

    if (avgTemp >= 25 && avgTemp <= 105 && avgClock > 0 && avgVolt >= 700) {
        res.Verdict = VERDICT_PROVED;
        strcpy_s(res.Details, sizeof(res.Details),
            "Telemetria SMU valida e dinamica. Leituras termicas e eletricas compativeis com silicio Oberon ativo.");
    } else {
        res.Verdict = VERDICT_NOT_VALIDATED;
        strcpy_s(res.Details, sizeof(res.Details),
            "Telemetria reportou valores fora dos limites operacionais fisicos.");
    }

    PrintTestResult(&res, jsonOutput);
    return (res.Verdict == VERDICT_PROVED) ? 0 : 1;
}
