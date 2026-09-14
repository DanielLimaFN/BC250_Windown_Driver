#include "../core/bc250_gfx_ring_contract.h"
#include "../core/bc250_gfx_ring_transaction.h"

#include <string.h>

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

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#else
#include <stdio.h>
/* Falha devolve 0 porque main() faz `return Run() ? 0 : 1`: devolver 1
 * aqui fazia a falha virar SUCESSO no build offline. Medido em 14/09
 * quebrando o emissor de proposito -- suite offline saiu EXIT=0 e a
 * kernel EXIT=1. Travado por audit_offline_check_is_not_inverted. */
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#endif

/* Janela de VRAM ficticia mas coerente com a geometria real do segmento 1. */
#define TEST_WINDOW_BASE   0x0000000000A8C000ull
#define TEST_WINDOW_BYTES  0x000000000F531000ull

static void Bc250GfxRingFillGoodPlan(BC250_GFX_RING_PLAN *Plan)
{
    memset(Plan, 0, sizeof(*Plan));
    Plan->VramWindowBase      = TEST_WINDOW_BASE;
    Plan->VramWindowBytes     = TEST_WINDOW_BYTES;
    Plan->RingGpuVa           = TEST_WINDOW_BASE + 0x10000ull;
    Plan->RingBytes           = 8192u;
    Plan->RptrWritebackGpuVa  = TEST_WINDOW_BASE + 0x20000ull;
    Plan->WptrPollGpuVa       = TEST_WINDOW_BASE + 0x20010ull;
    Plan->RbExe               = 1u;
    Plan->CeHqdNeqRbHqd       = 1u;
}

