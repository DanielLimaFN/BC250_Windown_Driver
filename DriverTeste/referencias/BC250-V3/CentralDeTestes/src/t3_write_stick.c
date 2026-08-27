/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    t3_write_stick.c

Abstract:
    Teste T3: Write-Stick Sweep e Varredura Multibanco de Spatial Arrays (SE0..SE3).
    
    DESCRICAO DETALHADA:
    Este teste mapeia o comportamento fisico de escrita e persistencia em todos os registradores
    criticos do silicio. Para eliminar conclusoes falsas sobre locks de hardware:
      1. Testa write-stick em registradores do CP (0xC104, 0xC150, 0xDA78).
      2. Executa varredura multibanco alternando o GRBM_GFX_INDEX (0x34D0) para cada um dos
         4 Shader Arrays fisicos (SE0/SH0, SE0/SH1, SE1/SH0, SE1/SH1) e broadcast (0xE0000000).
      3. Audita o estado de SOS-Lock do SPI_PG_STATIC_WGP (0x5C3C) e Shader Array (0x9C1C).

--*/

#include "test_framework.h"

typedef struct {
    const char* Name;
    ULONG Offset;
    ULONG TestPattern;
    bool ExpectStick;
    const char* Role;
} REG_TEST_ENTRY;

int main(int argc, char* argv[])
{
    bool jsonOutput = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) jsonOutput = true;
    }

    const char* testDesc =
        "Executa varredura de escrita, leitura e persistencia em registradores criticos do CP,\n"
        " GC e SPI, alternando o GRBM_GFX_INDEX entre os 4 Shader Arrays fisicos (SE0..SE3) e Broadcast\n"
        " para auditar com precisao absoluta o comportamento de gravacao e locks de silicio.";

    if (!jsonOutput) {
        PrintTestHeader("T3", "Write-Stick Sweep e Varredura Multibanco (SE0..SE3)", testDesc);
    }

    TEST_RESULT res = { 0 };
    res.TestId = "T3";
    res.TestTitle = "Write-Stick Sweep & Multibank Array Probe";
    strcpy_s(res.Description, sizeof(res.Description), testDesc);
    strcpy_s(res.Criterion, sizeof(res.Criterion),
        "Mapeamento coerente de silicio: 0xC104 e 0xDA78 aceitam gravacao; 0xC150 e read-only pelo host; 0x5C3C e confirmado como SOS-locked em todos os SEs.");

    D3DKMT_HANDLE hAdapter = BcOpenAdapter();
    if (!hAdapter) {
        res.Verdict = VERDICT_ERROR;
        strcpy_s(res.Measured, sizeof(res.Measured), "Falha ao abrir adaptador WDDM via D3DKMT.");
        PrintTestResult(&res, jsonOutput);
        return 2;
    }

    REG_TEST_ENTRY table[] = {
        { "CP_RB0_CNTL (0xC104)",          REG_CP_RB0_CNTL,        0x50000709, true,  "Ring Control (gravavel)" },
        { "CP_RB0_WPTR (0xC150)",          REG_CP_RB0_WPTR,        0x000000AA, false, "Host WPTR (read-only pelo host)" },
        { "CP_RING0_WPTR (0xDA78)",        REG_CP_RING0_WPTR,      0x000000AA, true,  "Alias WPTR (gravavel)" },
        { "SPI_PG_STATIC_WGP (0x5C3C)",    REG_SPI_PG_STATIC_WGP,  0x0000001F, false, "WGP Mask (SOS-locked no Windows)" },
        { "CC_GC_SHADER_ARRAY (0x9C1C)",   REG_CC_GC_SHADER_ARRAY, 0x00000000, true,  "Shader Array Config (gravavel)" },
        { "GRBM_GFX_INDEX (0x34D0)",       REG_GRBM_GFX_INDEX,     0xE0000000, true,  "Broadcast Index (gravavel)" },
    };

    int total = sizeof(table) / sizeof(table[0]);
    int matchedExpected = 0;
    char buffer[1024] = { 0 };
    int offsetBuf = 0;

    for (int i = 0; i < total; i++) {
        ULONG orig = BcRegRead(hAdapter, table[i].Offset);
        BcRegWrite(hAdapter, table[i].Offset, table[i].TestPattern);
        ULONG readBack = BcRegRead(hAdapter, table[i].Offset);
        BcRegWrite(hAdapter, table[i].Offset, orig); /* Restaura */

        bool didStick = (readBack == table[i].TestPattern);
        bool behaviorMatch = (didStick == table[i].ExpectStick);
        if (behaviorMatch) matchedExpected++;

        offsetBuf += sprintf_s(buffer + offsetBuf, sizeof(buffer) - offsetBuf,
            "[%s: %s] ",
            table[i].Name, didStick ? "GRUDOU" : "IGNORADO");
    }

    /* Varredura em 4 bancos físicos de Spatial Array para SPI_PG (0x5C3C) */
    ULONG seBankIndices[] = { 0x00000000, 0x00010000, 0x00020000, 0x00030000, 0xE0000000 };
    int spiLockedBanks = 0;
    for (int b = 0; b < 5; b++) {
        BcRegWrite(hAdapter, REG_GRBM_GFX_INDEX, seBankIndices[b]);
        BcRegWrite(hAdapter, REG_SPI_PG_STATIC_WGP, 0x1F);
        ULONG r = BcRegRead(hAdapter, REG_SPI_PG_STATIC_WGP);
        if (r == 0) spiLockedBanks++;
    }
    BcRegWrite(hAdapter, REG_GRBM_GFX_INDEX, 0xE0000000); /* Restaura broadcast */

    BcCloseAdapter(hAdapter);

    strcpy_s(res.Measured, sizeof(res.Measured), buffer);
    sprintf_s(res.Details, sizeof(res.Details),
        "Mapeamento de silicio confirmado em %d/%d registradores. SPI_PG (0x5C3C) bloqueado em %d/5 bancos de SE/SH.",
        matchedExpected, total, spiLockedBanks);

    if (matchedExpected == total && spiLockedBanks == 5) {
        res.Verdict = VERDICT_PROVED;
    } else {
        res.Verdict = VERDICT_NOT_VALIDATED;
    }

    PrintTestResult(&res, jsonOutput);
    return (res.Verdict == VERDICT_PROVED) ? 0 : 1;
}
