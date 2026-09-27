#include "../core/bc250_blt_proof_contract.h"

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) { ExitProcess((unsigned long)__LINE__); return 1; } } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) return 1; } while (0)
#endif

/*
 * A9/A10 -- trava da semantica da prova do blt.
 *
 * O caso que da nome a este teste: na corrida 2026-09-06 22:42 a
 * instrumentacao imprimiu "BYTE_PROOF PASS" 63 vezes com NonZero=0/256. Este
 * teste existe para que aquele rotulo nunca volte.
 */

/* O caso medido em 06/09 22:42: copia fiel de um frame todo preto. */
static int TestBlankFrameIsNotPass(void)
{
    BC250_BLT_PROOF_VERDICT v = Bc250ClassifyBltProof(1u, 0u, 256u);

    CHECK(v == BC250_BLT_PROOF_BLANK);
    /* Nao pode se chamar tripe: a perna de dados nao demonstra pixel. */
    CHECK(Bc250BltProofAllowsTripodClaim(v) == 0u);
    /*
     * E nao pode fechar o caminho: o DWM pode legitimamente apresentar preto.
     * Desligar o blt aqui transformaria sintoma em regressao.
     */
    CHECK(Bc250BltProofRequiresFailClosed(v) == 0u);
    return 0;
}

static int TestContentFrameIsPass(void)
{
    BC250_BLT_PROOF_VERDICT v = Bc250ClassifyBltProof(1u, 1u, 256u);

    CHECK(v == BC250_BLT_PROOF_CONTENT_OK);
    CHECK(Bc250BltProofAllowsTripodClaim(v) == 1u);
    CHECK(Bc250BltProofRequiresFailClosed(v) == 0u);

    /* Uma amostra nao-zero basta; o limite superior tambem vale. */
    v = Bc250ClassifyBltProof(1u, 256u, 256u);
    CHECK(v == BC250_BLT_PROOF_CONTENT_OK);
    CHECK(Bc250BltProofAllowsTripodClaim(v) == 1u);
    return 0;
}

static int TestMismatchFailsClosed(void)
{
    /* Divergencia de bytes manda fechar, mesmo com conteudo na amostra. */
    BC250_BLT_PROOF_VERDICT v = Bc250ClassifyBltProof(0u, 128u, 256u);

    CHECK(v == BC250_BLT_PROOF_MISMATCH);
    CHECK(Bc250BltProofAllowsTripodClaim(v) == 0u);
    CHECK(Bc250BltProofRequiresFailClosed(v) == 1u);

    /* E tambem quando nada foi amostrado. */
    v = Bc250ClassifyBltProof(0u, 0u, 0u);
    CHECK(v == BC250_BLT_PROOF_MISMATCH);
    CHECK(Bc250BltProofRequiresFailClosed(v) == 1u);
    return 0;
}

static int TestUnsampledIsIndeterminate(void)
{
    /*
     * Bytes conferem mas ninguem olhou o destino: nao da para afirmar conteudo,
     * logo nao ha direito a "TRIPE" -- e tambem nao e' falha.
     */
    BC250_BLT_PROOF_VERDICT v = Bc250ClassifyBltProof(1u, 0u, 0u);

    CHECK(v == BC250_BLT_PROOF_INDETERMINATE);
    CHECK(Bc250BltProofAllowsTripodClaim(v) == 0u);
    CHECK(Bc250BltProofRequiresFailClosed(v) == 0u);
    return 0;
}

static int TestOnlyContentEverClaimsTripod(void)
{
    /* Exaustivo sobre os quatro vereditos: so' um deles autoriza "TRIPE". */
    BC250_BLT_PROOF_VERDICT all[4];
    int i;
    unsigned claims = 0u;

    all[0] = BC250_BLT_PROOF_INDETERMINATE;
    all[1] = BC250_BLT_PROOF_CONTENT_OK;
    all[2] = BC250_BLT_PROOF_BLANK;
    all[3] = BC250_BLT_PROOF_MISMATCH;

    for (i = 0; i < 4; ++i) {
        claims += Bc250BltProofAllowsTripodClaim(all[i]);
    }
    CHECK(claims == 1u);
    return 0;
}


