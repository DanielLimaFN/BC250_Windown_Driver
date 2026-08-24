/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_verdict.h

Abstract:
    Harness de veredito para a suite BC250-V3 (Fase 0).

    Antes da Fase 0 os sete testes imprimiam "100% OPERACIONAL" sem verificar
    condicao fisica nenhuma: o teste de PM4 declarava sucesso com RPTR parado em
    zero, e o de compute declarava os 1536 shaders disparando com GRBM inalterado
    e duracao de 0 us.

    Este harness torna isso impossivel. Todo teste:

      1. DECLARA o criterio antes de medir      -> BC_CRITERIO(...)
      2. MEDE e registra o valor observado      -> BC_MEDIDO(...)
      3. AVALIA cada prova exigida pela regra   -> BC_PROVA(...)
      4. EMITE o veredito derivado das provas   -> BcVeredito()

    Estados possiveis:

      PROVADO                    todas as provas aplicaveis satisfeitas
      NAO VALIDADO EM HARDWARE   executou, faltou pelo menos uma prova
      BLOQUEADO                  pre-requisito ausente (ex: firmware nao aceito)

    O processo sai com codigo != 0 em qualquer estado que nao seja PROVADO ou
    BLOQUEADO, para que Central-Testes.cmd possa falhar de verdade.

--*/

#ifndef _TEST_VERDICT_H_
#define _TEST_VERDICT_H_

#include "test_common.h"

#define BC_VERDICT_PROVADO    0
#define BC_VERDICT_NAO_VALID  1
#define BC_VERDICT_BLOQUEADO  2

typedef struct _BC_TEST_CTX {
    const char *Nome;
    int   TotalProvas;
    int   ProvasOk;
    int   Bloqueado;
    char  MotivoBloqueio[256];
    char  Faltando[8][160];
    int   FaltandoCount;
} BC_TEST_CTX;

static BC_TEST_CTX g_Bc;

/*
  Padroes de preenchimento que a BAR5 devolve quando o offset nao decodifica.
  Qualquer leitura que caia aqui e' offset INVALIDO, nao dado.
  (Foi assim que se descobriu que A_CP_STAT = 0x8684 lia 0xDEF4DEF4.)
*/
static __inline int BcEhFillPattern(ULONG v)
{
    return (v == 0xFFFFFFFFu) ||
           (v == 0xBADBAD00u) ||
           ((v & 0xFFF0FFF0u) == 0xDEF0DEF0u);
}

static __inline void BcInicio(const char *nome)
{
    memset(&g_Bc, 0, sizeof(g_Bc));
    g_Bc.Nome = nome;
    printf("=================================================================\n");
    printf(" AMD BC-250 (BC250-V3) - %s\n", nome);
    printf("=================================================================\n\n");
}

static __inline void BcCriterio(const char *fmt, ...)
{
    va_list ap;
    printf("[CRITERIO]  ");
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n");
}

static __inline void BcMedido(const char *fmt, ...)
{
    va_list ap;
    printf("[MEDIDO]    ");
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n");
}

static __inline void BcInfo(const char *fmt, ...)
{
    va_list ap;
    printf("            ");
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n");
}

/* Registra uma prova exigida pela regra BC-250 e se ela foi satisfeita. */
static __inline void BcProva(int ok, const char *descricao)
{
    g_Bc.TotalProvas++;
    if (ok) {
        g_Bc.ProvasOk++;
        printf("[PROVA  OK] %s\n", descricao);
    } else {
        printf("[PROVA FALTA] %s\n", descricao);
        if (g_Bc.FaltandoCount < 8) {
            strncpy_s(g_Bc.Faltando[g_Bc.FaltandoCount],
                      sizeof(g_Bc.Faltando[0]), descricao, _TRUNCATE);
            g_Bc.FaltandoCount++;
        }
    }
}

