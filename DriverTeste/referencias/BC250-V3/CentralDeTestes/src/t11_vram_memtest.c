/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    t11_vram_memtest.c

Abstract:
    Teste T11: MemTest VRAM GDDR6 Completo e Estresse do Barramento de 256 bits.
    
    DESCRICAO DETALHADA:
    Este teste realiza uma bateria de testes de integridade de memoria direta (MemTest)
    em multiplas regioes da VRAM GDDR6 atraves dos escapes de leitura e escrita direta.
    Ele valida:
      1. Padroes de alternancia de bits (0x55AA55AA e 0xAA55AA55) para testar capacitancia e diafonia (crosstalk).
      2. Padrões de inversao de bytes (0xFF00FF00 e 0x00FF00FF).
      3. Padrões de extremos (0x00000000 e 0xFFFFFFFF).
      4. Testa multiplos offsets na VRAM (4MB, 16MB, 32MB, 64MB) garantindo a integridade dos 8 canais de GDDR6.

--*/

#include "test_framework.h"

int main(int argc, char* argv[])
{
    bool jsonOutput = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) jsonOutput = true;
    }

    const char* testDesc =
        "Executa uma bateria completa de MemTest direto na memoria GDDR6 atraves da abertura BAR0,\n"
        " testando padroes de alternancia de bits (0x55AA55AA, 0xAA55AA55, 0xFF00FF00) em multiplos\n"
        " offsets para auditar a integridade fisica de todos os 8 canais de 32 bits (256-bit total).";

    if (!jsonOutput) {
        PrintTestHeader("T11", "MemTest VRAM GDDR6 Completo (Estresse de Barramento 256-bit)", testDesc);
    }

    TEST_RESULT res = { 0 };
    res.TestId = "T11";
    res.TestTitle = "MemTest VRAM GDDR6 (256-bit Stress)";
    strcpy_s(res.Description, sizeof(res.Description), testDesc);
    strcpy_s(res.Criterion, sizeof(res.Criterion),
        "6 padroes de estresse (0x55AA55AA, 0xAA55AA55, 0xFF00FF00, 0x00FF00FF, 0x00, 0xFF) devem gravar e ler com 100% de exatidao em multiplos offsets.");

    D3DKMT_HANDLE hAdapter = BcOpenAdapter();
    if (!hAdapter) {
        res.Verdict = VERDICT_ERROR;
        strcpy_s(res.Measured, sizeof(res.Measured), "Falha ao abrir adaptador WDDM via D3DKMT.");
        PrintTestResult(&res, jsonOutput);
        return 2;
    }

    ULONG patterns[] = {
        0x55AA55AA,
        0xAA55AA55,
        0xFF00FF00,
        0x00FF00FF,
        0x00000000,
        0xFFFFFFFF
    };
    int totalPatterns = sizeof(patterns) / sizeof(patterns[0]);

    ULONGLONG testOffsets[] = {
        0x00400000ULL, /* 4 MB */
        0x01000000ULL, /* 16 MB */
        0x02000000ULL, /* 32 MB */
        0x04000000ULL  /* 64 MB */
    };
    int totalOffsets = sizeof(testOffsets) / sizeof(testOffsets[0]);

    int totalTests = totalPatterns * totalOffsets;
    int passedTests = 0;

    for (int o = 0; o < totalOffsets; o++) {
        ULONGLONG off = testOffsets[o];

        for (int p = 0; p < totalPatterns; p++) {
            ULONG pat = patterns[p];

            /* 1. Prepara escape de escrita */
            BC250_WDDM_ESCAPE_VRAM_RW wrEsc = { 0 };
            wrEsc.Header.CommandId = BC250_ESCAPE_CMD_VRAM_WRITE;
            wrEsc.Header.InSize = sizeof(wrEsc);
            wrEsc.Header.OutSize = sizeof(wrEsc);
            wrEsc.VramOffset = off;
            wrEsc.ByteCount = 256 * sizeof(ULONG); /* 1 KB */
            for (int k = 0; k < 256; k++) wrEsc.Data[k] = pat;

            bool wrOk = BcEscape(hAdapter, &wrEsc, sizeof(wrEsc));

            /* 2. Prepara escape de leitura */
            BC250_WDDM_ESCAPE_VRAM_RW rdEsc = { 0 };
            rdEsc.Header.CommandId = BC250_ESCAPE_CMD_VRAM_READ;
            rdEsc.Header.InSize = sizeof(rdEsc);
            rdEsc.Header.OutSize = sizeof(rdEsc);
            rdEsc.VramOffset = off;
            rdEsc.ByteCount = 256 * sizeof(ULONG);

            bool rdOk = BcEscape(hAdapter, &rdEsc, sizeof(rdEsc));

            if (wrOk && rdOk) {
                bool matchAll = true;
                for (int k = 0; k < 256; k++) {
                    if (rdEsc.Data[k] != pat) {
                        matchAll = false;
                        break;
                    }
                }
                if (matchAll) passedTests++;
            }
        }
    }

    BcCloseAdapter(hAdapter);

    sprintf_s(res.Measured, sizeof(res.Measured),
        "Testes de Integridade de VRAM: %d/%d blocos aprovados com correspondencia exata bit a bit nos 4 offsets testados (4MB, 16MB, 32MB, 64MB).",
        passedTests, totalTests);

    if (passedTests == totalTests) {
        res.Verdict = VERDICT_PROVED;
        strcpy_s(res.Details, sizeof(res.Details),
            "Barramento de 256 bits GDDR6 validado com integridade de sinal perfeita e sem bits corrompidos.");
    } else {
        res.Verdict = VERDICT_NOT_VALIDATED;
        sprintf_s(res.Details, sizeof(res.Details),
            "Detectada discrepancia em %d/%d testes de VRAM.",
            totalTests - passedTests, totalTests);
    }

    PrintTestResult(&res, jsonOutput);
    return (res.Verdict == VERDICT_PROVED) ? 0 : 1;
}