/*
 * =====================================================================
 * A13 -- trava da perna 1 do tripe (RPTR == WPTR).
 *
 * O caso que da nome a estes testes: corrida 2026-09-07 16:19, linha 15 do
 * bc250_display_log.txt, "TRIPÉ NO SILÍCIO COMPROVADO ... WPTR=68 RPTR=52".
 * =====================================================================
 */

/* O caso medido: WPTR=68, writeback=52, fence e dados OK. */
static int TestRptrLagBreaksTripodWhenRegisterAlsoLags(void)
{
    BC250_TRIPOD_RPTR_SOURCE src = BC250_TRIPOD_RPTR_SOURCE_BOTH;
    BC250_TRIPOD_VERDICT v = Bc250ClassifySdmaTripod(
        68u, 52u, 52u, 0x00001001u, 0x00001001u, 0u, &src);

    CHECK(v == BC250_TRIPOD_RPTR_NOT_RETIRED);
    CHECK(src == BC250_TRIPOD_RPTR_SOURCE_NONE);
    CHECK(Bc250TripodAllowsClaim(v) == 0u);
    return 0;
}

/*
 * O writeback pode legitimamente ficar atras do registrador -- as duas sao
 * leituras do mesmo ponteiro. Uma delas casando satisfaz a perna 1, mas o log
 * tem de dizer QUAL.
 */
static int TestRegisterAloneSatisfiesLegOne(void)
{
    BC250_TRIPOD_RPTR_SOURCE src = BC250_TRIPOD_RPTR_SOURCE_NONE;
    BC250_TRIPOD_VERDICT v = Bc250ClassifySdmaTripod(
        68u, 52u, 68u, 0x00001001u, 0x00001001u, 0u, &src);

    CHECK(v == BC250_TRIPOD_PROVEN);
    CHECK(src == BC250_TRIPOD_RPTR_SOURCE_REGISTER);
    CHECK(Bc250TripodAllowsClaim(v) == 1u);

    v = Bc250ClassifySdmaTripod(68u, 68u, 52u, 0x00001001u, 0x00001001u, 0u, &src);
    CHECK(v == BC250_TRIPOD_PROVEN);
    CHECK(src == BC250_TRIPOD_RPTR_SOURCE_WRITEBACK);

    v = Bc250ClassifySdmaTripod(68u, 68u, 68u, 0x00001001u, 0x00001001u, 0u, &src);
    CHECK(v == BC250_TRIPOD_PROVEN);
    CHECK(src == BC250_TRIPOD_RPTR_SOURCE_BOTH);
    return 0;
}

/* Fence com poison intacto: perna 2 reprovada, mesmo com RPTR e dados OK. */
static int TestPoisonedFenceBreaksTripod(void)
{
    BC250_TRIPOD_VERDICT v = Bc250ClassifySdmaTripod(
        68u, 68u, 68u, 0x00001001u, 0x43DAFFFEu, 0u, NULL);

    CHECK(v == BC250_TRIPOD_FENCE_NOT_WRITTEN);
    CHECK(Bc250TripodAllowsClaim(v) == 0u);
    return 0;
}

/* Bytes divergentes: perna 3 reprovada. */
static int TestDataMismatchBreaksTripod(void)
{
    BC250_TRIPOD_VERDICT v = Bc250ClassifySdmaTripod(
        68u, 68u, 68u, 0x00001001u, 0x00001001u, 1u, NULL);

    CHECK(v == BC250_TRIPOD_DATA_MISMATCH);
    CHECK(Bc250TripodAllowsClaim(v) == 0u);
    return 0;
}

/*
 * Anel que nunca armou: alvo zero com tudo zero seria "RPTR==WPTR" por
 * vacuidade. Tem de sair indeterminado, nunca provado.
 */
static int TestNeverArmedRingIsIndeterminate(void)
{
    BC250_TRIPOD_RPTR_SOURCE src = BC250_TRIPOD_RPTR_SOURCE_BOTH;
    BC250_TRIPOD_VERDICT v = Bc250ClassifySdmaTripod(
        0u, 0u, 0u, 0x00001001u, 0x00001001u, 0u, &src);

    CHECK(v == BC250_TRIPOD_INDETERMINATE);
    CHECK(src == BC250_TRIPOD_RPTR_SOURCE_NONE);
    CHECK(Bc250TripodAllowsClaim(v) == 0u);
    return 0;
}