/* Pre-requisito ausente: o teste nao pode nem comecar. Nao conta como falha. */
static __inline void BcBloquear(const char *fmt, ...)
{
    va_list ap;
    g_Bc.Bloqueado = 1;
    va_start(ap, fmt);
    vsnprintf(g_Bc.MotivoBloqueio, sizeof(g_Bc.MotivoBloqueio), fmt, ap);
    va_end(ap);
}

static __inline int BcVeredito(void)
{
    int i;
    printf("\n-----------------------------------------------------------------\n");

    if (g_Bc.Bloqueado) {
        printf("[VEREDITO]  BLOQUEADO\n");
        printf("            %s\n", g_Bc.MotivoBloqueio);
        printf("-----------------------------------------------------------------\n");
        return BC_VERDICT_BLOQUEADO;
    }

    if (g_Bc.TotalProvas == 0) {
        printf("[VEREDITO]  NAO VALIDADO EM HARDWARE\n");
        printf("            O teste nao declarou nenhuma prova.\n");
        printf("-----------------------------------------------------------------\n");
        return BC_VERDICT_NAO_VALID;
    }

    printf("[PROVAS]    %d de %d satisfeitas\n", g_Bc.ProvasOk, g_Bc.TotalProvas);

    if (g_Bc.ProvasOk == g_Bc.TotalProvas) {
        printf("[VEREDITO]  PROVADO\n");
        printf("-----------------------------------------------------------------\n");
        return BC_VERDICT_PROVADO;
    }

    printf("[VEREDITO]  NAO VALIDADO EM HARDWARE\n");
    printf("            Provas ausentes:\n");
    for (i = 0; i < g_Bc.FaltandoCount; i++) {
        printf("              - %s\n", g_Bc.Faltando[i]);
    }
    printf("-----------------------------------------------------------------\n");
    return BC_VERDICT_NAO_VALID;
}

/*---------------------------------------------------------------------------
  Helpers de escape reutilizaveis
---------------------------------------------------------------------------*/

typedef struct _BC_ESC_REG {
    BC250_ESCAPE_HEADER Header;
    ULONG Offset;
    ULONG Value;
} BC_ESC_REG;

static __inline ULONG BcRegLer(D3DKMT_HANDLE h, ULONG offset)
{
    BC_ESC_REG r = { 0 };
    r.Header.CommandId = BC250_ESCAPE_CMD_READ_REG;
    r.Header.InSize = sizeof(r);
    r.Header.OutSize = sizeof(r);
    r.Offset = offset;
    if (!CallBc250Escape(h, &r, sizeof(r))) return 0xBADBAD00u;
    return r.Value;
}

/*---------------------------------------------------------------------------
  Sentinela de adaptador (contencao de cascata pos-TDR)

  Quando uma espera longa no KMD estoura o TDR, o Windows reseta o driver e o
  handle do adaptador vira invalido. A partir dai TODO escape falha e os testes
  seguintes leem 0xBADBAD00 — e reportam isso como se fosse medicao.

  Foi o que aconteceu numa corrida: a SDMA bloqueou 14,4 s, o TDR disparou, e
  test_pm4_ring, test_cp_execution, test_compute e test_opencl produziram lixo.
  test_pm4_ring chegou a marcar "[PROVA OK] RPTR alcancou o WPTR" comparando
  0xBADBAD00 com 0xBADBAD00.

  Todo teste deve chamar BcAdaptadorVivo() logo depois de abrir o adaptador e
  declarar BLOQUEADO se ele estiver morto — nunca produzir numeros.
---------------------------------------------------------------------------*/
static __inline int BcAdaptadorVivo(D3DKMT_HANDLE h)
{
    /* SCRATCH_REG0 e' inofensivo e sempre responde num adaptador saudavel. */
    ULONG v;
    if (h == 0) return 0;
    v = BcRegLer(h, 0x30100u);
    return (v != 0xBADBAD00u);
}