static int Bc250RunGfxRingContractTest(void)
{
    BC250_GFX_RING_PLAN plan;
    BC250_GFX_RING_CNTL_FIELDS fields;
    BC250_GFX_ME_CNTL_FIELDS meFields;
    BC250_TRIPOD_RPTR_SOURCE source;
    uint32_t bufsz;
    uint32_t blksz;
    uint32_t cntl;

    /* ------------------------------------------------------------------
     * 1. Derivacao de RB_BUFSZ / RB_BLKSZ.
     *
     * A ANCORA E' ARITMETICA, NAO MEDICAO. 8192 bytes tem de produzir (9, 7),
     * que sao os campos de BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE.
     *
     * CORRECAO DE REGISTRO (14/09): a versao anterior deste comentario dizia
     * "os mesmos campos que o CP_RB0_CNTL medido nesta placa". Era FALSO. A
     * medicao das 11:43 mostrou que o boot entrega 0x00A00000, com RB_BUFSZ=0
     * e RB_EXE=0. O 0x50000709 e' referencia de um anel CONFIGURADO, vinda de
     * docs/03 e observada em outro momento -- nao e' o estado de boot.
     * ------------------------------------------------------------------ */
    CHECK(Bc250GfxRingDeriveBufsz(8192u, &bufsz, &blksz) == BC250_GFX_RING_OK);
    CHECK(bufsz == 9u);
    CHECK(blksz == 7u);

    CHECK(Bc250GfxRingDeriveBufsz(4096u, &bufsz, &blksz) == BC250_GFX_RING_OK);
    CHECK(bufsz == 8u);
    CHECK(blksz == 6u);

    CHECK(Bc250GfxRingDeriveBufsz(0x100000u, &bufsz, &blksz) == BC250_GFX_RING_OK);
    CHECK(bufsz == 16u);
    CHECK(blksz == 14u);

    /* Tamanhos recusados: nao potencia de dois, abaixo do piso, acima do teto. */
    CHECK(Bc250GfxRingDeriveBufsz(6144u, &bufsz, &blksz) == BC250_GFX_RING_BAD_SIZE);
    CHECK(Bc250GfxRingDeriveBufsz(2048u, &bufsz, &blksz) == BC250_GFX_RING_BAD_SIZE);
    CHECK(Bc250GfxRingDeriveBufsz(0x200000u, &bufsz, &blksz) == BC250_GFX_RING_BAD_SIZE);
    CHECK(Bc250GfxRingDeriveBufsz(8192u, NULL, &blksz) == BC250_GFX_RING_BAD_ARGUMENT);

    /* ------------------------------------------------------------------
     * 2. Plano valido -- o contraponto. Sem ele as recusas abaixo virariam
     *    peneira: um contrato que recusa tudo tambem passa nos negativos.
     * ------------------------------------------------------------------ */
    Bc250GfxRingFillGoodPlan(&plan);
    CHECK(Bc250GfxRingValidatePlan(&plan) == BC250_GFX_RING_OK);

    /* ------------------------------------------------------------------
     * 3. O CNTL calculado tem de REPRODUZIR a referencia de anel configurado.
     *
     * Anel de 8192 bytes, RB_EXE=1, CE_HQD_NEQ_RB_HQD=1, resto zero:
     *   9 | (7 << 8) | (1 << 28) | (1 << 30) == 0x50000709
     * que e' exatamente BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE.
     *
     * Isto confere a ARITMETICA e corrobora RB_BLKSZ = RB_BUFSZ - 2. NAO
     * afirma que a placa esta nesse estado -- ela nao esta (ver bloco 3b).
     * ------------------------------------------------------------------ */
    CHECK(Bc250GfxRingComputeCntl(&plan, &cntl) == BC250_GFX_RING_OK);
    CHECK(cntl == BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE);
    CHECK(cntl == 0x50000709u);

    /* ------------------------------------------------------------------
     * 4. Decodificacao do valor medido.
     * ------------------------------------------------------------------ */
    CHECK(Bc250GfxRingDecodeCntl(BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE, &fields)
          == BC250_GFX_RING_OK);
    CHECK(fields.RbBufsz == 9u);
    CHECK(fields.RbBlksz == 7u);
    CHECK(fields.RbExe == 1u);
    CHECK(fields.CeHqdNeqRbHqd == 1u);
    CHECK(fields.BufSwap == 0u);
    CHECK(fields.MinAvailsz == 0u);
    CHECK(fields.MinIbAvailsz == 0u);
    CHECK(fields.CachePolicy == 0u);
    CHECK(fields.RbVolatile == 0u);
    CHECK(fields.RbNoUpdate == 0u);
    CHECK(fields.RbRptrWrEna == 0u);

    /* ------------------------------------------------------------------
     * 3b. O ESTADO REAL DE BOOT, MEDIDO EM 14/09 11:43 (P0).
     *
     * Primeira leitura do anel grafico do projeto. O anel esta FRIO: sem
     * tamanho, sem execucao, e com as tres micro-engines halted. Estas
     * assercoes existem para que a proxima sessao nao volte a tratar o
     * 0x50000709 como ponto de partida -- foi o erro que esta corrida
     * refutou.
     * ------------------------------------------------------------------ */
    CHECK(Bc250GfxRingDecodeCntl(BC250_GFX_RING_CNTL_MEASURED_BOOT, &fields)
          == BC250_GFX_RING_OK);
    CHECK(fields.RbBufsz == 0u);
    CHECK(fields.RbBlksz == 0u);
    CHECK(fields.RbExe == 0u);
    CHECK(fields.MinAvailsz == 2u);
    CHECK(fields.MinIbAvailsz == 2u);
    CHECK(Bc250GfxRingCntlRefusesNonExecutable(BC250_GFX_RING_CNTL_MEASURED_BOOT) == 1u);
    /* e o boot NAO e' a referencia de anel configurado */
    CHECK(BC250_GFX_RING_CNTL_MEASURED_BOOT != BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE);

    /* As tres micro-engines graficas estao halted no boot. */
    CHECK(Bc250GfxRingDecodeMeCntl(BC250_GFX_RING_ME_CNTL_MEASURED_BOOT, &meFields)
          == BC250_GFX_RING_OK);
    CHECK(meFields.CeHalt == 1u);
    CHECK(meFields.PfpHalt == 1u);
    CHECK(meFields.MeHalt == 1u);
    CHECK(meFields.AllHalted == 1u);
    CHECK(Bc250GfxRingMicroEnginesAreHalted(BC250_GFX_RING_ME_CNTL_MEASURED_BOOT) == 1u);

    /* QUALQUER uma halted ja impede execucao -- exigir as tres deixaria passar
     * o caso mais dificil de diagnosticar. Um contraponto por engine. */
    CHECK(Bc250GfxRingMicroEnginesAreHalted(1u << 24) == 1u); /* so' CE */
    CHECK(Bc250GfxRingMicroEnginesAreHalted(1u << 26) == 1u); /* so' PFP */
    CHECK(Bc250GfxRingMicroEnginesAreHalted(1u << 28) == 1u); /* so' ME */
    /* nenhuma halted -> liberado. Sem este caso a funcao poderia devolver
     * sempre 1 e todos os positivos acima passariam. */
    CHECK(Bc250GfxRingMicroEnginesAreHalted(0u) == 0u);
    /* bits de STEP nao sao HALT */
    CHECK(Bc250GfxRingMicroEnginesAreHalted((1u << 25) | (1u << 27)) == 0u);

    /* ------------------------------------------------------------------
     * 5. O DEFEITO CONCRETO DO PROTOTIPO EXISTENTE -- agora com causa.
     *
     * bc250_compute_service.c escreve 0x00A00709 em CP_RB0_CNTL, que tem
     * RB_EXE = 0. A medicao das 11:43 explicou de onde veio: e' o CNTL do
     * BOOT (0x00A00000) com OR dos bits de tamanho (0x709). Quem escreveu leu
     * o boot e somou o tamanho, sem nunca ligar RB_EXE.
     * ------------------------------------------------------------------ */
    CHECK((BC250_GFX_RING_CNTL_MEASURED_BOOT | 0x709u) == 0x00A00709u);
    CHECK(Bc250GfxRingDecodeCntl(0x00A00709u, &fields) == BC250_GFX_RING_OK);
    CHECK(fields.RbBufsz == 9u);
    CHECK(fields.RbBlksz == 7u);
    CHECK(fields.RbExe == 0u);
    CHECK(Bc250GfxRingCntlRefusesNonExecutable(0x00A00709u) == 1u);
    CHECK(Bc250GfxRingCntlRefusesNonExecutable(BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE) == 0u);

    /* E o plano que pede anel nao executavel e' RECUSADO por nome. */
    Bc250GfxRingFillGoodPlan(&plan);
    plan.RbExe = 0u;
    CHECK(Bc250GfxRingValidatePlan(&plan) == BC250_GFX_RING_NOT_EXECUTABLE);
    CHECK(Bc250GfxRingComputeCntl(&plan, &cntl) == BC250_GFX_RING_NOT_EXECUTABLE);
    CHECK(cntl == 0u);

    /* ------------------------------------------------------------------
     * 5b. DESALT DAS MICRO-ENGINES -- calculo e pre-requisito.
     * ------------------------------------------------------------------ */
    {
        uint32_t unhalted = 0xFFFFFFFFu;
        /* Parte do valor MEDIDO no boot e tem de zerar exatamente as tres HALT. */
        CHECK(Bc250GfxRingComputeMeCntlUnhalt(BC250_GFX_RING_ME_CNTL_MEASURED_BOOT,
                  &unhalted) == BC250_GFX_RING_OK);
        CHECK(unhalted == 0u);
        CHECK(Bc250GfxRingMicroEnginesAreHalted(unhalted) == 0u);

        /* PRESERVA o que nao e' HALT. Escrever literal apagaria os bits de
         * reset e de invalidacao de icache sem que nenhum log mostrasse. */
        CHECK(Bc250GfxRingComputeMeCntlUnhalt(
                  BC250_GFX_RING_ME_CNTL_MEASURED_BOOT | (1u << 20) | (1u << 8),
                  &unhalted) == BC250_GFX_RING_OK);
        CHECK(unhalted == ((1u << 20) | (1u << 8)));

        /* E os bits de STEP, que ficam ENTRE os de HALT, sobrevivem. */
        CHECK(Bc250GfxRingComputeMeCntlUnhalt(
                  BC250_GFX_RING_ME_CNTL_MEASURED_BOOT | (1u << 25) | (1u << 27),
                  &unhalted) == BC250_GFX_RING_OK);
        CHECK(unhalted == ((1u << 25) | (1u << 27)));

        CHECK(Bc250GfxRingComputeMeCntlUnhalt(0u, NULL) == BC250_GFX_RING_BAD_ARGUMENT);
    }

    /* Pedir desalt sem declarar firmware carregado e' RECUSADO por nome. */
    Bc250GfxRingFillGoodPlan(&plan);
    plan.UnhaltMicroEngines = 1u;
    plan.MicroEngineFirmwareLoaded = 0u;
    CHECK(Bc250GfxRingValidatePlan(&plan) == BC250_GFX_RING_FIRMWARE_NOT_LOADED);
    /* com a declaracao, passa -- o contraponto que impede a recusa de virar peneira */
    plan.MicroEngineFirmwareLoaded = 1u;
    CHECK(Bc250GfxRingValidatePlan(&plan) == BC250_GFX_RING_OK);
    /* e nao pedir desalt nunca exige firmware */
    Bc250GfxRingFillGoodPlan(&plan);
    plan.MicroEngineFirmwareLoaded = 0u;
    CHECK(Bc250GfxRingValidatePlan(&plan) == BC250_GFX_RING_OK);

    /* ------------------------------------------------------------------
     * 6. Valor de poll importado do SDMA.
     * ------------------------------------------------------------------ */
    CHECK(Bc250GfxRingPollCntlIsBorrowed(0x00403004u) == 1u);
    CHECK(Bc250GfxRingPollCntlIsBorrowed(0x00400100u) == 0u);

    /* ------------------------------------------------------------------
     * 7. Geometria do plano -- cada recusa nomeada, com um caso cada.
     * ------------------------------------------------------------------ */
    Bc250GfxRingFillGoodPlan(&plan);
    plan.RingGpuVa += 4ull; /* deixa de ser multiplo de 256 */
    CHECK(Bc250GfxRingValidatePlan(&plan) == BC250_GFX_RING_RING_MISALIGNED);

    Bc250GfxRingFillGoodPlan(&plan);
    plan.RptrWritebackGpuVa += 1ull;
    CHECK(Bc250GfxRingValidatePlan(&plan) == BC250_GFX_RING_WRITEBACK_MISALIGNED);

    Bc250GfxRingFillGoodPlan(&plan);
    plan.RingGpuVa = TEST_WINDOW_BASE - 0x1000ull; /* antes da janela */
    CHECK(Bc250GfxRingValidatePlan(&plan) == BC250_GFX_RING_OUT_OF_WINDOW);

    Bc250GfxRingFillGoodPlan(&plan);
    /* anel termina exatamente no fim da janela: ainda DENTRO, nao pode recusar */
    plan.RingGpuVa = TEST_WINDOW_BASE + TEST_WINDOW_BYTES - plan.RingBytes;
    CHECK(Bc250GfxRingValidatePlan(&plan) == BC250_GFX_RING_OK);
    /* um byte alem: fora */
    plan.RingGpuVa += 0x100ull;
    CHECK(Bc250GfxRingValidatePlan(&plan) == BC250_GFX_RING_OUT_OF_WINDOW);

    Bc250GfxRingFillGoodPlan(&plan);
    plan.RptrWritebackGpuVa = plan.RingGpuVa + 0x100ull; /* dentro do anel */
    CHECK(Bc250GfxRingValidatePlan(&plan) == BC250_GFX_RING_OVERLAP);

    Bc250GfxRingFillGoodPlan(&plan);
    plan.WptrPollGpuVa = plan.RptrWritebackGpuVa; /* mesmo dword */
    CHECK(Bc250GfxRingValidatePlan(&plan) == BC250_GFX_RING_OVERLAP);

    Bc250GfxRingFillGoodPlan(&plan);
    plan.CachePolicy = 4u; /* campo de 2 bits */
    CHECK(Bc250GfxRingValidatePlan(&plan) == BC250_GFX_RING_FIELD_OVERFLOW);

    CHECK(Bc250GfxRingValidatePlan(NULL) == BC250_GFX_RING_BAD_ARGUMENT);

    /* ------------------------------------------------------------------
     * 8. PERNA 1 DO TRIPE COM VOLTA DO ANEL -- a licao A15, agora no GFX.
     *
     * Os numeros sao os MEDIDOS na corrida 07/09 17:43, que custou um
     * BugCheck 0x119: anel de 4096 bytes, alvo 516, RPTR do registrador 4612.
     * 4612 = 4096 + 516: a engine TERMINOU e a comparacao crua nunca fecharia.
     *
     * CADA LEITURA E' ISOLADA DE PROPOSITO. Bc250ClassifySdmaTripod satisfaz a
     * perna 1 com QUALQUER das duas leituras, entao um caso em que ambas ja
     * chegam reduzidas passa mesmo com a reducao removida -- foi exatamente
     * assim que a primeira versao deste teste deixou a mutacao do registrador
     * escapar. Aqui a outra leitura carrega o writeback ATRASADO de 52, o
     * valor observado em todas as corridas do projeto, para que so' a leitura
     * sob teste possa fechar a perna.
     * ------------------------------------------------------------------ */
    source = BC250_TRIPOD_RPTR_SOURCE_NONE;

    /* So' o REGISTRADOR pode fechar: writeback atrasado em 52. */
    CHECK(Bc250GfxRingClassifyTripod(4096u, 516u, 52u, 4612u,
              0x1030u, 0x1030u, 0u, &source) == BC250_TRIPOD_PROVEN);
    CHECK(source == BC250_TRIPOD_RPTR_SOURCE_REGISTER);

    /* So' o WRITEBACK pode fechar: registrador atrasado em 52. */
    source = BC250_TRIPOD_RPTR_SOURCE_NONE;
    CHECK(Bc250GfxRingClassifyTripod(4096u, 516u, 4612u, 52u,
              0x1030u, 0x1030u, 0u, &source) == BC250_TRIPOD_PROVEN);
    CHECK(source == BC250_TRIPOD_RPTR_SOURCE_WRITEBACK);

    /* O proprio ALVO pode vir alem de uma volta e tem de ser reduzido. */
    source = BC250_TRIPOD_RPTR_SOURCE_NONE;
    CHECK(Bc250GfxRingClassifyTripod(4096u, 4612u, 52u, 516u,
              0x1030u, 0x1030u, 0u, &source) == BC250_TRIPOD_PROVEN);

    /* Caso negativo: 4608 reduz para 512, que NAO e' o alvo 516. Sem ele a
     * reducao viraria peneira, aceitando qualquer ponteiro. */
    source = BC250_TRIPOD_RPTR_SOURCE_NONE;
    CHECK(Bc250GfxRingClassifyTripod(4096u, 516u, 4608u, 4608u,
              0x1030u, 0x1030u, 0u, &source) == BC250_TRIPOD_RPTR_NOT_RETIRED);

    /* Fence que nao retirou o poison nao e' tripe, mesmo com RPTR casando. */
    CHECK(Bc250GfxRingClassifyTripod(4096u, 516u, 516u, 4612u,
              0x1030u, 0x43DAFFFEu, 0u, &source) == BC250_TRIPOD_FENCE_NOT_WRITTEN);

    /* Dados divergentes tambem nao. */
    CHECK(Bc250GfxRingClassifyTripod(4096u, 516u, 516u, 4612u,
              0x1030u, 0x1030u, 3u, &source) == BC250_TRIPOD_DATA_MISMATCH);

    /* ------------------------------------------------------------------
     * 9. Nomes estaveis para evidencia.
     * ------------------------------------------------------------------ */
    CHECK(Bc250GfxRingStatusName(BC250_GFX_RING_NOT_EXECUTABLE)[0] == 'N');
    CHECK(Bc250GfxRingStatusName(BC250_GFX_RING_OK)[0] == 'O');

    return 1;
}

