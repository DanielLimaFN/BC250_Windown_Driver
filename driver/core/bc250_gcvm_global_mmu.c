#include "bc250_gcvm_global_mmu.h"

#include <string.h>

static const uint32_t g_Bc250GlobalMmuRegisters[BC250_GCVM_GLOBAL_MMU_REG_COUNT] = {
    BC250_REG_GCMC_VM_AGP_BASE,
    BC250_REG_GCMC_VM_AGP_BOT,
    BC250_REG_GCMC_VM_AGP_TOP,
    BC250_REG_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR,
    BC250_REG_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR,
    BC250_REG_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB,
    BC250_REG_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB,
    BC250_REG_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_LO,
    BC250_REG_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_HI,
    BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL2,
    BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO,
    BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI,
    BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO,
    BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI,
    BC250_REG_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO,
    BC250_REG_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI,
    BC250_REG_GCVM_INVALIDATE_ENG0_ADDR_RANGE_LO,
    BC250_REG_GCVM_INVALIDATE_ENG0_ADDR_RANGE_HI,
    BC250_REG_GCVM_INVALIDATE_ENG17_ADDR_RANGE_LO,
    BC250_REG_GCVM_INVALIDATE_ENG17_ADDR_RANGE_HI,
    BC250_REG_GCMC_VM_MX_L1_TLB_CNTL,
    BC250_REG_GCVM_L2_CNTL,
    BC250_REG_GCVM_L2_CNTL2,
    BC250_REG_GCVM_L2_CNTL3,
    BC250_REG_GCVM_L2_CNTL4,
    BC250_REG_GCVM_L2_CNTL5
};

