#ifndef BC250_GCVM_VMID8_TRANSACTION_H
#define BC250_GCVM_VMID8_TRANSACTION_H

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

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_GCVM_USER_VMID 7u
#define BC250_GCVM_VMID7_CNTL        0x0000A21Cu
#define BC250_GCVM_VMID7_PT_BASE_LO  0x0000A3E4u
#define BC250_GCVM_VMID7_PT_BASE_HI  0x0000A3E8u
#define BC250_GCVM_VMID7_PT_START_LO 0x0000A464u
#define BC250_GCVM_VMID7_PT_START_HI 0x0000A468u
#define BC250_GCVM_VMID7_PT_END_LO   0x0000A4E4u
#define BC250_GCVM_VMID7_PT_END_HI   0x0000A4E8u
#define BC250_GCVM_VMID8 8u
/* P1: docs/03, GC_BASE[0]=0x1260, gc_10_1_0_offset.h, BASE_IDX=0. */
#define BC250_GCVM_COMPUTE_VMID 9u
#define BC250_GCVM_VMID9_CNTL        0x0000A224u
#define BC250_GCVM_VMID9_PT_BASE_LO  0x0000A3F4u
#define BC250_GCVM_VMID9_PT_BASE_HI  0x0000A3F8u
#define BC250_GCVM_VMID9_PT_START_LO 0x0000A474u
#define BC250_GCVM_VMID9_PT_START_HI 0x0000A478u
#define BC250_GCVM_VMID9_PT_END_LO   0x0000A4F4u
#define BC250_GCVM_VMID9_PT_END_HI   0x0000A4F8u
#define BC250_GCVM_VMID8_CNTL        0x0000A220u
#define BC250_GCVM_VMID8_PT_BASE_LO  0x0000A3ECu
#define BC250_GCVM_VMID8_PT_BASE_HI  0x0000A3F0u
#define BC250_GCVM_VMID8_PT_START_LO 0x0000A46Cu
#define BC250_GCVM_VMID8_PT_START_HI 0x0000A470u
#define BC250_GCVM_VMID8_PT_END_LO   0x0000A4ECu
#define BC250_GCVM_VMID8_PT_END_HI   0x0000A4F0u
#define BC250_GCVM_ENABLE_CONTEXT    0x00000001u
#define BC250_GCVM_VMID8_IDENTITY    0x564D4938u /* VMI8 */

/*
 * GCVM_CONTEXT_CNTL bitfield shifts e mascaras.
 * Proveniencia: drivers/gpu/drm/amd/amdgpu/gfxhub_v2_0.c (gfxhub_v2_0_setup_vmid_config)
 * e gc_10_1_0_sh_mask.h (linhas 12666-12704).
 */
#define BC250_GCVM_CONTEXT_CNTL_ENABLE_CONTEXT_SHIFT                          0u
#define BC250_GCVM_CONTEXT_CNTL_ENABLE_CONTEXT_MASK                           0x00000001u

#define BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_DEPTH_SHIFT                        1u
#define BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_DEPTH_MASK                         0x00000006u

#define BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_BLOCK_SIZE_SHIFT                   3u
#define BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_BLOCK_SIZE_MASK                    0x00000078u

#define BC250_GCVM_CONTEXT_CNTL_RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_SHIFT  7u
#define BC250_GCVM_CONTEXT_CNTL_RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK   0x00000080u

#define BC250_GCVM_CONTEXT_CNTL_RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_SHIFT   10u
#define BC250_GCVM_CONTEXT_CNTL_RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK    0x00000400u

#define BC250_GCVM_CONTEXT_CNTL_DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_SHIFT 12u
#define BC250_GCVM_CONTEXT_CNTL_DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK  0x00001000u

#define BC250_GCVM_CONTEXT_CNTL_PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_SHIFT    14u
#define BC250_GCVM_CONTEXT_CNTL_PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK     0x00004000u

#define BC250_GCVM_CONTEXT_CNTL_VALID_PROTECTION_FAULT_ENABLE_DEFAULT_SHIFT   16u
#define BC250_GCVM_CONTEXT_CNTL_VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK    0x00010000u

#define BC250_GCVM_CONTEXT_CNTL_READ_PROTECTION_FAULT_ENABLE_DEFAULT_SHIFT    18u
#define BC250_GCVM_CONTEXT_CNTL_READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK     0x00040000u

#define BC250_GCVM_CONTEXT_CNTL_WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_SHIFT   20u
#define BC250_GCVM_CONTEXT_CNTL_WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK    0x00100000u

