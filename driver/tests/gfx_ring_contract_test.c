#include "../core/bc250_gfx_ring_contract.h"
#include "../core/bc250_gfx_ring_transaction.h"

#include <string.h>

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);
int _fltused = 0x9875;

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) ExitProcess((unsigned long)__LINE__); } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) return __LINE__; } while (0)
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

#define FAKE_REG_SLOTS 48u

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
static uint32_t g_fakeAutoRetireGpu;
static uint32_t *g_fakeFenceAddress;
static uint32_t g_fakeFenceRetireValue;
static uint32_t *g_fakeRptrWbAddress;

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
    g_fakeAutoRetireGpu = 0u;
    g_fakeFenceAddress = NULL;
    g_fakeFenceRetireValue = 0u;
    g_fakeRptrWbAddress = NULL;
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

    if (g_fakeAutoRetireGpu && Offset == BC250_GFX_REG_CP_RB0_WPTR) {
        FAKE_REG *rptrSlot = FakeSlot(BC250_GFX_REG_CP_RB0_RPTR);
        if (rptrSlot != NULL) {
            rptrSlot->Value = Value;
        }
        if (g_fakeRptrWbAddress != NULL) {
            *g_fakeRptrWbAddress = Value;
        }
        if (g_fakeFenceAddress != NULL) {
            *g_fakeFenceAddress = g_fakeFenceRetireValue;
        }
    }

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

