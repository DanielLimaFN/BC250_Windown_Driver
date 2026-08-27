/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_cp_execution.c

Abstract:
    T2 - Prova de execucao do Command Processor via scratch register.

    Este e' o teste mais importante da suite e custa 8 dwords.

    Ideia: o host grava um valor A em SCRATCH_REG0. Em seguida submete um pacote
    PM4 SET_UCONFIG_REG que manda o CP gravar um valor B DIFERENTE no mesmo
    registrador. Se, depois da submissao, o host ler B, entao foi o CP que
    escreveu — nao existe outra explicacao. Nenhum caminho de CPU no driver toca
    esse registrador.

    Isto separa de forma definitiva "o ring aceitou o pacote" de "o silicio
    executou o pacote". O teste antigo de PM4 declarava
    "COMMAND PROCESSOR PM4 RING 0 100% OPERACIONAL" com RPTR parado em zero.

    Provas exigidas pela regra BC-250 cobertas aqui:
      2. comando submetido a um ring fisico valido
      3. WPTR avancando
      4. RPTR avancando ate alcancar o WPTR
      6. resultado produzido pela GPU comparado ao esperado
      7. GRBM/engine mudando durante a carga

--*/

#include "test_verdict.h"

#define PM4_TYPE3_HEADER(op, count)  (0xC0000000u | (((count) - 1u) << 16) | ((op) << 8))
#define PM4_SET_UCONFIG_REG          0x79u
#define PM4_NOP                      0x10u

/* mmSCRATCH_REG0 = 0x2040, BASE_IDX 1. Em PM4 SET_UCONFIG_REG o offset e'
   relativo a' base UCONFIG (0xC000 em dwords para GFX10). */
#define UCONFIG_SPACE_START          0xC000u
#define MM_SCRATCH_REG0              0x2040u

/* Espelha BC250_WDDM_ESCAPE_SUBMIT de wddm.h. */
typedef struct _BC_ESC_PM4 {
    BC250_ESCAPE_HEADER Header;
    ULONG DwordCount;
    ULONG RptrBefore;
    ULONG RptrAfter;
    ULONG Data[256];
} BC_ESC_PM4;

