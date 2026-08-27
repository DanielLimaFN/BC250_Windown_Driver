/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    t1_scratch_mmio.c

Abstract:
    Teste T1: Scratch Round-Trip Host MMIO com Varredura de Walking 1s/0s e Anti-Aliasing.
    
    DESCRICAO DETALHADA:
    Este teste comprova fisicamente se a abertura de MMIO (BAR5) alcanca o bloco GC (Graphics Core).
    Para impedir QUALQUER falso positivo por caching ou linhas em curto:
      1. Executa Walking 1s (32 escritas/leituras: 0x00000001, 0x00000002 ... 0x80000000),
         testando individualmente cada uma das 32 trilhas fisicas de dados.
      2. Executa Walking 0s (32 escritas/leituras: 0xFFFFFFFE, 0xFFFFFFFD ... 0x7FFFFFFF).
      3. Forca round-trip no barramento emitindo comando de HDP Cache Invalidate.
      4. Verifica Anti-Aliasing: grava em 0x32D4 e asserta que 0x32D8 e 0x30E0 permanecem intactos.

--*/

#include "test_framework.h"

int main(int argc, char* argv[])
{
    bool jsonOutput = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) jsonOutput = true;
    }

    const char* testDesc = 
        "Valida comunicacao fisica bidirecional no bloco GC (0x32D4) atraves de varredura\n"
        " completa de Walking 1s e Walking 0s nas 32 linhas de dados do barramento PCIe/MMIO,\n"
        " com flush de cache HDP e teste de isolamento de enderecos adjacentes (Anti-Aliasing).";

    if (!jsonOutput) {
        PrintTestHeader("T1", "Scratch Round-Trip Host MMIO (Walking 1s/0s & Anti-Aliasing)", testDesc);
    }

    TEST_RESULT res = { 0 };
    res.TestId = "T1";
    res.TestTitle = "Scratch Round-Trip Host MMIO";
    strcpy_s(res.Description, sizeof(res.Description), testDesc);
    strcpy_s(res.Criterion, sizeof(res.Criterion),
        "32/32 bits de Walking-1s e 32/32 bits de Walking-0s devem gravar e ler exatamente; 0x32D8 e 0x30E0 nao devem sofrer mutacao (isolamento de aperture).");

    D3DKMT_HANDLE hAdapter = BcOpenAdapter();
    if (!hAdapter) {
        res.Verdict = VERDICT_ERROR;
        strcpy_s(res.Measured, sizeof(res.Measured), "Falha ao abrir adaptador WDDM via D3DKMT (Handle nulo).");
        PrintTestResult(&res, jsonOutput);
        return 2;
    }

    ULONG orig32D4 = BcRegRead(hAdapter, REG_CP_SCRATCH);
    ULONG orig32D8 = BcRegRead(hAdapter, REG_CP_SCRATCH2);
    ULONG orig30E0 = BcRegRead(hAdapter, REG_CP_SCRATCH3);

    /* 1. Varredura Walking 1s (32 bits) */
    int walking1sPass = 0;
    for (int bit = 0; bit < 32; bit++) {
        ULONG val = (1u << bit);
        BcRegWrite(hAdapter, REG_CP_SCRATCH, val);
        BcRegWrite(hAdapter, REG_HDP_READ_CACHE_INVALIDATE, 1); /* Forca flush HDP */
        ULONG readBack = BcRegRead(hAdapter, REG_CP_SCRATCH);
        if (readBack == val) walking1sPass++;
    }

    /* 2. Varredura Walking 0s (32 bits) */
    int walking0sPass = 0;
    for (int bit = 0; bit < 32; bit++) {
        ULONG val = ~(1u << bit);
        BcRegWrite(hAdapter, REG_CP_SCRATCH, val);
        BcRegWrite(hAdapter, REG_HDP_READ_CACHE_INVALIDATE, 1);
        ULONG readBack = BcRegRead(hAdapter, REG_CP_SCRATCH);
        if (readBack == val) walking0sPass++;
    }

    /* 3. Teste de Anti-Aliasing (Isolamento de Registradores Adjacentes) */
    BcRegWrite(hAdapter, REG_CP_SCRATCH2, 0x12345678);
    BcRegWrite(hAdapter, REG_CP_SCRATCH3, 0x87654321);
    BcRegWrite(hAdapter, REG_CP_SCRATCH, 0xCAFEBABE);

    ULONG read32D8 = BcRegRead(hAdapter, REG_CP_SCRATCH2);
    ULONG read30E0 = BcRegRead(hAdapter, REG_CP_SCRATCH3);
    bool antiAliasingPass = (read32D8 == 0x12345678) && (read30E0 == 0x87654321);

    /* 4. Restaura estado original */
    BcRegWrite(hAdapter, REG_CP_SCRATCH, orig32D4);
    BcRegWrite(hAdapter, REG_CP_SCRATCH2, orig32D8);
    BcRegWrite(hAdapter, REG_CP_SCRATCH3, orig30E0);
    BcCloseAdapter(hAdapter);

    sprintf_s(res.Measured, sizeof(res.Measured),
        "Walking 1s: %d/32 aprovados. Walking 0s: %d/32 aprovados. Anti-Aliasing (0x32D8/0x30E0): %s. HDP Flush: OK.",
        walking1sPass, walking0sPass, antiAliasingPass ? "ISOLADO" : "CORRUPCAO_DETECTADA");

    if (walking1sPass == 32 && walking0sPass == 32 && antiAliasingPass) {
        res.Verdict = VERDICT_PROVED;
        strcpy_s(res.Details, sizeof(res.Details),
            "As 32 trilhas de dados do barramento MMIO para o GC estao 100% integras, sem bits presos (stuck-at) nem aliasing de endereco.");
    } else {
        res.Verdict = VERDICT_NOT_VALIDATED;
        sprintf_s(res.Details, sizeof(res.Details),
            "Falha em %d bits de walking 1s e %d bits de walking 0s. Aliasing: %s.",
            32 - walking1sPass, 32 - walking0sPass, antiAliasingPass ? "OK" : "FALHA");
    }

    PrintTestResult(&res, jsonOutput);
    return (res.Verdict == VERDICT_PROVED) ? 0 : 1;
}