BC250_GCVM_GLOBAL_MMU_STATUS Bc250GcvmGlobalMmuProgram(
    const BC250_GCVM_GLOBAL_MMU_CONFIG *Config,
    const BC250_GCVM_GLOBAL_MMU_INTERFACE *Interface,
    BC250_GCVM_GLOBAL_MMU_EVIDENCE *Evidence)
{
    uint32_t before[BC250_GCVM_GLOBAL_MMU_REG_COUNT];
    uint32_t planned[BC250_GCVM_GLOBAL_MMU_REG_COUNT];
    uint32_t ignoreMasks[BC250_GCVM_GLOBAL_MMU_REG_COUNT];
    uint32_t i, value;
    uint64_t fbStart = 0ull;
    uint64_t fbEnd = 0ull;
    uint64_t dummyPa = 0ull;

    if (Evidence != NULL) {
        memset(Evidence, 0, sizeof(*Evidence));
    }
    if (Config == NULL || Interface == NULL || Evidence == NULL ||
        Interface->Read32 == NULL || Interface->Write32 == NULL ||
        Interface->Flush == NULL ||
        Interface->Identity != BC250_GCVM_GLOBAL_MMU_IDENTITY) {
        return BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT;
    }

    dummyPa = Config->DummyPagePhysical;
    if ((dummyPa & 0xFFFull) != 0ull) {
        Evidence->FailureStep = 1u;
        return BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT;
    }

    /* 1. Preflight e Leitura de Base da VRAM (se nao fornecida explicitamente) */
    fbStart = Config->FbLocationStart;
    fbEnd   = Config->FbLocationEnd;
    if (fbStart == 0ull || fbEnd == 0ull) {
        uint32_t fbBaseReg = 0u;
        uint32_t fbTopReg = 0u;
        if (!Interface->Read32(Interface->Context,
                BC250_REG_GCMC_VM_FB_LOCATION_BASE, &fbBaseReg) ||
            !Interface->Read32(Interface->Context,
                BC250_REG_GCMC_VM_FB_LOCATION_TOP, &fbTopReg)) {
            Evidence->FailureStep = 2u;
            return BC250_GCVM_GLOBAL_MMU_SNAPSHOT_FAILED;
        }
        fbStart = ((uint64_t)fbBaseReg & 0x00FFFFFFull) << 24;
        fbEnd   = (((uint64_t)fbTopReg & 0x00FFFFFFull) << 24) | 0x00FFFFFFull;
    }

    /* 2. Snapshot transacional dos 26 registradores antes de qualquer escrita */
    for (i = 0u; i < BC250_GCVM_GLOBAL_MMU_REG_COUNT; ++i) {
        if (!Interface->Read32(Interface->Context,
                g_Bc250GlobalMmuRegisters[i], &before[i])) {
            Evidence->FailureStep = 3u;
            Evidence->FailureRegister = g_Bc250GlobalMmuRegisters[i];
            return BC250_GCVM_GLOBAL_MMU_SNAPSHOT_FAILED;
        }
        Evidence->Registers[i] = g_Bc250GlobalMmuRegisters[i];
        Evidence->ValuesBefore[i] = before[i];
    }
    Evidence->SnapshotPresent = 1u;
    Evidence->RegisterCount = BC250_GCVM_GLOBAL_MMU_REG_COUNT;

    /* 3. Montagem do plano de escrita */
    memset(ignoreMasks, 0, sizeof(ignoreMasks));

    /* Estagio System */
    planned[0] = 0x00000000u; /* AGP_BASE */
    planned[1] = 0x00FFFFFFu; /* AGP_BOT */
    planned[2] = 0x00000000u; /* AGP_TOP */
    planned[3] = (uint32_t)(fbStart >> 18); /* SYSTEM_APERTURE_LOW_ADDR */
    planned[4] = (uint32_t)(fbEnd >> 18);   /* SYSTEM_APERTURE_HIGH_ADDR */
    planned[5] = (uint32_t)(dummyPa >> 12); /* SYSTEM_APERTURE_DEFAULT_ADDR_LSB */
    planned[6] = (uint32_t)(dummyPa >> 44); /* SYSTEM_APERTURE_DEFAULT_ADDR_MSB */
    planned[7] = (uint32_t)(dummyPa >> 12); /* L2_PROT_FAULT_DEFAULT_ADDR_LO */
    planned[8] = (uint32_t)(dummyPa >> 44); /* L2_PROT_FAULT_DEFAULT_ADDR_HI */
    planned[9] = before[9] | BC250_GCVM_GLOBAL_FAULT_CNTL2_RETRY_BIT; /* FAULT_CNTL2 */

    /* Estagio Identity Aperture (fechada/desabilitada) */
    planned[10] = 0xFFFFFFFFu; /* LOW_ADDR_LO */
    planned[11] = 0x0000000Fu; /* LOW_ADDR_HI */
    planned[12] = 0x00000000u; /* HIGH_ADDR_LO */
    planned[13] = 0x00000000u; /* HIGH_ADDR_HI */
    planned[14] = 0x00000000u; /* PHYS_OFFSET_LO */
    planned[15] = 0x00000000u; /* PHYS_OFFSET_HI */

    /* Estagio Invalidation Ranges (Engines 0 e 17 em faixa completa) */
    planned[16] = 0xFFFFFFFFu; /* ENG0_ADDR_RANGE_LO */
    planned[17] = 0x0000001Fu; /* ENG0_ADDR_RANGE_HI */
    planned[18] = 0xFFFFFFFFu; /* ENG17_ADDR_RANGE_LO */
    planned[19] = 0x0000001Fu; /* ENG17_ADDR_RANGE_HI */

    /* Estagio TLB Control (SYSTEM_ACCESS_MODE=3, ENABLE_ADVANCED_DRIVER_MODEL=1) */
    planned[20] = BC250_GCVM_GLOBAL_TLB_CNTL_TARGET; /* MX_L1_TLB_CNTL = 0x1D59 */

    /* Estagio Cache Control (L2 ligada, VMC_TAP_PDE/PTE_REQUEST_PHYSICAL=0) */
    planned[21] = BC250_GCVM_GLOBAL_L2_CNTL_TARGET;  /* L2_CNTL = 0x00080E01 */
    planned[22] = BC250_GCVM_GLOBAL_L2_CNTL2_TRIGGERS; /* L2_CNTL2 = 0x3 (triggers) */
    ignoreMasks[22] = BC250_GCVM_GLOBAL_L2_CNTL2_TRIGGERS; /* bits 0 e 1 sao limpos pelo silicio */
    planned[23] = BC250_GCVM_GLOBAL_L2_CNTL3_TARGET; /* L2_CNTL3 = 0x80130009 */
    planned[24] = BC250_GCVM_GLOBAL_L2_CNTL4_TARGET; /* L2_CNTL4 = 0x00000001 */
    planned[25] = BC250_GCVM_GLOBAL_L2_CNTL5_TARGET; /* L2_CNTL5 = 0x00003FE0 */

    /* 4. Escrita transacional */
    for (i = 0u; i < BC250_GCVM_GLOBAL_MMU_REG_COUNT; ++i) {
        if (!Interface->Write32(Interface->Context,
                g_Bc250GlobalMmuRegisters[i], planned[i])) {
            Evidence->FailureStep = 4u;
            Evidence->FailureRegister = g_Bc250GlobalMmuRegisters[i];
            Evidence->FailureValue = planned[i];
            goto Rollback;
        }
        Evidence->ValuesProgrammed[i] = planned[i];
    }
    Evidence->ProgramPassed = 1u;

    /* 5. Readback check de todos os 26 registradores */
    for (i = 0u; i < BC250_GCVM_GLOBAL_MMU_REG_COUNT; ++i) {
        uint32_t keep = ~ignoreMasks[i];
        if (!Interface->Read32(Interface->Context,
                g_Bc250GlobalMmuRegisters[i], &value) ||
            ((value & keep) != (planned[i] & keep))) {
            Evidence->FailureStep = 5u;
            Evidence->FailureRegister = g_Bc250GlobalMmuRegisters[i];
            Evidence->FailureValue = value;
            goto Rollback;
        }
    }
    Evidence->ReadbackPassed = 1u;

    /* 6. Invalidação de TLB pós-programação global */
    if (!Interface->Flush(Interface->Context)) {
        Evidence->FailureStep = 6u;
        goto Rollback;
    }
    Evidence->TlbFlushPassed = 1u;

    return BC250_GCVM_GLOBAL_MMU_OK;

Rollback:
    /* Teardown/Rollback em ordem inversa em caso de falha */
    for (i = BC250_GCVM_GLOBAL_MMU_REG_COUNT; i != 0u; --i) {
        if (!Interface->Write32(Interface->Context,
                g_Bc250GlobalMmuRegisters[i - 1u], before[i - 1u])) {
            ++Evidence->RollbackFailureCount;
        }
    }
    for (i = 0u; i < BC250_GCVM_GLOBAL_MMU_REG_COUNT; ++i) {
        uint32_t keep = g_Bc250GlobalMmuRegisters[i] ==
            BC250_REG_GCVM_L2_CNTL2 ?
            ~BC250_GCVM_GLOBAL_L2_CNTL2_TRIGGERS : ~0u;
        if (!Interface->Read32(Interface->Context,
                g_Bc250GlobalMmuRegisters[i], &value) ||
            ((value & keep) != (before[i] & keep))) {
            ++Evidence->RollbackFailureCount;
        }
    }
    if (!Interface->Flush(Interface->Context)) {
        ++Evidence->RollbackFailureCount;
    }
    Evidence->RollbackPassed = Evidence->RollbackFailureCount == 0u;

    if (Evidence->FailureStep == 4u) return BC250_GCVM_GLOBAL_MMU_WRITE_FAILED;
    if (Evidence->FailureStep == 5u) return BC250_GCVM_GLOBAL_MMU_READBACK_FAILED;
    if (Evidence->FailureStep == 6u) return BC250_GCVM_GLOBAL_MMU_TLB_FAILED;
    return BC250_GCVM_GLOBAL_MMU_WRITE_FAILED;
}