/* =====================================================================
 * MMIO FALSO -- a transacao inteira e' exercitada sem placa.
 * ===================================================================== */

#define FAKE_REG_SLOTS 24u

typedef struct FAKE_REG {
    uint32_t Offset;
    uint32_t Value;
    uint32_t Used;
} FAKE_REG;

static FAKE_REG g_fake[FAKE_REG_SLOTS];
static uint32_t g_fakeReadFailOffset;   /* leitura deste offset falha; 0 = nenhum */
static uint32_t g_fakeWriteFailOffset;  /* escrita deste offset falha */
static uint32_t g_fakeIgnoreWriteAt;    /* escrita aceita mas descartada (readback mentiroso) */
static uint32_t g_fakeStalls;

static void FakeReset(void)
{
    uint32_t i;
    for (i = 0u; i < FAKE_REG_SLOTS; ++i) {
        g_fake[i].Offset = 0u;
        g_fake[i].Value = 0u;
        g_fake[i].Used = 0u;
    }
    g_fakeReadFailOffset = 0u;
    g_fakeWriteFailOffset = 0u;
    g_fakeIgnoreWriteAt = 0u;
    g_fakeStalls = 0u;
}

static FAKE_REG *FakeSlot(uint32_t Offset)
{
    uint32_t i;
    for (i = 0u; i < FAKE_REG_SLOTS; ++i) {
        if (g_fake[i].Used && g_fake[i].Offset == Offset) {
            return &g_fake[i];
        }
    }
    for (i = 0u; i < FAKE_REG_SLOTS; ++i) {
        if (!g_fake[i].Used) {
            g_fake[i].Used = 1u;
            g_fake[i].Offset = Offset;
            g_fake[i].Value = 0u;
            return &g_fake[i];
        }
    }
    return NULL;
}

