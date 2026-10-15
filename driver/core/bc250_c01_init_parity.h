#ifndef BC250_C01_INIT_PARITY_H
#define BC250_C01_INIT_PARITY_H

/*
 * C01 (doc 764): estado de init que o amdgpu deixa nesta placa e que o nosso
 * KMD nunca escrevia. Fonte da lista: o rastro de 10.018 escritas MMIO do
 * amdgpu no BC-250 (Nagi, referencias/amdgpu-wddm/evidence/linux/
 * 2026-09-21-E03-init-trace, comparado pelo tools/c01-comparar-rastro-e03.py
 * com o sweep antes/depois do driver) e o codigo de inc/linux_ref/gfx_v10_0.c.
 * Offsets pelo gc_10_1_0_offset.h (BASE_IDX 0 -> (0x1260 + mm) * 4), conferidos
 * pelo regcalc do Nagi.
 *
 * 1. SH_MEM_CONFIG por VMID (0..15) = 0x0000C00C: DEFAULT_SH_MEM_CONFIG de
 *    gfx_v10_0_constants_init (ALIGNMENT_MODE=UNALIGNED, INITIAL_INST_PREFETCH=3).
 *    POST: 0. SH_MEM_BASES por VMID (1..15): 0x20001000 nas VMIDs de grafico
 *    (1..7, aperturas privada 0x1000 e compartilhada 0x2000 << 48) e 0x60006000
 *    nas de compute (8..15, gfx_v10_0_init_compute_vmid); a VMID 0 nao recebe.
 *    Selecao por GRBM_GFX_CNTL.VMID (bits 7:4), como nv_grbm_select.
 * 2. SDMA0/SDMA1 PHASE0/1/2_QUANTUM = 0x2000 (sdma_v5_0_ctx_switch_enable).
 *    POST: 0x00010002.
 * 3. GCVM_INVALIDATE_ENGn_ADDR_RANGE_LO32/HI32 = 0xFFFFFFFF/0x1F para n=2..16
 *    (gfxhub_v2_0_program_invalidation faz 0..17; 0, 1 e 17 ja' tem escritor
 *    proprio no KMD: MMU global e Bc250ActiveArmSdmaInvalidateEngine).
 *
 * Fora, declarado, e de proposito: GDS_VMIDn_BASE/SIZE, GDS_GWS_VMIDn e
 * GDS_OA_VMIDn. O amdgpu zera as VMIDs 1..15 (gfx_v10_0_init_gds_vmid) porque
 * reprograma o GDS por tarefa no anel (gfx_v10_0_ring_emit_gds_switch) quando
 * a tarefa pede GDS. Nos nao temos essa troca por tarefa; o POST deixa 64 KB
 * (SIZE=0x10000) em todas as VMIDs e a RADV do GFX10 usa GDS (NGG streamout e
 * consultas). Zerar sem a troca por tarefa tiraria o GDS da RADV.
 * Fora tambem: as filas RLC0..7/PAGE do SDMA (WPTR_POLL_CNTL, filas nao
 * usadas), SPI_GDBG_TRAP_* (depurador do KFD), doorbell self-ring do BIF (KFD),
 * as faixas dos engines do MMHUB 1..16 (o MMHUB tem escritor unico na P3.0b) e
 * os contextos 1..15 do MMHUB (nao usamos VMID de usuario no MMHUB).
 */

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stdint.h>
#endif

#define BC250_C01_REG_GRBM_GFX_CNTL        0x00008088u   /* mm 0x0DC2 */
#define BC250_C01_GRBM_GFX_CNTL_VMID_SHIFT 4u
#define BC250_C01_REG_SH_MEM_BASES         0x00008C28u   /* mm 0x10AA */
#define BC250_C01_REG_SH_MEM_CONFIG        0x00008C34u   /* mm 0x10AD */
#define BC250_C01_SH_MEM_CONFIG_VALUE      0x0000C00Cu
#define BC250_C01_SH_MEM_BASES_GFX         0x20001000u
#define BC250_C01_SH_MEM_BASES_COMPUTE     0x60006000u
#define BC250_C01_FIRST_COMPUTE_VMID       8u
#define BC250_C01_VMID_COUNT               16u

#define BC250_C01_REG_GCVM_INV_ENG0_RANGE_LO 0x0000A31Cu /* mm 0x1667, passo 2 dw */

#define BC250_C01_PLAIN_SDMA    6u
#define BC250_C01_PLAIN_ENG     (15u * 2u)
#define BC250_C01_PLAIN_COUNT   (BC250_C01_PLAIN_SDMA + BC250_C01_PLAIN_ENG)

typedef struct BC250_C01_PLAIN_ENTRY {
    uint32_t BarOffset;
    uint32_t Value;
} BC250_C01_PLAIN_ENTRY;

static const uint32_t g_Bc250C01SdmaQuantum[BC250_C01_PLAIN_SDMA] = {
    0x00004A30u, 0x00004A34u, 0x00004ABCu,   /* SDMA0_PHASE0/1/2_QUANTUM */
    0x00006230u, 0x00006234u, 0x000062BCu    /* SDMA1_PHASE0/1/2_QUANTUM */
};

/* Lista plana na ordem do rastro (SDMA, depois engines). */
static __inline uint32_t Bc250C01BuildPlain(BC250_C01_PLAIN_ENTRY *Out, uint32_t Capacity)
{
    uint32_t n = 0u, i;
    if (Out == 0 || Capacity < BC250_C01_PLAIN_COUNT) return 0u;
    for (i = 0u; i < BC250_C01_PLAIN_SDMA; ++i) {
        Out[n].BarOffset = g_Bc250C01SdmaQuantum[i]; Out[n++].Value = 0x00002000u;
    }
    for (i = 2u; i <= 16u; ++i) {
        Out[n].BarOffset = BC250_C01_REG_GCVM_INV_ENG0_RANGE_LO + 8u * i;      Out[n++].Value = 0xFFFFFFFFu;
        Out[n].BarOffset = BC250_C01_REG_GCVM_INV_ENG0_RANGE_LO + 8u * i + 4u; Out[n++].Value = 0x0000001Fu;
    }
    return n;
}

static __inline uint32_t Bc250C01ShMemBasesFor(uint32_t Vmid)
{
    return Vmid < BC250_C01_FIRST_COMPUTE_VMID ? BC250_C01_SH_MEM_BASES_GFX : BC250_C01_SH_MEM_BASES_COMPUTE;
}

/* Allowlist de ESCRITA do C01: so' os offsets acima (GRBM_GFX_CNTL ja' tem
 * a sua). Nada de faixa aberta: cada offset e' conferido. */
static __inline uint32_t Bc250C01InitParityWriteAllowed(uint32_t ByteOffset)
{
    uint32_t i;
    if (ByteOffset == BC250_C01_REG_SH_MEM_CONFIG || ByteOffset == BC250_C01_REG_SH_MEM_BASES) return 1u;
    for (i = 0u; i < BC250_C01_PLAIN_SDMA; ++i) {
        if (ByteOffset == g_Bc250C01SdmaQuantum[i]) return 1u;
    }
    if (ByteOffset >= BC250_C01_REG_GCVM_INV_ENG0_RANGE_LO + 8u * 2u &&
        ByteOffset <= BC250_C01_REG_GCVM_INV_ENG0_RANGE_LO + 8u * 16u + 4u && (ByteOffset & 3u) == 0u) return 1u;
    return 0u;
}

#endif