static int Bc250RunGfxRingSubmitShRegTest(void)
{
    BC250_GFX_RING_INTERFACE iface;
    BC250_GFX_RING_PLAN plan;
    BC250_GFX_RING_TXN_EVIDENCE ev;
    BC250_GFX_RING_SUBMIT_SH_REG_PLAN subPlan;
    BC250_GFX_RING_SUBMIT_SH_REG_EVIDENCE subEv;
    BC250_GFX_SHADER_REGS vsRegs;
    BC250_GFX_SHADER_REGS psRegs;
    uint32_t ringMem[64];
    uint32_t fenceMem = 0u;
    uint32_t rptrWbMem = 0u;
    uint32_t wptrPollMem = 0u;
    uint32_t i;

    for (i = 0; i < 64u; ++i) ringMem[i] = 0xCCCCCCCCu;
    FakeIface(&iface);

    vsRegs.PgmLo = 0x0000A8C0u;
    vsRegs.PgmHi = 0u;
    vsRegs.Rsrc1 = 0x00AF0000u;
    vsRegs.Rsrc2 = 0u;

    psRegs.PgmLo = 0x0000A8C2u;
    psRegs.PgmHi = 0u;
    psRegs.Rsrc1 = 0x02AF0000u;
    psRegs.Rsrc2 = 0u;

    memset(&subPlan, 0, sizeof(subPlan));
    subPlan.RingCpuAddress = ringMem;
    subPlan.RingDwords = 64u;
    subPlan.WptrPollCpuAddress = &wptrPollMem;
    subPlan.RptrWbCpuAddress = &rptrWbMem;
    subPlan.FenceCpuAddress = &fenceMem;
    subPlan.FenceGpuVa = TEST_WINDOW_BASE + 0x30000ull;
    subPlan.FencePoisonValue = 0x43DAFFFEu;
    subPlan.FenceExpectedValue = 0x0000B240u;
    subPlan.VsRegs = &vsRegs;
    subPlan.PsRegs = &psRegs;
    subPlan.MaxPollIterations = 10u;
    subPlan.StallMicroseconds = 0u;

    /* 1. Recusa de submissao quando o anel nao esta ativo (Apply nao rodou) */
    memset(&ev, 0, sizeof(ev));
    CHECK(Bc250GfxRingTxnSubmitShReg(&iface, &plan, &ev, &subPlan, &subEv) ==
          BC250_GFX_RING_TXN_NOT_ACTIVE);

    /* 2. Ciclo completo feliz: Preflight -> Apply -> SubmitShReg (Tripod PROVEN) -> Rollback (Lei 5) */
    FakeReset();
    FakePowerOnState();
    Bc250GfxRingFillGoodPlan(&plan);
    plan.UnhaltMicroEngines = 1u;
    plan.MicroEngineFirmwareLoaded = 1u;
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(ev.RingActivatePassed == 1u);

    /* Configura mock GPU para executar quando WPTR avancar */
    g_fakeAutoRetireGpu = 1u;
    g_fakeFenceAddress = &fenceMem;
    g_fakeFenceRetireValue = 0x0000B240u;
    g_fakeRptrWbAddress = &rptrWbMem;

    CHECK(Bc250GfxRingTxnSubmitShReg(&iface, &plan, &ev, &subPlan, &subEv) ==
          BC250_GFX_RING_TXN_OK);
    CHECK(subEv.SubmitCompleted == 1u);
    CHECK(subEv.TripodVerdict == BC250_TRIPOD_PROVEN);
    CHECK(subEv.FencePoisonPlanted == 0x43DAFFFEu);
    CHECK(subEv.FencePoisonReadback == 0x43DAFFFEu);
    CHECK(subEv.FenceObserved == 0x0000B240u);
    CHECK(subEv.EmittedDwords == 20u); /* 12 dw shader + 5 dw fence + 3 dw NOP */
    CHECK(wptrPollMem == 20u);
    CHECK(ringMem[12] == 0xC0033700u); /* PACKET3 WRITE_DATA count 3 */
    CHECK(ringMem[13] == 0x00100500u); /* WR_CONFIRM=1, DST_SEL=5 Memory Direct MC */
    CHECK(ringMem[16] == 0x0000B240u); /* FenceExpectedValue */
    CHECK(ringMem[17] == 0x80000000u); /* PACKET2 NOP */
    CHECK(ringMem[18] == 0x80000000u); /* PACKET2 NOP */
    CHECK(ringMem[19] == 0x80000000u); /* PACKET2 NOP */

    /* Rollback incondicional apos a submissao restaura o estado de boot (Lei 5) */
    CHECK(Bc250GfxRingTxnRollback(&iface, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(ev.RollbackPassed == 1u);

    /* 3. Caso Negativo: Timeout quando a GPU nao avanca RPTR nem grava fence */
    FakeReset();
    FakePowerOnState();
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    g_fakeAutoRetireGpu = 0u; /* GPU congelada: nao consome pacote */

    CHECK(Bc250GfxRingTxnSubmitShReg(&iface, &plan, &ev, &subPlan, &subEv) ==
          BC250_GFX_RING_TXN_SUBMIT_TIMEOUT);
    CHECK(subEv.SubmitCompleted == 0u);
    CHECK(subEv.TripodVerdict == BC250_TRIPOD_RPTR_NOT_RETIRED);
    CHECK(subEv.FenceObserved == 0x43DAFFFEu); /* poison segue intacto */

    /* Rollback restaura o estado limpo mesmo apos timeout */
    CHECK(Bc250GfxRingTxnRollback(&iface, &ev) == BC250_GFX_RING_TXN_OK);

    return 1;
}

static int Bc250RunGfxRingSubmit3DDrawTest(void)
{
    BC250_GFX_RING_INTERFACE iface;
    BC250_GFX_RING_PLAN plan;
    BC250_GFX_RING_TXN_EVIDENCE ev;
    BC250_GFX_RING_SUBMIT_3D_DRAW_PLAN subPlan;
    BC250_GFX_RING_SUBMIT_3D_DRAW_EVIDENCE subEv;
    BC250_GFX_SHADER_REGS vsRegs, psRegs;
    static uint32_t ringMem[256];
    uint32_t wptrPollMem = 0u;
    uint32_t rptrWbMem = 0u;
    uint32_t fenceMem = 0u;
    uint32_t i;

    memset(&iface, 0, sizeof(iface));
    iface.Read32 = FakeRead32;
    iface.Write32 = FakeWrite32;
    iface.Context = NULL;
    iface.Identity = BC250_GFX_RING_TXN_IDENTITY;

    memset(ringMem, 0, sizeof(ringMem));
    memset(&vsRegs, 0, sizeof(vsRegs));
    memset(&psRegs, 0, sizeof(psRegs));
    vsRegs.PgmLo = 0x00010000u;
    vsRegs.PgmHi = 0x00000000u;
    vsRegs.Rsrc1 = 0x00AF0000u;
    vsRegs.Rsrc2 = 0u;

    psRegs.PgmLo = 0x00020000u;
    psRegs.PgmHi = 0x00000000u;
    psRegs.Rsrc1 = 0x02AF0000u;
    psRegs.Rsrc2 = 0u;

    memset(&subPlan, 0, sizeof(subPlan));
    subPlan.RingCpuAddress = ringMem;
    subPlan.RingDwords = 256u;
    subPlan.WptrPollCpuAddress = &wptrPollMem;
    subPlan.RptrWbCpuAddress = &rptrWbMem;
    subPlan.FenceCpuAddress = &fenceMem;
    subPlan.FenceGpuVa = TEST_WINDOW_BASE + 0x30000ull;
    subPlan.FencePoisonValue = 0x43DAFFFEu;
    subPlan.FenceExpectedValue = 0x0000B400u;
    subPlan.VsRegs = &vsRegs;
    subPlan.PsRegs = &psRegs;
    subPlan.ColorTargetBaseGpuVa = TEST_WINDOW_BASE + 0x40000ull;
    subPlan.ColorTargetWidth = 256u;
    subPlan.ColorTargetHeight = 256u;
    subPlan.ColorTargetPitchBytes = 1024u;
    subPlan.ColorTargetFormatIndex = 8u;
    subPlan.ColorTargetCompSwap = 1u;
    subPlan.Viewport.X = 0.0f;
    subPlan.Viewport.Y = 0.0f;
    subPlan.Viewport.Width = 256.0f;
    subPlan.Viewport.Height = 256.0f;
    subPlan.Viewport.MinDepth = 0.0f;
    subPlan.Viewport.MaxDepth = 1.0f;
    subPlan.Scissor.Left = 0u;
    subPlan.Scissor.Top = 0u;
    subPlan.Scissor.Right = 256u;
    subPlan.Scissor.Bottom = 256u;
    subPlan.PrimitiveTopology = BC250_GFX_PRIM_TRILIST;
    subPlan.NumInstances = 1u;
    subPlan.VertexCount = 3u;
    subPlan.DrawInitiator = BC250_GFX_DRAW_INITIATOR_AUTO_INDEX;
    subPlan.MaxPollIterations = 10u;
    subPlan.StallMicroseconds = 0u;

    /* 1. Recusa de submissao quando o anel nao esta ativo (Apply nao rodou) */
    memset(&ev, 0, sizeof(ev));
    CHECK(Bc250GfxRingTxnSubmit3DDraw(&iface, &plan, &ev, &subPlan, &subEv) ==
          BC250_GFX_RING_TXN_NOT_ACTIVE);

    /* 2. Recusa de argumentos invalidos */
    FakeReset();
    FakePowerOnState();
    Bc250GfxRingFillGoodPlan(&plan);
    plan.UnhaltMicroEngines = 1u;
    plan.MicroEngineFirmwareLoaded = 1u;
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);

    {
        BC250_GFX_RING_SUBMIT_3D_DRAW_PLAN badPlan = subPlan;
        badPlan.ColorTargetBaseGpuVa = 0x12345ull; /* Not 256-byte aligned */
        CHECK(Bc250GfxRingTxnSubmit3DDraw(&iface, &plan, &ev, &badPlan, &subEv) ==
              BC250_GFX_RING_TXN_BAD_ARGUMENT);

        badPlan = subPlan;
        badPlan.VertexCount = 0u;
        CHECK(Bc250GfxRingTxnSubmit3DDraw(&iface, &plan, &ev, &badPlan, &subEv) ==
              BC250_GFX_RING_TXN_BAD_ARGUMENT);

        badPlan = subPlan;
        badPlan.ColorTargetWidth = 0u;
        CHECK(Bc250GfxRingTxnSubmit3DDraw(&iface, &plan, &ev, &badPlan, &subEv) ==
              BC250_GFX_RING_TXN_BAD_ARGUMENT);

        badPlan = subPlan;
        badPlan.RingDwords = 32u;
        CHECK(Bc250GfxRingTxnSubmit3DDraw(&iface, &plan, &ev, &badPlan, &subEv) ==
              BC250_GFX_RING_TXN_BAD_ARGUMENT);
    }

    /* 3. Ciclo completo feliz: Preflight -> Apply -> Submit3DDraw (Tripod PROVEN) -> Rollback (Lei 5) */
    g_fakeAutoRetireGpu = 1u;
    g_fakeFenceAddress = &fenceMem;
    g_fakeFenceRetireValue = 0x0000B400u;
    g_fakeRptrWbAddress = &rptrWbMem;
    FakeSlot(BC250_GFX_REG_GRBM_STATUS)->Value = 0x80200000u;
    FakeSlot(BC250_GFX_REG_CP_STAT)->Value = 0x80020000u;
    FakeSlot(BC250_GFX_REG_CP_PFP_INSTR_PNTR)->Value = 0x0123u;
    FakeSlot(BC250_GFX_REG_CP_ME_INSTR_PNTR)->Value = 0x0456u;

    CHECK(Bc250GfxRingTxnSubmit3DDraw(&iface, &plan, &ev, &subPlan, &subEv) ==
          BC250_GFX_RING_TXN_OK);
    CHECK(subEv.SubmitCompleted == 1u);
    CHECK(subEv.TripodVerdict == BC250_TRIPOD_PROVEN);
    CHECK(subEv.FencePoisonPlanted == 0x43DAFFFEu);
    CHECK(subEv.FencePoisonReadback == 0x43DAFFFEu);
    CHECK(subEv.FenceObserved == 0x0000B400u);
    CHECK(subEv.EmittedDwords == 64u);
    CHECK(subEv.GrbmStatus == 0x80200000u);
    CHECK(subEv.CpStat == 0x80020000u);
    CHECK(subEv.PfpPntr == 0x0123u);
    CHECK(subEv.MePntr == 0x0456u);
    CHECK(wptrPollMem == 64u);

    /* Verificacao de blocos estruturais no anel */
    CHECK(ringMem[0] == 0xC0047600u);
    CHECK(ringMem[6] == 0xC0047600u);
    CHECK(ringMem[12] == 0xC0066900u);
    CHECK(ringMem[13] == 0x00000318u);
    CHECK(ringMem[20] == 0xC0016900u);
    CHECK(ringMem[21] == 0x0000008Eu);
    CHECK(ringMem[22] == 0x0000000Fu);
    CHECK(ringMem[23] == 0x80000000u);
    CHECK(ringMem[24] == 0xC0066900u);
    CHECK(ringMem[25] == 0x0000010Fu);
    CHECK(ringMem[44] == 0xC0017900u);
    CHECK(ringMem[45] == 0x00000242u);
    CHECK(ringMem[46] == 0x00000004u);
    CHECK(ringMem[52] == 0xC0012D00u);
    CHECK(ringMem[53] == 3u);
    CHECK(ringMem[54] == 2u);
    CHECK(ringMem[55] == 0x80000000u);
    CHECK(ringMem[56] == 0xC0033700u);
    CHECK(ringMem[57] == 0x00100500u);
    CHECK(ringMem[58] == (uint32_t)(subPlan.FenceGpuVa & 0xFFFFFFFFull));
    CHECK(ringMem[59] == (uint32_t)(subPlan.FenceGpuVa >> 32));
    CHECK(ringMem[60] == 0x0000B400u);
    CHECK(ringMem[61] == 0x80000000u);
    CHECK(ringMem[62] == 0x80000000u);
    CHECK(ringMem[63] == 0x80000000u);

    /* Verificacao formal atraves de Bc250GfxVerify3DDrawStream */
    CHECK(Bc250GfxVerify3DDrawStream(ringMem, 256u, 0u, &subPlan) == BC250_GFX_RING_TXN_OK);

    /* 4. Deteccao de corrupcao no stream via Bc250GfxVerify3DDrawStream */
    {
        uint32_t tampered[64];
        for (i = 0; i < 64u; ++i) tampered[i] = ringMem[i];
        tampered[22] = 0x00000000u;
        CHECK(Bc250GfxVerify3DDrawStream(tampered, 64u, 0u, &subPlan) == BC250_GFX_RING_TXN_READBACK_MISMATCH);

        for (i = 0; i < 64u; ++i) tampered[i] = ringMem[i];
        tampered[46] = 0x00000001u;
        CHECK(Bc250GfxVerify3DDrawStream(tampered, 64u, 0u, &subPlan) == BC250_GFX_RING_TXN_READBACK_MISMATCH);

        for (i = 0; i < 64u; ++i) tampered[i] = ringMem[i];
        tampered[53] = 4u;
        CHECK(Bc250GfxVerify3DDrawStream(tampered, 64u, 0u, &subPlan) == BC250_GFX_RING_TXN_READBACK_MISMATCH);

        for (i = 0; i < 64u; ++i) tampered[i] = ringMem[i];
        tampered[56] = 0x00000000u;
        CHECK(Bc250GfxVerify3DDrawStream(tampered, 64u, 0u, &subPlan) == BC250_GFX_RING_TXN_READBACK_MISMATCH);

        for (i = 0; i < 64u; ++i) tampered[i] = ringMem[i];
        tampered[61] = 0x12345678u;
        CHECK(Bc250GfxVerify3DDrawStream(tampered, 64u, 0u, &subPlan) == BC250_GFX_RING_TXN_READBACK_MISMATCH);
    }

    /* Rollback incondicional apos a submissao restaura o estado de boot (Lei 5) */
    CHECK(Bc250GfxRingTxnRollback(&iface, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(ev.RollbackPassed == 1u);

    /* 5. Caso Negativo: Timeout quando a GPU nao avanca RPTR */
    FakeReset();
    FakePowerOnState();
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    g_fakeAutoRetireGpu = 0u;
    rptrWbMem = 0u;

    FakeSlot(BC250_GFX_REG_GRBM_STATUS)->Value = 0x80600000u;
    FakeSlot(BC250_GFX_REG_CP_STAT)->Value = 0x80030000u;
    FakeSlot(BC250_GFX_REG_CP_PFP_INSTR_PNTR)->Value = 0x0040u;
    FakeSlot(BC250_GFX_REG_CP_ME_INSTR_PNTR)->Value = 0x002Du;

    CHECK(Bc250GfxRingTxnSubmit3DDraw(&iface, &plan, &ev, &subPlan, &subEv) ==
          BC250_GFX_RING_TXN_SUBMIT_TIMEOUT);
    CHECK(subEv.SubmitCompleted == 0u);
    CHECK(subEv.TripodVerdict == BC250_TRIPOD_RPTR_NOT_RETIRED);
    CHECK(subEv.FenceObserved == 0x43DAFFFEu);
    CHECK(subEv.GrbmStatus == 0x80600000u);
    CHECK(subEv.CpStat == 0x80030000u);
    CHECK(subEv.PfpPntr == 0x0040u);
    CHECK(subEv.MePntr == 0x002Du);

    CHECK(Bc250GfxRingTxnRollback(&iface, &ev) == BC250_GFX_RING_TXN_OK);

    /* 6. Teste de Isolamento de Estado PM4 (SkipDraw = 1) */
    FakeReset();
    FakePowerOnState();
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    g_fakeAutoRetireGpu = 1u;
    g_fakeFenceAddress = &fenceMem;
    g_fakeFenceRetireValue = 0x0000B400u;
    g_fakeRptrWbAddress = &rptrWbMem;

    {
        BC250_GFX_RING_SUBMIT_3D_DRAW_PLAN skipPlan = subPlan;
        skipPlan.SkipDraw = 1u;

        CHECK(Bc250GfxRingTxnSubmit3DDraw(&iface, &plan, &ev, &skipPlan, &subEv) ==
              BC250_GFX_RING_TXN_OK);
        CHECK(subEv.SubmitCompleted == 1u);
        CHECK(subEv.TripodVerdict == BC250_TRIPOD_PROVEN);
        CHECK(subEv.FenceObserved == 0x0000B400u);

        /* Bloco 6 deve conter exatamente 4 NOPs (Packet-2) */
        CHECK(ringMem[52] == BC250_PM4_PACKET2_NOP);
        CHECK(ringMem[53] == BC250_PM4_PACKET2_NOP);
        CHECK(ringMem[54] == BC250_PM4_PACKET2_NOP);
        CHECK(ringMem[55] == BC250_PM4_PACKET2_NOP);

        /* Verificacao de stream sob SkipDraw */
        CHECK(Bc250GfxVerify3DDrawStream(ringMem, 256u, 0u, &skipPlan) == BC250_GFX_RING_TXN_OK);

        /* Mismatch se algum NOP for adulterado */
        {
            uint32_t tampered[64];
            for (i = 0; i < 64u; ++i) tampered[i] = ringMem[i];
            tampered[54] = 0x12345678u;
            CHECK(Bc250GfxVerify3DDrawStream(tampered, 64u, 0u, &skipPlan) == BC250_GFX_RING_TXN_READBACK_MISMATCH);
        }
    }

    /* 7. Teste de Isolamento Sub-etapa 4.1 (Stage 1: Shaders + ColorTarget 0 + TargetMask + Fence) */
    FakeReset();
    FakePowerOnState();
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    memset(ringMem, 0, sizeof(ringMem));
    g_fakeAutoRetireGpu = 1u;
    g_fakeFenceAddress = &fenceMem;
    g_fakeRptrWbAddress = &rptrWbMem;

    {
        BC250_GFX_RING_SUBMIT_3D_DRAW_PLAN stage1Plan = subPlan;
        stage1Plan.IsolationStage = BC250_GFX_ISOLATION_STAGE_4_1_RT;
        stage1Plan.FenceExpectedValue = 0x0000B410u;
        g_fakeFenceRetireValue = 0x0000B410u;

        CHECK(Bc250GfxRingTxnSubmit3DDraw(&iface, &plan, &ev, &stage1Plan, &subEv) ==
              BC250_GFX_RING_TXN_OK);
        CHECK(subEv.SubmitCompleted == 1u);
        CHECK(subEv.TripodVerdict == BC250_TRIPOD_PROVEN);
        CHECK(subEv.FenceObserved == 0x0000B410u);

        /* Blocos 1..3 normais */
        CHECK(ringMem[0] == 0xC0047600u);
        CHECK(ringMem[12] == 0xC0066900u);
        CHECK(ringMem[20] == 0xC0016900u);

        /* Bloco 4 (Viewport: 20 DW), Bloco 5 (Prim: 8 DW), Bloco 6 (Draw: 4 DW) todos em NOP */
        for (i = 24u; i < 56u; ++i) {
            CHECK(ringMem[i] == BC250_PM4_PACKET2_NOP);
        }

        /* Bloco 7: Fence normal */
        CHECK(ringMem[56] == 0xC0033700u);
        CHECK(ringMem[60] == 0x0000B410u);
        CHECK(ringMem[61] == 0x80000000u);

        CHECK(Bc250GfxVerify3DDrawStream(ringMem, 256u, 0u, &stage1Plan) == BC250_GFX_RING_TXN_OK);

        {
            uint32_t tampered[64];
            for (i = 0; i < 64u; ++i) tampered[i] = ringMem[i];
            tampered[30] = 0x12345678u;
            CHECK(Bc250GfxVerify3DDrawStream(tampered, 64u, 0u, &stage1Plan) == BC250_GFX_RING_TXN_READBACK_MISMATCH);
        }
    }

    /* 8. Teste de Isolamento Sub-etapa 4.2 (Stage 2: + Viewport/Scissor + Fence) */
    FakeReset();
    FakePowerOnState();
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    memset(ringMem, 0, sizeof(ringMem));
    g_fakeAutoRetireGpu = 1u;
    g_fakeFenceAddress = &fenceMem;
    g_fakeRptrWbAddress = &rptrWbMem;

    {
        BC250_GFX_RING_SUBMIT_3D_DRAW_PLAN stage2Plan = subPlan;
        stage2Plan.IsolationStage = BC250_GFX_ISOLATION_STAGE_4_2_VPORT;
        stage2Plan.FenceExpectedValue = 0x0000B420u;
        g_fakeFenceRetireValue = 0x0000B420u;

        CHECK(Bc250GfxRingTxnSubmit3DDraw(&iface, &plan, &ev, &stage2Plan, &subEv) ==
              BC250_GFX_RING_TXN_OK);
        CHECK(subEv.SubmitCompleted == 1u);
        CHECK(subEv.TripodVerdict == BC250_TRIPOD_PROVEN);
        CHECK(subEv.FenceObserved == 0x0000B420u);

        /* Bloco 4 emitido */
        CHECK(ringMem[24] == 0xC0066900u);
        CHECK(ringMem[25] == 0x0000010Fu);

        /* Bloco 5 (Prim: 8 DW) e Bloco 6 (Draw: 4 DW) em NOP */
        for (i = 44u; i < 56u; ++i) {
            CHECK(ringMem[i] == BC250_PM4_PACKET2_NOP);
        }

        CHECK(ringMem[56] == 0xC0033700u);
        CHECK(ringMem[60] == 0x0000B420u);
        CHECK(ringMem[61] == 0x80000000u);

        CHECK(Bc250GfxVerify3DDrawStream(ringMem, 256u, 0u, &stage2Plan) == BC250_GFX_RING_TXN_OK);

        {
            uint32_t tampered[64];
            for (i = 0; i < 64u; ++i) tampered[i] = ringMem[i];
            tampered[48] = 0x12345678u;
            CHECK(Bc250GfxVerify3DDrawStream(tampered, 64u, 0u, &stage2Plan) == BC250_GFX_RING_TXN_READBACK_MISMATCH);
        }
    }

    /* 9. Teste de Isolamento Sub-etapa 4.0 (Stage 4.0: Shaders Only, No Context, 44 NOPs + Fence) */
    FakeReset();
    FakePowerOnState();
    CHECK(Bc250GfxRingTxnPreflight(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    CHECK(Bc250GfxRingTxnApply(&iface, &plan, &ev) == BC250_GFX_RING_TXN_OK);
    memset(ringMem, 0, sizeof(ringMem));
    g_fakeAutoRetireGpu = 1u;
    g_fakeFenceAddress = &fenceMem;
    g_fakeRptrWbAddress = &rptrWbMem;

    {
        BC250_GFX_RING_SUBMIT_3D_DRAW_PLAN stage0Plan = subPlan;
        stage0Plan.IsolationStage = BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT;
        stage0Plan.FenceExpectedValue = 0x0000B400u;
        g_fakeFenceRetireValue = 0x0000B400u;

        CHECK(Bc250GfxRingTxnSubmit3DDraw(&iface, &plan, &ev, &stage0Plan, &subEv) ==
              BC250_GFX_RING_TXN_OK);
        CHECK(subEv.SubmitCompleted == 1u);
        CHECK(subEv.TripodVerdict == BC250_TRIPOD_PROVEN);
        CHECK(subEv.FenceObserved == 0x0000B400u);

        /* Bloco 1 normal (Shaders 12 DW) */
        CHECK(ringMem[0] == 0xC0047600u);

        /* Blocos 2..6 (Indices 12..55: 44 DW) todos em NOP */
        for (i = 12u; i < 56u; ++i) {
            CHECK(ringMem[i] == BC250_PM4_PACKET2_NOP);
        }

        /* Bloco 7: Fence normal */
        CHECK(ringMem[56] == 0xC0033700u);
        CHECK(ringMem[60] == 0x0000B400u);
        CHECK(ringMem[61] == 0x80000000u);

        CHECK(Bc250GfxVerify3DDrawStream(ringMem, 256u, 0u, &stage0Plan) == BC250_GFX_RING_TXN_OK);

        {
            uint32_t tampered[64];
            for (i = 0; i < 64u; ++i) tampered[i] = ringMem[i];
            tampered[15] = 0x12345678u;
            CHECK(Bc250GfxVerify3DDrawStream(tampered, 64u, 0u, &stage0Plan) == BC250_GFX_RING_TXN_READBACK_MISMATCH);
        }
    }

    CHECK(Bc250GfxRingTxnRollback(&iface, &ev) == BC250_GFX_RING_TXN_OK);

    return 1;
}


/*
 * =========================================================================
 * SONDA DE ONDA -- suite offline, contra um SQ falso.
 * =========================================================================
 * Esta suite existe porque a sonda de onda e' a UNICA parte da faixa de 19/09
 * que ESCREVE registrador, e a escrita em questao (GRBM_GFX_INDEX fora de
 * broadcast) redireciona todo acesso posterior a um unico shader engine --
 * inclusive o soft-reset que o quiesce dispara logo depois. Por isso o caso
 * do seletor preso vem antes de qualquer caso feliz.
 */

#define WAVE_FAKE_MAX 64u

typedef struct WaveFakeSq {
    uint32_t GfxIndex;          /* ultimo valor escrito em GRBM_GFX_INDEX */
    uint32_t IndIndex;          /* ultimo valor escrito em SQ_IND_INDEX */
    uint32_t GfxIndexWrites;
    uint32_t RefuseSelectorRestore;  /* 1 = recusa a volta a broadcast */
    uint32_t RefuseIndData;          /* 1 = recusa leitura de SQ_IND_DATA */
    uint32_t LiveSlotMask;      /* bit i = slot i tem onda viva */
    uint32_t SlotCursor;        /* quantas leituras de STATUS ja aconteceram */
    uint32_t IndDataReads;
} WaveFakeSq;

static uint32_t WaveFakeRead32(void *Context, uint32_t Offset, uint32_t *Value)
{
    WaveFakeSq *sq = (WaveFakeSq *)Context;
    if (Value == NULL) return 0u;
    *Value = 0u;
    if (Offset == BC250_GFX_REG_GRBM_GFX_INDEX) {
        *Value = sq->GfxIndex;
        return 1u;
    }
    if (Offset == BC250_GFX_REG_SQ_IND_DATA) {
        uint32_t ix = (sq->IndIndex >> 16) & 0xFFFFu;
        if (sq->RefuseIndData != 0u) return 0u;
        sq->IndDataReads++;
        if (ix == BC250_IX_SQ_WAVE_STATUS) {
            uint32_t live = (sq->LiveSlotMask >> (sq->SlotCursor & 31u)) & 1u;
            sq->SlotCursor++;
            *Value = live ? 0x00001234u : 0u;   /* 0 = slot vazio */
            return 1u;
        }
        if (ix == BC250_IX_SQ_WAVE_PC_LO)   { *Value = 0x0000A100u; return 1u; }
        if (ix == BC250_IX_SQ_WAVE_PC_HI)   { *Value = 0x00000002u; return 1u; }
        if (ix == BC250_IX_SQ_WAVE_IB_STS)  { *Value = 0x00000041u; return 1u; }
        if (ix == BC250_IX_SQ_WAVE_INST_DW0){ *Value = 0xBF8C0F70u; return 1u; }
        if (ix == BC250_IX_SQ_WAVE_TRAPSTS) { *Value = 0u; return 1u; }
        return 1u;
    }
    return 0u;
}

static uint32_t WaveFakeWrite32(void *Context, uint32_t Offset, uint32_t Value)
{
    WaveFakeSq *sq = (WaveFakeSq *)Context;
    if (Offset == BC250_GFX_REG_GRBM_GFX_INDEX) {
        if (sq->RefuseSelectorRestore != 0u &&
            Value == BC250_GRBM_GFX_INDEX_BROADCAST_ALL) {
            return 0u;
        }
        sq->GfxIndex = Value;
        sq->GfxIndexWrites++;
        return 1u;
    }
    if (Offset == BC250_GFX_REG_SQ_IND_INDEX) {
        sq->IndIndex = Value;
        return 1u;
    }
    return 0u;
}

static void WaveFakeIface(BC250_GFX_RING_INTERFACE *Iface, WaveFakeSq *Sq)
{
    Iface->Read32 = WaveFakeRead32;
    Iface->Write32 = WaveFakeWrite32;
    Iface->Stall = NULL;
    Iface->Context = Sq;
    Iface->Identity = BC250_GFX_RING_TXN_IDENTITY;
}

static void WavePlanFill(BC250_WAVE_PROBE_PLAN *Plan, uint32_t slots)
{
    uint32_t i;
    Plan->SlotCount = slots;
    Plan->WavesInFlight = 6u;   /* o numero MEDIDO na Corrida 35 */
    Plan->ReadBudget = BC250_WAVE_PROBE_MAX_READS;
    for (i = 0u; i < slots; ++i) {
        Plan->Slots[i].SeIndex = i & 1u;
        Plan->Slots[i].SaIndex = (i >> 1) & 1u;
        Plan->Slots[i].InstanceIndex = i;
        Plan->Slots[i].WaveId = i & 0x1Fu;
    }
}

static int Bc250RunWaveProbeTest(void)
{
    BC250_GFX_RING_INTERFACE iface;
    BC250_WAVE_PROBE_PLAN plan;
    BC250_WAVE_PROBE_EVIDENCE ev;
    WaveFakeSq sq;

    /* [TEST] SONDA DE ONDA: codificacao dos seletores... */
    /* SQ_IND_INDEX: INDEX em 31:16, WAVE_ID em 4:0, resto ZERO. */
    CHECK(Bc250WaveProbeEncodeIndIndex(BC250_IX_SQ_WAVE_PC_LO, 3u) == 0x01080003u);
    CHECK(Bc250WaveProbeEncodeIndIndex(BC250_IX_SQ_WAVE_STATUS, 31u) == 0x0102001Fu);
    /* WAVE_ID satura em 5 bits: 32 nao pode virar bit de WORKITEM_ID. */
    CHECK(Bc250WaveProbeEncodeIndIndex(BC250_IX_SQ_WAVE_STATUS, 32u) == 0x01020000u);
    /* GRBM_GFX_INDEX: os TRES bits de broadcast tem de sair ZERADOS, senao a
     * selecao nao vale e a leitura devolve agregado. */
    CHECK(Bc250WaveProbeEncodeGfxIndex(1u, 1u, 5u) == 0x00010105u);
    CHECK((Bc250WaveProbeEncodeGfxIndex(1u, 1u, 5u) & 0xE0000000u) == 0u);

    /* [TEST] SONDA DE ONDA: status zero NAO conta como onda... */
    CHECK(Bc250WaveProbeStatusIsValid(0u) == 0u);
    CHECK(Bc250WaveProbeStatusIsValid(0xFFFFFFFFu) == 0u);
    CHECK(Bc250WaveProbeStatusIsValid(0x1234u) == 1u);

    /* [TEST] SONDA DE ONDA: sem onda em voo NAO escreve nada... */
    memset(&sq, 0, sizeof(sq));
    WaveFakeIface(&iface, &sq);
    memset(&plan, 0, sizeof(plan));
    WavePlanFill(&plan, 4u);
    plan.WavesInFlight = 0u;
    CHECK(Bc250WaveProbeRun(&iface, &plan, &ev) == BC250_WAVE_PROBE_NO_WAVES);
    CHECK(sq.GfxIndexWrites == 0u);   /* o seletor NUNCA saiu de broadcast */

    /* [TEST] SONDA DE ONDA: plano acima do orcamento e recusado... */
    memset(&sq, 0, sizeof(sq));
    WaveFakeIface(&iface, &sq);
    WavePlanFill(&plan, 8u);
    plan.ReadBudget = 4u;             /* 8 slots nao cabem em 4 leituras */
    CHECK(Bc250WaveProbeRun(&iface, &plan, &ev) == BC250_WAVE_PROBE_PLAN_REJECTED);
    CHECK(sq.GfxIndexWrites == 0u);
    WavePlanFill(&plan, 8u);
    plan.ReadBudget = BC250_WAVE_PROBE_MAX_READS + 1u;
    CHECK(Bc250WaveProbeRun(&iface, &plan, &ev) == BC250_WAVE_PROBE_PLAN_REJECTED);

    /* [TEST] SONDA DE ONDA: varredura encontra a onda e le PC e IB_STS... */
    memset(&sq, 0, sizeof(sq));
    sq.LiveSlotMask = 0x2u;           /* segundo slot tem onda */
    WaveFakeIface(&iface, &sq);
    WavePlanFill(&plan, 8u);
    CHECK(Bc250WaveProbeRun(&iface, &plan, &ev) == BC250_WAVE_PROBE_OK);
    CHECK(ev.WavesFound == 1u);
    CHECK(ev.Found[0].Status == 0x00001234u);
    CHECK(ev.Found[0].PcHi == 0x00000002u);
    CHECK(ev.Found[0].PcLo == 0x0000A100u);
    CHECK(ev.Found[0].InstDw0 == 0xBF8C0F70u);
    CHECK(ev.SelectorRestored == 1u);
    CHECK(sq.GfxIndex == BC250_GRBM_GFX_INDEX_BROADCAST_ALL);

    /* [TEST] SONDA DE ONDA: o seletor volta a broadcast MESMO sem achar onda... */
    memset(&sq, 0, sizeof(sq));
    sq.LiveSlotMask = 0u;             /* nenhum slot vivo */
    WaveFakeIface(&iface, &sq);
    WavePlanFill(&plan, 8u);
    CHECK(Bc250WaveProbeRun(&iface, &plan, &ev) == BC250_WAVE_PROBE_OK);
    CHECK(ev.WavesFound == 0u);
    CHECK(ev.SelectorRestored == 1u);
    CHECK(sq.GfxIndex == BC250_GRBM_GFX_INDEX_BROADCAST_ALL);

    /* [TEST] SONDA DE ONDA: seletor preso e o PIOR desfecho e tem de ser dito... */
    memset(&sq, 0, sizeof(sq));
    sq.LiveSlotMask = 0x1u;
    sq.RefuseSelectorRestore = 1u;
    WaveFakeIface(&iface, &sq);
    WavePlanFill(&plan, 4u);
    CHECK(Bc250WaveProbeRun(&iface, &plan, &ev) == BC250_WAVE_PROBE_SELECTOR_STUCK);
    CHECK(ev.SelectorRestored == 0u);
    CHECK(sq.GfxIndex != BC250_GRBM_GFX_INDEX_BROADCAST_ALL);

    /* [TEST] SONDA DE ONDA: leitura recusada vira INDETERMINADO, nao zero... */
    memset(&sq, 0, sizeof(sq));
    sq.RefuseIndData = 1u;
    WaveFakeIface(&iface, &sq);
    WavePlanFill(&plan, 4u);
    CHECK(Bc250WaveProbeRun(&iface, &plan, &ev) == BC250_WAVE_PROBE_OK);
    CHECK(ev.ReadRefused == 1u);
    CHECK(ev.WavesFound == 0u);
    CHECK(ev.SelectorRestored == 1u);

    /* [TEST] SONDA DE ONDA: teto de ondas reportadas e respeitado... */
    memset(&sq, 0, sizeof(sq));
    sq.LiveSlotMask = 0xFFFFFFFFu;    /* todo slot tem onda */
    WaveFakeIface(&iface, &sq);
    WavePlanFill(&plan, BC250_WAVE_PROBE_MAX_SLOTS);
    CHECK(Bc250WaveProbeRun(&iface, &plan, &ev) == BC250_WAVE_PROBE_OK);
    CHECK(ev.WavesFound == BC250_WAVE_PROBE_MAX_REPORTED);
    CHECK(ev.ReadsIssued <= plan.ReadBudget + 6u);
    CHECK(ev.SelectorRestored == 1u);

    /* SONDA DE ONDA: OK (SQ falso, selecao indireta, restauracao de broadcast) */
    return 1;
}

static int Bc250RunGfxRingSuite(void)
{
    if (!Bc250RunGfxRingContractTest()) return 0;
    if (!Bc250RunGfxRingTransactionTest()) return 0;
    if (!Bc250RunGfxRingSubmitShRegTest()) return 0;
    if (!Bc250RunGfxRingSubmit3DDrawTest()) return 0;
    if (!Bc250RunWaveProbeTest()) return 0;
    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    Bc250RunGfxRingSuite();
    ExitProcess(0ul);
}
#else
int main(void)
{
    return Bc250RunGfxRingSuite() ? 0 : 1;
}
#endif
