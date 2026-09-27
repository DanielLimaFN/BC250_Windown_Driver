#include "../core/bc250_persistent_ring_contract.h"

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

#define TEST_RING_DWORDS 128u

static uint32_t g_ring[TEST_RING_DWORDS];

#define RING_GPUVA   0x0000000000020000ull
#define FENCE_GPUVA  0x0000000000021000ull
#define CSA_GPUVA    0x0000000000021100ull
#define IB_GPUVA     0x0000000000030000ull

static BC250_PRING_STATUS Bc250TestInitCompute(BC250_PERSISTENT_RING *Ring)
{
    return Bc250PersistentRingInit(Ring,
        (uint32_t)BC250_ENGINE_TYPE_COMPUTE, 1u,
        RING_GPUVA, TEST_RING_DWORDS, FENCE_GPUVA, 0ull, 0u,
        (uint32_t)BC250_PRING_COMPLETION_POLL);
}

static BC250_PRING_STATUS Bc250TestInitSdma(BC250_PERSISTENT_RING *Ring)
{
    return Bc250PersistentRingInit(Ring,
        (uint32_t)BC250_ENGINE_TYPE_SDMA, 0u,
        RING_GPUVA, TEST_RING_DWORDS, FENCE_GPUVA, CSA_GPUVA, 0u,
        (uint32_t)BC250_PRING_COMPLETION_POLL);
}

/* Submete um lote completo (plan + emit + commit) e devolve o plano. */
static int Bc250TestSubmitOne(
    BC250_PERSISTENT_RING *Ring,
    BC250_PRING_SUBMISSION_PLAN *Plan)
{
    if (Bc250PersistentRingPlanSubmission(Ring, IB_GPUVA, 8u,
            Ring->LastSubmittedFence + 1ull, Plan) != BC250_PRING_OK) {
        return 0;
    }
    if (Bc250PersistentRingEmitSubmission(Ring, Plan,
            g_ring, TEST_RING_DWORDS) != BC250_PRING_OK) {
        return 0;
    }
    if (Bc250PersistentRingCommit(Ring, Plan) != BC250_PRING_OK) {
        return 0;
    }
    return 1;
}

static int Bc250TestInitValidation(void)
{
    BC250_PERSISTENT_RING ring;

    CHECK(Bc250PersistentRingInit(NULL,
        (uint32_t)BC250_ENGINE_TYPE_COMPUTE, 1u, RING_GPUVA,
        TEST_RING_DWORDS, FENCE_GPUVA, 0ull, 0u,
        (uint32_t)BC250_PRING_COMPLETION_POLL) == BC250_PRING_BAD_ARGUMENT);

    /* Engine invalida */
    CHECK(Bc250PersistentRingInit(&ring, 7u, 1u, RING_GPUVA,
        TEST_RING_DWORDS, FENCE_GPUVA, 0ull, 0u,
        (uint32_t)BC250_PRING_COMPLETION_POLL) == BC250_PRING_INVALID_ENGINE);

    /* Tamanho que nao e' potencia de dois */
    CHECK(Bc250PersistentRingInit(&ring,
        (uint32_t)BC250_ENGINE_TYPE_COMPUTE, 1u, RING_GPUVA,
        100u, FENCE_GPUVA, 0ull, 0u,
        (uint32_t)BC250_PRING_COMPLETION_POLL) ==
        BC250_PRING_INVALID_RING_SIZE);

    /* Tamanho abaixo do minimo */
    CHECK(Bc250PersistentRingInit(&ring,
        (uint32_t)BC250_ENGINE_TYPE_COMPUTE, 1u, RING_GPUVA,
        32u, FENCE_GPUVA, 0ull, 0u,
        (uint32_t)BC250_PRING_COMPLETION_POLL) ==
        BC250_PRING_INVALID_RING_SIZE);

    /* Anel desalinhado (256 bytes) */
    CHECK(Bc250PersistentRingInit(&ring,
        (uint32_t)BC250_ENGINE_TYPE_COMPUTE, 1u, RING_GPUVA + 16ull,
        TEST_RING_DWORDS, FENCE_GPUVA, 0ull, 0u,
        (uint32_t)BC250_PRING_COMPLETION_POLL) ==
        BC250_PRING_MISALIGNED_ADDRESS);

    /* Fence desalinhada */
    CHECK(Bc250PersistentRingInit(&ring,
        (uint32_t)BC250_ENGINE_TYPE_COMPUTE, 1u, RING_GPUVA,
        TEST_RING_DWORDS, FENCE_GPUVA + 1ull, 0ull, 0u,
        (uint32_t)BC250_PRING_COMPLETION_POLL) ==
        BC250_PRING_MISALIGNED_ADDRESS);

    /* VMID fora de faixa */
    CHECK(Bc250PersistentRingInit(&ring,
        (uint32_t)BC250_ENGINE_TYPE_COMPUTE, 1u, RING_GPUVA,
        TEST_RING_DWORDS, FENCE_GPUVA, 0ull, 16u,
        (uint32_t)BC250_PRING_COMPLETION_POLL) == BC250_PRING_BAD_ARGUMENT);

    /* Modo de conclusao invalido */
    CHECK(Bc250PersistentRingInit(&ring,
        (uint32_t)BC250_ENGINE_TYPE_COMPUTE, 1u, RING_GPUVA,
        TEST_RING_DWORDS, FENCE_GPUVA, 0ull, 0u, 9u) ==
        BC250_PRING_BAD_ARGUMENT);

    /* Anel valido: cursores zerados e ativo */
    CHECK(Bc250TestInitCompute(&ring) == BC250_PRING_OK);
    CHECK(ring.IsActive == 1u);
    CHECK(ring.WptrDwords == 0u);
    CHECK(ring.RptrShadowDwords == 0u);
    CHECK(ring.LastSubmittedFence == 0ull);
    CHECK(ring.LastRetiredFence == 0ull);

    return 1;
}

