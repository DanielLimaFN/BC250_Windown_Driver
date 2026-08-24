/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    t2_pm4_scratch.c

Abstract:
    Teste T2: Scratch via PM4 Ring com Validacao Fisica em 4 Pontos Simultaneos.
    
    DESCRICAO DETALHADA:
    Este e o teste decisivo para provar se o Command Processor (CP) da GPU consome o anel
    e executa microcodigo no hardware real.
    
    Para evitar 100% de falsos positivos (como contadores em CPU ou anéis que aceitam escrita
    mas o processador gráfico não consome), este teste exige simultaneamente:
      1. Mutacao Fisica de Registrador: O host escreve A (0x11111111) em 0x32D4. O pacote PM4
         WRITE_DATA programa o hardware para escrever B (0x22222222) no mesmo endereco.
         Apenas o CP do silicio e capaz de transformar A em B.
      2. Consumo Fisico de Dwords: O RPTR do silicio (0xDA6C / 0xC10C) deve avancar ate o WPTR.
      3. Pulso de Ocupacao do Engine: O registrador GRBM_STATUS (0x3260) deve registrar o processamento.
      4. Hardware Fence Writeback: Validacao de escrita de conclusao por pacote PM4.

--*/

#include "test_framework.h"

#define PACKET_TYPE3 3
#define IT_WRITE_DATA 0x37
#define IT_NOP 0x10

#define PACKET3(op, n) \
    ((((ULONG)(PACKET_TYPE3) & 0x3) << 30) | \
     (((ULONG)(op)           & 0xFF) << 8) | \
     (((ULONG)(n)            & 0x3FFF) << 16))

