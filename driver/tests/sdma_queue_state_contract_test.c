#include "../core/bc250_sdma_queue_state_contract.h"

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);
#define CHECK(x) do { if (!(x)) { ExitProcess((unsigned long)__LINE__); return 1; } } while (0)
#else
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FALHOU linha %d\n", __LINE__); return 1; } } while (0)
#endif

/*
 * Este teste trava a DECODIFICACAO da telemetria de fila da SDMA0 contra dois
 * gabaritos independentes:
 *
 *   1. as mascaras de inc/linux_ref/gc_10_1_0_sh_mask.h (P1);
 *   2. os valores MEDIDOS no silicio em 2026-09-05, arquivados em
 *      docs/168_HANDOFF_2026-09-05_INDIRECT_SDMA.md secao 5.3 (P0).
 *
 * O ponto do (2) e' que nao basta o decodificador ser aritmeticamente
 * coerente: ele tem de reproduzir a leitura real, senao a proxima corrida sai
 * com telemetria bonita e errada.
 */

static int TestMeasuredExecVmid8(void)
{
    BC250_SDMA_QUEUE_STATE st;

    /*
     * A variante EXEC, medida:
     *   IB_BASE = 0x0000000000201000   IB_CNTL = 0x00080101
     *   IB_RPTR = 0x20  IB_SIZE = 0  CONTEXT_STATUS = 0x09
     */
    CHECK(Bc250SdmaDecodeQueueState(
        BC250_SDMA_IB_CNTL_MEASURED_EXEC_VMID8,
        BC250_SDMA_IB_RPTR_MEASURED_ALL_VARIANTS,
        0u, 0u, 0x3CB3u,
        BC250_SDMA_CONTEXT_STATUS_MEASURED_EXEC_VMID8,
        0x0000000000201000ull, 0u, &st) == BC250_SDMA_QUEUE_STATE_OK);

    CHECK(st.RawIbSubRemain == 0u);
    CHECK(st.IbSubRemainDwords == 0u);

    /* IB_CNTL: a engine decodificou o INDIRECT e propagou o VMID do pacote. */
    CHECK(st.IbEnable == 1u);
    CHECK(st.SwitchInsideIb == 1u);
    CHECK(st.CmdVmid == 8u);
    CHECK(st.IbSwapEnable == 0u);

    /*
     * IB_RPTR cru 0x20 -> 8 DWORDS, porque o campo comeca no bit 2.
     * Se alguem remover o shift, isto vira 32 e o teste cai.
     */
    CHECK(st.IbRptrDwords == 8u);

    /* CONTEXT_STATUS 0x09: o discriminante. */
    CHECK(st.Selected == 1u);
    CHECK(st.Expired == 1u);
    CHECK(st.Idle == 0u);           /* <-- NAO OCIOSA */
    CHECK(st.Exception == 0u);
    CHECK(st.Preempted == 0u);
    CHECK(st.CtxSwAble == 0u);
    CHECK(st.CtxSwReady == 0u);
    CHECK(st.PreemptDisable == 0u);
    CHECK(st.UnknownBits == 0u);

    /* SKIP_CNTL: o valor inteiro e' SKIP_COUNT, bits [19:0]. */
    CHECK(st.SkipCount == 0x3CB3u);

    CHECK(Bc250SdmaClassifyQueueState(&st) == BC250_SDMA_QUEUE_BUSY_OR_STALLED);
    CHECK(Bc250SdmaQueueLooksStalledInsideIb(&st, 1u) == 1u);
    /* Sem anel drenado o rotulo nao se aplica -- e' outro cenario. */
    CHECK(Bc250SdmaQueueLooksStalledInsideIb(&st, 0u) == 0u);
    return 0;
}

