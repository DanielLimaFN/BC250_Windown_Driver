#ifndef BC250_GCVM_GLOBAL_MMU_H
#define BC250_GCVM_GLOBAL_MMU_H

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

#define BC250_GCVM_GLOBAL_MMU_REG_COUNT 26u
#define BC250_GCVM_GLOBAL_MMU_IDENTITY  0x474D4D55u /* GMMU */

/*
 * Offsets dos registradores globais do GFXHUB MMU.
 * Proveniencia: dbg/hw.c (provado no silicio em 07/09/2026),
 * docs/03_CATALOGO_DE_REGISTRADORES.md e gc_10_1_0_sh_mask.h.
 */
#define BC250_REG_GCMC_VM_AGP_BASE                   0x0000A610u
#define BC250_REG_GCMC_VM_AGP_BOT                    0x0000A60Cu
#define BC250_REG_GCMC_VM_AGP_TOP                    0x0000A608u
#define BC250_REG_GCMC_VM_FB_LOCATION_BASE           0x0000A600u
#define BC250_REG_GCMC_VM_FB_LOCATION_TOP            0x0000A604u
#define BC250_REG_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR   0x0000A614u
#define BC250_REG_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR  0x0000A618u
#define BC250_REG_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB 0x0000A5B0u
#define BC250_REG_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB 0x0000A5B4u
#define BC250_REG_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_LO 0x0000A13Cu
#define BC250_REG_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_HI 0x0000A140u
#define BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL2     0x0000A124u

#define BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO  0x0000A148u
#define BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI  0x0000A14Cu
#define BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO 0x0000A150u
#define BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI 0x0000A154u
#define BC250_REG_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO     0x0000A158u
#define BC250_REG_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI     0x0000A15Cu

#define BC250_REG_GCVM_INVALIDATE_ENG0_ADDR_RANGE_LO 0x0000A31Cu
#define BC250_REG_GCVM_INVALIDATE_ENG0_ADDR_RANGE_HI 0x0000A320u
#define BC250_REG_GCVM_INVALIDATE_ENG17_ADDR_RANGE_LO 0x0000A3A4u
#define BC250_REG_GCVM_INVALIDATE_ENG17_ADDR_RANGE_HI 0x0000A3A8u

/*
 * 4c (doc 567): engine 1, o do SDMA0 (o Linux da um engine por anel,
 * amdgpu_gmc_allocate_vm_inv_eng). Quem ESCREVE REQ e' o proprio SDMA, por
 * SRBM_WRITE; a CPU so' le REQ/ACK como prova e programa a faixa, como
 * gfxhub_v2_0_program_invalidation faz para os 18 engines.
 *   REQ   = (0x1260+0x1643+1)*4 = 0xA290   ACK = (0x1260+0x1655+1)*4 = 0xA2D8
 *   RANGE = 0xA31C + 8*1 = 0xA324 / 0xA328
 */
#define BC250_REG_GCVM_INVALIDATE_ENG1_REQ           0x0000A290u
#define BC250_REG_GCVM_INVALIDATE_ENG1_ACK           0x0000A2D8u
#define BC250_REG_GCVM_INVALIDATE_ENG1_ADDR_RANGE_LO 0x0000A324u
#define BC250_REG_GCVM_INVALIDATE_ENG1_ADDR_RANGE_HI 0x0000A328u

#define BC250_REG_GCMC_VM_MX_L1_TLB_CNTL             0x0000A61Cu

#define BC250_REG_GCVM_L2_CNTL                       0x0000A100u
#define BC250_REG_GCVM_L2_CNTL2                      0x0000A104u
#define BC250_REG_GCVM_L2_CNTL3                      0x0000A108u
#define BC250_REG_GCVM_L2_CNTL4                      0x0000A160u
#define BC250_REG_GCVM_L2_CNTL5                      0x0000A184u

/*
 * Valores alvo medidos e provados no silicio em 07/09/2026:
 * - MX_L1_TLB_CNTL: 0x00001D59 (SYSTEM_ACCESS_MODE=3, ENABLE_ADVANCED_DRIVER_MODEL=1)
 * - L2_CNTL: 0x00080E01 (L2 ligada, fragmentos desligados, default page out)
 * - L2_CNTL2: triggers 0x00000003 (INVALIDATE_ALL_L1_TLBS | INVALIDATE_L2_CACHE)
 * - L2_CNTL3: 0x80130009 (BANK_SELECT=9, BIGK=6)
 * - L2_CNTL4: 0x00000001 (VMC_TAP_PDE/PTE_REQUEST_PHYSICAL=0)
 * - L2_CNTL5: 0x00003FE0
 */
