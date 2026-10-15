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

#include "bc250_gpuva_geometry.h"

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

/*
 * =========================================================================
 * INVENTARIO DOS 16 CONTEXTOS GCVM (21/09, apos a Corrida 49).
 *
 * POR QUE EXISTE: a Corrida 49 mediu que varios contextos do DWM disputam o
 * VMID 7. Foram 5 faults e 5 comparacoes de raiz, TODAS divergentes (2, 8, 9,
 * 14 e 15 carimbos de 16), com 6 raizes distintas em 82 submissoes e o MESMO
 * IB (0x244FE4000) submetido sob TRES raizes diferentes. O hardware so' tem
 * UMA raiz por VMID: quando o IB do contexto A executa com a raiz de B
 * instalada, os enderecos de A nao traduzem -- e' o WALKER_ERROR=6 com
 * MAPPING_ERROR=1 medido desde a Corrida 45.
 *
 * O conserto e' VMID POR CONTEXTO. As pecas ja existem: Bc250GcvmInstallVmid
 * recebe o VMID, e Bc250Pm4EmitIndirectBuffer JA codifica o VMID no pacote.
 * Falta saber QUAIS VMIDs estao livres -- e assumir isso seria a quarta
 * hipotese desta investigacao a morrer por leitura de codigo.
 *
 * DERIVACAO DOS ENDERECOS (Lei 7). Conferida contra DUAS fontes independentes:
 * os strides de gc_10_1_0_offset.h (CNTL 1 dword por contexto, PT_BASE 2) e as
 * constantes de CONTEXT0 ja presentes em bc250_compute_service.c.
 *
 *   CNTL       = 0xA200 + vmid*4     (VMID7 -> 0xA21C, VMID8 -> 0xA220)
 *   PT_BASE_LO = 0xA3AC + vmid*8     (VMID7 -> 0xA3E4, VMID8 -> 0xA3EC)
 *   PT_BASE_HI = PT_BASE_LO + 4
 *
 * O inventario continua SOMENTE LEITURA. Depois da Corrida 52, a allowlist de
 * ESCRITA ganhou somente os sete offsets exatos dos VMIDs pertencentes ao
 * pool; isto nao muda o inventario nem abre intervalos de MMIO.
 * =========================================================================
 */
#define BC250_GCVM_CONTEXT_COUNT 16u
#define BC250_GCVM_CNTL_BASE     0x0000A200u
#define BC250_GCVM_CNTL_STRIDE   4u
#define BC250_GCVM_PT_BASE_LO_BASE   0x0000A3ACu
#define BC250_GCVM_PT_BASE_STRIDE    8u
#define BC250_GCVM_PT_START_LO_BASE  0x0000A42Cu
#define BC250_GCVM_PT_END_LO_BASE    0x0000A4ACu


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

/*
 * Valor alvo: ENABLE + os 7 *_ENABLE_DEFAULT + DEPTH/BLOCK_SIZE da geometria
 * (bc250_gpuva_geometry.h). 2 niveis: 0x00555403. 4 niveis (P5): 0x00555407.
 */
#define BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE \
    (BC250_GCVM_CONTEXT_CNTL_FAULT_DEFAULTS_MASK | 0x00000001u | \
     ((BC250_GPUVA_PT_GCVM_DEPTH << BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_DEPTH_SHIFT) & \
      BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_DEPTH_MASK) | \
     ((BC250_GPUVA_PT_GCVM_BLOCK_SIZE << BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_BLOCK_SIZE_SHIFT) & \
      BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_BLOCK_SIZE_MASK))

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
/* DEFAULT + INTERRUPT das 7 classes, mais ENABLE e a geometria. 2 niveis: 0x007FFE03; 4 niveis: 0x007FFE07. */
#define BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE \
    (BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE | BC250_GCVM_CONTEXT_CNTL_FAULT_INTERRUPTS_MASK)

/*
 * Limites do espaco: START 0 e END = max_pfn - 1 da geometria. 34 bits:
 * 0x00000000:003FFFFF (2^22 - 1). 48 bits (P5): 0x0000000F:FFFFFFFF (2^36 - 1),
 * como gfxhub_v2_0_setup_vmid_config.
 */
#define BC250_GCVM_CONTEXT8_PT_START_VALUE  0x00000000u
#define BC250_GCVM_CONTEXT8_PT_END_LO_VALUE ((uint32_t)(BC250_GPUVA_PT_GCVM_END_PFN & 0xFFFFFFFFull))
#define BC250_GCVM_CONTEXT8_PT_END_HI_VALUE ((uint32_t)(BC250_GPUVA_PT_GCVM_END_PFN >> 32))

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
typedef uint32_t (*BC250_GCVM_FLUSH_VMID)(void *Context, uint32_t Vmid);

