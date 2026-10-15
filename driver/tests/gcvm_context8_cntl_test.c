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

    /*
     * 1. Valores canonicos, derivados de bc250_gpuva_geometry.h (P5, doc 667).
     * Os numeros esperados ficam escritos aqui, por extenso, nos dois ramos:
     * se a derivacao do header errar, o teste reprova em vez de concordar.
     *   4 niveis x 9 bits (amdgpu gfx10): CNTL 0x00555407, END 0xF:FFFFFFFF
     *   2 niveis (ate a 251b):           CNTL 0x00555403, END 0x0:003FFFFF
     */
    CHECK(BC250_GCVM_CONTEXT8_CNTL_POST_BASELINE == 0x007FFE80u);
    CHECK(BC250_GCVM_CONTEXT8_PT_START_VALUE == 0x00000000u);
#if BC250_P5_QUATRO_NIVEIS
    CHECK(BC250_GPUVA_PT_LEVEL_COUNT == 4u);
    CHECK(BC250_GPUVA_PT_VA_BITS_DERIVED == 48u);
    CHECK(BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE == 0x00555407u);
    CHECK(BC250_GCVM_CONTEXT8_PT_END_LO_VALUE == 0xFFFFFFFFu);
    CHECK(BC250_GCVM_CONTEXT8_PT_END_HI_VALUE == 0x0000000Fu);
#else
    CHECK(BC250_GPUVA_PT_LEVEL_COUNT == 2u);
    CHECK(BC250_GPUVA_PT_VA_BITS_DERIVED == 34u);
    CHECK(BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE == 0x00555403u);
    CHECK(BC250_GCVM_CONTEXT8_PT_END_LO_VALUE == 0x003FFFFFu);
    CHECK(BC250_GCVM_CONTEXT8_PT_END_HI_VALUE == 0x00000000u);
#endif

    /*
     * 2. O alvo do header e' o MESMO numero que o construtor generico da
     * (fonte independente da derivacao do header).
     */
    cntl = Bc250GcvmBuildVmidCntl(1u, BC250_GPUVA_PT_LEVEL_COUNT, BC250_GPUVA_PT_LEAF_INDEX_BITS);
    CHECK(cntl == BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE);
    CHECK(Bc250GcvmBuildVmidCntl(1u, 4u, 9u) == 0x00555407u);
    CHECK(((Bc250GcvmBuildVmidCntl(1u, 4u, 9u) & BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_DEPTH_MASK) >>
           BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_DEPTH_SHIFT) == 3u); /* num_level do amdgpu */

    /* 2b. A geometria de 2 niveis continua construivel (chave em 0). */
    cntl = Bc250GcvmBuildVmidCntl(1u, 2u, 9u);
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
#if BC250_P5_QUATRO_NIVEIS
    CHECK(BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE == 0x007FFE07u);
#else
    CHECK(BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE == 0x007FFE03u);
#endif
    /*
     * E ele difere do POST medido em apenas 0x83: ENABLE_CONTEXT e DEPTH, que
     * ligamos de proposito, mais RETRY_PERMISSION_OR_INVALID_PAGE_FAULT, que o
     * upstream desliga em APU (gfxhub_v2_0.c:313). Nao e' valor inventado -- e'
     * o estado de fabrica com a observabilidade preservada.
     */
    /* P5: o DEPTH muda de 1 para 3 (bits 1 e 2); o resto da diferenca e' o mesmo. */
    CHECK((BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE ^
           BC250_GCVM_CONTEXT8_CNTL_POST_BASELINE) ==
        (BC250_GCVM_CONTEXT_CNTL_ENABLE_CONTEXT_MASK |
         (BC250_GPUVA_PT_GCVM_DEPTH << BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_DEPTH_SHIFT) |
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