#define BC250_GCVM_CONTEXT_CNTL_EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_SHIFT 22u
#define BC250_GCVM_CONTEXT_CNTL_EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK  0x00400000u

/* Defaults aggregate: 0x00555400 (bits 10, 12, 14, 16, 18, 20, 22) */
#define BC250_GCVM_CONTEXT_CNTL_FAULT_DEFAULTS_MASK                           0x00555400u

/* Valor alvo pretendido para WDDM 34-bit / 2-level space: 0x00555403 */
#define BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE                                 0x00555403u

/* Valor baseline medido no POST em silicio: 0x007FFE80 */
#define BC250_GCVM_CONTEXT8_CNTL_POST_BASELINE                                0x007FFE80u

/*
 * =====================================================================
 * OBSERVABILIDADE DE FAULT -- DECLARADO, NAO USADO (07/09)
 *
 * As sete classes de fault tem DOIS bits cada: ENABLE_DEFAULT (redireciona
 * para a dummy page) e ENABLE_INTERRUPT (avisa). O nosso valor alvo liga os
 * sete DEFAULT e desliga os sete INTERRUPT -- o POST do silicio tinha os
 * quatorze ligados. Isso descreve um hardware que, ao falhar a traducao,
 * manda o acesso para a dummy page e NAO avisa ninguem.
 *
 * HIPOTESE (H) registrada em docs/167, secao da pergunta 3: e' a primeira
 * explicacao coerente com as tres medicoes do B1 ao mesmo tempo --
 * RPTR==WPTR, FAULT_STATUS zero, e destino sem os dados.
 *
 * NAO PROVADO: nao se sabe se GCVM_L2_PROTECTION_FAULT_STATUS latcha quando
 * ENABLE_DEFAULT=1 e ENABLE_INTERRUPT=0. Se latchar, a hipotese esta
 * REFUTADA pela medicao que ja temos.
 *
 * Estas constantes existem para que o valor do experimento seja definido UMA
 * vez, conferido por aritmetica no teste, e nao apareca escrito a mao numa
 * corrida. NENHUM caminho do driver as usa -- armar o experimento e' decisao
 * do dono do projeto, nao efeito colateral de compilar.
 * =====================================================================
 */
#define BC250_GCVM_CONTEXT_CNTL_FAULT_INTERRUPTS_MASK                         0x002AAA00u
#define BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE                       0x007FFE03u

/* Limites de enderecamento para espaco virtual de 34 bits (16 GiB = 4.194.304 paginas de 4 KiB) */
#define BC250_GCVM_CONTEXT8_PT_START_VALUE                                    0x00000000u
#define BC250_GCVM_CONTEXT8_PT_END_LO_VALUE                                   0x003FFFFFu
#define BC250_GCVM_CONTEXT8_PT_END_HI_VALUE                                   0x00000000u

typedef enum BC250_GCVM_VMID8_STATUS {
    BC250_GCVM_VMID8_OK = 0,
    BC250_GCVM_VMID8_BAD_ARGUMENT,
    BC250_GCVM_VMID8_CONTEXT_ENABLED,
    BC250_GCVM_VMID8_READ_FAILED,
    BC250_GCVM_VMID8_WRITE_FAILED,
    BC250_GCVM_VMID8_READBACK_FAILED,
    BC250_GCVM_VMID8_TLB_FAILED,
    BC250_GCVM_VMID8_RESTORE_FAILED
} BC250_GCVM_VMID8_STATUS;

typedef uint32_t (*BC250_GCVM_VMID8_READ32)(
    void *Context, uint32_t ByteOffset, uint32_t *Value);
typedef uint32_t (*BC250_GCVM_VMID8_WRITE32)(
    void *Context, uint32_t ByteOffset, uint32_t Value);
typedef uint32_t (*BC250_GCVM_VMID8_FLUSH)(void *Context);

typedef struct BC250_GCVM_VMID8_INTERFACE {
    BC250_GCVM_VMID8_READ32 Read32;
    BC250_GCVM_VMID8_WRITE32 Write32;
    BC250_GCVM_VMID8_FLUSH Flush;
    void *Context;
    uint32_t Identity;
} BC250_GCVM_VMID8_INTERFACE;

typedef struct BC250_GCVM_VMID8_EVIDENCE {
    uint64_t PageTablePhysical;
    uint32_t ContextControlBefore;
    uint32_t BaseLowBefore;
    uint32_t BaseHighBefore;
    uint32_t StartLowBefore;
    uint32_t StartHighBefore;
    uint32_t EndLowBefore;
    uint32_t EndHighBefore;
    uint32_t ProgramReadbackPassed;
    uint32_t TlbFlushPassed;
    uint32_t RestorePassed;
    uint32_t PhysicalReady;
} BC250_GCVM_VMID8_EVIDENCE;