static int Bc250TestComputeSubmission(void)
{
    BC250_PERSISTENT_RING ring;
    BC250_PRING_SUBMISSION_PLAN plan;
    uint32_t available = 0u;

    memset(g_ring, 0, sizeof(g_ring));
    CHECK(Bc250TestInitCompute(&ring) == BC250_PRING_OK);

    /* Anel vazio deixa exatamente um dword de folga. */
    CHECK(Bc250PersistentRingAvailableDwords(&ring, &available) ==
        BC250_PRING_OK);
    CHECK(available == TEST_RING_DWORDS - 1u);

    /* A fence tem de ser exatamente a proxima da sequencia. */
    CHECK(Bc250PersistentRingPlanSubmission(&ring, IB_GPUVA, 8u, 2ull,
        &plan) == BC250_PRING_FENCE_REGRESSION);
    CHECK(Bc250PersistentRingPlanSubmission(&ring, IB_GPUVA, 8u, 0ull,
        &plan) == BC250_PRING_FENCE_REGRESSION);

    /* IB nulo e tamanho zero sao rejeitados. */
    CHECK(Bc250PersistentRingPlanSubmission(&ring, 0ull, 8u, 1ull,
        &plan) == BC250_PRING_BAD_ARGUMENT);
    CHECK(Bc250PersistentRingPlanSubmission(&ring, IB_GPUVA, 0u, 1ull,
        &plan) == BC250_PRING_BAD_ARGUMENT);
    CHECK(Bc250PersistentRingPlanSubmission(&ring, IB_GPUVA + 1ull, 8u, 1ull,
        &plan) == BC250_PRING_MISALIGNED_ADDRESS);

    CHECK(Bc250TestSubmitOne(&ring, &plan) == 1);
    CHECK(plan.PadDwords == 0u);
    CHECK(plan.PayloadDwords == BC250_PRING_COMPUTE_PAYLOAD_DWORDS);
    CHECK(plan.PayloadDwords == 12u);
    CHECK(plan.StartWptrDwords == 0u);
    CHECK(plan.NewWptrDwords == 12u);
    CHECK(plan.Wrapped == 0u);
    CHECK(ring.WptrDwords == 12u);
    CHECK(ring.LastSubmittedFence == 1ull);
    CHECK(ring.SubmissionCount == 1u);

    /* INDIRECT_BUFFER: header type 3, opcode 0x3F, 4 dwords. */
    CHECK(g_ring[0] == 0xC0023F00u);
    CHECK(g_ring[1] == (uint32_t)(IB_GPUVA & 0xFFFFFFFFull));
    CHECK(g_ring[2] == (uint32_t)(IB_GPUVA >> 32));
    /*
     * Literal independente da macro: impede que uma regressao que zere
     * BC250_PM4_IB_VALID_BIT faca produtor e teste concordarem no erro.
     */
    CHECK(g_ring[3] == 0x00800008u);

    /* RELEASE_MEM na forma literal provada em silicio no 3.43. */
    CHECK(g_ring[4] == BC250_PM4_RELEASE_MEM_HEADER_PROVEN);
    CHECK(g_ring[5] == BC250_PM4_RELEASE_MEM_EVENT_PROVEN);
    CHECK(g_ring[6] == BC250_PM4_RELEASE_MEM_DATA_SEL_32);
    CHECK(g_ring[7] == (uint32_t)(FENCE_GPUVA & 0xFFFFFFFFull));
    CHECK(g_ring[8] == (uint32_t)(FENCE_GPUVA >> 32));
    CHECK(g_ring[9] == 1u);
    CHECK(g_ring[10] == 0u);
    CHECK(g_ring[11] == 0u);

    /* Segundo lote encosta no primeiro, sem buraco. */
    CHECK(Bc250TestSubmitOne(&ring, &plan) == 1);
    CHECK(plan.StartWptrDwords == 12u);
    CHECK(ring.WptrDwords == 24u);
    CHECK(g_ring[12] == 0xC0023F00u);
    CHECK(g_ring[16] == BC250_PM4_RELEASE_MEM_HEADER_PROVEN);
    CHECK(g_ring[21] == 2u); /* fence do segundo lote */

    return 1;
}

