/*
 * Trava a aritmetica "offset de segmento -> MC" com os numeros MEDIDOS na
 * corrida 12/09 12:29, sem arredondar.
 *
 * O defeito que este teste existe para impedir: o present somava apenas
 * VramMcBase ao offset de segmento e lia 11.059.200 bytes antes da imagem.
 * O caso 1 e' exatamente essa corrida; o caso 2 e' o valor errado, e falharia
 * se alguem reintroduzisse a soma sem LocalSegmentBaseOffset.
 */
#include "../core/bc250_segment_address_contract.h"
#include <stdio.h>
#include <assert.h>

/* Numeros medidos, sem arredondar (corrida 2026-09-12 12:29). */
#define MEASURED_VRAM_MC_BASE       0x000000F400000000ull
#define MEASURED_LOCAL_SEG_BASE     0x0000000000A8C000ull /* 11.059.200 */
#define MEASURED_LOCAL_SEG_BYTES    0x000000000F531000ull /* 257.101.824 */
#define MEASURED_SEGMENT_OFFSET     0x0000000001560000ull /* 22.413.312 */
#define MEASURED_FRAME_BYTES        11059200ull
#define OBSERVED_WRONG_SRC_MC       0x000000F401560000ull /* 12:29 -- faltava SegBase */
#define EXPECTED_CORRECT_SRC_MC     0x000000F401FEC000ull /* o que devia ter usado */

/*
 * Segunda armadilha, medida na corrida 12:51: existem DUAS bases de VRAM e elas
 * NAO sao intercambiaveis.
 *
 *   GCVM  (PTE/root/VMID)      = 0x450000000
 *   SDMA present (anel de blt) = 0xF400000000
 *
 * A correcao de 12:29 acertou o termo que faltava e trocou a base por engano:
 * a ORIGEM foi para 0x451FEC000 e o DESTINO ficou em 0xF400000000. Dominios
 * diferentes -> blt em timeout com a fence no poison, 8 de 8, e present caindo
 * na copia por CPU. Origem e destino tem de sair da MESMA base.
 */
#define MEASURED_GCVM_MC_BASE       0x0000000450000000ull
#define OBSERVED_WRONG_DOMAIN_MC    0x0000000451FEC000ull /* 12:51 -- dominio trocado */
#define MEASURED_SCANOUT_DST_MC     0x000000F400000000ull

