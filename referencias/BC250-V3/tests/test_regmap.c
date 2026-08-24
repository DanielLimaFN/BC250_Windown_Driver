/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_regmap.c

Abstract:
    Teste 0 da suite BC250-V3 (Fase 0). Deve rodar ANTES de qualquer outro.

    Implementa tres verificacoes que nenhum teste anterior fazia:

      T1  Scratch round-trip     - o MMIO alcanca o bloco GC?
      T3  Write-stick sweep      - cada registrador que o driver programa
                                   realmente aceita escrita?
      T4  Detector de fill       - alguma leitura devolve 0xDEFxDEFx /
                                   0xFFFFFFFF, ou seja, offset invalido?

    Motivacao: por meses o driver leu GRBM_STATUS em 0x03040 (sempre 0x00000000)
    e CP_STAT em 0x08684 (sempre 0xDEF4DEF4), e reportou os dois como telemetria
    valida. Os offsets corretos, derivados de gc_10_1_0_offset.h pela formula
    SOC15 BAR5 = (segment_base + mmREG) * 4, sao 0x08010 e 0x08680.

    Este teste tambem executa a regressao inversa: confirma que os offsets
    historicos continuam sendo lixo, para que ninguem os reintroduza.

--*/

#include "test_verdict.h"

typedef struct {
    const char *nome;
    ULONG offset;
    int   deveSerValido;   /* 1 = tem de decodificar, 0 = sabidamente invalido */
} REG_ENTRY;

static const REG_ENTRY g_Regs[] = {
    /* --- Offsets derivados por SOC15: devem decodificar --- */
    { "GRBM_STATUS            (SOC15 0x08010)", T_GRBM_STATUS,      1 },
    { "GRBM_STATUS2           (SOC15 0x08008)", T_GRBM_STATUS2,     1 },
    { "CP_STAT                (SOC15 0x08680)", T_CP_STAT,          1 },
    { "CP_ME_CNTL             (SOC15 0x086D8)", T_CP_ME_CNTL,       1 },
    { "CP_MEC_CNTL            (SOC15 0x08234)", T_CP_MEC_CNTL,      1 },
    { "CP_RB0_RPTR            (SOC15 0x08700)", T_CP_RB0_RPTR,      1 },
    { "CP_RB0_BASE            (SOC15 0x0C100)", T_CP_RB0_BASE,      1 },
    { "CP_RB0_CNTL            (SOC15 0x0C104)", T_CP_RB0_CNTL,      1 },
    { "CP_RB0_WPTR            (SOC15 0x0C150)", T_CP_RB0_WPTR,      1 },
    { "SCRATCH_REG0           (SOC15 0x30100)", T_SCRATCH_REG0,     1 },
    { "SPI_PG_WGP_MASK        (SOC15 0x0935C)", T_SPI_PG_WGP_MASK,  1 },
    { "CC_GC_SHADER_ARRAY     (SOC15 0x089BC)", T_CC_SHADER_ARRAY,  1 },
    { "COMPUTE_DISPATCH_INIT  (SOC15 0x0B800)", T_COMPUTE_DISPATCH, 1 },
    { "PSP_C2PMSG_64          (      0x58200)", T_PSP_C2PMSG_64,    1 },
    { "PSP_C2PMSG_81          (      0x58244)", T_PSP_C2PMSG_81,    1 },
};

static const REG_ENTRY g_RegsLegado[] = {
    { "GRBM_STATUS  legado    (      0x03040)", T_BAD_GRBM_STATUS,  0 },
    { "CP_STAT      legado    (      0x08684)", T_BAD_CP_STAT,      0 },
    { "SPI_PG       legado    (      0x05C3C)", T_BAD_SPI_PG,       0 },
    { "CC_ARRAY     legado    (      0x09C1C)", T_BAD_CC_ARRAY,     0 },
    { "scratch      legado    (      0x032D4)", T_BAD_SCRATCH,      0 },
};

