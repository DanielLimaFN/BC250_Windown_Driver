#include "bc250_mmhub_gart.h"

#include <string.h>

/*
 * Ordem = mmhub_v2_0_gart_enable (mmhub_v2_0.c:426-440), depois
 * set_fault_enable_default(true) (gmc_v10_0.c:1073). O contexto 0 liga
 * (indice 22) depois da tabela, das aberturas, do TLB e do L2.
 */
static const uint32_t g_Bc250MmhubGartRegisters[BC250_MMHUB_GART_REG_COUNT] = {
    /* init_gart_aperture_regs */
    BC250_MMHUB_VM_CONTEXT0_PT_BASE_LO32,   /* 0 */
    BC250_MMHUB_VM_CONTEXT0_PT_BASE_HI32,   /* 1 */
    BC250_MMHUB_VM_CONTEXT0_PT_START_LO32,  /* 2 */
    BC250_MMHUB_VM_CONTEXT0_PT_START_HI32,  /* 3 */
    BC250_MMHUB_VM_CONTEXT0_PT_END_LO32,    /* 4 */
    BC250_MMHUB_VM_CONTEXT0_PT_END_HI32,    /* 5 */
    /* init_system_aperture_regs */
    BC250_MMHUB_MC_VM_AGP_BASE,             /* 6 */
    BC250_MMHUB_MC_VM_AGP_BOT,              /* 7 */
    BC250_MMHUB_MC_VM_AGP_TOP,              /* 8 */
    BC250_MMHUB_MC_VM_SYS_APERTURE_LOW,     /* 9 */
    BC250_MMHUB_MC_VM_SYS_APERTURE_HIGH,    /* 10 */
    BC250_MMHUB_MC_VM_SYS_APERTURE_DEF_LSB, /* 11 */
    BC250_MMHUB_MC_VM_SYS_APERTURE_DEF_MSB, /* 12 */
    BC250_MMHUB_VM_L2_PROT_FAULT_DEF_LO32,  /* 13 */
    BC250_MMHUB_VM_L2_PROT_FAULT_DEF_HI32,  /* 14 */
    BC250_MMHUB_VM_L2_PROT_FAULT_CNTL2,     /* 15 */
    /* init_tlb_regs */
    BC250_MMHUB_MC_VM_MX_L1_TLB_CNTL,       /* 16 */
    /* init_cache_regs */
    BC250_MMHUB_VM_L2_CNTL,                 /* 17 */
    BC250_MMHUB_VM_L2_CNTL2,                /* 18 */
    BC250_MMHUB_VM_L2_CNTL3,                /* 19 */
    BC250_MMHUB_VM_L2_CNTL4,                /* 20 */
    BC250_MMHUB_VM_L2_CNTL5,                /* 21 */
    /* enable_system_domain */
    BC250_MMHUB_VM_CONTEXT0_CNTL,           /* 22 */
    /* disable_identity_aperture */
    BC250_MMHUB_VM_L2_CTX1_ID_LOW_LO32,     /* 23 */
    BC250_MMHUB_VM_L2_CTX1_ID_LOW_HI32,     /* 24 */
    BC250_MMHUB_VM_L2_CTX1_ID_HIGH_LO32,    /* 25 */
    BC250_MMHUB_VM_L2_CTX1_ID_HIGH_HI32,    /* 26 */
    BC250_MMHUB_VM_L2_CTX_ID_PHYS_LO32,     /* 27 */
    BC250_MMHUB_VM_L2_CTX_ID_PHYS_HI32,     /* 28 */
    /* program_invalidation (engines 0 e 17) */
    BC250_MMHUB_VM_INV_ENG0_RANGE_LO32,     /* 29 */
    BC250_MMHUB_VM_INV_ENG0_RANGE_HI32,     /* 30 */
    BC250_MMHUB_VM_INV_ENG17_RANGE_LO32,    /* 31 */
    BC250_MMHUB_VM_INV_ENG17_RANGE_HI32,    /* 32 */
    /* set_fault_enable_default(true) */
    BC250_MMHUB_VM_L2_PROT_FAULT_CNTL       /* 33 */
};

#define BC250_MMHUB_IDX_CNTL2 18u

uint32_t Bc250MmhubGartRegisterIndex(uint32_t ByteOffset)
{
    uint32_t i;
    for (i = 0u; i < BC250_MMHUB_GART_REG_COUNT; ++i) {
        if (g_Bc250MmhubGartRegisters[i] == ByteOffset) return i;
    }
    return BC250_MMHUB_GART_REG_COUNT;
}

