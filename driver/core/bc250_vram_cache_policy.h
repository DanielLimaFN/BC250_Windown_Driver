#ifndef BC250_VRAM_CACHE_POLICY_H
#define BC250_VRAM_CACHE_POLICY_H

/*
 * T3.1 (28/09/2026, doc 520) -- FONTE UNICA da politica de tipo de cache da
 * VRAM vista pela CPU (BAR 0, 256 MB).
 *
 * O DEFEITO (P0 medido + P1 lido):
 *   Ate a 3.311 o framebuffer era mapeado WRITE-COMBINED sobre os 256 MB
 *   INTEIROS da BAR 0 (bc250_display_ddi.c, "256 MB cobre 100% do BAR 0"),
 *   e as estruturas privadas do driver eram mapeadas UNCACHED por cima dele:
 *     0xFA00000  pagina de migalhas (Corrida 166: "Phys=0xCFA00000 UNCACHED")
 *     0xFF00000  anel + work do SDMA de apresentacao (MmNonCached)
 *     0xFF70000  anel grafico (MmNonCached; Corrida 166: RingMc=0xF40FF70000)
 *     0xFF90000  bloco do MEC / MQD (DxgkCbMapMemory MmNonCached)
 *   e a sonda de VRAM do StartDevice mapeava a BAR inteira UC com o WC vivo.
 *   Intel SDM vol. 3 (PAT) e AMD APM vol. 2 declaram NAO SUPORTADO mapear a
 *   mesma pagina fisica com tipos de memoria diferentes: o comportamento e'
 *   indefinido e pode derrubar o sistema. Se o Windows repassa o conflito ao
 *   hardware em espaco de I/O nao esta documentado: HIPOTESE (H).
 *
 * A REGRA:
 *   [0, BC250_VRAM_WC_WINDOW_BYTES)  -- scanout do GOP + segmento do VidMm.
 *       Somente WRITE-COMBINED: e' o tipo padrao que o VidMm usa para
 *       alocacoes CpuVisible (DXGK_ALLOCATIONINFOFLAGS_WDDM2_0: "by default,
 *       the allocation backing store is stored as write-combined memory").
 *   [BC250_VRAM_WC_WINDOW_BYTES, 256 MB) -- estruturas privadas do driver.
 *       Somente UNCACHED, e fora de qualquer janela WC.
 *
 * O limite e' o inicio da faixa privada: a pagina de migalhas, que a DX.67
 * reserva no inventario e que por isso corta o fim do segmento publicado
 * (Corrida 166: "segmento [0xA8C000, 0xFA00000)"). NAO VALIDADO EM HARDWARE.
 */
#define BC250_VRAM_WC_WINDOW_BYTES   0x0FA00000ull
#define BC250_VRAM_BAR0_BYTES        0x10000000ull

/* 1 se [Offset, Offset+Length) cabe inteiro na faixa privada UC. */
static __inline unsigned int Bc250VramRangeIsPrivateUc(
    unsigned long long Offset,
    unsigned long long Length)
{
    if (Length == 0ull || Offset < BC250_VRAM_WC_WINDOW_BYTES ||
        Offset >= BC250_VRAM_BAR0_BYTES ||
        Length > BC250_VRAM_BAR0_BYTES - Offset) {
        return 0u;
    }
    return 1u;
}

#endif /* BC250_VRAM_CACHE_POLICY_H */