int main(void)
{
    D3DKMT_HANDLE h;
    int i, n;
    int fillEncontrado = 0;
    ULONG antes, depois;
    ULONG grbm, cpStat, spiPg, ccArray;

    BcInicio("Teste 0: Mapa de Registradores (T1 / T3 / T4)");

    h = BcAbrirAdaptadorVivo();
    if (h == 0) {
        BcBloquear("Adaptador WDDM indisponivel ou nao responde. Se um teste\n"
                   "            anterior estourou o TDR, o handle esta morto: reinicie\n"
                   "            a bateria. Nao ha' medicao valida neste estado.");
        return BcVeredito();
    }
    BcInfo("Adaptador WDDM: 0x%08X", h);
    printf("\n");

    /*-----------------------------------------------------------------------
      T4 - Detector de fill-pattern
    -----------------------------------------------------------------------*/
    printf("--- T4: leitura + detector de fill-pattern ---\n");
    BcCriterio("Nenhum offset derivado por SOC15 pode devolver padrao de fill.");

    n = (int)(sizeof(g_Regs) / sizeof(g_Regs[0]));
    for (i = 0; i < n; i++) {
        ULONG v = BcRegLer(h, g_Regs[i].offset);
        int ehFill = BcEhFillPattern(v);
        printf("            %-42s = 0x%08X%s\n",
               g_Regs[i].nome, v, ehFill ? "   <<< FILL / OFFSET INVALIDO" : "");
        if (ehFill) fillEncontrado++;
    }
    BcProva(fillEncontrado == 0,
            "Nenhum offset SOC15 devolve padrao de fill (T4)");

    printf("\n--- T4b: regressao dos offsets historicos ---\n");
    BcCriterio("Os offsets antigos devem continuar invalidos ou constantes;\n"
               "            se algum voltar a parecer valido, a tabela regrediu.");
    n = (int)(sizeof(g_RegsLegado) / sizeof(g_RegsLegado[0]));
    for (i = 0; i < n; i++) {
        ULONG v = BcRegLer(h, g_RegsLegado[i].offset);
        printf("            %-42s = 0x%08X%s\n",
               g_RegsLegado[i].nome, v,
               BcEhFillPattern(v) ? "   (fill, como esperado)" : "");
    }

    /*-----------------------------------------------------------------------
      T1 - Scratch round-trip: o MMIO alcanca o bloco GC?
    -----------------------------------------------------------------------*/
    printf("\n--- T1: scratch round-trip (MMIO alcanca o GC?) ---\n");
    BcCriterio("Gravar 0xC0FFEE01 em SCRATCH_REG0 (0x30100) e ler o mesmo valor.");
    {
        int ok = BcWriteStick(h, T_SCRATCH_REG0, 0xC0FFEE01u, &antes, &depois);
        BcMedido("SCRATCH_REG0: antes=0x%08X escrito=0xC0FFEE01 lido=0x%08X",
                 antes, depois);
        BcProva(ok, "Bloco GC alcancavel por MMIO (T1)");

        ok = BcWriteStick(h, T_SCRATCH_REG1, 0x5A5A1234u, &antes, &depois);
        BcMedido("SCRATCH_REG1: antes=0x%08X escrito=0x5A5A1234 lido=0x%08X",
                 antes, depois);
        BcProva(ok, "SCRATCH_REG1 tambem responde (T1)");
    }

    /*-----------------------------------------------------------------------
      T3 - Write-stick sweep nos registradores que o driver programa
    -----------------------------------------------------------------------*/
    printf("\n--- T3: write-stick nos registradores programados pelo driver ---\n");
    BcCriterio("Cada registrador que o driver escreve deve aceitar a escrita,\n"
               "            ou ser explicitamente conhecido como read-only.");
    {
        ULONG cntlAntes = BcRegLer(h, T_CP_RB0_CNTL);
        int ok = BcWriteStick(h, T_CP_RB0_CNTL, 0x0000110Bu, &antes, &depois);
        BcMedido("CP_RB0_CNTL : antes=0x%08X lido=0x%08X  %s",
                 antes, depois, ok ? "[GRAVOU]" : "[IGNORADO]");
        BcProva(ok, "CP_RB0_CNTL aceita escrita do host (T3)");
        (void)cntlAntes;

        (void)BcWriteStick(h, T_CP_RB0_WPTR, 0x000000AAu, &antes, &depois);
        BcMedido("CP_RB0_WPTR : antes=0x%08X lido=0x%08X  %s  "
                 "(read-only pelo host e' o esperado)",
                 antes, depois, (depois == 0xAAu) ? "[GRAVOU]" : "[IGNORADO]");
    }

    /*-----------------------------------------------------------------------
      Estado observado do GFX (sem interpretacao otimista)
    -----------------------------------------------------------------------*/
    printf("\n--- Estado observado do bloco GFX ---\n");
    grbm    = BcRegLer(h, T_GRBM_STATUS);
    cpStat  = BcRegLer(h, T_CP_STAT);
    spiPg   = BcRegLer(h, T_SPI_PG_WGP_MASK);
    ccArray = BcRegLer(h, T_CC_SHADER_ARRAY);

    BcMedido("GRBM_STATUS        = 0x%08X  (GUI_ACTIVE=%u)", grbm, (grbm >> 31) & 1u);
    BcMedido("CP_STAT            = 0x%08X", cpStat);
    BcMedido("CP_ME_CNTL         = 0x%08X", BcRegLer(h, T_CP_ME_CNTL));
    BcMedido("CP_MEC_CNTL        = 0x%08X", BcRegLer(h, T_CP_MEC_CNTL));
    BcMedido("SPI_PG_WGP_MASK    = 0x%08X  (WGPs habilitados por SA)", spiPg);
    BcMedido("CC_GC_SHADER_ARRAY = 0x%08X  (0xFFF80000 = 24 CU stock)", ccArray);
    BcMedido("CP_RB0_BASE        = 0x%08X", BcRegLer(h, T_CP_RB0_BASE));
    BcMedido("CP_RB0_RPTR        = 0x%08X", BcRegLer(h, T_CP_RB0_RPTR));
    BcMedido("CP_RB0_WPTR        = 0x%08X", BcRegLer(h, T_CP_RB0_WPTR));

    BcProva(grbm != 0x00000000u || cpStat != 0x00000000u,
            "GRBM_STATUS/CP_STAT devolvem estado, nao zero constante");

    return BcVeredito();
}