static int TestMeasuredPhysVmid0(void)
{
    BC250_SDMA_QUEUE_STATE st;

    /*
     * A variante PHYS, medida:
     *   IB_BASE = 0x000000F40F020000   IB_CNTL = 0x00000101
     *   CONTEXT_STATUS = 0x0D
     */
    CHECK(Bc250SdmaDecodeQueueState(
        BC250_SDMA_IB_CNTL_MEASURED_PHYS_VMID0,
        BC250_SDMA_IB_RPTR_MEASURED_ALL_VARIANTS,
        0u, 0u, 0u,
        BC250_SDMA_CONTEXT_STATUS_MEASURED_PHYS_VMID0,
        0x000000F40F020000ull, 0u, &st) == BC250_SDMA_QUEUE_STATE_OK);

    CHECK(st.RawIbSubRemain == 0u);
    CHECK(st.IbSubRemainDwords == 0u);

    CHECK(st.IbEnable == 1u);
    /*
     * SWITCH_INSIDE_IB vem ligado AQUI TAMBEM. Escrevi este teste supondo 0 e
     * ele me corrigiu: 0x00000101 tem o bit 8. Os dois IB_CNTL medidos diferem
     * SO' no CMD_VMID -- 0x00000101 contra 0x00080101 -- o que reforca que a
     * engine decodificou o mesmo pacote nas duas variantes e propagou o VMID.
     */
    CHECK(st.SwitchInsideIb == 1u);
    CHECK(st.CmdVmid == 0u);

    /* 0x0D difere de 0x09 em UM bit, e e' o IDLE. */
    CHECK(st.Selected == 1u);
    CHECK(st.Idle == 1u);           /* <-- OCIOSA: terminou */
    CHECK(st.Expired == 1u);
    CHECK(st.Exception == 0u);
    CHECK(st.UnknownBits == 0u);

    CHECK(Bc250SdmaClassifyQueueState(&st) == BC250_SDMA_QUEUE_DRAINED);
    /*
     * ATENCAO: DRAINED nao e' sucesso. A prova de bytes desta variante TAMBEM
     * falhou (docs/168 secao 5.2). O contrato existe justamente para nao
     * confundir "a fila drenou" com "o IB escreveu" -- a segunda afirmacao
     * exige o tripe da Lei 2.
     */
    CHECK(Bc250SdmaQueueLooksStalledInsideIb(&st, 1u) == 0u);
    return 0;
}

static int TestDiscriminantIsExactlyOneBit(void)
{
    /*
     * O achado de 07/09 em duas linhas de aritmetica sobre valores medidos:
     *
     *   os dois CONTEXT_STATUS diferem exatamente no bit de IDLE;
     *   os dois IB_CNTL diferem exatamente no campo CMD_VMID.
     *
     * Ou seja: a engine tratou o pacote igual nas duas variantes, e a unica
     * diferenca de DESFECHO e' que a variante traduzida nao ficou ociosa.
     */
    CHECK((BC250_SDMA_CONTEXT_STATUS_MEASURED_PHYS_VMID0 ^
           BC250_SDMA_CONTEXT_STATUS_MEASURED_EXEC_VMID8) ==
        BC250_SDMA_CONTEXT_STATUS_IDLE_MASK);
    CHECK((BC250_SDMA_IB_CNTL_MEASURED_PHYS_VMID0 ^
           BC250_SDMA_IB_CNTL_MEASURED_EXEC_VMID8) ==
        (8u << BC250_SDMA_IB_CNTL_CMD_VMID_SHIFT));
    CHECK(((BC250_SDMA_IB_CNTL_MEASURED_PHYS_VMID0 ^
            BC250_SDMA_IB_CNTL_MEASURED_EXEC_VMID8) &
           ~BC250_SDMA_IB_CNTL_CMD_VMID_MASK) == 0u);
    return 0;
}

static int TestFieldMasksMatchUpstreamHeader(void)
{
    /* Copiados de inc/linux_ref/gc_10_1_0_sh_mask.h, linhas citadas no .h. */
    CHECK(BC250_SDMA_IB_CNTL_IB_ENABLE_MASK == 0x00000001u);
    CHECK(BC250_SDMA_IB_CNTL_IB_SWAP_ENABLE_MASK == 0x00000010u);
    CHECK(BC250_SDMA_IB_CNTL_SWITCH_INSIDE_IB_MASK == 0x00000100u);
    CHECK(BC250_SDMA_IB_CNTL_CMD_VMID_MASK == 0x000F0000u);
    CHECK(BC250_SDMA_IB_RPTR_OFFSET_MASK == 0x003FFFFCu);
    CHECK(BC250_SDMA_IB_RPTR_OFFSET_SHIFT == 2u);
    CHECK(BC250_SDMA_IB_SIZE_SIZE_MASK == 0x000FFFFFu);
    CHECK(BC250_SDMA_SKIP_CNTL_SKIP_COUNT_MASK == 0x000FFFFFu);
    CHECK(BC250_SDMA_CONTEXT_STATUS_SELECTED_MASK == 0x00000001u);
    CHECK(BC250_SDMA_CONTEXT_STATUS_IDLE_MASK == 0x00000004u);
    CHECK(BC250_SDMA_CONTEXT_STATUS_EXPIRED_MASK == 0x00000008u);
    CHECK(BC250_SDMA_CONTEXT_STATUS_EXCEPTION_MASK == 0x00000070u);
    CHECK(BC250_SDMA_CONTEXT_STATUS_CTXSW_ABLE_MASK == 0x00000080u);
    CHECK(BC250_SDMA_CONTEXT_STATUS_CTXSW_READY_MASK == 0x00000100u);
    CHECK(BC250_SDMA_CONTEXT_STATUS_PREEMPTED_MASK == 0x00000200u);
    CHECK(BC250_SDMA_CONTEXT_STATUS_PREEMPT_DISABLE_MASK == 0x00000400u);

    /*
     * O bit 1 NAO tem nome no header do upstream. A mascara de conhecidos
     * deixa esse buraco explicito, para que UnknownBits o denuncie.
     */
    CHECK(BC250_SDMA_CONTEXT_STATUS_KNOWN_MASK == 0x000007FDu);
    CHECK((BC250_SDMA_CONTEXT_STATUS_KNOWN_MASK & 0x00000002u) == 0u);

    /* SDMA0_GFX_IB_SUB_REMAIN (linhas 1013-1014) */
    CHECK(BC250_SDMA_IB_SUB_REMAIN_REMAIN_DW_MASK == 0x00003FFFu);
    return 0;
}