BC250_GCVM_GLOBAL_MMU_STATUS Bc250GcvmGlobalMmuRestore(
    const BC250_GCVM_GLOBAL_MMU_INTERFACE *Interface,
    BC250_GCVM_GLOBAL_MMU_EVIDENCE *Evidence)
{
    uint32_t i, value;
    uint32_t flushFailed = 0u;

    if (Interface == NULL || Evidence == NULL ||
        Interface->Read32 == NULL || Interface->Write32 == NULL ||
        Interface->Flush == NULL ||
        Interface->Identity != BC250_GCVM_GLOBAL_MMU_IDENTITY ||
        Evidence->SnapshotPresent == 0u ||
        Evidence->RegisterCount != BC250_GCVM_GLOBAL_MMU_REG_COUNT) {
        return BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT;
    }

    Evidence->RestorePassed = 0u;
    Evidence->RestoreFailureCount = 0u;

    /*
     * Restaura em ordem inversa à programacao. Uma escrita recusada nao pode
     * impedir a tentativa sobre os demais registradores: o veredito continua
     * fail-closed, mas o teardown e' best-effort sobre o snapshot inteiro.
     */
    for (i = BC250_GCVM_GLOBAL_MMU_REG_COUNT; i != 0u; --i) {
        if (!Interface->Write32(Interface->Context,
                Evidence->Registers[i - 1u],
                Evidence->ValuesBefore[i - 1u])) {
            if (Evidence->RestoreFailureCount == 0u) {
                Evidence->FailureStep = 10u;
                Evidence->FailureRegister = Evidence->Registers[i - 1u];
                Evidence->FailureValue = Evidence->ValuesBefore[i - 1u];
            }
            ++Evidence->RestoreFailureCount;
        }
    }

    /* Readback check da restauracao */
    for (i = 0u; i < BC250_GCVM_GLOBAL_MMU_REG_COUNT; ++i) {
        uint32_t keep = Evidence->Registers[i] == BC250_REG_GCVM_L2_CNTL2 ?
            ~BC250_GCVM_GLOBAL_L2_CNTL2_TRIGGERS : ~0u;
        if (!Interface->Read32(Interface->Context,
                Evidence->Registers[i], &value) ||
            (value & keep) != (Evidence->ValuesBefore[i] & keep)) {
            if (Evidence->RestoreFailureCount == 0u) {
                Evidence->FailureStep = 11u;
                Evidence->FailureRegister = Evidence->Registers[i];
                Evidence->FailureValue = value;
            }
            ++Evidence->RestoreFailureCount;
        }
    }

    /* Invalida TLB para expurgar cache remanescente */
    if (!Interface->Flush(Interface->Context)) {
        flushFailed = 1u;
        if (Evidence->RestoreFailureCount == 0u) {
            Evidence->FailureStep = 12u;
        }
        ++Evidence->RestoreFailureCount;
    }

    if (Evidence->RestoreFailureCount != 0u) {
        return flushFailed != 0u && Evidence->RestoreFailureCount == 1u ?
            BC250_GCVM_GLOBAL_MMU_TLB_FAILED :
            BC250_GCVM_GLOBAL_MMU_RESTORE_FAILED;
    }
    Evidence->RestorePassed = 1u;
    return BC250_GCVM_GLOBAL_MMU_OK;
}
