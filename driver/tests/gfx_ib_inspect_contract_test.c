/*
 * gfx_ib_inspect_contract_test.c -- suite offline do classificador de stream PM4
 * do IB submetido ao anel grafico CP_RB0.
 *
 * POR QUE OS CASOS NEGATIVOS SAO A METADE QUE IMPORTA
 *
 * Este repositorio registra SEIS ocasioes em que "o portao carimbou o falso
 * sucesso": A15 exigindo a comparacao crua de RPTR; mcdm_callbacks_test exigindo
 * STATUS_SUCCESS sem executor de TDR; C1 exigindo o memcpy no Draw;
 * sdma_blt_executed casando com a string PRESENT_BLT; 1787 COMPOSITE_BLT com
 * ZERO_INCONCLUSIVO contados como progresso; e a imagem do rollback atribuida ao
 * nosso driver. Um teste que so exercita o caso bom nao reprova nada.
 *
 * Por isso as assercoes centrais aqui sao NEGATIVAS:
 *   - stream que AINDA tem salto de IB nao pode sair como linear;
 *   - stream que nao pode ser lido por inteiro nao pode sair como linear;
 *   - stream maior que o orcamento de varredura nao pode sair como linear;
 *   - o caso exato medido na corrida 2026-09-18 10:20 (24 DW terminando em
 *     PACKET3_INDIRECT_BUFFER, cabecalho 0xC0023F00) tem de sair como
 *     IB_AINDA_ENCADEADO.
 *
 * Convencao de link do projeto: /NODEFAULTLIB. Sem CRT em kernel-mode, logo sem
 * printf e sem strcmp -- a falha se reporta pelo codigo de saida com o numero da
 * linha, como nas outras suites.
 */
#include "../core/bc250_gfx_ib_inspect_contract.h"

/*
 * Link do projeto: /NODEFAULTLIB. Nao ha CRT -- sem printf, sem strcmp, sem
 * memset da biblioteca. A falha se reporta pelo codigo de saida com o numero da
 * linha, exatamente como nas outras suites (ver blt_proof_contract_test.c).
 */
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#define CHECK(Expression) \
    do { if (!(Expression)) { ExitProcess((unsigned long)__LINE__); return 1; } } while (0)

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Comparacao de string local: sem CRT, strcmp nao existe no link. */
static int StrEq(const char *A, const char *B)
{
    if (A == NULL || B == NULL) return (A == B) ? 1 : 0;
    while (*A != '\0' && *A == *B) { ++A; ++B; }
    return (*A == *B) ? 1 : 0;
}

/* ------------------------------------------------------------- leitor de teste */

typedef struct ARRAY_READER {
    const uint32_t *Words;
    uint32_t Count;
    uint32_t FailAtIndex;   /* indice a partir do qual a leitura falha */
    uint32_t ReadCalls;
} ARRAY_READER;

static int ArrayRead(void *Ctx, uint32_t Index, uint32_t *Value)
{
    ARRAY_READER *r = (ARRAY_READER *)Ctx;
    ++r->ReadCalls;
    if (Index >= r->Count) return 0;
    if (Index >= r->FailAtIndex) return 0;
    *Value = r->Words[Index];
    return 1;
}

/* Cabecalho PM4 type-3. count = dwords depois do cabecalho, menos 1. */
static uint32_t Pkt3(uint32_t Opcode, uint32_t Count)
{
    return (3u << 30) | ((Count & 0x3FFFu) << 16) | ((Opcode & 0xFFu) << 8);
}

#define NOP_T2 0x80000000u

/* ---------------------------------------------------------------------- casos */

/*
 * Caso 1 -- O CASO MEDIDO NO SILICIO. A corrida 2026-09-18 10:20 submeteu 24 DW
 * cujo fim era o salto de IB que a RADV instalava por chaining; o cabecalho
 * observado foi 0xC0023F00 (type3, count=2, opcode=0x3F).
 */