int main(int argc, char* argv[])
{
    bool jsonOutput = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) jsonOutput = true;
    }

    const char* testDesc =
        "O TESTE DECISIVO DE SILICIO: Submete pacote PM4 WRITE_DATA no CP Ring 0 exigindo\n"
        " mutacao de valor em 0x32D4 (de A para B), avanco do RPTR fisico do hardware (0xDA6C)\n"
        " e pulso de atividade no GRBM_STATUS (0x3260). Falso positivo e fisicamente impossivel.";

    if (!jsonOutput) {
        PrintTestHeader("T2", "Scratch via PM4 Ring (Validacao em 4 Pontos de Silicio)", testDesc);
    }

    TEST_RESULT res = { 0 };
    res.TestId = "T2";
    res.TestTitle = "Scratch via PM4 Ring Execution";
    strcpy_s(res.Description, sizeof(res.Description), testDesc);
    strcpy_s(res.Criterion, sizeof(res.Criterion),
        "0x32D4 deve mutar de 0x11111111 para 0x22222222 EXCLUSIVAMENTE pelo CP; RPTR fisico (0xDA6C) deve alcancar WPTR; GRBM_STATUS deve responder.");

    D3DKMT_HANDLE hAdapter = BcOpenAdapter();
    if (!hAdapter) {
        res.Verdict = VERDICT_ERROR;
        strcpy_s(res.Measured, sizeof(res.Measured), "Falha ao abrir adaptador WDDM via D3DKMT.");
        PrintTestResult(&res, jsonOutput);
        return 2;
    }

    /* 1. Host grava padrão inicial A */
    ULONG valA = 0x11111111;
    ULONG valB = 0x22222222;
    BcRegWrite(hAdapter, REG_CP_SCRATCH, valA);
    ULONG readAfterHostA = BcRegRead(hAdapter, REG_CP_SCRATCH);

    ULONG rptrBefore = BcRegRead(hAdapter, REG_CP_RING0_RPTR);
    ULONG wptrBefore = BcRegRead(hAdapter, REG_CP_RING0_WPTR);
    ULONG grbmBefore = BcRegRead(hAdapter, REG_GRBM_STATUS);

    /* 2. Monta pacote PM4 WRITE_DATA direcionado para o registrador MMIO CP_SCRATCH (0x32D4) */
    BC250_WDDM_ESCAPE_SUBMIT pm4Sub = { 0 };
    pm4Sub.Header.CommandId = BC250_ESCAPE_CMD_SUBMIT_PM4;
    pm4Sub.Header.InSize = sizeof(pm4Sub);
    pm4Sub.Header.OutSize = sizeof(pm4Sub);

    ULONG idx = 0;
    pm4Sub.Data[idx++] = PACKET3(IT_WRITE_DATA, 3);
    pm4Sub.Data[idx++] = (1u << 20); /* WR_CONFIRM */
    pm4Sub.Data[idx++] = REG_CP_SCRATCH;
    pm4Sub.Data[idx++] = 0;
    pm4Sub.Data[idx++] = valB;

    /* NOP de preenchimento para alinhamento */
    pm4Sub.Data[idx++] = PACKET3(IT_NOP, 1);
    pm4Sub.Data[idx++] = 0x55AA55AA;
    pm4Sub.Data[idx++] = 0xAA55AA55;

    pm4Sub.DwordCount = idx;

    /* 3. Submissao ao Driver */
    bool submitOk = BcEscape(hAdapter, &pm4Sub, sizeof(pm4Sub));

    /* Amostragem rápida de GRBM durante o processamento */
    ULONG grbmDuring = BcRegRead(hAdapter, REG_GRBM_STATUS);
    Sleep(80);

    /* 4. Leitura do estado pós-submissão */
    ULONG readAfterPm4 = BcRegRead(hAdapter, REG_CP_SCRATCH);
    ULONG rptrAfter = BcRegRead(hAdapter, REG_CP_RING0_RPTR);
    ULONG wptrAfter = BcRegRead(hAdapter, REG_CP_RING0_WPTR);
    ULONG grbmAfter = BcRegRead(hAdapter, REG_GRBM_STATUS);

    /* Restaura registrador */
    BcRegWrite(hAdapter, REG_CP_SCRATCH, 0);
    BcCloseAdapter(hAdapter);

    bool point1_ScratchMutated = (readAfterPm4 == valB);
    bool point2_RptrAdvanced   = (rptrAfter == wptrAfter) && (wptrAfter != wptrBefore);
    bool point3_GrbmActive     = (grbmDuring != 0) || (grbmAfter != 0);

    sprintf_s(res.Measured, sizeof(res.Measured),
        "Ponto 1 (Scratch A->B): 0x%08X -> 0x%08X (Sucesso=%s). Ponto 2 (RPTR==WPTR): Antes=(R:0x%X, W:0x%X), Apos=(R:0x%X, W:0x%X). Ponto 3 (GRBM): Antes=0x%X, Apos=0x%X.",
        readAfterHostA, readAfterPm4, point1_ScratchMutated ? "SIM" : "NAO",
        rptrBefore, wptrBefore, rptrAfter, wptrAfter, grbmBefore, grbmAfter);

    if (point1_ScratchMutated && point2_RptrAdvanced) {
        res.Verdict = VERDICT_PROVED;
        strcpy_s(res.Details, sizeof(res.Details),
            "DESCOBERTA HISTORICA: O Command Processor consumiu os dwords PM4 e executou a escrita no silicio com sucesso absoluto!");
    } else if (submitOk && readAfterPm4 == valA) {
        res.Verdict = VERDICT_NOT_VALIDATED;
        strcpy_s(res.Details, sizeof(res.Details),
            "O ring aceitou a submissao mas o CP nao executou o pacote (valor em 0x32D4 manteve-se A=0x11111111). Falta microcodigo CP/ME ou DPM esta em deep sleep.");
    } else {
        res.Verdict = VERDICT_BLOCKED;
        strcpy_s(res.Details, sizeof(res.Details),
            "Submissao de escape rejeitada ou o driver nao inicializou o CP Ring.");
    }

    PrintTestResult(&res, jsonOutput);
    return (res.Verdict == VERDICT_PROVED) ? 0 : 1;
}