int main(void)
{
    uint64_t mc = 0;
    BC250_SEGMENT_ADDRESS_STATUS st;

    printf("=========== BC250 SEGMENT ADDRESS CONTRACT TEST ===========\n");

    /* 1. O caso da corrida: a conversao correta inclui a base do segmento. */
    st = Bc250SegmentOffsetToMc(MEASURED_VRAM_MC_BASE, MEASURED_LOCAL_SEG_BASE,
        MEASURED_LOCAL_SEG_BYTES, 1u, MEASURED_SEGMENT_OFFSET,
        MEASURED_FRAME_BYTES, &mc);
    assert(st == BC250_SEGMENT_ADDRESS_OK);
    assert(mc == EXPECTED_CORRECT_SRC_MC);

    /* 2. E NAO pode dar o valor que a corrida 12:29 realmente usou. */
    assert(mc != OBSERVED_WRONG_SRC_MC);
    assert(mc - OBSERVED_WRONG_SRC_MC == MEASURED_LOCAL_SEG_BASE);
    printf("  -> 12:29: SrcMc 0x%llX (errado) -> 0x%llX (certo), delta 0x%llX\n",
        (unsigned long long)OBSERVED_WRONG_SRC_MC,
        (unsigned long long)mc,
        (unsigned long long)MEASURED_LOCAL_SEG_BASE);

    /*
     * 2b. A ARMADILHA DO DOMINIO (corrida 12:51). Com a base da GCVM, a mesma
     *     conta produz o valor que fez o blt dar timeout -- e esse valor NAO
     *     pode ser confundido com o correto. Origem e destino tem de partir da
     *     mesma base; aqui se prova que partir da outra da' um endereco
     *     distante do scanout.
     */
    st = Bc250SegmentOffsetToMc(MEASURED_GCVM_MC_BASE, MEASURED_LOCAL_SEG_BASE,
        MEASURED_LOCAL_SEG_BYTES, 1u, MEASURED_SEGMENT_OFFSET,
        MEASURED_FRAME_BYTES, &mc);
    assert(st == BC250_SEGMENT_ADDRESS_OK);           /* a conta e' valida... */
    assert(mc == OBSERVED_WRONG_DOMAIN_MC);           /* ...e da' o valor de 12:51 */
    assert(mc != EXPECTED_CORRECT_SRC_MC);            /* que NAO e' o correto */
    /* E fica fora do dominio do scanout por mais de 600 GB. */
    assert(MEASURED_SCANOUT_DST_MC - mc > 0x9000000000ull);
    printf("  -> 12:51: base da GCVM da' 0x%llX, fora do dominio do destino 0x%llX\n",
        (unsigned long long)mc, (unsigned long long)MEASURED_SCANOUT_DST_MC);

    /* 3. Segmento nao-local e' recusado: o SDMA so' enderecca VRAM por MC. */
    st = Bc250SegmentOffsetToMc(MEASURED_VRAM_MC_BASE, MEASURED_LOCAL_SEG_BASE,
        MEASURED_LOCAL_SEG_BYTES, 0u, MEASURED_SEGMENT_OFFSET,
        MEASURED_FRAME_BYTES, &mc);
    assert(st == BC250_SEGMENT_ADDRESS_NOT_LOCAL_SEGMENT);
    assert(mc == 0ull);

    /* 4. Faixa que nao cabe no segmento e' RECUSADA, nao truncada. Sem isto a
     *    origem leria memoria vizinha e a fence ainda assim concluiria. */
    st = Bc250SegmentOffsetToMc(MEASURED_VRAM_MC_BASE, MEASURED_LOCAL_SEG_BASE,
        MEASURED_LOCAL_SEG_BYTES, 1u,
        MEASURED_LOCAL_SEG_BYTES - 4096ull, MEASURED_FRAME_BYTES, &mc);
    assert(st == BC250_SEGMENT_ADDRESS_OUT_OF_SEGMENT);

    /* 5. O ultimo byte que CABE e' aceito -- a recusa nao pode virar peneira. */
    st = Bc250SegmentOffsetToMc(MEASURED_VRAM_MC_BASE, MEASURED_LOCAL_SEG_BASE,
        MEASURED_LOCAL_SEG_BYTES, 1u,
        MEASURED_LOCAL_SEG_BYTES - MEASURED_FRAME_BYTES, MEASURED_FRAME_BYTES, &mc);
    assert(st == BC250_SEGMENT_ADDRESS_OK);
    assert(mc == MEASURED_VRAM_MC_BASE + MEASURED_LOCAL_SEG_BASE +
                 (MEASURED_LOCAL_SEG_BYTES - MEASURED_FRAME_BYTES));

    /* 6. Offset fora do segmento. */
    st = Bc250SegmentOffsetToMc(MEASURED_VRAM_MC_BASE, MEASURED_LOCAL_SEG_BASE,
        MEASURED_LOCAL_SEG_BYTES, 1u, MEASURED_LOCAL_SEG_BYTES, 0ull, &mc);
    assert(st == BC250_SEGMENT_ADDRESS_OUT_OF_SEGMENT);

    /* 7. Argumentos degenerados falham fechado. */
    st = Bc250SegmentOffsetToMc(0ull, MEASURED_LOCAL_SEG_BASE,
        MEASURED_LOCAL_SEG_BYTES, 1u, 0ull, 0ull, &mc);
    assert(st == BC250_SEGMENT_ADDRESS_BAD_ARGUMENT);
    st = Bc250SegmentOffsetToMc(MEASURED_VRAM_MC_BASE, MEASURED_LOCAL_SEG_BASE,
        0ull, 1u, 0ull, 0ull, &mc);
    assert(st == BC250_SEGMENT_ADDRESS_BAD_ARGUMENT);
    st = Bc250SegmentOffsetToMc(MEASURED_VRAM_MC_BASE, MEASURED_LOCAL_SEG_BASE,
        MEASURED_LOCAL_SEG_BYTES, 1u, 0ull, 0ull, NULL);
    assert(st == BC250_SEGMENT_ADDRESS_BAD_ARGUMENT);

    /* 8. Overflow do espaco MC nao pode dar a volta. */
    st = Bc250SegmentOffsetToMc(0xFFFFFFFFFFFFF000ull, 0x1000ull,
        0x10000ull, 1u, 0x1000ull, 0ull, &mc);
    assert(st == BC250_SEGMENT_ADDRESS_OVERFLOW);

    printf("SEGMENT ADDRESS CONTRACT: OK (base do segmento obrigatoria, "
           "faixa conferida, fail-closed; offline)\n");
    return 0;
}