static int TestStreamMedidoDaCorrida(void)
{
    uint32_t words[24];
    ARRAY_READER r;
    BC250_GFX_IB_INSPECTION insp;
    uint32_t i;

    /* [1] 24 DW terminando em salto de IB -- o stream medido em 10:20 */

    for (i = 0u; i < 20u; ++i) words[i] = NOP_T2;
    words[20] = Pkt3(BC250_GFX_IB_OPCODE_INDIRECT_BUFFER, 2u);
    words[21] = 0x00015000u;
    words[22] = 0x00000002u;
    words[23] = 0x07D00280u;

    /* Trava o encoding contra o valor medido no anel. Se o construtor de
     * cabecalho mudar, este teste reprova antes do hardware. */
    CHECK(words[20] == 0xC0023F00u);
    /* E trava a classe: 0x07D00280 tem o bit 20 (CHAIN) em 1 -- e' SALTO.
     * Decomposto: CHAIN(20)|VALID(23)|INHERIT_VMID_PFP(22)|VMID7(24..27)|size 640. */
    CHECK((words[23] & BC250_GFX_IB_CTRL_CHAIN_MASK) != 0u);

    r.Words = words; r.Count = 24u; r.FailAtIndex = 0xFFFFFFFFu; r.ReadCalls = 0u;
    Bc250GfxIbInspectStream(24u, ArrayRead, &r, &insp);

    CHECK(insp.RequestedDwords == 24u);
    CHECK(insp.ScanComplete == 1u);
    CHECK(insp.IndirectBufferPackets == 1u);
    CHECK(insp.ChainJumps == 1u);
    CHECK(insp.Ib2Calls == 0u);
    CHECK(insp.FirstChainDwordOffset == 20u);
    CHECK(insp.FirstIndirectDwordOffset == 20u);
    CHECK(StrEq(Bc250GfxIbInspectVerdictName(&insp), "IB_AINDA_ENCADEADO"));
    /* NEGATIVA CENTRAL. */
    CHECK(!StrEq(Bc250GfxIbInspectVerdictName(&insp), "IB_LINEAR_CONFIRMADO"));
    return 0;
}

/*
 * Caso 1b -- O OUTRO CASO MEDIDO, da corrida 2026-09-18 13:12. Depois da
 * concatenacao linear o stream de 664 DW comeca com um INDIRECT_BUFFER cujo
 * controle medido foi 0x000000B0: so' o campo de tamanho (176 DW), com CHAIN em
 * ZERO. E' uma CHAMADA IB2, nao um salto -- o CP executa o alvo e retorna.
 *
 * Este caso existe porque a primeira versao do classificador contava qualquer
 * opcode 0x3F como salto e acusou IB_AINDA_ENCADEADO numa corrida em que a RADV
 * provou o contrario ("BC-250 UNIFICADO ... DW=664", zero linhas de Chain). O
 * teste tem de reprovar se essa confusao voltar.
 */
static int TestStreamIb2NaoEhSalto(void)
{
    static uint32_t words[664];
    ARRAY_READER r;
    BC250_GFX_IB_INSPECTION insp;
    uint32_t i;

    for (i = 0u; i < 664u; ++i) words[i] = NOP_T2;
    words[0] = Pkt3(BC250_GFX_IB_OPCODE_INDIRECT_BUFFER, 2u);
    words[1] = 0x00014000u;
    words[2] = 0x00000002u;
    words[3] = 0x000000B0u; /* medido: size=176, CHAIN=0 */

    CHECK(words[0] == 0xC0023F00u);
    CHECK((words[3] & BC250_GFX_IB_CTRL_CHAIN_MASK) == 0u);

    r.Words = words; r.Count = 664u; r.FailAtIndex = 0xFFFFFFFFu; r.ReadCalls = 0u;
    Bc250GfxIbInspectStream(664u, ArrayRead, &r, &insp);

    CHECK(insp.ScanComplete == 1u);
    CHECK(insp.IndirectBufferPackets == 1u);
    CHECK(insp.ChainJumps == 0u);          /* nao ha salto */
    CHECK(insp.Ib2Calls == 1u);            /* ha uma chamada aninhada */
    CHECK(insp.HeadIsType3 == 1u);
    /* A ASSERCAO QUE IMPORTA: IB2 nao desqualifica o stream. */
    CHECK(StrEq(Bc250GfxIbInspectVerdictName(&insp), "IB_LINEAR_CONFIRMADO"));
    CHECK(!StrEq(Bc250GfxIbInspectVerdictName(&insp), "IB_AINDA_ENCADEADO"));
    return 0;
}