static int TestOffsetsAreConsecutive(void)
{
    /*
     * A derivacao do catalogo: o bloco e' consecutivo entre as duas ancoras
     * ja catalogadas, e 0x04BC8 - 0x04BA8 = 0x20 = 8 dwords.
     */
    CHECK(BC250_SDMA0_GFX_IB_RPTR_OFFSET ==
        BC250_SDMA0_GFX_IB_CNTL_OFFSET + 4u);
    CHECK(BC250_SDMA0_GFX_IB_OFFSET_OFFSET ==
        BC250_SDMA0_GFX_IB_CNTL_OFFSET + 8u);
    CHECK(BC250_SDMA0_GFX_IB_BASE_LO_OFFSET ==
        BC250_SDMA0_GFX_IB_CNTL_OFFSET + 12u);
    CHECK(BC250_SDMA0_GFX_IB_BASE_HI_OFFSET ==
        BC250_SDMA0_GFX_IB_CNTL_OFFSET + 16u);
    CHECK(BC250_SDMA0_GFX_IB_SIZE_OFFSET ==
        BC250_SDMA0_GFX_IB_CNTL_OFFSET + 20u);
    CHECK(BC250_SDMA0_GFX_SKIP_CNTL_OFFSET ==
        BC250_SDMA0_GFX_IB_CNTL_OFFSET + 24u);
    CHECK(BC250_SDMA0_GFX_CONTEXT_STATUS_OFFSET ==
        BC250_SDMA0_GFX_IB_CNTL_OFFSET + 28u);
    /* E a ancora de cima fecha o bloco de 8 dwords. */
    CHECK(BC250_SDMA0_GFX_CONTEXT_STATUS_OFFSET + 4u == 0x04BC8u);

    /* SDMA0_GFX_IB_SUB_REMAIN: dw 0x00af -> 0x4980 + 0x00af * 4 = 0x04C3C */
    CHECK(BC250_SDMA0_GFX_IB_SUB_REMAIN_OFFSET == 0x04C3Cu);
    return 0;
}

static int TestClassificationPrecedence(void)
{
    BC250_SDMA_QUEUE_STATE st;

    /* EXCEPTION ganha de tudo, inclusive de IDLE. */
    CHECK(Bc250SdmaDecodeQueueState(0x00000001u, 0u, 0u, 0u, 0u,
        BC250_SDMA_CONTEXT_STATUS_IDLE_MASK | 0x00000010u, 0ull, 0u, &st) ==
        BC250_SDMA_QUEUE_STATE_OK);
    CHECK(st.Exception == 1u);
    CHECK(st.Idle == 1u);
    CHECK(Bc250SdmaClassifyQueueState(&st) == BC250_SDMA_QUEUE_EXCEPTION);

    /* EXCEPTION e' um campo de 3 bits, nao um flag. */
    CHECK(Bc250SdmaDecodeQueueState(0x00000001u, 0u, 0u, 0u, 0u,
        0x00000070u, 0ull, 0u, &st) == BC250_SDMA_QUEUE_STATE_OK);
    CHECK(st.Exception == 7u);

    /* IB_ENABLE desligado tem rotulo proprio -- foi hipotese viva em 05/09. */
    CHECK(Bc250SdmaDecodeQueueState(0x00000000u, 0u, 0u, 0u, 0u,
        BC250_SDMA_CONTEXT_STATUS_IDLE_MASK, 0ull, 0u, &st) ==
        BC250_SDMA_QUEUE_STATE_OK);
    CHECK(Bc250SdmaClassifyQueueState(&st) == BC250_SDMA_QUEUE_IB_DISABLED);

    /* Preempcao antes de "nao ocioso": e' estado transitorio conhecido. */
    CHECK(Bc250SdmaDecodeQueueState(0x00000001u, 0u, 0u, 0u, 0u,
        BC250_SDMA_CONTEXT_STATUS_PREEMPTED_MASK, 0ull, 0u, &st) ==
        BC250_SDMA_QUEUE_STATE_OK);
    CHECK(Bc250SdmaClassifyQueueState(&st) == BC250_SDMA_QUEUE_PREEMPTED);

    /* Ponteiro nulo nao pode sair como DRAINED. */
    CHECK(Bc250SdmaClassifyQueueState(NULL) == BC250_SDMA_QUEUE_EXCEPTION);
    CHECK(Bc250SdmaDecodeQueueState(0u, 0u, 0u, 0u, 0u, 0u, 0ull, 0u, NULL) ==
        BC250_SDMA_QUEUE_STATE_BAD_ARGUMENT);
    return 0;
}