static int Bc250TestInterruptCompletionMode(void)
{
    BC250_PERSISTENT_RING ring;
    BC250_PRING_SUBMISSION_PLAN plan;

    memset(g_ring, 0, sizeof(g_ring));
    CHECK(Bc250PersistentRingInit(&ring,
        (uint32_t)BC250_ENGINE_TYPE_COMPUTE, 1u, RING_GPUVA,
        TEST_RING_DWORDS, FENCE_GPUVA, 0ull, 0u,
        (uint32_t)BC250_PRING_COMPLETION_INTERRUPT) == BC250_PRING_OK);
    CHECK(Bc250TestSubmitOne(&ring, &plan) == 1);

    /* INT_SEL = write confirm faz o CP emitir o vetor de EOP para o IH. */
    CHECK(g_ring[6] == (BC250_PM4_RELEASE_MEM_DATA_SEL_32 |
                        BC250_PM4_RELEASE_MEM_INT_SEL_WRITE_CONFIRM));
    return 1;
}

static int Bc250TestWraparoundNeverStraddles(void)
{
    BC250_PERSISTENT_RING ring;
    BC250_PRING_SUBMISSION_PLAN plan;
    uint32_t i;

    memset(g_ring, 0, sizeof(g_ring));
    CHECK(Bc250TestInitCompute(&ring) == BC250_PRING_OK);

    /* Cursor a 8 dwords do fim: o payload de 12 nao cabe. */
    ring.WptrDwords = TEST_RING_DWORDS - 8u;
    ring.RptrShadowDwords = 100u;

    CHECK(Bc250PersistentRingPlanSubmission(&ring, IB_GPUVA, 4u, 1ull,
        &plan) == BC250_PRING_OK);
    CHECK(plan.PadDwords == 8u);
    CHECK(plan.Wrapped == 1u);
    CHECK(plan.StartWptrDwords == 0u);
    CHECK(plan.TotalDwords == 20u);
    CHECK(plan.NewWptrDwords == 12u);

    CHECK(Bc250PersistentRingEmitSubmission(&ring, &plan, g_ring,
        TEST_RING_DWORDS) == BC250_PRING_OK);
    CHECK(Bc250PersistentRingCommit(&ring, &plan) == BC250_PRING_OK);

    /* O rabo do anel e' PACKET2 de um dword, nunca PACKET3 truncado. */
    for (i = TEST_RING_DWORDS - 8u; i < TEST_RING_DWORDS; ++i) {
        CHECK(g_ring[i] == BC250_PM4_PACKET2_NOP);
    }
    /* O pacote real recomeca em zero, inteiro. */
    CHECK(g_ring[0] == 0xC0023F00u);
    CHECK(g_ring[4] == BC250_PM4_RELEASE_MEM_HEADER_PROVEN);
    CHECK(ring.WptrDwords == 12u);
    CHECK(ring.WrapCount == 1u);
    CHECK(ring.PadDwordsTotal == 8u);

    return 1;
}

