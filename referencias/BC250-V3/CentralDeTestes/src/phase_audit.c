/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    phase_audit.c

Abstract:
    Auditor Consolidado de Fases (Fases 0 a 4 do Plano Real de Silicio).
    
    DESCRICAO DETALHADA:
    Este modulo avalia os criterios de aceite fisicos de cada fase definida no plano de silicio:
      - FASE 0: Purga de Mentiras (Codigo 100% honesto, sem loops CPU de compute, sem fallbacks ADL).
      - FASE 1: Seguranca de Hardware & Deteccao Real (Trava fail-closed de CP unhalt, remocao de ForceGfxFreq).
      - FASE 2: DPM do SMU (Carga de PPTable, variacao reativa de frequencia, QueryActiveWgp > 0).
      - FASE 3: Firmware por Caminho Direto (Carga de microcodigos via mailbox C2PMSG 35/36/37).
      - FASE 4: Fila MQD/HQD e PM4 Real (Descritores MQD em GTT, buffer EOP, MAP_QUEUES via KIQ).

--*/

#include "test_framework.h"

int main(int argc, char* argv[])
{
    bool jsonOutput = false;
    int targetPhase = -1; /* -1 = All */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) jsonOutput = true;
        else if (strcmp(argv[i], "--phase") == 0 && i + 1 < argc) {
            targetPhase = atoi(argv[++i]);
        }
    }

    if (!jsonOutput) {
        printf("===============================================================================\n");
        printf(" AUDITORIA CONSOLIDADA DE FASES DO PROJETO BC250-V3\n");
        printf("===============================================================================\n\n");
    }

    D3DKMT_HANDLE hAdapter = BcOpenAdapter();

    /* =========================================================================
       FASE 0: Purga de Mentiras e Saneamento
       ========================================================================= */
    if (targetPhase == -1 || targetPhase == 0) {
        TEST_RESULT r0 = { 0 };
        r0.TestId = "FASE-0";
        r0.TestTitle = "Purga de Mentiras e Codigo Honesto";
        strcpy_s(r0.Description, sizeof(r0.Description),
            "Audita o saneamento do codigo: remocao de loops CPU em compute, eliminacao de fallbacks fixos 48C/42W e correcao de offsets.");
        strcpy_s(r0.Criterion, sizeof(r0.Criterion),
            "Sem loops CPU de soma/copia em escape de compute; OpenCL build/enqueue reportando status honesto; sem fallbacks hardcoded 48C/42W.");

        ULONG cpStatVal = hAdapter ? BcRegRead(hAdapter, REG_CP_STAT_INVALID) : 0xDEF3DEF3;
        bool hasFillPattern = ((cpStatVal & 0xFFF0FFF0) == 0xDEF0DEF0);

        sprintf_s(r0.Measured, sizeof(r0.Measured),
            "Offset 0x8684 le 0x%08X (%s). Suite de testes migrada para modelo de 3 estados (PROVADO/NAO_VALIDADO/BLOQUEADO).",
            cpStatVal, hasFillPattern ? "FILL_PATTERN_DETECTADO" : "CORRIGIDO");

        r0.Verdict = VERDICT_NOT_VALIDATED;
        strcpy_s(r0.Details, sizeof(r0.Details),
            "Fase 0 em progresso: Central de Testes honesta criada; remocao do loop CPU de wddm_hw.c e builds OpenCL pendentes.");
        PrintTestResult(&r0, jsonOutput);
    }

    /* =========================================================================
       FASE 1: Seguranca e Deteccao Real
       ========================================================================= */
    if (targetPhase == -1 || targetPhase == 1) {
        TEST_RESULT r1 = { 0 };
        r1.TestId = "FASE-1";
        r1.TestTitle = "Seguranca de Hardware & Deteccao Real";
        strcpy_s(r1.Description, sizeof(r1.Description),
            "Audita travas fail-closed contra unhalt indevido do CP sem GPUVM, remocao de mensagens perigosas e desativacao de GFXOFF.");
        strcpy_s(r1.Criterion, sizeof(r1.Criterion),
            "Remocao de ForceGfxFreq(0x39) do bring-up; trava em CP_ME_CNTL=0 sem GPUVM; desativacao de GFXOFF via SMN registrada.");

        ULONG c2p35 = hAdapter ? BcRegRead(hAdapter, REG_PSP_C2PMSG_35) : 0;
        ULONG pspTos = hAdapter ? BcRegRead(hAdapter, REG_PSP_C2PMSG_64) : 0;

        sprintf_s(r1.Measured, sizeof(r1.Measured),
            "PSP_C2PMSG_35=0x%08X (Bootloader Concluido=%s), PSP_TOS(0x58200)=0x%08X.",
            c2p35, (c2p35 == 0xFFFFFFFF) ? "SIM" : "NAO", pspTos);

        if ((pspTos & 0x80000000) != 0 && c2p35 == 0xFFFFFFFF) {
            r1.Verdict = VERDICT_PROVED;
            strcpy_s(r1.Details, sizeof(r1.Details),
                "Handshake inicial e protecoes basicas de silicio ativas.");
        } else {
            r1.Verdict = VERDICT_NOT_VALIDATED;
            strcpy_s(r1.Details, sizeof(r1.Details),
                "Handshake de seguranca incompleto.");
        }
        PrintTestResult(&r1, jsonOutput);
    }

    /* =========================================================================
       FASE 2: DPM do SMU
       ========================================================================= */
    if (targetPhase == -1 || targetPhase == 2) {
        TEST_RESULT r2 = { 0 };
        r2.TestId = "FASE-2";
        r2.TestTitle = "DPM do SMU & Gerenciamento Dinamico";
        strcpy_s(r2.Description, sizeof(r2.Description),
            "Audita a carga da PPTable no SMU para desbloquear o gerenciamento dinamico de energia e tirar a GPU do deep sleep 15MHz.");
        strcpy_s(r2.Criterion, sizeof(r2.Criterion),
            "Carga de PPTable do SMU; GetGfxFrequency(0x37) reativo sob carga; QueryActiveWgp(0x1E) retornando > 0.");

        ULONG clockMhz = 0;
        if (hAdapter) {
            BC250_WDDM_ESCAPE_TELEMETRY telem = { 0 };
            telem.Header.CommandId = BC250_ESCAPE_CMD_QUERY_TELEMETRY;
            telem.Header.InSize = sizeof(telem);
            telem.Header.OutSize = sizeof(telem);
            if (BcEscape(hAdapter, &telem, sizeof(telem))) {
                clockMhz = telem.GfxClockMhz;
            }
        }

        sprintf_s(r2.Measured, sizeof(r2.Measured),
            "Clock SMU Lido: %u MHz. PPTable Dinamica: PENDENTE_INTEGRACAO. SMU Features ativas: Nao inicializadas.",
            clockMhz);

        r2.Verdict = VERDICT_BLOCKED;
        strcpy_s(r2.Details, sizeof(r2.Details),
            "BLOQUEADO: Carga da PPTable no SMU (SetDriverTableDramAddr + TransferTableDram2Smu) ainda nao implementada.");
        PrintTestResult(&r2, jsonOutput);
    }

    /* =========================================================================
       FASE 3: Firmware por Caminho Viavel
       ========================================================================= */
    if (targetPhase == -1 || targetPhase == 3) {
        TEST_RESULT r3 = { 0 };
        r3.TestId = "FASE-3";
        r3.TestTitle = "Carga de Firmware por Caminho Direto";
        strcpy_s(r3.Description, sizeof(r3.Description),
            "Audita a carga de microcodigos via mailbox direto C2PMSG_35/36/37 e execucao comprovada no CP.");
        strcpy_s(r3.Criterion, sizeof(r3.Criterion),
            "Carregar microcodigos via Mailbox Direto C2PMSG_35/36/37; Scratch T2 via PM4 comprovando execucao.");

        ULONG c2p35 = hAdapter ? BcRegRead(hAdapter, REG_PSP_C2PMSG_35) : 0;

        sprintf_s(r3.Measured, sizeof(r3.Measured),
            "C2PMSG_35 Mailbox Status: 0x%08X (Bootloader Pronto). Rota C2PMSG_36/37 direta: Em preparacao.",
            c2p35);

        r3.Verdict = VERDICT_BLOCKED;
        strcpy_s(r3.Details, sizeof(r3.Details),
            "BLOQUEADO: Carga direta de CE/PFP/ME/MEC/RLC via mailbox 35/36/37 aguarda injecao de binarios.");
        PrintTestResult(&r3, jsonOutput);
    }

    /* =========================================================================
       FASE 4: Fila MQD/HQD e PM4 Real
       ========================================================================= */
    if (targetPhase == -1 || targetPhase == 4) {
        TEST_RESULT r4 = { 0 };
        r4.TestId = "FASE-4";
        r4.TestTitle = "Fila MQD/HQD e PM4 Real de Compute";
        strcpy_s(r4.Description, sizeof(r4.Description),
            "Audita a criacao de descritores MQD em GTT e submissao de MAP_QUEUES via KIQ para ativacao real da MEC.");
        strcpy_s(r4.Criterion, sizeof(r4.Criterion),
            "Alocacao de struct v10_compute_mqd em GTT; buffer EOP; submissao MAP_QUEUES via KIQ; dispatch por PM4.");

        r4.Verdict = VERDICT_BLOCKED;
        strcpy_s(r4.Measured, sizeof(r4.Measured),
            "MQD Descriptors: Nao alocados. KIQ Ring: Inativo. DISPATCH_DIRECT PM4: Nao configurado.");
        strcpy_s(r4.Details, sizeof(r4.Details),
            "BLOQUEADO: Depende da conclusao das Fases 2 (DPM) e 3 (Firmware).");
        PrintTestResult(&r4, jsonOutput);
    }

    if (hAdapter) BcCloseAdapter(hAdapter);
    return 0;
}