/* Caso 2 -- o desfecho que o pacote 26 deve produzir: stream linear. */
static int TestStreamLinearConcatenado(void)
{
    uint32_t words[64];
    ARRAY_READER r;
    BC250_GFX_IB_INSPECTION insp;
    uint32_t i;

    /* [2] 64 DW lineares, sem salto de IB */

    for (i = 0u; i < 64u; ++i) words[i] = NOP_T2;
    /* Um SET_CONTEXT_REG plausivel, para nao testar apenas NOPs. */
    words[16] = Pkt3(0x69u, 2u);
    words[17] = 0x00000001u;
    words[18] = 0x00000002u;
    words[19] = 0x00000003u;

    r.Words = words; r.Count = 64u; r.FailAtIndex = 0xFFFFFFFFu; r.ReadCalls = 0u;
    Bc250GfxIbInspectStream(64u, ArrayRead, &r, &insp);

    CHECK(insp.ScanComplete == 1u);
    CHECK(insp.IndirectBufferPackets == 0u);
    CHECK(insp.HeadIsType3 == 0u); /* comeca em NOP type-2 */
    CHECK(insp.PacketsWalked > 0u);
    CHECK(StrEq(Bc250GfxIbInspectVerdictName(&insp), "IB_LINEAR_CONFIRMADO"));
    return 0;
}

/*
 * Caso 3 -- NEGATIVO: leitura falha no meio. Nenhum salto visto ate ali, e o
 * veredito ainda assim NAO pode ser linear. Esta assercao e' o que impede o
 * classificador de virar carimbo.
 */
static int TestLeituraFalhaNaoViraLinear(void)
{
    uint32_t words[64];
    ARRAY_READER r;
    BC250_GFX_IB_INSPECTION insp;
    uint32_t i;

    /* [3] NEGATIVO: leitura falha em DW 8 -- nao pode sair linear */

    for (i = 0u; i < 64u; ++i) words[i] = NOP_T2;

    r.Words = words; r.Count = 64u; r.FailAtIndex = 8u; r.ReadCalls = 0u;
    Bc250GfxIbInspectStream(64u, ArrayRead, &r, &insp);

    CHECK(insp.IndirectBufferPackets == 0u);   /* nenhum salto visto... */
    CHECK(insp.ScanComplete == 0u);            /* ...mas a varredura nao fechou */
    CHECK(insp.WalkAborted == 1u);
    CHECK(StrEq(Bc250GfxIbInspectVerdictName(&insp), "INDETERMINADO"));
    CHECK(!StrEq(Bc250GfxIbInspectVerdictName(&insp), "IB_LINEAR_CONFIRMADO"));
    return 0;
}

/*
 * Caso 4 -- NEGATIVO: stream maior que o orcamento de varredura. Sem a quarta
 * condicao de ScanComplete (budget == RequestedDwords) isto sairia como
 * "completo", e o veredito de linearidade seria FALSO para o trecho nao varrido.
 */
static uint32_t g_bigStream[BC250_GFX_IB_SCAN_MAX_DWORDS + 64u];