#define BC250_GCVM_GLOBAL_TLB_CNTL_TARGET            0x00001D59u
#define BC250_GCVM_GLOBAL_L2_CNTL_TARGET             0x00080E01u
#define BC250_GCVM_GLOBAL_L2_CNTL2_TRIGGERS          0x00000003u
#define BC250_GCVM_GLOBAL_L2_CNTL3_TARGET            0x80130009u
#define BC250_GCVM_GLOBAL_L2_CNTL4_TARGET            0x00000001u
#define BC250_GCVM_GLOBAL_L2_CNTL5_TARGET            0x00003FE0u
#define BC250_GCVM_GLOBAL_FAULT_CNTL2_RETRY_BIT      0x00040000u

typedef enum BC250_GCVM_GLOBAL_MMU_STATUS {
    BC250_GCVM_GLOBAL_MMU_OK = 0,
    BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT,
    BC250_GCVM_GLOBAL_MMU_SNAPSHOT_FAILED,
    BC250_GCVM_GLOBAL_MMU_WRITE_FAILED,
    BC250_GCVM_GLOBAL_MMU_READBACK_FAILED,
    BC250_GCVM_GLOBAL_MMU_TLB_FAILED,
    BC250_GCVM_GLOBAL_MMU_RESTORE_FAILED
} BC250_GCVM_GLOBAL_MMU_STATUS;

/*
 * D2 (doc 583): abertura AGP para o anel e o Work do SDMA0 em memoria de
 * sistema. Geometria do amdgpu_gmc_agp_location (amdgpu_gmc.c:358, P1),
 * ramo "depois da FB": inicio = ALIGN(fb_end + 1, 16 GB). A traducao e'
 * MC = agp_start + PA (amdgpu_gmc_agp_addr, amdgpu_gmc.c:188) com AGP_BASE = 0,
 * e a abertura de sistema passa a cobrir min/max de FB e AGP
 * (gfxhub_v2_0_init_system_aperture_regs, inc/linux_ref/gfxhub_v2_0.c:157-165).
 * NOTA (P0, coleta L-B): no BC-250 o Linux NAO liga a AGP (amdgpu_agp=-1 no
 * gmc_v10) e usa GART de 512 MB na VMID 0. A AGP e' escolha nossa, menor que a
 * GART, para mover so' o lugar onde a CPU consulta o anel e as fences.
 */
#define BC250_GCVM_AGP_ALIGN_BYTES   (1ull << 34) /* 16 GB */
#define BC250_GCVM_AGP_D2_BYTES      (1ull << 35) /* 32 GB de PA de sistema */
#define BC250_GCVM_AGP_GRANULE_BYTES (1ull << 24) /* campos BOT/TOP em 16 MB */
#define BC250_GCVM_MC_LIMIT          (1ull << 48)

typedef struct BC250_GCVM_GLOBAL_MMU_CONFIG {
    uint64_t FbLocationStart;   /* 0 = auto-detectar de GCMC_VM_FB_LOCATION_BASE */
    uint64_t FbLocationEnd;     /* 0 = auto-detectar de GCMC_VM_FB_LOCATION_TOP */
    uint64_t DummyPagePhysical; /* PA da dummy fault page (alinhada a 4 KB) */
    uint64_t AgpBytes;          /* 0 = AGP desligada (BOT > TOP), como antes da D2 */
} BC250_GCVM_GLOBAL_MMU_CONFIG;

typedef uint32_t (*BC250_GCVM_MMU_READ32)(
    void *Context, uint32_t ByteOffset, uint32_t *Value);
typedef uint32_t (*BC250_GCVM_MMU_WRITE32)(
    void *Context, uint32_t ByteOffset, uint32_t Value);
typedef uint32_t (*BC250_GCVM_MMU_FLUSH)(void *Context);

typedef struct BC250_GCVM_GLOBAL_MMU_INTERFACE {
    BC250_GCVM_MMU_READ32 Read32;
    BC250_GCVM_MMU_WRITE32 Write32;
    BC250_GCVM_MMU_FLUSH Flush;
    void *Context;
    uint32_t Identity;
} BC250_GCVM_GLOBAL_MMU_INTERFACE;