static int TestUnknownBitsAreReported(void)
{
    BC250_SDMA_QUEUE_STATE st;

    /* Bit 1 e bit 31 nao tem nome: tem de aparecer em UnknownBits. */
    CHECK(Bc250SdmaDecodeQueueState(0x00000001u, 0u, 0u, 0u, 0u,
        0x80000002u, 0ull, 0u, &st) == BC250_SDMA_QUEUE_STATE_OK);
    CHECK(st.UnknownBits == 0x80000002u);
    /* E nenhum campo nomeado foi contaminado por eles. */
    CHECK(st.Selected == 0u && st.Idle == 0u && st.Expired == 0u);
    CHECK(st.Exception == 0u && st.Preempted == 0u);
    return 0;
}

static int TestRptrShiftIsNotForgotten(void)
{
    BC250_SDMA_QUEUE_STATE st;

    /* 8 dwords = 32 bytes = 0x20 cru. */
    CHECK(Bc250SdmaDecodeQueueState(0x00000001u, 0x20u, 0x20u, 8u, 0u,
        0u, 0ull, 0x00000005u, &st) == BC250_SDMA_QUEUE_STATE_OK);
    CHECK(st.IbRptrDwords == 8u);
    CHECK(st.IbOffsetDwords == 8u);
    CHECK(st.IbSizeDwords == 8u);   /* IB_SIZE ja' e' em dwords, sem shift */
    CHECK(st.IbSubRemainDwords == 5u);

    /* Os dois bits baixos do cru sao ignorados pela mascara do campo. */
    CHECK(Bc250SdmaDecodeQueueState(0x00000001u, 0x23u, 0u, 0u, 0u,
        0u, 0ull, 0u, &st) == BC250_SDMA_QUEUE_STATE_OK);
    CHECK(st.IbRptrDwords == 8u);

    /* Topo do campo: bits [21:2] cheios. E mascara de 14 bits em SubRemain. */
    CHECK(Bc250SdmaDecodeQueueState(0x00000001u, 0xFFFFFFFFu, 0u, 0u, 0u,
        0u, 0ull, 0xFFFFFFFFu, &st) == BC250_SDMA_QUEUE_STATE_OK);
    CHECK(st.IbRptrDwords == 0x000FFFFFu);
    CHECK(st.IbSubRemainDwords == 0x00003FFFu);
    return 0;
}

static int TestVerdictNames(void)
{
    CHECK(Bc250SdmaQueueVerdictName(BC250_SDMA_QUEUE_DRAINED)[0] == 'D');
    CHECK(Bc250SdmaQueueVerdictName(BC250_SDMA_QUEUE_BUSY_OR_STALLED)[0] == 'B');
    CHECK(Bc250SdmaQueueVerdictName(BC250_SDMA_QUEUE_EXCEPTION)[0] == 'E');
    CHECK(Bc250SdmaQueueVerdictName(BC250_SDMA_QUEUE_PREEMPTED)[0] == 'P');
    CHECK(Bc250SdmaQueueVerdictName(BC250_SDMA_QUEUE_IB_DISABLED)[0] == 'I');
    CHECK(Bc250SdmaQueueVerdictName((BC250_SDMA_QUEUE_VERDICT)99)[0] == 'D');
    return 0;
}

static int Bc250RunSdmaQueueStateContractTest(void)
{
    if (TestFieldMasksMatchUpstreamHeader() != 0) return 1;
    if (TestOffsetsAreConsecutive() != 0) return 1;
    if (TestMeasuredExecVmid8() != 0) return 1;
    if (TestMeasuredPhysVmid0() != 0) return 1;
    if (TestDiscriminantIsExactlyOneBit() != 0) return 1;
    if (TestClassificationPrecedence() != 0) return 1;
    if (TestUnknownBitsAreReported() != 0) return 1;
    if (TestRptrShiftIsNotForgotten() != 0) return 1;
    if (TestVerdictNames() != 0) return 1;
    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess((unsigned long)Bc250RunSdmaQueueStateContractTest());
}
#else
int main(void)
{
    int failed = Bc250RunSdmaQueueStateContractTest();
    if (failed == 0) {
        printf("sdma_queue_state_contract_test: PASS\n");
    }
    return failed;
}
#endif
