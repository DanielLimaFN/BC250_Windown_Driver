#ifndef BC250_GCVM_CONTRACT_H
#define BC250_GCVM_CONTRACT_H

#include "bc250_vram_allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_PAGE_SIZE_4K 0x1000ull
#define BC250_PAGE_MASK_4K (BC250_PAGE_SIZE_4K - 1ull)

/*
 * Base da VRAM que aparece nas ENTRADAS de page table e no ponteiro de root do
 * VMID8. Fonte unica, para que as duas nao possam divergir.
 *
 * E a base PA -- GCMC_VM_FB_OFFSET << 24 --, nao a base MC de
 * GCMC_VM_FB_LOCATION_BASE. Sao dominios diferentes: o walker do GCVM opera em
 * PA, enquanto anel e blts fisicos operam em MC (0xF400000000). O catalogo ja
 * documentava isso em docs/03_CATALOGO_DE_REGISTRADORES.md linha 640:
 *
 *     PA = offset_de_VRAM + (GCMC_VM_FB_OFFSET << 24)
 *
 * A corrida de 05/09 01:29 trocou esta base para a MC por causa da prova byte
 * a byte dos blts -- prova que vale para acesso FISICO, nao para traducao. Foi
 * um erro de dominio e nao mudou nada, porque o defeito real estava nas flags
 * do PDE (ver Bc250GcvmEncodePde). Revertido.
 */
#define BC250_GCVM_VRAM_MC_BASE_FALLBACK 0x0000000450000000ull

#define BC250_PTE_VALID      0x00000001u
#define BC250_PTE_SYSTEM     0x00000002u
#define BC250_PTE_SNOOPED    0x00000004u
#define BC250_PTE_TMZ        0x00000008u
/*
 * Layout de PTE do Navi 10 (gmc_v10_0.c e amdgpu_vm.h):
 *   11:7 fragment | 6 write | 5 read | 4 exe | 3 Z | 2 snooped | 1 system | 0 valid
 *
 * EXECUTABLE e o bit 4, NAO o bit 7. Ate 05/09 este contrato o definia como
 * 0x80 e o codificava em (1ull << 7) -- ou seja, ligava FRAG[0] e deixava a
 * permissao de execucao SEMPRE DESLIGADA. A PTE do IB de paginacao saia como
 * 0xE7 (valid|system|snooped|read|write|FRAG[0]) em vez de 0x77.
 *
 * A busca de um indirect buffer e leitura de INSTRUCAO: sem o bit 4 a engine
 * nao tem permissao para busca-lo. E o defeito ficava invisivel porque
 * Bc250GcvmDecodePte reportava "EXECUTABLE" lendo o mesmo bit 7 errado --
 * exatamente o mesmo tipo de cegueira do decodificador de PDE.
 */
#define BC250_PTE_EXECUTABLE 0x00000010u
#define BC250_PTE_READABLE   0x00000020u
#define BC250_PTE_WRITEABLE  0x00000040u
#define BC250_PTE_PRT        0x00000100u
/*
 * P6 (doc 618): MTYPE_UC na folha. Flag de SOFTWARE deste contrato (nao e' bit
 * de hardware); Bc250GcvmEncodePte a traduz para MTYPE = 3 nos bits 50:48
 * (P1: AMDGPU_PTE_MTYPE_NV10(MTYPE_UC), gmc_v10_0_map_mtype). Sem ela a PTE
 * sai com MTYPE = 0 (NC), o default do amdgpu. O PDE a recusa (so' VALID/
 * SYSTEM/SNOOPED).
 */
#define BC250_PTE_MTYPE_UC   0x00000200u

#define BC250_PTE_RW_DEFAULT \
    (BC250_PTE_VALID | BC250_PTE_READABLE | BC250_PTE_WRITEABLE)

#define BC250_PTE_RX_DEFAULT \
    (BC250_PTE_VALID | BC250_PTE_READABLE | BC250_PTE_EXECUTABLE)

#define BC250_PTE_RWX_DEFAULT \
    (BC250_PTE_VALID | BC250_PTE_READABLE | BC250_PTE_WRITEABLE | \
     BC250_PTE_EXECUTABLE)

#define BC250_GCVM_HUB_GFXHUB 0u
#define BC250_GCVM_HUB_MMHUB  1u

#define BC250_GCVM_ENGINE_GFX  0u
#define BC250_GCVM_ENGINE_SDMA 17u