typedef struct BC250_GCVM_VMID8_INTERFACE {
    BC250_GCVM_VMID8_READ32 Read32;
    BC250_GCVM_VMID8_WRITE32 Write32;
    BC250_GCVM_VMID8_FLUSH Flush;
    /* InstallVmid exige este callback: o VMID escolhido nao pode se perder. */
    BC250_GCVM_FLUSH_VMID FlushVmid;
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
    uint32_t Vmid;
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

/*
 * Reconhece somente os sete registradores de UM contexto GCVM. Nao usar uma
 * faixa: entre CNTL, PT_BASE, PT_START e PT_END existem registradores de
 * outras familias. O limite de VMID tambem e parte do contrato fail-closed.
 */
static __inline uint32_t Bc250GcvmIsContextRegisterForVmid(
    uint32_t Vmid, uint32_t Offset)
{
    uint32_t pairOffset;

    if (Vmid >= BC250_GCVM_CONTEXT_COUNT) return 0u;
    if (Offset == BC250_GCVM_CNTL_BASE +
            (Vmid * BC250_GCVM_CNTL_STRIDE)) return 1u;

    pairOffset = Vmid * BC250_GCVM_PT_BASE_STRIDE;
    return Offset == BC250_GCVM_PT_BASE_LO_BASE + pairOffset ||
        Offset == BC250_GCVM_PT_BASE_LO_BASE + pairOffset + 4u ||
        Offset == BC250_GCVM_PT_START_LO_BASE + pairOffset ||
        Offset == BC250_GCVM_PT_START_LO_BASE + pairOffset + 4u ||
        Offset == BC250_GCVM_PT_END_LO_BASE + pairOffset ||
        Offset == BC250_GCVM_PT_END_LO_BASE + pairOffset + 4u;
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

uint32_t Bc250GcvmContextCntlOffset(uint32_t Vmid);
uint32_t Bc250GcvmContextPtBaseLoOffset(uint32_t Vmid);

typedef struct BC250_GCVM_CONTEXT_INVENTORY_ENTRY {
    uint32_t Vmid;
    uint32_t CntlRead;       /* 1 = leitura bem-sucedida */
    uint32_t Cntl;
    uint32_t Enabled;        /* ENABLE_CONTEXT, bit 0 */
    uint32_t PtBaseRead;
    uint64_t PageTableBase;  /* HI:LO, sem os bits de flag */
    uint32_t InUse;          /* Enabled != 0 OU raiz != 0 */
} BC250_GCVM_CONTEXT_INVENTORY_ENTRY;

typedef struct BC250_GCVM_CONTEXT_INVENTORY {
    BC250_GCVM_CONTEXT_INVENTORY_ENTRY Entries[BC250_GCVM_CONTEXT_COUNT];
    uint32_t Scanned;
    uint32_t InUseCount;
    uint32_t FreeCount;
    uint32_t UnreadableCount;
    uint32_t FreeMask;       /* bit N = VMID N livre e legivel */
} BC250_GCVM_CONTEXT_INVENTORY;

BC250_GCVM_VMID8_STATUS Bc250GcvmScanContexts(
    const BC250_GCVM_VMID8_INTERFACE *Interface,
    BC250_GCVM_CONTEXT_INVENTORY *Inventory);

BC250_GCVM_VMID8_STATUS Bc250GcvmVmid8Install(
    uint64_t PageTablePhysical,
    const BC250_GCVM_VMID8_INTERFACE *Interface,
    BC250_GCVM_VMID8_INSTALL_EVIDENCE *Evidence);

BC250_GCVM_VMID8_STATUS Bc250GcvmVmid8Uninstall(
    const BC250_GCVM_VMID8_INTERFACE *Interface,
    BC250_GCVM_VMID8_INSTALL_EVIDENCE *Evidence);

/*
 * DX.15 -- DESARMAR O CONTEXTO GCVM DE UM VMID DO POOL.
 *
 * POR QUE EXISTE (achado por leitura apos a Corrida 61): Bc250VmidPoolRelease
 * era SOMENTE SOFTWARE. Quando o contexto morria, a entrada do pool era
 * limpa e o VMID voltava para a lista de livres -- mas o SILICIO continuava
 * com CNTL habilitado e PT_BASE apontando para a raiz do processo morto, que
 * o VidMm pode liberar e reutilizar a qualquer momento.
 *
 * E o gemeo exato de Bc250GcvmVmid8Uninstall, que so' existia para o VMID 8
 * fixo. Mesma ordem, pelo mesmo motivo: desabilitar o CNTL PRIMEIRO (para que
 * nenhuma traducao nova nasca da tabela velha), depois zerar os registradores
 * de endereco, e so' entao invalidar a TLB DAQUELE VMID -- invalidar antes de
 * desabilitar deixaria a janela aberta em vez de fecha-la.
 *
 * Usa FlushVmid (por VMID), nunca Flush (fixo): invalidar o VMID errado
 * deixaria a traducao velha viva justamente no slot que esta sendo devolvido.
 */
BC250_GCVM_VMID8_STATUS Bc250GcvmUninstallVmid(
    uint32_t Vmid,
    const BC250_GCVM_VMID8_INTERFACE *Interface,
    BC250_GCVM_VMID8_INSTALL_EVIDENCE *Evidence);

#ifdef __cplusplus
}
#endif

#endif