uint32_t Bc250MmhubGartIsWritable(uint32_t ByteOffset)
{
    return Bc250MmhubGartRegisterIndex(ByteOffset) < BC250_MMHUB_GART_REG_COUNT ||
        ByteOffset == BC250_MMHUB_VM_INV_ENG17_SEM ||
        ByteOffset == BC250_MMHUB_VM_INV_ENG17_REQ;
}

static uint32_t Bc250MmhubKeepMask(uint32_t Index)
{
    /* Os gatilhos de invalidacao do L2_CNTL2 sao limpos pelo silicio. */
    return Index == BC250_MMHUB_IDX_CNTL2 ? ~BC250_MMHUB_L2_CNTL2_TRIGGERS : ~0u;
}

static uint32_t Bc250MmhubSet(uint32_t Value, uint32_t Mask, uint32_t Field)
{
    return (Value & ~Mask) | (Field & Mask); /* Lei 6 */
}

static void Bc250MmhubWriteBack(
    const BC250_GCVM_GLOBAL_MMU_INTERFACE *Interface,
    BC250_MMHUB_GART_EVIDENCE *Evidence,
    uint32_t Count,
    uint32_t *FailureCount)
{
    uint32_t i, value;

    /* Ordem inversa: FAULT_CNTL, faixas, identidade, CONTEXTO 0 (desliga), L2, TLB, aberturas, tabela. */
    for (i = Count; i != 0u; --i) {
        if (!Interface->Write32(Interface->Context,
                g_Bc250MmhubGartRegisters[i - 1u], Evidence->ValuesBefore[i - 1u])) {
            ++*FailureCount;
        }
    }
    for (i = 0u; i < Count; ++i) {
        uint32_t keep = Bc250MmhubKeepMask(i);
        if (!Interface->Read32(Interface->Context, g_Bc250MmhubGartRegisters[i], &value) ||
            (value & keep) != (Evidence->ValuesBefore[i] & keep)) {
            if (*FailureCount == 0u) {
                Evidence->FailureRegister = g_Bc250MmhubGartRegisters[i];
                Evidence->FailureValue = value;
            }
            ++*FailureCount;
        }
    }
    if (!Interface->Flush(Interface->Context)) {
        ++*FailureCount;
    }
}