/* Abre o adaptador e confirma que ele responde. Devolve 0 se estiver morto. */
static __inline D3DKMT_HANDLE BcAbrirAdaptadorVivo(void)
{
    D3DKMT_HANDLE h = OpenBc250Adapter();
    if (h == 0) return 0;
    if (!BcAdaptadorVivo(h)) {
        /* Uma reabertura resolve o caso de handle obsoleto apos TDR. */
        h = OpenBc250Adapter();
        if (h == 0 || !BcAdaptadorVivo(h)) return 0;
    }
    return h;
}

static __inline void BcRegGravar(D3DKMT_HANDLE h, ULONG offset, ULONG value)
{
    BC_ESC_REG r = { 0 };
    r.Header.CommandId = BC250_ESCAPE_CMD_WRITE_REG;
    r.Header.InSize = sizeof(r);
    r.Header.OutSize = sizeof(r);
    r.Offset = offset;
    r.Value = value;
    (void)CallBc250Escape(h, &r, sizeof(r));
}

/* T3: le, grava, le, restaura. Devolve 1 se o valor grudou. */
static __inline int BcWriteStick(D3DKMT_HANDLE h, ULONG offset, ULONG teste,
                                 ULONG *pAntes, ULONG *pDepois)
{
    ULONG antes = BcRegLer(h, offset);
    ULONG depois;
    BcRegGravar(h, offset, teste);
    depois = BcRegLer(h, offset);
    BcRegGravar(h, offset, antes);       /* restaura sempre */
    if (pAntes)  *pAntes = antes;
    if (pDepois) *pDepois = depois;
    return (depois == teste);
}

/* Offsets BAR5 derivados por SOC15 — espelham wddm_hw.c / wddm.h. */
#define T_GRBM_STATUS         0x08010u
#define T_GRBM_STATUS2        0x08008u
#define T_CP_STAT             0x08680u
#define T_CP_ME_CNTL          0x086D8u
#define T_CP_MEC_CNTL         0x08234u
#define T_CP_RB0_RPTR         0x08700u
#define T_CP_RB0_BASE         0x0C100u
#define T_CP_RB0_CNTL         0x0C104u
#define T_CP_RB0_WPTR         0x0C150u
#define T_SCRATCH_REG0        0x30100u
#define T_SCRATCH_REG1        0x30104u
#define T_SPI_PG_WGP_MASK     0x0935Cu
#define T_CC_SHADER_ARRAY     0x089BCu
#define T_GRBM_GFX_INDEX      0x30800u
#define T_COMPUTE_DISPATCH    0x0B800u
#define T_PSP_C2PMSG_64       0x58200u
#define T_PSP_C2PMSG_81       0x58244u

/* Estado interno da SDMA0 (derivados por SOC15 de gc_10_1_0_offset.h). */
#define T_SDMA0_CNTL          0x049F0u
#define T_SDMA0_STATUS_REG    0x04A14u
#define T_SDMA0_STATUS1_REG   0x04A18u
#define T_SDMA0_F32_CNTL      0x04A28u
#define T_SDMA0_FREEZE        0x04A2Cu
#define T_SDMA0_UTCL1_RD_STAT 0x04A78u
#define T_SDMA0_UTCL1_WR_STAT 0x04A7Cu
#define T_SDMA0_GFX_RB_CNTL   0x04B80u
#define T_SDMA0_GFX_RB_BASE   0x04B84u
#define T_SDMA0_GFX_RB_RPTR   0x04B8Cu
#define T_SDMA0_GFX_RB_WPTR   0x04B94u

/* Offsets historicos, comprovadamente invalidos. Usados so' em regressao. */
#define T_BAD_GRBM_STATUS     0x03040u
#define T_BAD_CP_STAT         0x08684u
#define T_BAD_SPI_PG          0x05C3Cu
#define T_BAD_CC_ARRAY        0x09C1Cu
#define T_BAD_SCRATCH         0x032D4u
#define T_BAD_COMPUTE_DISP    0x080E0u

#endif /* _TEST_VERDICT_H_ */
