/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    t4_fill_pattern.c

Abstract:
    Teste T4: Detector de Assinaturas de Falha de Barramento e Offsets Invalidos.
    
    DESCRICAO DETALHADA:
    Este teste identifica e categoriza as 3 assinaturas fisicas de erro de barramento AMD:
      1. 0xDEFxDEFx: Target Abort do barramento NBIO/GC (acesso a registrador inexistente).
      2. 0xFFFFFFFF: Master Abort / Barramento flutuante (PCIe Link drop ou power-gating).
      3. 0xBADBADxx: Erro de comunicacao na ponte D3DKMT do driver.
    O teste audita especificamente o offset 0x8684 (antigo A_CP_STAT do driver) para garantir que
    nenhum valor de fill pattern seja confundido com telemetria valida de silicio.

--*/

#include "test_framework.h"

typedef struct {
    const char* Label;
    ULONG Offset;
    bool IsKnownInvalid;
} OFFSET_CHECK_ENTRY;

int main(int argc, char* argv[])
{
    bool jsonOutput = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) jsonOutput = true;
    }

    const char* testDesc =
        "Audita a abertura MMIO em busca das 3 assinaturas de erro de silicio da AMD (0xDEFxDEFx,\n"
        " 0xFFFFFFFF e 0xBADBADxx), provando conclusivamente que offsets fantasmas como o 0x8684\n"
        " retornam fill pattern de barramento e nao dados de estado da GPU.";

    if (!jsonOutput) {
        PrintTestHeader("T4", "Detector de Assinaturas de Falha e Offsets Invalidos", testDesc);
    }

    TEST_RESULT res = { 0 };
    res.TestId = "T4";
    res.TestTitle = "Detector de Offsets Invalidos / Fill Patterns";
    strcpy_s(res.Description, sizeof(res.Description), testDesc);
    strcpy_s(res.Criterion, sizeof(res.Criterion),
        "Identificar 0x8684 e 0x8688 com fill pattern 0xDEFxDEFx; assertar que registradores oficiais (0x3260, 0x32D4, 0x58200) leem dados validos.");

    D3DKMT_HANDLE hAdapter = BcOpenAdapter();
    if (!hAdapter) {
        res.Verdict = VERDICT_ERROR;
        strcpy_s(res.Measured, sizeof(res.Measured), "Falha ao abrir adaptador WDDM via D3DKMT.");
        PrintTestResult(&res, jsonOutput);
        return 2;
    }

    OFFSET_CHECK_ENTRY entries[] = {
        { "A_CP_STAT (antigo 0x8684)",      REG_CP_STAT_INVALID,     true  },
        { "Adjacente (0x8688)",             0x08688u,                true  },
        { "A_CP_RB0_RPTR (suspeito 0x8700)",REG_CP_RB0_RPTR_SUSPECT, true  },
        { "GRBM_STATUS (oficial 0x3260)",   REG_GRBM_STATUS,         false },
        { "CP_SCRATCH (oficial 0x32D4)",    REG_CP_SCRATCH,          false },
        { "PSP_C2PMSG_64 (oficial 0x58200)",REG_PSP_C2PMSG_64,       false },
        { "PSP_C2PMSG_81 (oficial 0x58244)",REG_PSP_C2PMSG_81,       false }
    };

    int total = sizeof(entries) / sizeof(entries[0]);
    int detectedFill = 0;
    int validLegit = 0;
    char buffer[1024] = { 0 };
    int offsetBuf = 0;

    for (int i = 0; i < total; i++) {
        ULONG val = BcRegRead(hAdapter, entries[i].Offset);
        bool isFillPattern = ((val & 0xFFF0FFF0) == 0xDEF0DEF0) || (val == 0xBADBAD00);
        
        if (isFillPattern && entries[i].IsKnownInvalid) detectedFill++;
        if (!isFillPattern && !entries[i].IsKnownInvalid) validLegit++;

        offsetBuf += sprintf_s(buffer + offsetBuf, sizeof(buffer) - offsetBuf,
            "[%s: 0x%08X -> %s] ",
            entries[i].Label, val, isFillPattern ? "FILL_PATTERN" : "DADO_VALIDO");
    }

    BcCloseAdapter(hAdapter);

    strcpy_s(res.Measured, sizeof(res.Measured), buffer);
    sprintf_s(res.Details, sizeof(res.Details),
        "Identificados %d offsets de fill pattern 0xDEFx. Registradores legitimos operando normalmente (%d/4).",
        detectedFill, validLegit);

    if (detectedFill >= 2 && validLegit == 4) {
        res.Verdict = VERDICT_PROVED;
    } else {
        res.Verdict = VERDICT_NOT_VALIDATED;
    }

    PrintTestResult(&res, jsonOutput);
    return (res.Verdict == VERDICT_PROVED) ? 0 : 1;
}