typedef enum BC250_GCVM_STATUS {
    BC250_GCVM_OK = 0,
    BC250_GCVM_BAD_ARGUMENT,
    BC250_GCVM_MISALIGNED,
    BC250_GCVM_RANGE_OVERFLOW,
    BC250_GCVM_OUT_OF_RANGE,
    BC250_GCVM_PAGE_COUNT_ZERO,
    BC250_GCVM_BUFFER_TOO_SMALL,
    BC250_GCVM_TRANSLATION_FAULT
} BC250_GCVM_STATUS;

typedef struct BC250_GCVM_PAGE_TABLE_PLAN {
    uint64_t VaStart;
    uint64_t VaEnd;
    uint64_t PhysicalStart;
    uint64_t Length;
    uint32_t PageCount;
    uint32_t TableSizeBytes;
    uint32_t TableAlignment;
    uint32_t PteFlags;
    BC250_VRAM_TOKEN TableToken;
    uint64_t TableVramOffset;
} BC250_GCVM_PAGE_TABLE_PLAN;

typedef struct BC250_GCVM_TLB_INVALIDATION_PLAN {
    uint32_t Hub;
    uint32_t EngineId;
    uint32_t VmId;
    uint32_t FlushType;
} BC250_GCVM_TLB_INVALIDATION_PLAN;

BC250_GCVM_STATUS Bc250GcvmEncodePte(
    uint64_t PhysicalAddress,
    uint32_t Flags,
    uint64_t *PteOut);

BC250_GCVM_STATUS Bc250GcvmDecodePte(
    uint64_t Pte,
    uint64_t *PhysicalAddressOut,
    uint32_t *FlagsOut);

/*
 * PDE (entrada de DIRETORIO) NAO tem o mesmo layout de um PTE (entrada FOLHA).
 * Formato do Navi 10, comentario de gmc_v10_0.c no amdgpu:
 *
 *   PTE:  47:12 endereco da pagina de 4 KiB | 6 write | 5 read | 4 exe
 *         3 Z | 2 snooped | 1 system | 0 valid
 *   PDE:  63:59 block fragment size | 54 P
 *         47:6 endereco fisico da PD ou PTE   <-- comeca no bit 6
 *         5:3 RESERVADO                       <-- precisa ser zero
 *         2 C | 1 system | 0 valid
 *
 * Consequencia medida na corrida de 05/09 01:29: gravavamos o PDE com
 * VALID|READABLE|WRITEABLE = 0x61. O bit 6 nao e "writeable" num PDE -- e o
 * BIT MENOS SIGNIFICATIVO DO ENDERECO. O hardware leu a tabela folha 64 bytes
 * adiante (8 PTEs deslocadas), e o bit 5 caiu em campo reservado.
 *
 * Pior: Bc250GcvmDecodePte mascara com 4 KiB, entao a caminhada pela CPU
 * descartava justamente esses bits e reportava o PDE como limpo. O falso
 * positivo durou seis corridas. Por isso PDE tem par proprio de encode/decode.
 */
BC250_GCVM_STATUS Bc250GcvmEncodePde(
    uint64_t PhysicalAddress,
    uint32_t Flags,
    uint64_t *PdeOut);

/*
 * ReservedViolationOut recebe os bits [5:3] tal como estao na entrada. Nao
 * zero significa PDE malformado -- a GPU vai caminhar endereco errado.
 */
BC250_GCVM_STATUS Bc250GcvmDecodePde(
    uint64_t Pde,
    uint64_t *PhysicalAddressOut,
    uint32_t *FlagsOut,
    uint32_t *ReservedViolationOut);

BC250_GCVM_STATUS Bc250GcvmBuildPageTablePlan(
    uint64_t VaStart,
    uint64_t PhysicalStart,
    uint64_t Length,
    uint32_t PteFlags,
    BC250_GCVM_PAGE_TABLE_PLAN *Plan);

BC250_GCVM_STATUS Bc250GcvmPopulatePageTable(
    const BC250_GCVM_PAGE_TABLE_PLAN *Plan,
    uint64_t *PteBuffer,
    size_t BufferBytes);

BC250_GCVM_STATUS Bc250GcvmTranslateVa(
    const BC250_GCVM_PAGE_TABLE_PLAN *Plan,
    const uint64_t *PteBuffer,
    uint64_t Va,
    uint64_t *PhysicalAddressOut);

const char *Bc250GcvmStatusName(BC250_GCVM_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