static int TestOnlyProvenEverClaimsTripod(void)
{
    BC250_TRIPOD_VERDICT all[5];
    int i;
    unsigned claims = 0u;

    all[0] = BC250_TRIPOD_INDETERMINATE;
    all[1] = BC250_TRIPOD_PROVEN;
    all[2] = BC250_TRIPOD_RPTR_NOT_RETIRED;
    all[3] = BC250_TRIPOD_FENCE_NOT_WRITTEN;
    all[4] = BC250_TRIPOD_DATA_MISMATCH;

    for (i = 0; i < 5; ++i) {
        claims += Bc250TripodAllowsClaim(all[i]);
        /* Todo veredito tem nome nao vazio: o log nunca fica mudo. */
        CHECK(Bc250TripodVerdictName(all[i])[0] != '\0');
    }
    CHECK(claims == 1u);
    return 0;
}


/*
 * =====================================================================
 * A14 -- trava da perna 3 (canaries).
 *
 * Caso que da nome: corrida 2026-09-07 17:13, dez "IB_TRIPOD PASS" com
 * ZERO bytes do destino conferidos. Aqui tambem se trava o motivo de os
 * canaries existirem: o padrao dos fills de paginacao e' 0x00000000, e
 * comparar zero com zero nao prova escrita nenhuma.
 * =====================================================================
 */

/* Geometria do destino da corrida 17:13: 0x450AB0000, 11.075.584 bytes. */
static int TestCanaryPlanCoversMeasuredFill(void)
{
    BC250_DATA_PROOF_PLAN plan;
    uint32_t ok = Bc250PlanDataProofCanaries(
        0x450AB0000ull, 11075584u, 0u,
        0x450000000ull, 128u * 1024u * 1024u, 1u, &plan);

    CHECK(ok == 1u);
    CHECK(plan.Usable == 1u);
    CHECK(plan.CanaryCount == BC250_DATA_PROOF_CANARY_COUNT);
    CHECK(plan.VramOffset == 0xAB0000ull);
    /* Cinco pontos ao longo do buffer, todos alinhados a dword. */
    CHECK(plan.Offsets[0] == 0u);
    CHECK(plan.Offsets[4] == ((11075584u - 16u) & ~3u));
    CHECK((plan.Offsets[1] & 3u) == 0u);
    CHECK((plan.Offsets[2] & 3u) == 0u);
    CHECK((plan.Offsets[3] & 3u) == 0u);
    CHECK(plan.Offsets[1] < plan.Offsets[2]);
    CHECK(plan.Offsets[2] < plan.Offsets[3]);
    CHECK(plan.Offsets[3] < plan.Offsets[4]);
    /* Nenhum canary pode valer o padrao esperado, senao a prova e' vazia. */
    CHECK(plan.Values[0] != 0u);
    CHECK(plan.Values[4] != 0u);
    return 0;
}

/* Fora da abertura mapeada: perna 3 fica NAO CONFERIDA, e isso nao e' falha. */
static int TestCanaryPlanRefusesUnmappedDestination(void)
{
    BC250_DATA_PROOF_PLAN plan;

    /* Sem ponteiro de CPU para a abertura. */
    CHECK(Bc250PlanDataProofCanaries(0x450AB0000ull, 4096u, 0u,
        0x450000000ull, 128u * 1024u * 1024u, 0u, &plan) == 0u);
    CHECK(plan.Usable == 0u);

    /* Destino abaixo da base de VRAM. */
    CHECK(Bc250PlanDataProofCanaries(0x100ull, 4096u, 0u,
        0x450000000ull, 128u * 1024u * 1024u, 1u, &plan) == 0u);

    /* Faixa que passa do fim da abertura. */
    CHECK(Bc250PlanDataProofCanaries(0x457FFF000ull, 8192u, 0u,
        0x450000000ull, 128u * 1024u * 1024u, 1u, &plan) == 0u);

    /* Base de VRAM desconhecida. */
    CHECK(Bc250PlanDataProofCanaries(0x450AB0000ull, 4096u, 0u,
        0ull, 128u * 1024u * 1024u, 1u, &plan) == 0u);

    /* Menor que um dword: os canaries escreveriam fora da regiao. */
    CHECK(Bc250PlanDataProofCanaries(0x450AB0000ull, 2u, 0u,
        0x450000000ull, 128u * 1024u * 1024u, 1u, &plan) == 0u);
    return 0;
}

