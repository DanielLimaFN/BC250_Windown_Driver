#ifndef BC250_GPUVA_GEOMETRY_H
#define BC250_GPUVA_GEOMETRY_H

/*
 * =========================================================================
 * Geometria da tabela de paginas da GPU (GCVM, contextos 1..15) -- FONTE UNICA.
 *
 * So' macros, sem tipos: e' incluido por bc250_gpuva_space_contract.h (o que o
 * driver declara ao dxgkrnl) e por bc250_gcvm_vmid8_transaction.h (o que o
 * driver programa no silicio). As duas coisas saem das MESMAS constantes, para
 * que GPUMMUCAPS/PAGETABLELEVELDESC e GCVM_CONTEXTn_CNTL/END nunca divirjam.
 *
 * P5 (doc 667, Corrida 252): 4 niveis de 9 bits e VA de 48 bits, como o
 * amdgpu neste chip. PROVENIENCIA (Lei 7, P1):
 *   inc/linux_ref/gmc_v10_0.c:934  amdgpu_vm_adjust_size(adev, 256*1024, 9, 3, 48)
 *       -> 256 TB = 48 bits, bloco de 9 bits, num_level 3 (PDB2+PDB1+PDB0+PTB)
 *   inc/linux_ref/gfxhub_v2_0.c:290-317 (gfxhub_v2_0_setup_vmid_config)
 *       -> PAGE_TABLE_DEPTH = num_level, PAGE_TABLE_BLOCK_SIZE = block_size-9,
 *          START = 0, END = max_pfn - 1
 *   doc 552: dmesg do amdgpu nesta placa, "vm size is 262144 GB, 4 levels,
 *       block size is 9-bit" (P0 do lado Linux).
 *   gmc_v10_0_get_vm_pde: sem translate_further no gfx10 (o campo so' e'
 *       ligado no gmc_v9), entao o PDE nao leva BFS nem TF em nenhum nivel --
 *       o encoder de PDE atual vale para os niveis 1..3 sem mudanca.
 *
 * BC250_P5_QUATRO_NIVEIS = 0 volta a geometria anterior (2 niveis, raiz de
 * 13 bits, VA de 34 bits, Corridas ate a 251b), sem nenhuma outra mudanca.
 *
 * Indexacao (nivel 0 = folha/PTB, nivel LEVEL_COUNT-1 = raiz):
 *   indice do nivel L = (VA >> (12 + 9*L)) & ((1 << bits(L)) - 1)
 *   bits(L) = ROOT_INDEX_BITS na raiz, LEAF_INDEX_BITS nos demais (9).
 * 4 niveis: PTB VA[20:12], PDB0 VA[29:21], PDB1 VA[38:30], PDB2 VA[47:39].
 * 2 niveis: folha VA[20:12], raiz VA[33:21].
 * =========================================================================
 */
#ifndef BC250_P5_QUATRO_NIVEIS
#define BC250_P5_QUATRO_NIVEIS 1
#endif

#if BC250_P5_QUATRO_NIVEIS
#define BC250_GPUVA_PT_LEVEL_COUNT     4u
#define BC250_GPUVA_PT_LEAF_INDEX_BITS 9u
#define BC250_GPUVA_PT_ROOT_INDEX_BITS 9u
#else
#define BC250_GPUVA_PT_LEVEL_COUNT     2u
#define BC250_GPUVA_PT_LEAF_INDEX_BITS 9u
#define BC250_GPUVA_PT_ROOT_INDEX_BITS 13u
#endif

/* Bits de indice e deslocamento de cada nivel (0 = folha). */
#define BC250_GPUVA_PT_LEVEL_INDEX_BITS(level) \
    ((((level) + 1u) == BC250_GPUVA_PT_LEVEL_COUNT) ? \
        BC250_GPUVA_PT_ROOT_INDEX_BITS : BC250_GPUVA_PT_LEAF_INDEX_BITS)
#define BC250_GPUVA_PT_LEVEL_SHIFT(level) \
    (12u + BC250_GPUVA_PT_LEAF_INDEX_BITS * (level))

/* VA = 12 de offset + 9 por nivel abaixo da raiz + bits da raiz. */
#define BC250_GPUVA_PT_VA_BITS_DERIVED \
    (12u + BC250_GPUVA_PT_LEAF_INDEX_BITS * (BC250_GPUVA_PT_LEVEL_COUNT - 1u) + \
     BC250_GPUVA_PT_ROOT_INDEX_BITS)

/* Programacao do GCVM derivada da mesma geometria. */
#define BC250_GPUVA_PT_GCVM_DEPTH      (BC250_GPUVA_PT_LEVEL_COUNT - 1u)
#define BC250_GPUVA_PT_GCVM_BLOCK_SIZE (BC250_GPUVA_PT_LEAF_INDEX_BITS - 9u)
/* END = max_pfn - 1, em paginas de 4 KiB. */
#define BC250_GPUVA_PT_GCVM_END_PFN \
    ((1ull << (BC250_GPUVA_PT_VA_BITS_DERIVED - 12u)) - 1ull)

#endif /* BC250_GPUVA_GEOMETRY_H */