BC250_GCVM_GLOBAL_MMU_STATUS Bc250MmhubGartProgram(
    const BC250_MMHUB_GART_CONFIG *Config,
    const BC250_GCVM_GLOBAL_MMU_INTERFACE *Interface,
    BC250_MMHUB_GART_EVIDENCE *Evidence)
{
    uint32_t planned[BC250_MMHUB_GART_REG_COUNT];
    const uint32_t *b;
    uint32_t i, value, written = 0u, failures = 0u;
    uint64_t fbStart, fbEnd;

    if (Evidence != NULL) {
        memset(Evidence, 0, sizeof(*Evidence));
    }
    if (Config == NULL || Interface == NULL || Evidence == NULL ||
        Interface->Read32 == NULL || Interface->Write32 == NULL ||
        Interface->Flush == NULL ||
        Interface->Identity != BC250_MMHUB_GART_IDENTITY ||
        Config->TablePa == 0ull || (Config->TablePa & 0xFFFull) != 0ull ||
        (Config->TablePa & ~BC250_GCVM_GART_PTE_ADDR_MASK) != 0ull ||
        Config->DummyPa == 0ull || (Config->DummyPa & 0xFFFull) != 0ull) {
        if (Evidence != NULL) Evidence->FailureStep = 1u;
        return BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT;
    }

    /* PREFLIGHT: a abertura de sistema sai do FB do proprio MMHUB. */
    if (!Interface->Read32(Interface->Context, BC250_MMHUB_MC_VM_FB_LOCATION_BASE, &Evidence->FbBase) ||
        !Interface->Read32(Interface->Context, BC250_MMHUB_MC_VM_FB_LOCATION_TOP, &Evidence->FbTop)) {
        Evidence->FailureStep = 2u;
        return BC250_GCVM_GLOBAL_MMU_SNAPSHOT_FAILED;
    }
    fbStart = ((uint64_t)(Evidence->FbBase & 0x00FFFFFFu)) << 24;
    fbEnd = (((uint64_t)(Evidence->FbTop & 0x00FFFFFFu)) << 24) | 0x00FFFFFFull;
    if (fbStart == 0ull || fbEnd < fbStart) {
        Evidence->FailureStep = 2u;
        return BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT;
    }

    /* SNAPSHOT */
    for (i = 0u; i < BC250_MMHUB_GART_REG_COUNT; ++i) {
        if (!Interface->Read32(Interface->Context, g_Bc250MmhubGartRegisters[i],
                &Evidence->ValuesBefore[i])) {
            Evidence->FailureStep = 3u;
            Evidence->FailureRegister = g_Bc250MmhubGartRegisters[i];
            return BC250_GCVM_GLOBAL_MMU_SNAPSHOT_FAILED;
        }
        Evidence->Registers[i] = g_Bc250MmhubGartRegisters[i];
    }
    Evidence->SnapshotPresent = 1u;
    b = Evidence->ValuesBefore;

    /* PLANO */
    planned[0] = (uint32_t)((Config->TablePa | 1ull) & 0xFFFFFFFFull); /* pd_addr | VALID */
    planned[1] = (uint32_t)(Config->TablePa >> 32);
    planned[2] = 0u;                                                   /* gart_start >> 12 */
    planned[3] = 0u;                                                   /* gart_start >> 44 */
    planned[4] = (uint32_t)((BC250_MMHUB_GART_BYTES - 1ull) >> 12);    /* gart_end >> 12 (2 MB, COMP.VA.3) */
    planned[5] = 0u;                                                   /* gart_end >> 44 */
    planned[6] = 0u;                                /* AGP_BASE */
    planned[7] = 0x00FFFFFFu;                       /* AGP_BOT: agp_start = 0xFFFFFFFFFFFF >> 24 (AGP desligada) */
    planned[8] = 0u;                                /* AGP_TOP: agp_end = 0 */
    planned[9] = (uint32_t)(fbStart >> 18);         /* min(fb_start, agp_start) >> 18 */
    planned[10] = (uint32_t)(fbEnd >> 18);          /* max(fb_end, agp_end) >> 18 */
    planned[11] = (uint32_t)(Config->DummyPa >> 12);
    planned[12] = (uint32_t)(Config->DummyPa >> 44);
    planned[13] = (uint32_t)(Config->DummyPa >> 12);
    planned[14] = (uint32_t)(Config->DummyPa >> 44);
    planned[15] = b[15] | BC250_MMHUB_FAULT_CNTL2_PTE_READ_RETRY;
    value = Bc250MmhubSet(b[16], BC250_MMHUB_TLB_ENABLE_L1_TLB, BC250_MMHUB_TLB_ENABLE_L1_TLB);
    value = Bc250MmhubSet(value, BC250_MMHUB_TLB_SYSTEM_ACCESS_MODE, 3u << 3);
    value = Bc250MmhubSet(value, BC250_MMHUB_TLB_ADVANCED_DRIVER_MODEL, BC250_MMHUB_TLB_ADVANCED_DRIVER_MODEL);
    value = Bc250MmhubSet(value, BC250_MMHUB_TLB_UNMAPPED_ACCESS, 0u);
    planned[16] = Bc250MmhubSet(value, BC250_MMHUB_TLB_MTYPE, BC250_MMHUB_TLB_MTYPE_UC);
    value = Bc250MmhubSet(b[17], BC250_MMHUB_L2_ENABLE_L2_CACHE, BC250_MMHUB_L2_ENABLE_L2_CACHE);
    value = Bc250MmhubSet(value, BC250_MMHUB_L2_FRAGMENT_PROCESSING, 0u);
    value = Bc250MmhubSet(value, BC250_MMHUB_L2_DEFAULT_PAGE_OUT_SYSMEM, BC250_MMHUB_L2_DEFAULT_PAGE_OUT_SYSMEM);
    value = Bc250MmhubSet(value, BC250_MMHUB_L2_PDE0_TAG_GENERATION, 0u);
    value = Bc250MmhubSet(value, BC250_MMHUB_L2_PDE_FAULT_CLASSIFICATION, 0u);
    value = Bc250MmhubSet(value, BC250_MMHUB_L2_CTX1_IDENTITY_MODE, 1u << 19);
    planned[17] = Bc250MmhubSet(value, BC250_MMHUB_L2_IDENTITY_FRAGMENT_SIZE, 0u);
    planned[18] = b[18] | BC250_MMHUB_L2_CNTL2_TRIGGERS;
    value = Bc250MmhubSet(b[19], BC250_MMHUB_L2_CNTL3_BANK_SELECT, 9u);   /* translate_further = 0 */
    planned[19] = Bc250MmhubSet(value, BC250_MMHUB_L2_CNTL3_BIGK_FRAGMENT, 6u << 15);
    planned[20] = Bc250MmhubSet(b[20], BC250_MMHUB_L2_CNTL4_TAP_PHYSICAL, 0u);
    planned[21] = Bc250MmhubSet(b[21], BC250_MMHUB_L2_CNTL5_SMALLK_FRAGMENT, 0u);
    planned[22] = Bc250MmhubSet(b[22], BC250_MMHUB_CTX0_CNTL_GART_MASK, BC250_MMHUB_CTX0_CNTL_GART_VALUE);
    planned[23] = 0xFFFFFFFFu;
    planned[24] = 0x0000000Fu;
    planned[25] = 0u;
    planned[26] = 0u;
    planned[27] = 0u;
    planned[28] = 0u;
    planned[29] = 0xFFFFFFFFu;
    planned[30] = 0x0000001Fu;
    planned[31] = 0xFFFFFFFFu;
    planned[32] = 0x0000001Fu;
    planned[33] = b[33] | BC250_MMHUB_FAULT_CNTL_ENABLE_DEFAULTS;

    /* APPLY */
    for (i = 0u; i < BC250_MMHUB_GART_REG_COUNT; ++i) {
        if (!Interface->Write32(Interface->Context, g_Bc250MmhubGartRegisters[i], planned[i])) {
            Evidence->FailureStep = 4u;
            Evidence->FailureRegister = g_Bc250MmhubGartRegisters[i];
            Evidence->FailureValue = planned[i];
            written = i + 1u; /* a escrita recusada pode ter chegado: devolve-a tambem */
            goto Rollback;
        }
        Evidence->ValuesProgrammed[i] = planned[i];
    }
    written = BC250_MMHUB_GART_REG_COUNT;
    Evidence->ProgramPassed = 1u;

    /* READBACK */
    for (i = 0u; i < BC250_MMHUB_GART_REG_COUNT; ++i) {
        uint32_t keep = Bc250MmhubKeepMask(i);
        if (!Interface->Read32(Interface->Context, g_Bc250MmhubGartRegisters[i], &value) ||
            (value & keep) != (planned[i] & keep)) {
            Evidence->FailureStep = 5u;
            Evidence->FailureRegister = g_Bc250MmhubGartRegisters[i];
            Evidence->FailureValue = value;
            goto Rollback;
        }
    }
    Evidence->ReadbackPassed = 1u;

    /* gmc_v10_0_flush_gpu_tlb(adev, 0, AMDGPU_MMHUB0(0), 0) */
    if (!Interface->Flush(Interface->Context)) {
        Evidence->FailureStep = 6u;
        goto Rollback;
    }
    Evidence->TlbFlushPassed = 1u;
    return BC250_GCVM_GLOBAL_MMU_OK;

Rollback:
    Bc250MmhubWriteBack(Interface, Evidence, written, &failures);
    Evidence->RollbackFailureCount = failures;
    Evidence->RollbackPassed = failures == 0u;
    if (Evidence->FailureStep == 4u) return BC250_GCVM_GLOBAL_MMU_WRITE_FAILED;
    if (Evidence->FailureStep == 5u) return BC250_GCVM_GLOBAL_MMU_READBACK_FAILED;
    return BC250_GCVM_GLOBAL_MMU_TLB_FAILED;
}

BC250_GCVM_GLOBAL_MMU_STATUS Bc250MmhubGartRestore(
    const BC250_GCVM_GLOBAL_MMU_INTERFACE *Interface,
    BC250_MMHUB_GART_EVIDENCE *Evidence)
{
    uint32_t failures = 0u;

    if (Interface == NULL || Evidence == NULL ||
        Interface->Read32 == NULL || Interface->Write32 == NULL ||
        Interface->Flush == NULL ||
        Interface->Identity != BC250_MMHUB_GART_IDENTITY ||
        Evidence->SnapshotPresent == 0u) {
        return BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT;
    }
    Bc250MmhubWriteBack(Interface, Evidence, BC250_MMHUB_GART_REG_COUNT, &failures);
    Evidence->RestoreFailureCount = failures;
    Evidence->RestorePassed = failures == 0u;
    return failures == 0u ? BC250_GCVM_GLOBAL_MMU_OK : BC250_GCVM_GLOBAL_MMU_RESTORE_FAILED;
}