/* Padrao igual ao canary anularia a prova: plano tem de recusar. */
static int TestCanaryPlanRefusesPatternCollision(void)
{
    BC250_DATA_PROOF_PLAN plan;
    uint32_t ok = Bc250PlanDataProofCanaries(
        0x450AB0000ull, 4096u, 0xDEADBEEFu ^ 0x1111u,
        0x450000000ull, 128u * 1024u * 1024u, 1u, &plan);

    CHECK(ok == 0u);
    CHECK(plan.Usable == 0u);
    return 0;
}

static int TestCanaryMismatchCounting(void)
{
    BC250_DATA_PROOF_PLAN plan;
    uint32_t post[BC250_DATA_PROOF_CANARY_COUNT];
    uint32_t i;

    CHECK(Bc250PlanDataProofCanaries(0x450AB0000ull, 4096u, 0u,
        0x450000000ull, 128u * 1024u * 1024u, 1u, &plan) == 1u);

    /* GPU substituiu todos pelo padrao zero: perna 3 provada. */
    for (i = 0u; i < BC250_DATA_PROOF_CANARY_COUNT; ++i) post[i] = 0u;
    CHECK(Bc250CountDataProofMismatches(&plan, post, 0u) == 0u);

    /* Canaries intactos: a GPU nao escreveu nada. */
    for (i = 0u; i < BC250_DATA_PROOF_CANARY_COUNT; ++i) post[i] = plan.Values[i];
    CHECK(Bc250CountDataProofMismatches(&plan, post, 0u) ==
          BC250_DATA_PROOF_CANARY_COUNT);

    /* Escrita parcial: conta exatamente os que sobraram. */
    post[0] = 0u;
    post[1] = 0u;
    CHECK(Bc250CountDataProofMismatches(&plan, post, 0u) ==
          (BC250_DATA_PROOF_CANARY_COUNT - 2u));

    /*
     * Ausencia de leitura NAO pode virar sucesso: sem valores, sem plano ou
     * com plano inutilizavel, o resultado e divergencia total.
     */
    for (i = 0u; i < BC250_DATA_PROOF_CANARY_COUNT; ++i) post[i] = 0u;
    CHECK(Bc250CountDataProofMismatches(&plan, NULL, 0u) ==
          BC250_DATA_PROOF_CANARY_COUNT);
    CHECK(Bc250CountDataProofMismatches(NULL, post, 0u) ==
          BC250_DATA_PROOF_CANARY_COUNT);
    plan.Usable = 0u;
    CHECK(Bc250CountDataProofMismatches(&plan, post, 0u) ==
          BC250_DATA_PROOF_CANARY_COUNT);
    return 0;
}

/*
 * O elo que fecha a A14: canaries divergentes tem de derrubar o veredito do
 * tripe mesmo com anel drenado e fence gravada pela GPU. Era exatamente esse
 * estado que a corrida 17:13 declarou como PASS.
 */
static int TestDataLegBreaksOtherwisePerfectIb(void)
{
    BC250_TRIPOD_VERDICT v = Bc250ClassifySdmaTripod(
        132u, 132u, 132u, 0x00001002u, 0x00001002u,
        BC250_DATA_PROOF_CANARY_COUNT, NULL);

    CHECK(v == BC250_TRIPOD_DATA_MISMATCH);
    CHECK(Bc250TripodAllowsClaim(v) == 0u);

    /* Com os canaries substituidos, o mesmo IB fecha as tres pernas. */
    v = Bc250ClassifySdmaTripod(132u, 132u, 132u, 0x00001002u, 0x00001002u, 0u, NULL);
    CHECK(v == BC250_TRIPOD_PROVEN);
    CHECK(Bc250TripodAllowsClaim(v) == 1u);
    return 0;
}


/*
 * =====================================================================
 * A15 -- volta do anel na perna 1.
 *
 * Numeros medidos na corrida 2026-09-07 17:43, submissao FenceId=24:
 * TargetWPTR=516, RPTR=4612, anel de 4096 bytes. 4612 = 4096 + 516: a GPU
 * havia terminado (fence bateu), e a comparacao crua reprovou -- o timeout
 * devolveu STATUS_IO_DEVICE_ERROR e a maquina caiu em 0x119.
 * =====================================================================
 */