static uint32_t FakeRead32(void *Context, uint32_t Offset, uint32_t *Value)
{
    FAKE_REG *slot;
    (void)Context;
    if (Value == NULL) return 0u;
    if (g_fakeReadFailOffset != 0u && Offset == g_fakeReadFailOffset) return 0u;
    slot = FakeSlot(Offset);
    if (slot == NULL) return 0u;
    *Value = slot->Value;
    return 1u;
}

static uint32_t FakeWrite32(void *Context, uint32_t Offset, uint32_t Value)
{
    FAKE_REG *slot;
    (void)Context;
    if (g_fakeWriteFailOffset != 0u && Offset == g_fakeWriteFailOffset) return 0u;
    slot = FakeSlot(Offset);
    if (slot == NULL) return 0u;
    /* Escrita "aceita" que o silicio descarta: e' assim que um readback
     * mentiroso aparece de verdade, e o teste precisa cobrir esse caso. */
    if (g_fakeIgnoreWriteAt != 0u && Offset == g_fakeIgnoreWriteAt) return 1u;
    slot->Value = Value;
    return 1u;
}

static void FakeStall(void *Context, uint32_t Microseconds)
{
    (void)Context;
    (void)Microseconds;
    g_fakeStalls++;
}

static void FakeIface(BC250_GFX_RING_INTERFACE *Iface)
{
    Iface->Read32 = FakeRead32;
    Iface->Write32 = FakeWrite32;
    Iface->Stall = FakeStall;
    Iface->Context = NULL;
    Iface->Identity = BC250_GFX_RING_TXN_IDENTITY;
}