int main(void)
{
    D3DKMT_HANDLE h;
    BC_ESC_PM4 pm4 = { 0 };
    ULONG valorHost   = 0xAAAA1111u;
    ULONG valorGpu    = 0xBBBB2222u;
    ULONG scratchAntes, scratchDepois;
    ULONG wptrAntes, wptrDepois, rptrAntes, rptrDepois;
    ULONG grbmAntes, grbmDurante;
    ULONG cpStat, meCntl;
    int i;
    int submeteuOk;

    BcInicio("T2: Prova de Execucao do Command Processor (scratch via PM4)");

    h = BcAbrirAdaptadorVivo();
    if (h == 0) {
        BcBloquear("Adaptador WDDM indisponivel ou nao responde. Se um teste\n"
                   "            anterior estourou o TDR, o handle esta morto: reinicie\n"
                   "            a bateria. Nao ha' medicao valida neste estado.");
        return BcVeredito();
    }

    /*-----------------------------------------------------------------------
      Pre-requisito: o CP so' executa se tiver microcodigo.
    -----------------------------------------------------------------------*/
    cpStat = BcRegLer(h, T_CP_STAT);
    meCntl = BcRegLer(h, T_CP_ME_CNTL);
    BcInfo("CP_STAT = 0x%08X   CP_ME_CNTL = 0x%08X", cpStat, meCntl);

    if (BcEhFillPattern(cpStat)) {
        BcBloquear("CP_STAT devolve padrao de fill (0x%08X) - offset invalido. "
                   "Rode test_regmap primeiro.", cpStat);
        return BcVeredito();
    }

    /*-----------------------------------------------------------------------
      Passo 1: host marca o scratch
    -----------------------------------------------------------------------*/
    printf("\n--- Passo 1: host grava o valor de referencia ---\n");
    BcCriterio("SCRATCH_REG0 deve conter 0x%08X apos escrita do host.", valorHost);
    BcRegGravar(h, T_SCRATCH_REG0, valorHost);
    scratchAntes = BcRegLer(h, T_SCRATCH_REG0);
    BcMedido("SCRATCH_REG0 = 0x%08X", scratchAntes);
    if (scratchAntes != valorHost) {
        BcBloquear("O host nao consegue escrever em SCRATCH_REG0 (lido 0x%08X). "
                   "O bloco GC nao esta alcancavel; T1 tem de passar antes.",
                   scratchAntes);
        return BcVeredito();
    }

    /*-----------------------------------------------------------------------
      Passo 2: monta o pacote PM4 que manda o CP gravar outro valor
    -----------------------------------------------------------------------*/
    printf("\n--- Passo 2: submissao do pacote PM4 ---\n");
    BcCriterio("O CP deve consumir SET_UCONFIG_REG e gravar 0x%08X no scratch.",
               valorGpu);

    pm4.Header.CommandId = BC250_ESCAPE_CMD_SUBMIT_PM4;
    pm4.Header.InSize  = sizeof(pm4);
    pm4.Header.OutSize = sizeof(pm4);

    i = 0;
    /* SET_UCONFIG_REG: header, offset relativo, valor */
    pm4.Data[i++] = PM4_TYPE3_HEADER(PM4_SET_UCONFIG_REG, 3);
    pm4.Data[i++] = MM_SCRATCH_REG0 - UCONFIG_SPACE_START;
    pm4.Data[i++] = valorGpu;
    /* Padding com NOPs para alinhar o ring */
    pm4.Data[i++] = PM4_TYPE3_HEADER(PM4_NOP, 2);
    pm4.Data[i++] = 0x00000000u;
    pm4.DwordCount = (ULONG)i;

    BcInfo("Pacote (%u dwords):", pm4.DwordCount);
    for (i = 0; i < (int)pm4.DwordCount; i++) {
        BcInfo("  [%d] 0x%08X", i, pm4.Data[i]);
    }

    wptrAntes = BcRegLer(h, T_CP_RB0_WPTR);
    rptrAntes = BcRegLer(h, T_CP_RB0_RPTR);
    grbmAntes = BcRegLer(h, T_GRBM_STATUS);

    submeteuOk = CallBc250Escape(h, &pm4, sizeof(pm4));

    grbmDurante = BcRegLer(h, T_GRBM_STATUS);
    wptrDepois  = BcRegLer(h, T_CP_RB0_WPTR);

    /* Da' ao CP tempo de consumir o ring antes de julgar o RPTR. */
    for (i = 0; i < 200; i++) {
        rptrDepois = BcRegLer(h, T_CP_RB0_RPTR);
        if (rptrDepois == wptrDepois) break;
        Sleep(1);
    }
    rptrDepois = BcRegLer(h, T_CP_RB0_RPTR);

    Sleep(20);
    scratchDepois = BcRegLer(h, T_SCRATCH_REG0);

    /*-----------------------------------------------------------------------
      Passo 3: avaliacao — cada prova separada
    -----------------------------------------------------------------------*/
    printf("\n--- Passo 3: avaliacao das provas fisicas ---\n");
    BcMedido("Escape de submissao   : %s", submeteuOk ? "aceito" : "recusado");
    BcMedido("RPTR relatado pelo KMD: antes=0x%08X  depois=0x%08X",
             pm4.RptrBefore, pm4.RptrAfter);
    BcMedido("WPTR   antes=0x%08X  depois=0x%08X", wptrAntes, wptrDepois);
    BcMedido("RPTR   antes=0x%08X  depois=0x%08X", rptrAntes, rptrDepois);
    BcMedido("GRBM   antes=0x%08X  durante=0x%08X", grbmAntes, grbmDurante);
    BcMedido("SCRATCH antes=0x%08X  depois=0x%08X  (esperado 0x%08X)",
             scratchAntes, scratchDepois, valorGpu);

    BcProva(submeteuOk,
            "Prova 2: comando aceito por um ring fisico valido");
    BcProva(wptrDepois != wptrAntes,
            "Prova 3: WPTR avancou apos a submissao");
    BcProva(rptrDepois == wptrDepois,
            "Prova 4: RPTR alcancou o WPTR (o CP consumiu o ring)");
    BcProva(scratchDepois == valorGpu,
            "Prova 6: a GPU escreveu o valor esperado no scratch");
    BcProva(grbmDurante != grbmAntes,
            "Prova 7: GRBM_STATUS mudou durante a carga");

    /* Restaura */
    BcRegGravar(h, T_SCRATCH_REG0, 0);

    if (scratchDepois == valorHost) {
        printf("\n            Diagnostico: o scratch continua com o valor do host.\n");
        printf("            O pacote entrou no ring mas o CP nao o executou.\n");
        printf("            Causa provavel: microcodigo do CP nao carregado\n");
        printf("            (o SOS da BC-250 recusa LOAD_IP_FW para CP/RLC/SDMA).\n");
    }

    return BcVeredito();
}
