/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_pm4_ring.c

Abstract:
    Teste 5: CP PM4 Ring 0.

    Fase 0: reescrito. A versao anterior imprimia
    "COMMAND PROCESSOR PM4 RING 0 100% OPERACIONAL" logo abaixo de
    "RPTR Final: 0x00000000" — ou seja, declarava sucesso exatamente na
    condicao que prova que o CP nao executou nada.

    Agora o criterio e' declarado antes da medicao e o veredito e' derivado
    das provas 2, 3 e 4 da regra BC-250.

    Para a prova de execucao propriamente dita (prova 6), veja
    test_cp_execution.c — este teste cobre so' a mecanica do ring.

--*/

#include "test_verdict.h"

typedef struct _BC_ESC_PM4 {
    BC250_ESCAPE_HEADER Header;
    ULONG DwordCount;
    ULONG RptrBefore;
    ULONG RptrAfter;
    ULONG Data[256];
} BC_ESC_PM4;

#define PM4_TYPE3_HEADER(op, count)  (0xC0000000u | (((count) - 1u) << 16) | ((op) << 8))
#define PM4_NOP                      0x10u

int main(void)
{
    D3DKMT_HANDLE h;
    BC_ESC_PM4 pm4 = { 0 };
    ULONG base, wptrAntes, wptrDepois, rptrAntes, rptrDepois;
    int i, submeteuOk;

    BcInicio("Teste 5: Command Processor PM4 Ring 0");

    h = BcAbrirAdaptadorVivo();
    if (h == 0) {
        BcBloquear("Adaptador WDDM indisponivel ou nao responde. Se um teste\n"
                   "            anterior estourou o TDR, o handle esta morto: reinicie\n"
                   "            a bateria. Nao ha' medicao valida neste estado.");
        return BcVeredito();
    }

    base      = BcRegLer(h, T_CP_RB0_BASE);
    wptrAntes = BcRegLer(h, T_CP_RB0_WPTR);
    rptrAntes = BcRegLer(h, T_CP_RB0_RPTR);

    printf("--- Estado inicial do ring ---\n");
    BcMedido("CP_RB0_BASE = 0x%08X", base);
    BcMedido("CP_RB0_CNTL = 0x%08X", BcRegLer(h, T_CP_RB0_CNTL));
    BcMedido("WPTR = 0x%08X   RPTR = 0x%08X", wptrAntes, rptrAntes);

    if (base == 0) {
        BcBloquear("CP_RB0_BASE esta zerado: o ring nunca foi programado.");
        return BcVeredito();
    }

    printf("\n--- Submissao ---\n");
    BcCriterio("Apos submeter NOPs, o WPTR deve avancar E o RPTR deve\n"
               "            alcancar o WPTR dentro de 200 ms.");

    pm4.Header.CommandId = BC250_ESCAPE_CMD_SUBMIT_PM4;
    pm4.Header.InSize  = sizeof(pm4);
    pm4.Header.OutSize = sizeof(pm4);
    for (i = 0; i < 4; i++) {
        pm4.Data[i * 2 + 0] = PM4_TYPE3_HEADER(PM4_NOP, 2);
        pm4.Data[i * 2 + 1] = 0x00000000u;
    }
    pm4.DwordCount = 8;

    submeteuOk = CallBc250Escape(h, &pm4, sizeof(pm4));

    wptrDepois = BcRegLer(h, T_CP_RB0_WPTR);
    for (i = 0; i < 200; i++) {
        rptrDepois = BcRegLer(h, T_CP_RB0_RPTR);
        if (rptrDepois == wptrDepois) break;
        Sleep(1);
    }
    rptrDepois = BcRegLer(h, T_CP_RB0_RPTR);

    printf("\n--- Resultado ---\n");
    BcMedido("Escape: %s", submeteuOk ? "aceito" : "recusado");
    BcMedido("WPTR: 0x%08X -> 0x%08X  (delta %d dwords)",
             wptrAntes, wptrDepois, (int)(wptrDepois - wptrAntes));
    BcMedido("RPTR: 0x%08X -> 0x%08X  (esperado alcancar 0x%08X)",
             rptrAntes, rptrDepois, wptrDepois);
    BcMedido("RPTR relatado pelo KMD: antes=0x%08X depois=0x%08X",
             pm4.RptrBefore, pm4.RptrAfter);

    BcProva(submeteuOk,
            "Prova 2: pacote aceito por um ring fisico valido");
    BcProva(wptrDepois != wptrAntes,
            "Prova 3: WPTR avancou apos a submissao");
    BcProva(rptrDepois == wptrDepois,
            "Prova 4: RPTR alcancou o WPTR (o CP consumiu o ring)");

    if (rptrDepois != wptrDepois) {
        printf("\n            Diagnostico: o pacote foi escrito no ring e o WPTR\n");
        printf("            avancou, mas o CP nao consumiu. Um ring habilitado e um\n");
        printf("            WPTR alterado NAO provam execucao.\n");
    }

    return BcVeredito();
}