static int TestAcimaDoOrcamentoNaoViraLinear(void)
{
    ARRAY_READER r;
    BC250_GFX_IB_INSPECTION insp;
    uint32_t i;
    const uint32_t total = BC250_GFX_IB_SCAN_MAX_DWORDS + 64u;

    /* [4] NEGATIVO: stream acima do orcamento -- nao pode sair linear */

    for (i = 0u; i < total; ++i) g_bigStream[i] = NOP_T2;
    /* Salto colocado DEPOIS do orcamento: a varredura nao o alcanca. */
    g_bigStream[BC250_GFX_IB_SCAN_MAX_DWORDS + 8u] =
        Pkt3(BC250_GFX_IB_OPCODE_INDIRECT_BUFFER, 2u);

    r.Words = g_bigStream; r.Count = total; r.FailAtIndex = 0xFFFFFFFFu; r.ReadCalls = 0u;
    Bc250GfxIbInspectStream(total, ArrayRead, &r, &insp);

    CHECK(insp.IndirectBufferPackets == 0u);   /* nao alcancou o salto */
    CHECK(insp.ScanComplete == 0u);            /* e SABE que nao alcancou */
    CHECK(StrEq(Bc250GfxIbInspectVerdictName(&insp), "INDETERMINADO"));
    CHECK(!StrEq(Bc250GfxIbInspectVerdictName(&insp), "IB_LINEAR_CONFIRMADO"));
    return 0;
}

/* Caso 5 -- NEGATIVO: type 1 nao existe em PM4; tem de abortar, nao adivinhar. */
static int TestType1Aborta(void)
{
    uint32_t words[8];
    ARRAY_READER r;
    BC250_GFX_IB_INSPECTION insp;
    uint32_t i;

    /* [5] NEGATIVO: type 1 aborta a varredura */

    for (i = 0u; i < 8u; ++i) words[i] = NOP_T2;
    words[4] = (1u << 30);

    r.Words = words; r.Count = 8u; r.FailAtIndex = 0xFFFFFFFFu; r.ReadCalls = 0u;
    Bc250GfxIbInspectStream(8u, ArrayRead, &r, &insp);

    CHECK(insp.WalkAborted == 1u);
    CHECK(insp.ScanComplete == 0u);
    CHECK(StrEq(Bc250GfxIbInspectVerdictName(&insp), "INDETERMINADO"));
    return 0;
}

/* Caso 6 -- entradas degeneradas: saida sempre inicializada, nunca crash. */
static int TestEntradasDegeneradas(void)
{
    BC250_GFX_IB_INSPECTION insp;
    ARRAY_READER r;
    uint32_t one = NOP_T2;

    /* [6] entradas degeneradas: tamanho zero, leitor nulo, saida nula */

    r.Words = &one; r.Count = 1u; r.FailAtIndex = 0xFFFFFFFFu; r.ReadCalls = 0u;

    Bc250GfxIbInspectStream(0u, ArrayRead, &r, &insp);
    CHECK(insp.RequestedDwords == 0u);
    CHECK(insp.ScanComplete == 0u);
    CHECK(StrEq(Bc250GfxIbInspectVerdictName(&insp), "INDETERMINADO"));

    Bc250GfxIbInspectStream(4u, NULL, &r, &insp);
    CHECK(insp.ScanComplete == 0u);
    CHECK(insp.PacketsWalked == 0u);

    /* Saida nula nao pode escrever nem quebrar. */
    Bc250GfxIbInspectStream(4u, ArrayRead, &r, NULL);
    CHECK(StrEq(Bc250GfxIbInspectVerdictName(NULL), "INDETERMINADO"));
    return 0;
}

/*
 * Caso 7 -- multiplos saltos sao TODOS contados, e o offset registrado e' o do
 * primeiro. Impede que a contagem pare no primeiro achado e mascare um stream
 * que voltou ao encadeamento em varios pontos.
 */
