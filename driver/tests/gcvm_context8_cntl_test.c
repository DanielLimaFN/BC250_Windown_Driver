#include "../core/bc250_gcvm_vmid8_transaction.h"

#if defined(_MSC_VER)
#pragma warning(disable: 4127)
#endif

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

static unsigned long g_failureLine;
#define CHECK(x) do { if (!(x)) { g_failureLine = __LINE__; return 0; } } while (0)

static int Run(void)
{
    uint32_t cntl;

    /* 1. Checagem dos valores constantes canonicos */
    CHECK(BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE == 0x00555403u);
    CHECK(BC250_GCVM_CONTEXT8_CNTL_POST_BASELINE == 0x007FFE80u);
    CHECK(BC250_GCVM_CONTEXT8_PT_START_VALUE == 0x00000000u);
    CHECK(BC250_GCVM_CONTEXT8_PT_END_LO_VALUE == 0x003FFFFFu);
    CHECK(BC250_GCVM_CONTEXT8_PT_END_HI_VALUE == 0x00000000u);

    /* 2. Construcao a partir da geometria WDDM (34 bits, 2 niveis, folha 9 bits / 4 KiB) */
    cntl = Bc250GcvmBuildVmidCntl(1u, 2u, 9u);
    CHECK(cntl == BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE);
    CHECK(cntl == 0x00555403u);

    /* Decomposicao e validacao campo a campo */
    CHECK((cntl & BC250_GCVM_CONTEXT_CNTL_ENABLE_CONTEXT_MASK) == 1u);
    CHECK(((cntl & BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_DEPTH_MASK) >>
           BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_DEPTH_SHIFT) == 1u); /* Depth = 2 - 1 = 1 */
    CHECK(((cntl & BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_BLOCK_SIZE_MASK) >>
           BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_BLOCK_SIZE_SHIFT) == 0u); /* Block = 9 - 9 = 0 */
    CHECK((cntl & BC250_GCVM_CONTEXT_CNTL_RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK) == 0u);
    CHECK((cntl & BC250_GCVM_CONTEXT_CNTL_FAULT_DEFAULTS_MASK) == 0x00555400u);

    /* 3. Caso com contexto desabilitado */
    cntl = Bc250GcvmBuildVmidCntl(0u, 2u, 9u);
    CHECK(cntl == 0x00555402u);
    CHECK((cntl & BC250_GCVM_CONTEXT_CNTL_ENABLE_CONTEXT_MASK) == 0u);
    CHECK(((cntl & BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_DEPTH_MASK) >>
           BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_DEPTH_SHIFT) == 1u);

    /*
     * 3b. Observabilidade de fault -- a aritmetica do experimento da pergunta 3
     * do doc 167, travada aqui para nao ser escrita a mao numa corrida.
     *
     * NAO e' o valor em uso: BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE continua
     * sendo o que o driver programa. Isto so' garante que, se alguem armar o
     * experimento, o numero esta certo.
     */
    /* A mascara de interrupt e' exatamente os bits 9,11,13,15,17,19,21. */
    CHECK(BC250_GCVM_CONTEXT_CNTL_FAULT_INTERRUPTS_MASK ==
        ((1u << 9) | (1u << 11) | (1u << 13) | (1u << 15) |
         (1u << 17) | (1u << 19) | (1u << 21)));
    /* Cada classe de fault tem o par DEFAULT/INTERRUPT: as duas mascaras sao
     * disjuntas e o INTERRUPT de cada uma fica um bit abaixo do DEFAULT. */
    CHECK((BC250_GCVM_CONTEXT_CNTL_FAULT_INTERRUPTS_MASK &
           BC250_GCVM_CONTEXT_CNTL_FAULT_DEFAULTS_MASK) == 0u);
    CHECK((BC250_GCVM_CONTEXT_CNTL_FAULT_DEFAULTS_MASK >> 1) ==
        BC250_GCVM_CONTEXT_CNTL_FAULT_INTERRUPTS_MASK);
    /* O valor observavel e' o alvo com os sete INTERRUPT ligados. */
    CHECK(BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE ==
        (BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE |
         BC250_GCVM_CONTEXT_CNTL_FAULT_INTERRUPTS_MASK));
    CHECK(BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE == 0x007FFE03u);
    /*
     * E ele difere do POST medido em apenas 0x83: ENABLE_CONTEXT e DEPTH, que
     * ligamos de proposito, mais RETRY_PERMISSION_OR_INVALID_PAGE_FAULT, que o
     * upstream desliga em APU (gfxhub_v2_0.c:313). Nao e' valor inventado -- e'
     * o estado de fabrica com a observabilidade preservada.
     */
    CHECK((BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE ^
           BC250_GCVM_CONTEXT8_CNTL_POST_BASELINE) ==
        (BC250_GCVM_CONTEXT_CNTL_ENABLE_CONTEXT_MASK |
         (1u << BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_DEPTH_SHIFT) |
         BC250_GCVM_CONTEXT_CNTL_RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK));
    /* O alvo em uso NAO habilita interrupcao nenhuma -- e' isso que a hipotese diz. */
    CHECK((BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE &
           BC250_GCVM_CONTEXT_CNTL_FAULT_INTERRUPTS_MASK) == 0u);
    /* Mas o POST habilitava as sete. */
    CHECK((BC250_GCVM_CONTEXT8_CNTL_POST_BASELINE &
           BC250_GCVM_CONTEXT_CNTL_FAULT_INTERRUPTS_MASK) ==
        BC250_GCVM_CONTEXT_CNTL_FAULT_INTERRUPTS_MASK);

    /* 4. Casos de borda / invalidos devem retornar 0 */
    CHECK(Bc250GcvmBuildVmidCntl(1u, 0u, 9u) == 0u);   /* Niveis = 0 invalido */
    CHECK(Bc250GcvmBuildVmidCntl(1u, 5u, 9u) == 0u);   /* Niveis > 4 invalido */
    CHECK(Bc250GcvmBuildVmidCntl(1u, 2u, 8u) == 0u);   /* Leaf bits < 9 invalido */
    CHECK(Bc250GcvmBuildVmidCntl(1u, 2u, 25u) == 0u);  /* Leaf bits > 24 invalido */

    return 1;
}

void mainCRTStartup(void)
{
    ExitProcess(Run() ? 0u : g_failureLine);
}