typedef struct BC250_GCVM_GLOBAL_MMU_EVIDENCE {
    uint32_t SnapshotPresent;
    uint32_t RegisterCount;
    uint32_t Registers[BC250_GCVM_GLOBAL_MMU_REG_COUNT];
    uint32_t ValuesBefore[BC250_GCVM_GLOBAL_MMU_REG_COUNT];
    uint32_t ValuesProgrammed[BC250_GCVM_GLOBAL_MMU_REG_COUNT];
    uint32_t ProgramPassed;
    uint32_t ReadbackPassed;
    uint32_t TlbFlushPassed;
    uint32_t RollbackPassed;
    uint32_t RollbackFailureCount;
    uint32_t RestorePassed;
    uint32_t RestoreFailureCount;
    uint32_t FailureStep;
    uint32_t FailureRegister;
    uint32_t FailureValue;
    /* D2: abertura AGP efetivamente programada (AgpEnabled=0: desligada). */
    uint32_t AgpEnabled;
    uint64_t AgpStart;
    uint64_t AgpEnd;
} BC250_GCVM_GLOBAL_MMU_EVIDENCE;

/*
 * D2: planos puros, sem registrador. Devolvem 1 em sucesso e 0 em recusa.
 * PlanAgpAfterFb recusa tamanho fora do granulo de 16 MB, FB invertida e
 * abertura que passe de 2^48. AgpSystemPaToMc recusa faixa que nao caiba
 * inteira na abertura.
 */
uint32_t Bc250GcvmPlanAgpAfterFb(
    uint64_t FbStart, uint64_t FbEnd, uint64_t AgpBytes,
    uint64_t *AgpStart, uint64_t *AgpEnd);
uint32_t Bc250GcvmAgpSystemPaToMc(
    uint64_t AgpStart, uint64_t AgpEnd, uint64_t Pa, uint64_t Bytes,
    uint64_t *Mc);

/*
 * D2 por GART (doc 587). A AGP foi refutada na Corrida 214 (doc 586): o SDMA0
 * leu 1 dword pela abertura e parou. O Linux chega ao anel em GTT pela GART da
 * VMID 0 (P0 da coleta L-B: "GART: 512M 0x0 - 0x1FFFFFFF"); aqui a mesma
 * mecanica, com faixa de 2 MB (uma tabela de 4 KB) em vez de 512 MB.
 *
 * Offsets (Lei 7, gc_10_1_0_offset.h, byte = dw*4 + 0x4980, conferidos):
 *   CONTEXT0_CNTL 0x1620->0xA200, PT_BASE 0x168b/0x168c->0xA3AC/0xA3B0,
 *   PT_START 0x16ab/0x16ac->0xA42C/0xA430, PT_END 0x16cb/0x16cc->0xA4AC/0xA4B0.
 * Programacao (gfxhub_v2_0_init_gart_aperture_regs e enable_system_domain,
 * inc/linux_ref/gfxhub_v2_0.c:120-148,254-264): BASE = PA da tabela | VALID,
 * START = gart_start >> 12, END = gart_end >> 12, CNTL: ENABLE_CONTEXT=1,
 * PAGE_TABLE_DEPTH=0, RETRY_PERMISSION_OR_INVALID_PAGE_FAULT=0, resto preservado.
 */
#define BC250_REG_GCVM_CONTEXT0_CNTL        0x0000A200u
#define BC250_REG_GCVM_CONTEXT0_PT_BASE_LO  0x0000A3ACu
#define BC250_REG_GCVM_CONTEXT0_PT_BASE_HI  0x0000A3B0u
#define BC250_REG_GCVM_CONTEXT0_PT_START_LO 0x0000A42Cu
#define BC250_REG_GCVM_CONTEXT0_PT_START_HI 0x0000A430u
#define BC250_REG_GCVM_CONTEXT0_PT_END_LO   0x0000A4ACu
#define BC250_REG_GCVM_CONTEXT0_PT_END_HI   0x0000A4B0u
#define BC250_GCVM_GART_REG_COUNT           7u

