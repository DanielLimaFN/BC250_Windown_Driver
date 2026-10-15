#ifndef BC250_MMHUB_GART_H
#define BC250_MMHUB_GART_H

#include "bc250_gcvm_global_mmu.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * P3.0b (doc 611): GART da VMID 0 no MMHUB, como mmhub_v2_0_gart_enable
 * (inc/linux_ref/mmhub_v2_0.c:426) seguido de set_fault_enable_default(true)
 * e do flush do MMHUB (gmc_v10_0_gart_enable, gmc_v10_0.c:1041-1074).
 *
 * Por que: o IH (bloco OSS) e' cliente do MMHUB (mmhub_v2_0.c, [15]="OSS"), e
 * em APU o amdgpu poe o anel do IH em VA da VMID 0 (navi10_ih_sw_init,
 * use_bus_addr=false). A Corrida 229 (doc 610) fotografou o MMHUB como o
 * firmware deixa: CONTEXT0_CNTL=0x007FFE80 (ENABLE_CONTEXT=0), tabela zerada,
 * VM_L2_CNTL=0x00080602 (L2 desligado), MX_L1_TLB_CNTL=0x501 (sem ADVANCED_
 * DRIVER_MODEL), e AGP_BOT=AGP_TOP=0 -- isto e', uma abertura AGP de 16 MB em
 * MC 0, por cima de onde o IH ficaria (VA 0x80000). O amdgpu com a AGP
 * desligada (P0 da coleta L-B) escreve BOT=0xFFFFFF, TOP=0.
 *
 * A tabela e' a MESMA da GART da D2 no GFXHUB (amdgpu: os dois hubs usam
 * adev->gart.bo). As aberturas de FB e de sistema seguem cobrindo o FB: o
 * display (DCN, cliente do MMHUB) le o scanout em MC do FB, dentro da abertura
 * de sistema, que o SYSTEM_ACCESS_MODE=3 trata como fisico.
 *
 * Offsets (Lei 7): inc/linux_ref/mmhub_2_0_0_offset.h, BASE_IDX 0,
 * MMHUB_BASE seg0 = 0x1A000 (cyan_skillfish_ip_offset.h; confirmado pela foto
 * da 229, FB_OFFSET=0x460 = base da DRAM provada pela P1b), byte = (0x1A000 +
 * dw) * 4. Campos: inc/linux_ref/mmhub_2_0_0_sh_mask.h.
 *
 * DIFERENCAS DECLARADAS em relacao ao upstream:
 *  - VM_L2_CNTL3/4/5: o upstream parte de mmMMVM_L2_CNTLn_DEFAULT, que nao
 *    temos (sem mmhub_2_0_0_default.h). Aqui parte do valor lido (Lei 6) e
 *    muda so' os campos que o upstream muda.
 *  - Contextos 1..15 (setup_vmid_config) NAO sao programados: nenhum cliente
 *    do MMHUB usa VMID != 0 neste driver (sem VCN/JPEG). Ficam como o firmware.
 *  - Faixa de invalidacao so' nos engines 0 e 17 (o upstream faz os 18),
 *    como a MMU global do GFXHUB ja faz.
 */
#define BC250_MMHUB_REG(dw) ((0x1A000u + (uint32_t)(dw)) * 4u)