static int Bc250TestRingFull(void)
{
    BC250_PERSISTENT_RING ring;
    BC250_PRING_SUBMISSION_PLAN plan;
    uint32_t submitted = 0u;

    memset(g_ring, 0, sizeof(g_ring));
    CHECK(Bc250TestInitCompute(&ring) == BC250_PRING_OK);

    /*
     * Sem retire nenhum, o anel enche.  Com 128 dwords e lotes de 12, cabem 10
     * lotes (120 dwords) e o 11o e' recusado — nunca sobrescrito.
     */
    while (Bc250PersistentRingPlanSubmission(&ring, IB_GPUVA, 4u,
               ring.LastSubmittedFence + 1ull, &plan) == BC250_PRING_OK) {
        CHECK(Bc250PersistentRingEmitSubmission(&ring, &plan, g_ring,
            TEST_RING_DWORDS) == BC250_PRING_OK);
        CHECK(Bc250PersistentRingCommit(&ring, &plan) == BC250_PRING_OK);
        ++submitted;
        CHECK(submitted <= 16u);
    }
    CHECK(submitted == 10u);
    CHECK(Bc250PersistentRingPlanSubmission(&ring, IB_GPUVA, 4u,
        ring.LastSubmittedFence + 1ull, &plan) == BC250_PRING_RING_FULL);

    /* Depois que a GPU consome, o espaco volta. */
    {
        uint32_t retired = 0u;
        CHECK(Bc250PersistentRingRetire(&ring, 5u, 60u, &retired) ==
            BC250_PRING_OK);
        CHECK(retired == 5u);
        CHECK(ring.LastRetiredFence == 5ull);
    }
    CHECK(Bc250PersistentRingPlanSubmission(&ring, IB_GPUVA, 4u,
        ring.LastSubmittedFence + 1ull, &plan) == BC250_PRING_OK);

    return 1;
}

static int Bc250TestRetireAccounting(void)
{
    BC250_PERSISTENT_RING ring;
    BC250_PRING_SUBMISSION_PLAN plan;
    uint32_t retired = 0u;
    uint32_t complete = 0u;

    memset(g_ring, 0, sizeof(g_ring));
    CHECK(Bc250TestInitCompute(&ring) == BC250_PRING_OK);
    CHECK(Bc250TestSubmitOne(&ring, &plan) == 1);
    CHECK(Bc250TestSubmitOne(&ring, &plan) == 1);
    CHECK(Bc250TestSubmitOne(&ring, &plan) == 1);
    CHECK(ring.LastSubmittedFence == 3ull);

    /* Nada gravado ainda: fence 0 == ultima aposentada, delta zero. */
    CHECK(Bc250PersistentRingRetire(&ring, 0u, 0u, &retired) ==
        BC250_PRING_OK);
    CHECK(retired == 0u);
    CHECK(ring.LastRetiredFence == 0ull);

    CHECK(Bc250PersistentRingIsFenceComplete(&ring, 1ull, &complete) ==
        BC250_PRING_OK);
    CHECK(complete == 0u);

    /* A GPU gravou 2: dois lotes concluidos de uma vez. */
    CHECK(Bc250PersistentRingRetire(&ring, 2u, 24u, &retired) ==
        BC250_PRING_OK);
    CHECK(retired == 2u);
    CHECK(ring.LastRetiredFence == 2ull);
    CHECK(ring.RptrShadowDwords == 24u);
    CHECK(Bc250PersistentRingIsFenceComplete(&ring, 2ull, &complete) ==
        BC250_PRING_OK);
    CHECK(complete == 1u);
    CHECK(Bc250PersistentRingIsFenceComplete(&ring, 3ull, &complete) ==
        BC250_PRING_OK);
    CHECK(complete == 0u);

    /* Valor fora da janela em voo e' corrupcao, nao progresso. */
    CHECK(Bc250PersistentRingRetire(&ring, 9u, 24u, &retired) ==
        BC250_PRING_FENCE_REGRESSION);
    CHECK(retired == 0u);
    CHECK(ring.LastRetiredFence == 2ull);

    /* Poison classico do laboratorio tambem cai fora da janela. */
    CHECK(Bc250PersistentRingRetire(&ring, 0xA55A0FF0u, 24u, &retired) ==
        BC250_PRING_FENCE_REGRESSION);

    /* RPTR invalido e' rejeitado. */
    CHECK(Bc250PersistentRingRetire(&ring, 3u, TEST_RING_DWORDS, &retired) ==
        BC250_PRING_BAD_ARGUMENT);

    return 1;
}