#define BC250_GCVM_GART_BYTES    (2ull << 20)
#define BC250_GCVM_GART_ENTRIES  512u
/* Layout de PTE do Navi 10 (bc250_gcvm_contract.h): bits sao as flags. */
#define BC250_GCVM_GART_PTE_ADDR_MASK    0x0000FFFFFFFFF000ull
/*
 * VALID|SYSTEM|SNOOPED|EXECUTABLE|READABLE|WRITEABLE | MTYPE_UC (bits 50:48 = 3).
 * UC.1 (Corrida 217, doc 592): o Linux poe gart_pte_flags = MTYPE_UC|EXECUTABLE
 * em TODA PTE da GART (gmc_v10_0.c:738) e amdgpu_gart_bind soma VALID|SYSTEM|
 * SNOOPED|READ|WRITE. Sem MTYPE a PTE era NC (0): a escrita da GPU na RAM pode
 * ficar no GL2 -- o GCR antes da fence so' invalida, como no Linux -- e a fence
 * do SDMA0 nao aparecia para a CPU (217: anel drenado, fence no poison).
 */
#define BC250_GCVM_GART_PTE_MTYPE_UC     0x0003000000000000ull
#define BC250_GCVM_GART_PTE_SYSTEM_FLAGS (BC250_GCVM_GART_PTE_MTYPE_UC | 0x77ull)
/* VALID|READABLE|WRITEABLE na pagina dummy da VRAM (entradas sem uso). */
#define BC250_GCVM_GART_PTE_DUMMY_FLAGS  0x61u
#define BC250_GCVM_CONTEXT0_CNTL_GART_MASK  0x00000087u /* ENABLE|DEPTH|RETRY */
#define BC250_GCVM_CONTEXT0_CNTL_GART_VALUE 0x00000001u /* ENABLE=1 DEPTH=0 RETRY=0 */

typedef struct BC250_GCVM_GART_EVIDENCE {
    uint32_t SnapshotPresent;
    uint32_t Registers[BC250_GCVM_GART_REG_COUNT];
    uint32_t ValuesBefore[BC250_GCVM_GART_REG_COUNT];
    uint32_t ValuesProgrammed[BC250_GCVM_GART_REG_COUNT];
    uint32_t ProgramPassed;
    uint32_t ReadbackPassed;
    uint32_t TlbFlushPassed;
    uint32_t RollbackPassed;
    uint32_t RollbackFailureCount;
    uint32_t RestorePassed;
    uint32_t RestoreFailureCount;
    uint32_t FailureStep;
    uint32_t FailureRegister;
    uint32_t FailureValue;
} BC250_GCVM_GART_EVIDENCE;

/*
 * Monta a tabela plana de Entries (=512) PTEs: todas na dummy da VRAM, e
 * SystemPages paginas contiguas a partir de FirstSystemPa nas entradas
 * [FirstEntry, FirstEntry + SystemPages). A entrada 0 fica sempre na dummy
 * (MC 0 nunca vira endereco de anel). Devolve 1 em sucesso, 0 em recusa.
 */
uint32_t Bc250GcvmGartBuildTable(
    uint64_t *Table, uint32_t Entries, uint64_t DummyPa,
    uint64_t FirstSystemPa, uint32_t FirstEntry, uint32_t SystemPages);

BC250_GCVM_GLOBAL_MMU_STATUS Bc250GcvmGartProgram(
    uint64_t TablePa,
    const BC250_GCVM_GLOBAL_MMU_INTERFACE *Interface,
    BC250_GCVM_GART_EVIDENCE *Evidence);

/* Desliga o contexto 0 primeiro e devolve o snapshot; idempotente. */
BC250_GCVM_GLOBAL_MMU_STATUS Bc250GcvmGartRestore(
    const BC250_GCVM_GLOBAL_MMU_INTERFACE *Interface,
    BC250_GCVM_GART_EVIDENCE *Evidence);

BC250_GCVM_GLOBAL_MMU_STATUS Bc250GcvmGlobalMmuProgram(
    const BC250_GCVM_GLOBAL_MMU_CONFIG *Config,
    const BC250_GCVM_GLOBAL_MMU_INTERFACE *Interface,
    BC250_GCVM_GLOBAL_MMU_EVIDENCE *Evidence);

BC250_GCVM_GLOBAL_MMU_STATUS Bc250GcvmGlobalMmuRestore(
    const BC250_GCVM_GLOBAL_MMU_INTERFACE *Interface,
    BC250_GCVM_GLOBAL_MMU_EVIDENCE *Evidence);

#ifdef __cplusplus
}
#endif

#endif /* BC250_GCVM_GLOBAL_MMU_H */