#define BC250_MMHUB_VM_CONTEXT0_CNTL            BC250_MMHUB_REG(0x06C0u) /* 0x69B00 */
#define BC250_MMHUB_VM_CONTEXT0_PT_BASE_LO32    BC250_MMHUB_REG(0x072Bu) /* 0x69CAC */
#define BC250_MMHUB_VM_CONTEXT0_PT_BASE_HI32    BC250_MMHUB_REG(0x072Cu) /* 0x69CB0 */
#define BC250_MMHUB_VM_CONTEXT0_PT_START_LO32   BC250_MMHUB_REG(0x074Bu) /* 0x69D2C */
#define BC250_MMHUB_VM_CONTEXT0_PT_START_HI32   BC250_MMHUB_REG(0x074Cu) /* 0x69D30 */
#define BC250_MMHUB_VM_CONTEXT0_PT_END_LO32     BC250_MMHUB_REG(0x076Bu) /* 0x69DAC */
#define BC250_MMHUB_VM_CONTEXT0_PT_END_HI32     BC250_MMHUB_REG(0x076Cu) /* 0x69DB0 */
#define BC250_MMHUB_MC_VM_AGP_BASE              BC250_MMHUB_REG(0x0870u) /* 0x6A1C0 */
#define BC250_MMHUB_MC_VM_AGP_BOT               BC250_MMHUB_REG(0x086Fu) /* 0x6A1BC */
#define BC250_MMHUB_MC_VM_AGP_TOP               BC250_MMHUB_REG(0x086Eu) /* 0x6A1B8 */
#define BC250_MMHUB_MC_VM_FB_LOCATION_BASE      BC250_MMHUB_REG(0x086Cu) /* 0x6A1B0 */
#define BC250_MMHUB_MC_VM_FB_LOCATION_TOP       BC250_MMHUB_REG(0x086Du) /* 0x6A1B4 */
#define BC250_MMHUB_MC_VM_SYS_APERTURE_LOW      BC250_MMHUB_REG(0x0871u) /* 0x6A1C4 */
#define BC250_MMHUB_MC_VM_SYS_APERTURE_HIGH     BC250_MMHUB_REG(0x0872u) /* 0x6A1C8 */
#define BC250_MMHUB_MC_VM_SYS_APERTURE_DEF_LSB  BC250_MMHUB_REG(0x0858u) /* 0x6A160 */
#define BC250_MMHUB_MC_VM_SYS_APERTURE_DEF_MSB  BC250_MMHUB_REG(0x0859u) /* 0x6A164 */
#define BC250_MMHUB_VM_L2_PROT_FAULT_DEF_LO32   BC250_MMHUB_REG(0x068Fu) /* 0x69A3C */
#define BC250_MMHUB_VM_L2_PROT_FAULT_DEF_HI32   BC250_MMHUB_REG(0x0690u) /* 0x69A40 */
#define BC250_MMHUB_VM_L2_PROT_FAULT_CNTL       BC250_MMHUB_REG(0x0688u) /* 0x69A20 */
#define BC250_MMHUB_VM_L2_PROT_FAULT_CNTL2      BC250_MMHUB_REG(0x0689u) /* 0x69A24 */
#define BC250_MMHUB_VM_L2_PROT_FAULT_STATUS     BC250_MMHUB_REG(0x068Cu) /* 0x69A30 */
#define BC250_MMHUB_MC_VM_MX_L1_TLB_CNTL        BC250_MMHUB_REG(0x0873u) /* 0x6A1CC */
#define BC250_MMHUB_VM_L2_CNTL                  BC250_MMHUB_REG(0x0680u) /* 0x69A00 */
#define BC250_MMHUB_VM_L2_CNTL2                 BC250_MMHUB_REG(0x0681u) /* 0x69A04 */
#define BC250_MMHUB_VM_L2_CNTL3                 BC250_MMHUB_REG(0x0682u) /* 0x69A08 */
#define BC250_MMHUB_VM_L2_CNTL4                 BC250_MMHUB_REG(0x0698u) /* 0x69A60 */
#define BC250_MMHUB_VM_L2_CNTL5                 BC250_MMHUB_REG(0x06A1u) /* 0x69A84 */
#define BC250_MMHUB_VM_L2_CTX1_ID_LOW_LO32      BC250_MMHUB_REG(0x0692u) /* 0x69A48 */
#define BC250_MMHUB_VM_L2_CTX1_ID_LOW_HI32      BC250_MMHUB_REG(0x0693u) /* 0x69A4C */
#define BC250_MMHUB_VM_L2_CTX1_ID_HIGH_LO32     BC250_MMHUB_REG(0x0694u) /* 0x69A50 */
#define BC250_MMHUB_VM_L2_CTX1_ID_HIGH_HI32     BC250_MMHUB_REG(0x0695u) /* 0x69A54 */
#define BC250_MMHUB_VM_L2_CTX_ID_PHYS_LO32      BC250_MMHUB_REG(0x0696u) /* 0x69A58 */
#define BC250_MMHUB_VM_L2_CTX_ID_PHYS_HI32      BC250_MMHUB_REG(0x0697u) /* 0x69A5C */
#define BC250_MMHUB_VM_INV_ENG0_RANGE_LO32      BC250_MMHUB_REG(0x0707u) /* 0x69C1C */
#define BC250_MMHUB_VM_INV_ENG0_RANGE_HI32      BC250_MMHUB_REG(0x0708u) /* 0x69C20 */
#define BC250_MMHUB_VM_INV_ENG17_RANGE_LO32     BC250_MMHUB_REG(0x0729u) /* 0x69CA4 */
#define BC250_MMHUB_VM_INV_ENG17_RANGE_HI32     BC250_MMHUB_REG(0x072Au) /* 0x69CA8 */
/* Flush (gmc_v10_0_flush_vm_hub, engine 17, com semaforo no MMHUB). */
#define BC250_MMHUB_VM_INV_ENG17_SEM            BC250_MMHUB_REG(0x06E2u) /* 0x69B88 */
#define BC250_MMHUB_VM_INV_ENG17_REQ            BC250_MMHUB_REG(0x06F4u) /* 0x69BD0 */
#define BC250_MMHUB_VM_INV_ENG17_ACK            BC250_MMHUB_REG(0x0706u) /* 0x69C18 */