/* Placa "como encontrada": o CP_RB0_CNTL medido nesta unidade. */
static void FakePowerOnState(void)
{
    FakeSlot(BC250_GFX_REG_CP_RB0_CNTL)->Value = BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE;
    FakeSlot(BC250_GFX_REG_CP_RB0_BASE)->Value = 0x00F40FE0u;
    FakeSlot(BC250_GFX_REG_CP_RB0_BASE_HI)->Value = 0u;
    FakeSlot(BC250_GFX_REG_CP_RB0_RPTR)->Value = 0x40u;
    FakeSlot(BC250_GFX_REG_CP_RB0_WPTR)->Value = 0x40u;
    FakeSlot(BC250_GFX_REG_CP_RB_WPTR_POLL_CNTL)->Value = 0x00400100u;
    FakeSlot(BC250_GFX_REG_CP_ME_CNTL)->Value = BC250_GFX_RING_ME_CNTL_MEASURED_BOOT;
    FakeSlot(BC250_GFX_REG_CP_RB0_ACTIVE)->Value = 0u; /* anel desligado no boot */
}

static int Bc250RunGfxRingTransactionTest(void)
{
    BC250_GFX_RING_INTERFACE iface;
    BC250_GFX_RING_PLAN plan;
    BC250_GFX_RING_TXN_EVIDENCE ev;
    uint32_t probe = 0u;

    FakeIface(&iface);

    /* ------------------------------------------------------------------
     * 1. PREFLIGHT e' SOMENTE LEITURA.
     *
     * Esta e' a assercao que autoriza rodar o preflight numa corrida sem
     * risco. Se ele escrever um registrador que seja, a autorizacao cai.
     * ------------------------------------------------------------------ */
    FakeReset();
    FakePowerOnState();
    Bc250GfxRingFillGoodPlan(&plan);
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(ev.SnapshotTaken == 1u);
    CHECK(ev.Writes == 0u);
    CHECK(ev.Reads == 7u);
    CHECK(ev.CntlBefore == BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE);
    CHECK(ev.FieldsBefore.RbBufsz == 9u);
    CHECK(ev.FieldsBefore.RbExe == 1u);
    /* o preflight tambem decodifica o estado das micro-engines: a placa falsa
     * carrega o ME_CNTL MEDIDO no boot, com as tres halted */
    CHECK(ev.MeCntlBefore == BC250_GFX_RING_ME_CNTL_MEASURED_BOOT);
    CHECK(ev.MeCntlFields.AllHalted == 1u);
    CHECK(ev.MicroEnginesHalted == 1u);
    CHECK(ev.CntlPlanned == BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE);
    CHECK(ev.FieldsPlanned.RbExe == 1u);

    /* Plano recusado nao gera leitura nenhuma. */
    FakeReset();
    FakePowerOnState();
    Bc250GfxRingFillGoodPlan(&plan);
    plan.RbExe = 0u;
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_PLAN_REJECTED);
    CHECK(ev.SnapshotTaken == 0u);
    CHECK(ev.Reads == 0u);
    CHECK(ev.Writes == 0u);

    /* ------------------------------------------------------------------
     * 1b. MEDICAO PURA (Plan == NULL): fotografa sem propor, e NAO pode
     *     abrir caminho para escrita. Este e' o modo que autoriza rodar o
     *     preflight numa corrida antes de existir qualquer plano valido.
     * ------------------------------------------------------------------ */
    FakeReset();
    FakePowerOnState();
    CHECK(Bc250GfxRingTxnPreflight(&iface, NULL, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(ev.SnapshotTaken == 1u);
    CHECK(ev.Writes == 0u);
    CHECK(ev.Reads == 7u);
    CHECK(ev.CntlBefore == BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE);
    CHECK(ev.FieldsBefore.RbExe == 1u);
    CHECK(ev.CntlPlanned == 0u);
    /* e o APPLY recusa: medir nunca escorrega para escrever */
    Bc250GfxRingFillGoodPlan(&plan);
    probe = ev.Writes;
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_BAD_ARGUMENT);
    CHECK(ev.Writes == probe);

    /* Leitura que falha aborta antes do snapshot -- sem snapshot nao ha volta. */
    FakeReset();
    FakePowerOnState();
    g_fakeReadFailOffset = BC250_GFX_REG_CP_ME_CNTL;
    Bc250GfxRingFillGoodPlan(&plan);
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_PREFLIGHT_FAILED);
    CHECK(ev.SnapshotTaken == 0u);
    CHECK(ev.Writes == 0u);

    /* ------------------------------------------------------------------
     * 2. APPLY exige snapshot. Aplicar sem rota de volta viola a Lei 5.
     * ------------------------------------------------------------------ */
    FakeReset();
    FakePowerOnState();
    Bc250GfxRingFillGoodPlan(&plan);
    memset(&ev, 0, sizeof(ev));
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_BAD_ARGUMENT);
    CHECK(ev.Writes == 0u);

    /* ------------------------------------------------------------------
     * 3. O valor de poll do SDMA barra o APPLY ANTES da primeira escrita.
     *
     * O prototipo bc250_compute_service.c carrega 0x00403004 -- valor do
     * SdmaRingPollCntl -- para o CP. O catalogo ja proibia isso no
     * registrador irmao.
     * ------------------------------------------------------------------ */
    FakeReset();
    FakePowerOnState();
    FakeSlot(BC250_GFX_REG_CP_RB_WPTR_POLL_CNTL)->Value = 0x00403004u;
    Bc250GfxRingFillGoodPlan(&plan);
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    probe = ev.Writes;
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_POLL_CNTL_BORROWED);
    CHECK(ev.Writes == probe); /* nenhuma escrita aconteceu */

    /* ------------------------------------------------------------------
     * 4. APPLY completo: configura, confere por readback e PARA na
     *    proveniencia, nomeando o registrador que falta.
     * ------------------------------------------------------------------ */
    FakeReset();
    FakePowerOnState();
    Bc250GfxRingFillGoodPlan(&plan);
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(ev.ApplyReadbackPassed == 1u);
    CHECK(ev.CntlReadback == ev.CntlPlanned);
    CHECK(ev.BaseReadback == (uint32_t)(plan.RingGpuVa >> 8));
    /*
     * Ate 14/09 o APPLY parava aqui por falta de procedencia de CP_RB0_ACTIVE.
     * Com gc_10_1_0_offset.h vendorizado e provado, ele LIGA o anel -- e a
     * prova e' o readback, nao a escrita.
     */
    CHECK(ev.RingActivateAttempted == 1u);
    CHECK(ev.RingActivatePassed == 1u);
    CHECK((ev.RingActivateReadback & 1u) == 1u);
    /* Dois assentamentos neste caminho: apos a configuracao do anel e apos a
     * partida. O desalt acrescentaria um terceiro, e nao foi pedido aqui. */
    CHECK(g_fakeStalls == 2u);

    /* ------------------------------------------------------------------
     * 5. READBACK MENTIROSO: a escrita e' aceita e descartada pelo silicio.
     *    Sem esta cobertura, "escrevi" passaria por "esta escrito".
     *
     * ARMADILHA QUE ESTE TESTE JA CAIU: a primeira versao deixava o CNTL da
     * placa falsa no valor do POST, que e' EXATAMENTE o valor que o plano
     * calcula. Descartar a escrita nao mudava nada e o readback "conferia" --
     * a assercao era auto-anulavel. Aqui a placa entra com o valor do
     * prototipo (RB_EXE=0), que difere do planejado, para que descartar a
     * escrita seja mesmo detectavel.
     * ------------------------------------------------------------------ */
    FakeReset();
    FakePowerOnState();
    FakeSlot(BC250_GFX_REG_CP_RB0_CNTL)->Value = 0x00A00709u;
    Bc250GfxRingFillGoodPlan(&plan);
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(ev.CntlBefore != ev.CntlPlanned); /* o cenario so' vale se diferirem */
    g_fakeIgnoreWriteAt = BC250_GFX_REG_CP_RB0_CNTL;
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_READBACK_MISMATCH);
    CHECK(ev.ApplyReadbackPassed == 0u);
    CHECK(ev.CntlReadback == 0x00A00709u); /* a placa manteve o valor antigo */

    /* Escrita que falha no meio tambem nao vira sucesso. */
    FakeReset();
    FakePowerOnState();
    Bc250GfxRingFillGoodPlan(&plan);
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    g_fakeWriteFailOffset = BC250_GFX_REG_CP_RB0_BASE;
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_WRITE_FAILED);
    CHECK(ev.ApplyReadbackPassed == 0u);

    /* ------------------------------------------------------------------
     * 6. ROLLBACK devolve a placa ao estado do snapshot, e PROVA por
     *    readback -- restaurar sem conferir nao e' restaurar.
     *
     * A placa entra com o CNTL do prototipo (RB_EXE=0), DIFERENTE do que o
     * plano calcula. Se os dois fossem iguais -- e sao, quando a placa esta
     * no valor do POST -- nem o APPLY nem o ROLLBACK mudariam esse
     * registrador, e as assercoes abaixo passariam sem testar nada.
     * ------------------------------------------------------------------ */
    FakeReset();
    FakePowerOnState();
    FakeSlot(BC250_GFX_REG_CP_RB0_CNTL)->Value = 0x00A00709u;
    Bc250GfxRingFillGoodPlan(&plan);
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(ev.CntlBefore == 0x00A00709u);
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    /* o APPLY de fato mexeu na placa, nos dois registradores */
    CHECK(FakeRead32(NULL, BC250_GFX_REG_CP_RB0_BASE, &probe) == 1u);
    CHECK(probe == (uint32_t)(plan.RingGpuVa >> 8));
    CHECK(FakeRead32(NULL, BC250_GFX_REG_CP_RB0_CNTL, &probe) == 1u);
    CHECK(probe == BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE);

    CHECK(Bc250GfxRingTxnRollback(&iface, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(ev.RollbackAttempted == 1u);
    CHECK(ev.RollbackPassed == 1u);
    /* e o estado voltou ao do snapshot, nao ao do plano */
    CHECK(FakeRead32(NULL, BC250_GFX_REG_CP_RB0_CNTL, &probe) == 1u);
    CHECK(probe == 0x00A00709u);
    CHECK(FakeRead32(NULL, BC250_GFX_REG_CP_RB0_BASE, &probe) == 1u);
    CHECK(probe == 0x00F40FE0u);

    /* ------------------------------------------------------------------
     * 6b. DESALT NA TRANSACAO: acontece DEPOIS do anel, prova por readback,
     *     e o ROLLBACK re-HALTA.
     * ------------------------------------------------------------------ */
    FakeReset();
    FakePowerOnState();
    Bc250GfxRingFillGoodPlan(&plan);
    plan.UnhaltMicroEngines = 1u;
    plan.MicroEngineFirmwareLoaded = 1u;
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(ev.MicroEnginesHalted == 1u);
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(ev.UnhaltAttempted == 1u);
    CHECK(ev.UnhaltPassed == 1u);
    CHECK(ev.MicroEnginesHalted == 0u);
    CHECK(ev.MeCntlFields.AllHalted == 0u);
    /* a placa falsa de fato saiu do estado de boot */
    CHECK(FakeRead32(NULL, BC250_GFX_REG_CP_ME_CNTL, &probe) == 1u);
    CHECK(probe == 0u);
    /* e o ROLLBACK devolve o HALT */
    CHECK(Bc250GfxRingTxnRollback(&iface, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(FakeRead32(NULL, BC250_GFX_REG_CP_ME_CNTL, &probe) == 1u);
    CHECK(probe == BC250_GFX_RING_ME_CNTL_MEASURED_BOOT);

    /*
     * CONTRAPONTO: sem pedido de desalt, o CP_ME_CNTL NAO pode ser tocado.
     * Sem este caso o APPLY poderia desaltar sempre e todos os positivos
     * acima continuariam passando.
     */
    FakeReset();
    FakePowerOnState();
    Bc250GfxRingFillGoodPlan(&plan);
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(ev.UnhaltAttempted == 0u);
    CHECK(ev.UnhaltPassed == 0u);
    CHECK(FakeRead32(NULL, BC250_GFX_REG_CP_ME_CNTL, &probe) == 1u);
    CHECK(probe == BC250_GFX_RING_ME_CNTL_MEASURED_BOOT);

    /*
     * SILICIO QUE ACEITA A ESCRITA E MANTEM O HALT: o readback bateria com o
     * escrito, mas a engine continua parada. A prova tem de ser "o HALT saiu",
     * nao "o valor voltou".
     */
    FakeReset();
    FakePowerOnState();
    Bc250GfxRingFillGoodPlan(&plan);
    plan.UnhaltMicroEngines = 1u;
    plan.MicroEngineFirmwareLoaded = 1u;
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    g_fakeIgnoreWriteAt = BC250_GFX_REG_CP_ME_CNTL;
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_READBACK_MISMATCH);
    CHECK(ev.UnhaltAttempted == 1u);
    CHECK(ev.UnhaltPassed == 0u);

    /* Rollback sem snapshot e' recusado. */
    memset(&ev, 0, sizeof(ev));
    CHECK(Bc250GfxRingTxnRollback(&iface, &ev) == BC250_GFX_RING_TXN_BAD_ARGUMENT);

    /* Rollback cuja restauracao nao cola REPROVA em vez de declarar sucesso. */
    FakeReset();
    FakePowerOnState();
    FakeSlot(BC250_GFX_REG_CP_RB0_CNTL)->Value = 0x00A00709u;
    Bc250GfxRingFillGoodPlan(&plan);
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    g_fakeIgnoreWriteAt = BC250_GFX_REG_CP_RB0_CNTL;
    CHECK(Bc250GfxRingTxnRollback(&iface, &ev) == BC250_GFX_RING_TXN_ROLLBACK_FAILED);
    CHECK(ev.RollbackPassed == 0u);

    /* Interface sem identidade e' recusada nos tres estagios. */
    FakeReset();
    FakePowerOnState();
    Bc250GfxRingFillGoodPlan(&plan);
    iface.Identity = 0u;
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_BAD_ARGUMENT);
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_BAD_ARGUMENT);
    CHECK(Bc250GfxRingTxnRollback(&iface, &ev) == BC250_GFX_RING_TXN_BAD_ARGUMENT);

    CHECK(Bc250GfxRingTxnStatusName(BC250_GFX_RING_TXN_NOT_STARTABLE)[0] == 'N');

    /*
     * SILICIO QUE ACEITA A ESCRITA E NAO LIGA: NOT_STARTABLE agora significa
     * "escrevi 1 em CP_RB0_ACTIVE e o readback voltou 0". Sem este caso o
     * APPLY poderia declarar OK sem nunca conferir a partida -- que e' o falso
     * sucesso que a Lei 4 proibe.
     */
    FakeIface(&iface); /* o bloco acima zerou a identidade de proposito */
    FakeReset();
    FakePowerOnState();
    Bc250GfxRingFillGoodPlan(&plan);
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    g_fakeIgnoreWriteAt = BC250_GFX_REG_CP_RB0_ACTIVE;
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_NOT_STARTABLE);
    CHECK(ev.RingActivateAttempted == 1u);
    CHECK(ev.RingActivatePassed == 0u);
    /* o anel ficou configurado, mas a partida NAO e' declarada */
    CHECK(ev.ApplyReadbackPassed == 1u);
    return 1;
}

static int Bc250RunGfxRingSuite(void)
{
    if (!Bc250RunGfxRingContractTest()) return 0;
    if (!Bc250RunGfxRingTransactionTest()) return 0;
    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunGfxRingSuite() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunGfxRingSuite() ? 0 : 1;
}
#endif