typedef struct BC250_GCVM_VMID8_INSTALL_EVIDENCE {
    uint64_t PageTablePhysical;
    uint32_t BaseLowProgrammed;
    uint32_t BaseHighProgrammed;
    uint32_t StartLowProgrammed;
    uint32_t StartHighProgrammed;
    uint32_t EndLowProgrammed;
    uint32_t EndHighProgrammed;
    uint32_t ContextControlProgrammed;
    uint32_t ProgramReadbackPassed;
    uint32_t TlbFlushPassed;
    uint32_t PhysicalReady;
    uint32_t FailureStep;
    uint32_t FailureRegister;
    uint32_t FailureValue;
} BC250_GCVM_VMID8_INSTALL_EVIDENCE;

/* Caller serializes all users and proves the previous GPU work retired.
 * Flush callback MUST invalidate VMID9 and observe its ACK. */
typedef struct BC250_GCVM_COMPUTE_BINDING {
    uint32_t Before[7];
    uint32_t SnapshotValid;
    uint32_t Ready;
    uint64_t Root;
} BC250_GCVM_COMPUTE_BINDING;

static __inline uint32_t Bc250GcvmIsUserRegister(uint32_t Offset)
{
    return Offset == BC250_GCVM_VMID7_CNTL ||
        Offset == BC250_GCVM_VMID7_PT_BASE_LO || Offset == BC250_GCVM_VMID7_PT_BASE_HI ||
        Offset == BC250_GCVM_VMID7_PT_START_LO || Offset == BC250_GCVM_VMID7_PT_START_HI ||
        Offset == BC250_GCVM_VMID7_PT_END_LO || Offset == BC250_GCVM_VMID7_PT_END_HI;
}

static __inline uint32_t Bc250GcvmIsComputeRegister(uint32_t Offset)
{
    return Offset == BC250_GCVM_VMID9_CNTL ||
        Offset == BC250_GCVM_VMID9_PT_BASE_LO || Offset == BC250_GCVM_VMID9_PT_BASE_HI ||
        Offset == BC250_GCVM_VMID9_PT_START_LO || Offset == BC250_GCVM_VMID9_PT_START_HI ||
        Offset == BC250_GCVM_VMID9_PT_END_LO || Offset == BC250_GCVM_VMID9_PT_END_HI;
}
uint32_t Bc250GcvmComputeCanSwitch(uint64_t Submitted, uint64_t Retired);
BC250_GCVM_VMID8_STATUS Bc250GcvmComputeInstall(
    uint64_t Root, uint64_t Submitted, uint64_t Retired,
    const BC250_GCVM_VMID8_INTERFACE *Io, BC250_GCVM_COMPUTE_BINDING *Binding);
BC250_GCVM_VMID8_STATUS Bc250GcvmComputeRestore(
    const BC250_GCVM_VMID8_INTERFACE *Io, BC250_GCVM_COMPUTE_BINDING *Binding);

uint32_t Bc250GcvmBuildVmidCntl(
    uint32_t EnableContext,
    uint32_t PageTableLevels,
    uint32_t LeafIndexBits);

BC250_GCVM_VMID8_STATUS Bc250RunGcvmVmid8InertTransaction(
    uint64_t PageTablePhysical,
    const BC250_GCVM_VMID8_INTERFACE *Interface,
    BC250_GCVM_VMID8_EVIDENCE *Evidence);

BC250_GCVM_VMID8_STATUS Bc250GcvmInstallVmid(
    uint32_t Vmid,
    uint64_t PageTablePhysical,
    const BC250_GCVM_VMID8_INTERFACE *Interface,
    BC250_GCVM_VMID8_INSTALL_EVIDENCE *Evidence);

BC250_GCVM_VMID8_STATUS Bc250GcvmVmid8Install(
    uint64_t PageTablePhysical,
    const BC250_GCVM_VMID8_INTERFACE *Interface,
    BC250_GCVM_VMID8_INSTALL_EVIDENCE *Evidence);

BC250_GCVM_VMID8_STATUS Bc250GcvmVmid8Uninstall(
    const BC250_GCVM_VMID8_INTERFACE *Interface,
    BC250_GCVM_VMID8_INSTALL_EVIDENCE *Evidence);

#ifdef __cplusplus
}
#endif

#endif