/* Campos (mmhub_2_0_0_sh_mask.h). */
#define BC250_MMHUB_TLB_ENABLE_L1_TLB           0x00000001u
#define BC250_MMHUB_TLB_SYSTEM_ACCESS_MODE      0x00000018u
#define BC250_MMHUB_TLB_UNMAPPED_ACCESS         0x00000020u
#define BC250_MMHUB_TLB_ADVANCED_DRIVER_MODEL   0x00000040u
#define BC250_MMHUB_TLB_MTYPE                   0x00003800u
#define BC250_MMHUB_TLB_MTYPE_UC                (3u << 11)
#define BC250_MMHUB_L2_ENABLE_L2_CACHE          0x00000001u
#define BC250_MMHUB_L2_FRAGMENT_PROCESSING      0x00000002u
#define BC250_MMHUB_L2_PDE0_TAG_GENERATION      0x00000100u
#define BC250_MMHUB_L2_DEFAULT_PAGE_OUT_SYSMEM  0x00000800u
#define BC250_MMHUB_L2_PDE_FAULT_CLASSIFICATION 0x00040000u
#define BC250_MMHUB_L2_CTX1_IDENTITY_MODE       0x00180000u
#define BC250_MMHUB_L2_IDENTITY_FRAGMENT_SIZE   0x03E00000u
#define BC250_MMHUB_L2_CNTL2_TRIGGERS           0x00000003u /* INVALIDATE_ALL_L1_TLBS|L2_CACHE */
#define BC250_MMHUB_L2_CNTL3_BANK_SELECT        0x0000003Fu
#define BC250_MMHUB_L2_CNTL3_BIGK_FRAGMENT      0x000F8000u
#define BC250_MMHUB_L2_CNTL4_TAP_PHYSICAL       0x000000C0u /* PDE|PTE_REQUEST_PHYSICAL */
#define BC250_MMHUB_L2_CNTL5_SMALLK_FRAGMENT    0x0000001Fu
#define BC250_MMHUB_FAULT_CNTL_ENABLE_DEFAULTS  0x00001FFCu /* RANGE..EXECUTE_*_ENABLE_DEFAULT */
#define BC250_MMHUB_FAULT_CNTL2_PTE_READ_RETRY  0x00040000u
#define BC250_MMHUB_CTX0_CNTL_GART_MASK         0x00000087u /* ENABLE|DEPTH|RETRY */
#define BC250_MMHUB_CTX0_CNTL_GART_VALUE        0x00000001u
/* gmc_v10_0_get_invalidate_req(0, 0): VMID 0, L2 PTE/PDE0-2 e L1. */
#define BC250_MMHUB_INV_REQ_VMID0               0x00F80001u

#define BC250_MMHUB_GART_REG_COUNT 34u
#define BC250_MMHUB_GART_IDENTITY  0x4D4D4855u /* MMHU */

typedef struct BC250_MMHUB_GART_CONFIG {
    uint64_t TablePa;  /* a mesma tabela da D2 (GFXHUB), alinhada a 4 KB */
    uint64_t DummyPa;  /* pagina dummy da MMU global, alinhada a 4 KB */
} BC250_MMHUB_GART_CONFIG;

typedef struct BC250_MMHUB_GART_EVIDENCE {
    uint32_t SnapshotPresent;
    uint32_t Registers[BC250_MMHUB_GART_REG_COUNT];
    uint32_t ValuesBefore[BC250_MMHUB_GART_REG_COUNT];
    uint32_t ValuesProgrammed[BC250_MMHUB_GART_REG_COUNT];
    uint32_t FbBase;
    uint32_t FbTop;
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
} BC250_MMHUB_GART_EVIDENCE;

/*
 * Interface: Read32/Write32 do transporte e Flush = invalidacao da VMID 0 no
 * MMHUB (engine 17, semaforo). Identity tem de ser BC250_MMHUB_GART_IDENTITY.
 */
BC250_GCVM_GLOBAL_MMU_STATUS Bc250MmhubGartProgram(
    const BC250_MMHUB_GART_CONFIG *Config,
    const BC250_GCVM_GLOBAL_MMU_INTERFACE *Interface,
    BC250_MMHUB_GART_EVIDENCE *Evidence);

/* Devolve o snapshot em ordem inversa (contexto 0 desliga antes da tabela). */
BC250_GCVM_GLOBAL_MMU_STATUS Bc250MmhubGartRestore(
    const BC250_GCVM_GLOBAL_MMU_INTERFACE *Interface,
    BC250_MMHUB_GART_EVIDENCE *Evidence);

/* Indice do registrador no plano (para o log); BC250_MMHUB_GART_REG_COUNT se nao achar. */
uint32_t Bc250MmhubGartRegisterIndex(uint32_t ByteOffset);

/* 1 se o offset pertence ao conjunto que a P3.0b escreve (inclui SEM/REQ do flush). */
uint32_t Bc250MmhubGartIsWritable(uint32_t ByteOffset);

#ifdef __cplusplus
}
#endif

#endif