static int Bc250TestDrainedCursorReconciliation(void)
{
    BC250_PERSISTENT_RING ring;
    uint32_t before = 0u;
    uint32_t reconciled = 0u;

    CHECK(Bc250TestInitCompute(&ring) == BC250_PRING_OK);
    ring.WptrDwords = 204u;
    ring.RptrShadowDwords = 204u;
    ring.LastSubmittedFence = 17ull;
    ring.LastRetiredFence = 17ull;

    /* Caso medido em 3.115: logico 204, HQD fisica drenada em 0/0. */
    CHECK(Bc250PersistentRingReconcileDrainedCursor(
        &ring, 0u, 0u, &before, &reconciled) == BC250_PRING_OK);
    CHECK(before == 204u && reconciled == 0u);
    CHECK(ring.WptrDwords == 0u && ring.RptrShadowDwords == 0u);
    CHECK(ring.LastSubmittedFence == 17ull && ring.LastRetiredFence == 17ull);

    /* Um cursor fisico drenado nao-zero tambem e' fonte da verdade. */
    ring.WptrDwords = 204u;
    ring.RptrShadowDwords = 204u;
    CHECK(Bc250PersistentRingReconcileDrainedCursor(
        &ring, 12u, 12u, &before, &reconciled) == BC250_PRING_OK);
    CHECK(before == 204u && reconciled == 12u);

    /* Bits de volta sao reduzidos; 16396 mod 16384 = 12. */
    ring.WptrDwords = 204u;
    ring.RptrShadowDwords = 204u;
    CHECK(Bc250PersistentRingReconcileDrainedCursor(
        &ring, 16396u, 16396u, &before, &reconciled) == BC250_PRING_OK);
    CHECK(reconciled == 12u && ring.WptrDwords == 12u);

    /* Trabalho fisico nunca e' descartado para fazer os cursores coincidirem. */
    ring.WptrDwords = 204u;
    ring.RptrShadowDwords = 204u;
    CHECK(Bc250PersistentRingReconcileDrainedCursor(
        &ring, 0u, 12u, &before, &reconciled) == BC250_PRING_CURSOR_BUSY);
    CHECK(ring.WptrDwords == 204u && ring.RptrShadowDwords == 204u);

    /* Fence pendente proibe rebase ate com hardware aparentemente drenado. */
    ring.LastSubmittedFence = 18ull;
    ring.LastRetiredFence = 17ull;
    CHECK(Bc250PersistentRingReconcileDrainedCursor(
        &ring, 0u, 0u, &before, &reconciled) == BC250_PRING_CURSOR_BUSY);
    CHECK(ring.WptrDwords == 204u && ring.LastSubmittedFence == 18ull);

    ring.LastSubmittedFence = 17ull;
    ring.LastRetiredFence = 17ull;
    ring.IsActive = 0u;
    CHECK(Bc250PersistentRingReconcileDrainedCursor(
        &ring, 0u, 0u, &before, &reconciled) == BC250_PRING_NOT_ACTIVE);
    CHECK(Bc250PersistentRingReconcileDrainedCursor(
        NULL, 0u, 0u, &before, &reconciled) == BC250_PRING_BAD_ARGUMENT);
    return 1;
}