static int TestMultiplosSaltos(void)
{
    uint32_t words[32];
    ARRAY_READER r;
    BC250_GFX_IB_INSPECTION insp;
    uint32_t i;

    /* [7] multiplos saltos: contagem total e offset do primeiro */

    for (i = 0u; i < 32u; ++i) words[i] = NOP_T2;
    /* Tres SALTOS reais: cada um com o dword de controle carregando CHAIN=1.
     * Sem o controle explicito o teste mediria NOPs como controle e os tres
     * cairiam em IB2 -- foi esse detalhe que mascarou a classe na corrida 13:12. */
    words[4]  = Pkt3(BC250_GFX_IB_OPCODE_INDIRECT_BUFFER, 2u);
    words[7]  = BC250_GFX_IB_CTRL_CHAIN_MASK | 0x40u;
    words[12] = Pkt3(BC250_GFX_IB_OPCODE_INDIRECT_BUFFER, 2u);
    words[15] = BC250_GFX_IB_CTRL_CHAIN_MASK | 0x40u;
    words[20] = Pkt3(BC250_GFX_IB_OPCODE_INDIRECT_BUFFER, 2u);
    words[23] = BC250_GFX_IB_CTRL_CHAIN_MASK | 0x40u;

    r.Words = words; r.Count = 32u; r.FailAtIndex = 0xFFFFFFFFu; r.ReadCalls = 0u;
    Bc250GfxIbInspectStream(32u, ArrayRead, &r, &insp);

    CHECK(insp.ScanComplete == 1u);
    CHECK(insp.IndirectBufferPackets == 3u);
    CHECK(insp.ChainJumps == 3u);
    CHECK(insp.Ib2Calls == 0u);
    CHECK(insp.FirstChainDwordOffset == 4u);
    CHECK(StrEq(Bc250GfxIbInspectVerdictName(&insp), "IB_AINDA_ENCADEADO"));
    return 0;
}

/*
 * Caso 8 -- MISTURA: um salto e uma chamada IB2 no mesmo stream. O salto manda
 * no veredito, e as duas contagens tem de ficar separadas.
 */
static int TestMisturaSaltoEIb2(void)
{
    uint32_t words[32];
    ARRAY_READER r;
    BC250_GFX_IB_INSPECTION insp;
    uint32_t i;

    for (i = 0u; i < 32u; ++i) words[i] = NOP_T2;
    words[4]  = Pkt3(BC250_GFX_IB_OPCODE_INDIRECT_BUFFER, 2u);
    words[7]  = 0x000000B0u;                             /* IB2: CHAIN=0 */
    words[16] = Pkt3(BC250_GFX_IB_OPCODE_INDIRECT_BUFFER, 2u);
    words[19] = BC250_GFX_IB_CTRL_CHAIN_MASK | 0x80u;    /* SALTO: CHAIN=1 */

    r.Words = words; r.Count = 32u; r.FailAtIndex = 0xFFFFFFFFu; r.ReadCalls = 0u;
    Bc250GfxIbInspectStream(32u, ArrayRead, &r, &insp);

    CHECK(insp.ScanComplete == 1u);
    CHECK(insp.IndirectBufferPackets == 2u);
    CHECK(insp.Ib2Calls == 1u);
    CHECK(insp.ChainJumps == 1u);
    CHECK(insp.FirstIndirectDwordOffset == 4u);   /* o primeiro 0x3F e' o IB2 */
    CHECK(insp.FirstChainDwordOffset == 16u);     /* mas o primeiro SALTO e' o outro */
    CHECK(StrEq(Bc250GfxIbInspectVerdictName(&insp), "IB_AINDA_ENCADEADO"));
    return 0;
}

int main(void)
{
    if (TestStreamMedidoDaCorrida() != 0) return 1;
    if (TestStreamIb2NaoEhSalto() != 0) return 1;
    if (TestStreamLinearConcatenado() != 0) return 1;
    if (TestLeituraFalhaNaoViraLinear() != 0) return 1;
    if (TestAcimaDoOrcamentoNaoViraLinear() != 0) return 1;
    if (TestType1Aborta() != 0) return 1;
    if (TestEntradasDegeneradas() != 0) return 1;
    if (TestMultiplosSaltos() != 0) return 1;
    if (TestMisturaSaltoEIb2() != 0) return 1;

    /* GFX IB INSPECT CONTRACT: OK (9 casos, 4 negativos + 2 de classe CHAIN/IB2, offline) */
    /* Escopo: descreve a MEMORIA apontada ao CP. Nao prova busca, execucao nem dispatch de shader. */
    return 0;
}

/* Entry point sem CRT, na mesma convencao das outras suites do projeto. */
void __stdcall mainCRTStartup(void)
{
    ExitProcess((unsigned long)main());
}