static int TestRingWrapIsTheMeasuredCase(void)
{
    const uint32_t ringBytes = 4096u;

    /* O caso exato do log. */
    CHECK(Bc250TripodWrapRingPointer(4612u, ringBytes) == 516u);
    CHECK(Bc250TripodWrapRingPointer(516u, ringBytes) == 516u);

    /* Sem a reducao, o veredito e RPTR_NAO_ALCANCOU_WPTR (o que aconteceu). */
    CHECK(Bc250ClassifySdmaTripod(516u, 4612u, 4612u, 0x1030u, 0x1030u, 0u, NULL)
          == BC250_TRIPOD_RPTR_NOT_RETIRED);

    /* Com a reducao, as tres pernas fecham. */
    CHECK(Bc250ClassifySdmaTripod(
              Bc250TripodWrapRingPointer(516u, ringBytes),
              Bc250TripodWrapRingPointer(4612u, ringBytes),
              Bc250TripodWrapRingPointer(4612u, ringBytes),
              0x1030u, 0x1030u, 0u, NULL) == BC250_TRIPOD_PROVEN);

    /* Voltas multiplas e limites. */
    CHECK(Bc250TripodWrapRingPointer(4096u, ringBytes) == 0u);
    CHECK(Bc250TripodWrapRingPointer(4095u, ringBytes) == 4095u);
    CHECK(Bc250TripodWrapRingPointer(4096u * 7u + 12u, ringBytes) == 12u);

    /* Anel zero nao pode dividir por zero: devolve o valor cru. */
    CHECK(Bc250TripodWrapRingPointer(4612u, 0u) == 4612u);

    /*
     * A reducao NAO pode virar peneira: RPTR realmente atrasado dentro da
     * mesma volta continua reprovando.
     */
    CHECK(Bc250ClassifySdmaTripod(
              Bc250TripodWrapRingPointer(516u, ringBytes),
              Bc250TripodWrapRingPointer(4608u, ringBytes),
              Bc250TripodWrapRingPointer(4608u, ringBytes),
              0x1030u, 0x1030u, 0u, NULL) == BC250_TRIPOD_RPTR_NOT_RETIRED);
    return 0;
}

static int Bc250RunBltProofContractTest(void)
{
    if (TestBlankFrameIsNotPass() != 0) return 1;
    if (TestContentFrameIsPass() != 0) return 1;
    if (TestMismatchFailsClosed() != 0) return 1;
    if (TestUnsampledIsIndeterminate() != 0) return 1;
    if (TestOnlyContentEverClaimsTripod() != 0) return 1;
    if (TestRptrLagBreaksTripodWhenRegisterAlsoLags() != 0) return 1;
    if (TestRegisterAloneSatisfiesLegOne() != 0) return 1;
    if (TestPoisonedFenceBreaksTripod() != 0) return 1;
    if (TestDataMismatchBreaksTripod() != 0) return 1;
    if (TestNeverArmedRingIsIndeterminate() != 0) return 1;
    if (TestOnlyProvenEverClaimsTripod() != 0) return 1;
    if (TestCanaryPlanCoversMeasuredFill() != 0) return 1;
    if (TestCanaryPlanRefusesUnmappedDestination() != 0) return 1;
    if (TestCanaryPlanRefusesPatternCollision() != 0) return 1;
    if (TestCanaryMismatchCounting() != 0) return 1;
    if (TestDataLegBreaksOtherwisePerfectIb() != 0) return 1;
    if (TestRingWrapIsTheMeasuredCase() != 0) return 1;
    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess((unsigned long)Bc250RunBltProofContractTest());
}
#else
int main(void)
{
    /* F3.2: black/content have the same execution contract. */
    CHECK(Bc250PresentCopySucceeded(0u, 0u) == 0u);
    CHECK(Bc250PresentCopySucceeded(1u, 0u) == 1u);
    CHECK(Bc250PresentCopySucceeded(0u, 1u) == 1u);

    int failed = Bc250RunBltProofContractTest();
    if (failed == 0) {
        printf("blt_proof_contract_test: PASS\n");
    }
    return failed;
}
#endif