static int Bc250TestSdmaSubmission(void)
{
    BC250_PERSISTENT_RING ring;
    BC250_PRING_SUBMISSION_PLAN plan;

    memset(g_ring, 0, sizeof(g_ring));
    CHECK(Bc250TestInitSdma(&ring) == BC250_PRING_OK);

    /* O SDMA exige o IB alinhado a 32 bytes. */
    CHECK(Bc250PersistentRingPlanSubmission(&ring, IB_GPUVA + 4ull, 8u, 1ull,
        &plan) == BC250_PRING_MISALIGNED_ADDRESS);

    CHECK(Bc250TestSubmitOne(&ring, &plan) == 1);
    CHECK(plan.PayloadDwords == BC250_PRING_SDMA_PAYLOAD_DWORDS);
    CHECK(plan.PayloadDwords == 12u);

    /* INDIRECT: op 4 com VMID nos bits 19:16. */
    CHECK(g_ring[0] == 0x00000004u);
    CHECK(g_ring[1] == (uint32_t)(IB_GPUVA & 0xFFFFFFE0ull));
    CHECK(g_ring[3] == 8u);
    CHECK(g_ring[4] == (uint32_t)(CSA_GPUVA & 0xFFFFFFFFull));
    /* FENCE: op 5 com MTYPE_UC. */
    CHECK(g_ring[6] == (5u | (3u << 16)));
    CHECK(g_ring[9] == 1u);
    /* TRAP: op 6. */
    CHECK(g_ring[10] == 6u);

    return 1;
}

static int Bc250TestSdmaWrapPadIsZeroNop(void)
{
    BC250_PERSISTENT_RING ring;
    BC250_PRING_SUBMISSION_PLAN plan;
    uint32_t i;

    memset(g_ring, 0xFF, sizeof(g_ring));
    CHECK(Bc250TestInitSdma(&ring) == BC250_PRING_OK);
    ring.WptrDwords = TEST_RING_DWORDS - 4u;
    ring.RptrShadowDwords = 64u;

    CHECK(Bc250PersistentRingPlanSubmission(&ring, IB_GPUVA, 8u, 1ull,
        &plan) == BC250_PRING_OK);
    CHECK(plan.PadDwords == 4u);
    CHECK(Bc250PersistentRingEmitSubmission(&ring, &plan, g_ring,
        TEST_RING_DWORDS) == BC250_PRING_OK);

    for (i = TEST_RING_DWORDS - 4u; i < TEST_RING_DWORDS; ++i) {
        CHECK(g_ring[i] == 0x00000000u); /* SDMA NOP de um dword */
    }
    return 1;
}

static int Bc250TestEmitGuards(void)
{
    BC250_PERSISTENT_RING ring;
    BC250_PRING_SUBMISSION_PLAN plan;

    CHECK(Bc250TestInitCompute(&ring) == BC250_PRING_OK);
    CHECK(Bc250PersistentRingPlanSubmission(&ring, IB_GPUVA, 8u, 1ull,
        &plan) == BC250_PRING_OK);

    /* Tamanho de buffer diferente do anel declarado e' recusado. */
    CHECK(Bc250PersistentRingEmitSubmission(&ring, &plan, g_ring, 64u) ==
        BC250_PRING_INVALID_RING_SIZE);
    CHECK(Bc250PersistentRingEmitSubmission(&ring, &plan, NULL,
        TEST_RING_DWORDS) == BC250_PRING_BAD_ARGUMENT);

    /* Commit com plano adulterado nao avanca o cursor. */
    {
        BC250_PRING_SUBMISSION_PLAN tampered = plan;
        tampered.NewWptrDwords = 99u;
        CHECK(Bc250PersistentRingCommit(&ring, &tampered) ==
            BC250_PRING_BAD_ARGUMENT);
        CHECK(ring.WptrDwords == 0u);
        CHECK(ring.LastSubmittedFence == 0ull);
    }

    /* Anel inativo recusa tudo. */
    ring.IsActive = 0u;
    CHECK(Bc250PersistentRingPlanSubmission(&ring, IB_GPUVA, 8u, 1ull,
        &plan) == BC250_PRING_NOT_ACTIVE);
    CHECK(Bc250PersistentRingCommit(&ring, &plan) == BC250_PRING_NOT_ACTIVE);

    return 1;
}

static int Bc250RunPersistentRingContractTest(void)
{
    CHECK(Bc250TestInitValidation());
    CHECK(Bc250TestComputeSubmission());
    CHECK(Bc250TestInterruptCompletionMode());
    CHECK(Bc250TestWraparoundNeverStraddles());
    CHECK(Bc250TestRingFull());
    CHECK(Bc250TestRetireAccounting());
    CHECK(Bc250TestDrainedCursorReconciliation());
    CHECK(Bc250TestSdmaSubmission());
    CHECK(Bc250TestSdmaWrapPadIsZeroNop());
    CHECK(Bc250TestEmitGuards());
    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunPersistentRingContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunPersistentRingContractTest() ? 0 : 1;
}
#endif
