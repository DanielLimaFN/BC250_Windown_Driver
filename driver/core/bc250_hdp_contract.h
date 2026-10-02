#ifndef BC250_HDP_CONTRACT_H
#define BC250_HDP_CONTRACT_H

/*
 * HDP.1 / T1 (28/09/2026) -- FONTE UNICA dos enderecos de coerencia do HDP.
 *
 * O DEFEITO QUE ESTE ARQUIVO FECHA (P1, leitura de fonte):
 *
 * Ate a 3.310 o driver chamava 0x4A80 de "HDP_FLUSH" e escrevia 0 nele em
 * todo laco de espera do SDMA, em toda publicacao do MEC e no anel grafico.
 * 0x4A80 NAO e' registrador de HDP: e' o dword 0x40 do bloco SDMA0 --
 * mmSDMA0_UTCL1_INV0 (inc/linux_ref/gc_10_1_0_offset.h), base GC seg0
 * 0x1260 dw = 0x4980 bytes, a MESMA conta que da' UTCL1_CNTL=0x4A70,
 * F32_CNTL=0x4A28 e STATUS2=0x4A60 neste driver. dbg/hw.c:791 ja' o chamava
 * de A_SDMA0_UTCL1_INV0. Os bits que eram zerados incluem CPF_INVREQ_EN e
 * GPUVM_INVREQ_EN (gc_10_1_0_sh_mask.h); o upstream NUNCA escreve este
 * registrador (sdma_v5_0.c, gfx_v10_0.c, gmc_v10_0.c, gfxhub_v2_0.c).
 * O efeito de zera-lo no silicio e' HIPOTESE (H).
 *
 * E o flush de escrita do HDP nunca era feito: 0x3FC4 e' a INVALIDACAO de
 * leitura (mmHDP_READ_CACHE_INVALIDATE = 0xD1, HDP seg0 0xF20), e o nome
 * "HDP_MEM_COHERENCY_FLUSH_CNTL" colado nele em quatro arquivos era falso.
 *
 * PROVENIENCIA DO FLUSH REAL (Lei 7):
 *   - nbio_2_3_offset.h: mmBIF_BX_PF_HDP_MEM_COHERENCY_FLUSH_CNTL = 0x00f7,
 *     BASE_IDX 2; mmRCC_DEV0_EPF0_RCC_CONFIG_MEMSIZE = 0x00c3, BASE_IDX 2.
 *   - cyan_skillfish_ip_offset.h (a tabela DO CHIP): NBIO seg2 = 0x0D20 dw.
 *     (0xD20 + 0xF7) * 4 = 0x385C;  (0xD20 + 0xC3) * 4 = 0x378C.
 *   - amdgpu_discovery.c: Cyan Skillfish usa nbio_v2_3_funcs e hdp_v5_0_funcs.
 *   - nbio_v2_3.c (Linux v5.3/v5.4, Navi10 em bare metal):
 *       WREG32_SOC15_NO_KIQ(NBIO, 0, mmBIF_BX_PF_HDP_MEM_COHERENCY_FLUSH_CNTL, 0);
 *     O Linux atual escreve o mesmo registrador por uma pagina remapeada
 *     (rmmio_remap) e, sob SR-IOV, pelo endereco nativo acima.
 *   - amdgpu_hdp.c (amdgpu_hdp_generic_flush): escrita 0 seguida de LEITURA
 *     de RCC_CONFIG_MEMSIZE (get_memsize) como confirmacao de chegada.
 *   - amdgpu_vm_cpu.c (amdgpu_vm_cpu_commit): mb() e flush do HDP ao fim de
 *     TODA atualizacao de page table pela CPU, antes de qualquer uso.
 *   - A base NBIO seg2 esta validada no silicio (P0): este driver ja' le
 *     0x378C (BC250_RCC_CONFIG_MEMSIZE_BYTE_OFFSET) e recebe 512 MiB,
 *     coerente com FB_LOCATION 0xF400..0xF41F.
 *
 * A escrita em 0x385C NUNCA RODOU neste projeto: NAO VALIDADO EM HARDWARE.
 */

/* Flush de ESCRITA do HDP: escrever 0 e confirmar pela leitura abaixo. */
#define BC250_HDP_REG_MEM_COHERENCY_FLUSH_CNTL  0x0000385Cu
#define BC250_HDP_MEM_FLUSH_VALUE               0x00000000u

/* Leitura de confirmacao (o get_memsize do upstream). */
#define BC250_HDP_REG_FLUSH_POSTING_READ        0x0000378Cu

/* INVALIDACAO de leitura do HDP (CPU vai ler o que a GPU escreveu). */
#define BC250_HDP_REG_READ_CACHE_INVALIDATE     0x00003FC4u

/*
 * SDMA0_UTCL1_INV0: SOMENTE LEITURA para este driver. Existe aqui para que o
 * endereco tenha UM nome verdadeiro e para a sonda de valor da HDP.1.
 */
#define BC250_SDMA0_REG_UTCL1_INV0              0x00004A80u

#endif /* BC250_HDP_CONTRACT_H */
