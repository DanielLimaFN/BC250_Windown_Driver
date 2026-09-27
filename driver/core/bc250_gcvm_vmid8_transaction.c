#include "bc250_gcvm_vmid8_transaction.h"
#include "bc250_gcvm_contract.h"

#include <string.h>

static const uint32_t g_ComputeRegisters[7] = {
    BC250_GCVM_VMID9_PT_BASE_LO, BC250_GCVM_VMID9_PT_BASE_HI,
    BC250_GCVM_VMID9_PT_START_LO, BC250_GCVM_VMID9_PT_START_HI,
    BC250_GCVM_VMID9_PT_END_LO, BC250_GCVM_VMID9_PT_END_HI,
    BC250_GCVM_VMID9_CNTL
};

uint32_t Bc250GcvmComputeCanSwitch(uint64_t Submitted, uint64_t Retired)
{
    /* A future/corrupt retirement value must not authorize reuse either. */
    return Submitted == Retired;
}

static uint32_t ComputeIoValid(const BC250_GCVM_VMID8_INTERFACE *Io)
{
    return Io != NULL && Io->Read32 != NULL && Io->Write32 != NULL &&
        Io->Flush != NULL && Io->Identity == BC250_GCVM_VMID8_IDENTITY;
}

static uint32_t ComputeWriteChecked(const BC250_GCVM_VMID8_INTERFACE *Io,
    uint32_t Offset, uint32_t Value)
{
    uint32_t observed = 0u;
    return Io->Write32(Io->Context, Offset, Value) &&
        Io->Read32(Io->Context, Offset, &observed) && observed == Value;
}

BC250_GCVM_VMID8_STATUS Bc250GcvmComputeRestore(
    const BC250_GCVM_VMID8_INTERFACE *Io, BC250_GCVM_COMPUTE_BINDING *Binding)
{
    uint32_t i, value = 0u, ok = 1u;
    if (!ComputeIoValid(Io) || Binding == NULL) return BC250_GCVM_VMID8_BAD_ARGUMENT;
    Binding->Ready = 0u;
    if (!Binding->SnapshotValid) return BC250_GCVM_VMID8_OK;
    if (!Io->Read32(Io->Context, BC250_GCVM_VMID9_CNTL, &value) ||
        !ComputeWriteChecked(Io, BC250_GCVM_VMID9_CNTL,
            value & ~BC250_GCVM_ENABLE_CONTEXT)) return BC250_GCVM_VMID8_RESTORE_FAILED;
    for (i = 0u; i < 6u; ++i) {
        if (!ComputeWriteChecked(Io, g_ComputeRegisters[i], Binding->Before[i])) ok = 0u;
    }
    if (!ok || !ComputeWriteChecked(Io, BC250_GCVM_VMID9_CNTL, Binding->Before[6]) ||
        !Io->Flush(Io->Context)) return BC250_GCVM_VMID8_RESTORE_FAILED;
    Binding->SnapshotValid = 0u;
    Binding->Root = 0ull;
    return BC250_GCVM_VMID8_OK;
}

BC250_GCVM_VMID8_STATUS Bc250GcvmComputeInstall(
    uint64_t Root, uint64_t Submitted, uint64_t Retired,
    const BC250_GCVM_VMID8_INTERFACE *Io, BC250_GCVM_COMPUTE_BINDING *Binding)
{
    uint32_t i, value = 0u, programmed[7];
    BC250_GCVM_VMID8_STATUS failure = BC250_GCVM_VMID8_READBACK_FAILED;
    if (!ComputeIoValid(Io) || Binding == NULL || Root == 0ull || (Root & 0xFFFull))
        return BC250_GCVM_VMID8_BAD_ARGUMENT;
    /* Never rewrite even the same root while the GPU is using this VMID. */
    if (!Bc250GcvmComputeCanSwitch(Submitted, Retired)) return BC250_GCVM_VMID8_CONTEXT_ENABLED;
    if (Binding->SnapshotValid && !Binding->Ready) return BC250_GCVM_VMID8_RESTORE_FAILED;
    if (!Binding->SnapshotValid) {
        for (i = 0u; i < 7u; ++i) {
            if (!Io->Read32(Io->Context, g_ComputeRegisters[i], &Binding->Before[i]))
                return BC250_GCVM_VMID8_READ_FAILED;
        }
        /* Do not steal a context enabled by another owner. */
        if (Binding->Before[6] & BC250_GCVM_ENABLE_CONTEXT) return BC250_GCVM_VMID8_CONTEXT_ENABLED;
        Binding->SnapshotValid = 1u;
    }
    Binding->Ready = 0u;
    if (!Io->Read32(Io->Context, BC250_GCVM_VMID9_CNTL, &value) ||
        !ComputeWriteChecked(Io, BC250_GCVM_VMID9_CNTL, value & ~BC250_GCVM_ENABLE_CONTEXT)) goto Fail;
    programmed[0] = (uint32_t)Root | (uint32_t)BC250_PTE_VALID;
    programmed[1] = (uint32_t)(Root >> 32);
    programmed[2] = 0u; programmed[3] = 0u;
    programmed[4] = BC250_GCVM_CONTEXT8_PT_END_LO_VALUE;
    programmed[5] = BC250_GCVM_CONTEXT8_PT_END_HI_VALUE;
    /* Preserve fields outside the catalogued bits 0..22 (SOC15 RMW). */
    programmed[6] = (value & ~0x007FFFFFu) |
        (BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE & 0x007FFFFFu);
    for (i = 0u; i < 7u; ++i) {
        if (!ComputeWriteChecked(Io, g_ComputeRegisters[i], programmed[i])) goto Fail;
    }
    if (!Io->Flush(Io->Context)) { failure = BC250_GCVM_VMID8_TLB_FAILED; goto Fail; }
    Binding->Root = Root;
    Binding->Ready = 1u;
    return BC250_GCVM_VMID8_OK;
Fail:
    if (Bc250GcvmComputeRestore(Io, Binding) != BC250_GCVM_VMID8_OK)
        return BC250_GCVM_VMID8_RESTORE_FAILED;
    return failure;
}

static const uint32_t g_Bc250Vmid8Registers[6] = {
    BC250_GCVM_VMID8_PT_BASE_LO, BC250_GCVM_VMID8_PT_BASE_HI,
    BC250_GCVM_VMID8_PT_START_LO, BC250_GCVM_VMID8_PT_START_HI,
    BC250_GCVM_VMID8_PT_END_LO, BC250_GCVM_VMID8_PT_END_HI
};

static uint32_t Bc250RestoreVmid8(
    const BC250_GCVM_VMID8_INTERFACE *Interface,
    const uint32_t Before[6])
{
    uint32_t i, value;
    for (i = 6u; i != 0u; --i) {
        if (!Interface->Write32(Interface->Context,
                g_Bc250Vmid8Registers[i - 1u], Before[i - 1u])) return 0u;
    }
    for (i = 0u; i < 6u; ++i) {
        if (!Interface->Read32(Interface->Context,
                g_Bc250Vmid8Registers[i], &value) || value != Before[i]) {
            return 0u;
        }
    }
    return 1u;
}

BC250_GCVM_VMID8_STATUS Bc250RunGcvmVmid8InertTransaction(
    uint64_t PageTablePhysical,
    const BC250_GCVM_VMID8_INTERFACE *Interface,
    BC250_GCVM_VMID8_EVIDENCE *Evidence)
{
    uint32_t before[6], programmed[6], value, i;
    BC250_GCVM_VMID8_STATUS status = BC250_GCVM_VMID8_OK;

    if (Evidence != NULL) memset(Evidence, 0, sizeof(*Evidence));
    if (Interface == NULL || Evidence == NULL || Interface->Read32 == NULL ||
        Interface->Write32 == NULL || Interface->Flush == NULL ||
        Interface->Identity != BC250_GCVM_VMID8_IDENTITY ||
        (PageTablePhysical & 0xFFFu) != 0u) {
        return BC250_GCVM_VMID8_BAD_ARGUMENT;
    }
    Evidence->PageTablePhysical = PageTablePhysical;
    if (!Interface->Read32(Interface->Context, BC250_GCVM_VMID8_CNTL,
            &Evidence->ContextControlBefore)) {
        return BC250_GCVM_VMID8_READ_FAILED;
    }
    if ((Evidence->ContextControlBefore & BC250_GCVM_ENABLE_CONTEXT) != 0u) {
        return BC250_GCVM_VMID8_CONTEXT_ENABLED;
    }
    for (i = 0u; i < 6u; ++i) {
        if (!Interface->Read32(Interface->Context,
                g_Bc250Vmid8Registers[i], &before[i])) {
            return BC250_GCVM_VMID8_READ_FAILED;
        }
    }
    Evidence->BaseLowBefore = before[0];
    Evidence->BaseHighBefore = before[1];
    Evidence->StartLowBefore = before[2];
    Evidence->StartHighBefore = before[3];
    Evidence->EndLowBefore = before[4];
    Evidence->EndHighBefore = before[5];
    programmed[0] = (uint32_t)PageTablePhysical;
    programmed[1] = (uint32_t)(PageTablePhysical >> 32);
    programmed[2] = 0u;
    programmed[3] = 0u;
    programmed[4] = 0u;
    programmed[5] = 0u;

    for (i = 0u; i < 6u; ++i) {
        if (!Interface->Write32(Interface->Context,
                g_Bc250Vmid8Registers[i], programmed[i])) {
            status = BC250_GCVM_VMID8_WRITE_FAILED;
            goto Restore;
        }
    }
    for (i = 0u; i < 6u; ++i) {
        if (!Interface->Read32(Interface->Context,
                g_Bc250Vmid8Registers[i], &value) || value != programmed[i]) {
            status = BC250_GCVM_VMID8_READBACK_FAILED;
            goto Restore;
        }
    }
    Evidence->ProgramReadbackPassed = 1u;
    if (!Interface->Flush(Interface->Context)) {
        status = BC250_GCVM_VMID8_TLB_FAILED;
        goto Restore;
    }
    Evidence->TlbFlushPassed = 1u;

Restore:
    Evidence->RestorePassed = Bc250RestoreVmid8(Interface, before);
    if (Evidence->RestorePassed == 0u) return BC250_GCVM_VMID8_RESTORE_FAILED;
    if (status == BC250_GCVM_VMID8_OK) Evidence->PhysicalReady = 1u;
    return status;
}

uint32_t Bc250GcvmBuildVmidCntl(
    uint32_t EnableContext,
    uint32_t PageTableLevels,
    uint32_t LeafIndexBits)
{
    uint32_t depth;
    uint32_t blockSize;

    if (PageTableLevels == 0u || PageTableLevels > 4u) {
        return 0u;
    }
    if (LeafIndexBits < 9u || LeafIndexBits > 15u) {
        return 0u;
    }
    depth = PageTableLevels - 1u;
    blockSize = LeafIndexBits - 9u;

    return BC250_GCVM_CONTEXT_CNTL_FAULT_DEFAULTS_MASK |
           (EnableContext ? BC250_GCVM_ENABLE_CONTEXT : 0u) |
           ((depth << BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_DEPTH_SHIFT) &
            BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_DEPTH_MASK) |
           ((blockSize << BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_BLOCK_SIZE_SHIFT) &
            BC250_GCVM_CONTEXT_CNTL_PAGE_TABLE_BLOCK_SIZE_MASK);
}

/*
 * Inventario dos 16 contextos GCVM. Ver o bloco longo no header.
 * SOMENTE LEITURA: nao escreve registrador nenhum.
 */
uint32_t Bc250GcvmContextCntlOffset(uint32_t Vmid)
{
    return BC250_GCVM_CNTL_BASE + (Vmid * BC250_GCVM_CNTL_STRIDE);
}

uint32_t Bc250GcvmContextPtBaseLoOffset(uint32_t Vmid)
{
    return BC250_GCVM_PT_BASE_LO_BASE + (Vmid * BC250_GCVM_PT_BASE_STRIDE);
}

BC250_GCVM_VMID8_STATUS Bc250GcvmScanContexts(
    const BC250_GCVM_VMID8_INTERFACE *Interface,
    BC250_GCVM_CONTEXT_INVENTORY *Inventory)
{
    uint32_t v;

    if (Inventory == NULL) return BC250_GCVM_VMID8_BAD_ARGUMENT;
    /*
     * Zeragem EXPLICITA, campo a campo. memset sobre uma estrutura deste
     * tamanho e' reemitido pelo /O2 como chamada real a memset, que NAO existe
     * no link offline das suites -- armadilha ja registrada no projeto quando
     * uma copia caractere a caractere virou memcpy e quebrou o link.
     */
    for (v = 0u; v < BC250_GCVM_CONTEXT_COUNT; ++v) {
        Inventory->Entries[v].Vmid = 0u;
        Inventory->Entries[v].CntlRead = 0u;
        Inventory->Entries[v].Cntl = 0u;
        Inventory->Entries[v].Enabled = 0u;
        Inventory->Entries[v].PtBaseRead = 0u;
        Inventory->Entries[v].PageTableBase = 0ull;
        Inventory->Entries[v].InUse = 0u;
    }
    Inventory->Scanned = 0u;
    Inventory->InUseCount = 0u;
    Inventory->FreeCount = 0u;
    Inventory->UnreadableCount = 0u;
    Inventory->FreeMask = 0u;
    if (Interface == NULL || Interface->Read32 == NULL ||
        Interface->Identity != BC250_GCVM_VMID8_IDENTITY) {
        return BC250_GCVM_VMID8_BAD_ARGUMENT;
    }

    for (v = 0u; v < BC250_GCVM_CONTEXT_COUNT; ++v) {
        BC250_GCVM_CONTEXT_INVENTORY_ENTRY *e = &Inventory->Entries[v];
        uint32_t lo = 0u, hi = 0u;

        e->Vmid = v;
        ++Inventory->Scanned;

        if (Interface->Read32(Interface->Context,
                Bc250GcvmContextCntlOffset(v), &e->Cntl) != 0u) {
            e->CntlRead = 1u;
            e->Enabled = (e->Cntl & BC250_GCVM_CONTEXT_CNTL_ENABLE_CONTEXT_MASK) ? 1u : 0u;
        }
        if (Interface->Read32(Interface->Context,
                Bc250GcvmContextPtBaseLoOffset(v), &lo) != 0u &&
            Interface->Read32(Interface->Context,
                Bc250GcvmContextPtBaseLoOffset(v) + 4u, &hi) != 0u) {
            e->PtBaseRead = 1u;
            /*
             * Os bits baixos da PT_BASE carregam flags (BC250_PTE_VALID entre
             * elas). Mascarar para 4 KB devolve o endereco da raiz; NAO
             * mascarar faria um contexto livre com flag parecer ocupado.
             */
            e->PageTableBase = (((uint64_t)hi) << 32) |
                               ((uint64_t)lo & ~0xFFFull);
        }

        if (e->CntlRead == 0u && e->PtBaseRead == 0u) {
            ++Inventory->UnreadableCount;
            continue;   /* "nao sei" nunca vira "livre" */
        }

        e->InUse = (e->Enabled != 0u || e->PageTableBase != 0ull) ? 1u : 0u;
        if (e->InUse != 0u) {
            ++Inventory->InUseCount;
        } else {
            ++Inventory->FreeCount;
            Inventory->FreeMask |= (1u << v);
        }
    }

    return BC250_GCVM_VMID8_OK;
}

BC250_GCVM_VMID8_STATUS Bc250GcvmInstallVmid(
    uint32_t Vmid,
    uint64_t PageTablePhysical,
    const BC250_GCVM_VMID8_INTERFACE *Interface,
    BC250_GCVM_VMID8_INSTALL_EVIDENCE *Evidence)
{
    uint32_t programmed[6], value, i;
    uint32_t regs[6];
    uint32_t cntl;
    uint32_t cntlReg;

    if (Evidence != NULL) memset(Evidence, 0, sizeof(*Evidence));
    if (Interface == NULL || Evidence == NULL || Interface->Read32 == NULL ||
        Interface->Write32 == NULL || Interface->FlushVmid == NULL ||
        Interface->Identity != BC250_GCVM_VMID8_IDENTITY ||
        Vmid >= BC250_GCVM_CONTEXT_COUNT ||
        PageTablePhysical == 0ull ||
        (PageTablePhysical & 0xFFFu) != 0u) {
        return BC250_GCVM_VMID8_BAD_ARGUMENT;
    }

    cntlReg = Bc250GcvmContextCntlOffset(Vmid);
    regs[0] = Bc250GcvmContextPtBaseLoOffset(Vmid);
    regs[1] = regs[0] + 4u;
    regs[2] = BC250_GCVM_PT_START_LO_BASE +
        (Vmid * BC250_GCVM_PT_BASE_STRIDE);
    regs[3] = regs[2] + 4u;
    regs[4] = BC250_GCVM_PT_END_LO_BASE +
        (Vmid * BC250_GCVM_PT_BASE_STRIDE);
    regs[5] = regs[4] + 4u;

    Evidence->PageTablePhysical = PageTablePhysical;
    Evidence->Vmid = Vmid;
    programmed[0] = (uint32_t)((PageTablePhysical | BC250_PTE_VALID) & 0xFFFFFFFFull);
    programmed[1] = (uint32_t)(PageTablePhysical >> 32);
    programmed[2] = BC250_GCVM_CONTEXT8_PT_START_VALUE;
    programmed[3] = 0u;
    programmed[4] = BC250_GCVM_CONTEXT8_PT_END_LO_VALUE;
    programmed[5] = BC250_GCVM_CONTEXT8_PT_END_HI_VALUE;

    /* 1. Escrever registradores de endereco da tabela */
    for (i = 0u; i < 6u; ++i) {
        if (!Interface->Write32(Interface->Context,
                regs[i], programmed[i])) {
            Evidence->FailureStep = 1u;
            Evidence->FailureRegister = regs[i];
            Evidence->FailureValue = programmed[i];
            return BC250_GCVM_VMID8_WRITE_FAILED;
        }
    }

    /* 2. Readback check dos 6 registradores */
    for (i = 0u; i < 6u; ++i) {
        if (!Interface->Read32(Interface->Context,
                regs[i], &value) || value != programmed[i]) {
            Evidence->FailureStep = 2u;
            Evidence->FailureRegister = regs[i];
            Evidence->FailureValue = value;
            return BC250_GCVM_VMID8_READBACK_FAILED;
        }
    }
    Evidence->BaseLowProgrammed = programmed[0];
    Evidence->BaseHighProgrammed = programmed[1];
    Evidence->StartLowProgrammed = programmed[2];
    Evidence->StartHighProgrammed = programmed[3];
    Evidence->EndLowProgrammed = programmed[4];
    Evidence->EndHighProgrammed = programmed[5];

    /* 3. Programar e conferir CNTL com geometria 34-bit / 2-level e observabilidade de faults (0x007FFE03) */
    cntl = BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE;
    if (!Interface->Write32(Interface->Context, cntlReg, cntl)) {
        Evidence->FailureStep = 3u;
        Evidence->FailureRegister = cntlReg;
        Evidence->FailureValue = cntl;
        return BC250_GCVM_VMID8_WRITE_FAILED;
    }
    if (!Interface->Read32(Interface->Context, cntlReg, &value) ||
        value != cntl) {
        Evidence->FailureStep = 4u;
        Evidence->FailureRegister = cntlReg;
        Evidence->FailureValue = value;
        (void)Interface->Write32(Interface->Context, cntlReg, 0u);
        return BC250_GCVM_VMID8_READBACK_FAILED;
    }
    Evidence->ContextControlProgrammed = cntl;
    Evidence->ProgramReadbackPassed = 1u;

    /* 4. Invalidacao de TLB na engine para o VMID */
    if (!Interface->FlushVmid(Interface->Context, Vmid)) {
        Evidence->FailureStep = 5u;
        (void)Interface->Write32(Interface->Context, cntlReg, 0u);
        return BC250_GCVM_VMID8_TLB_FAILED;
    }
    Evidence->TlbFlushPassed = 1u;
    Evidence->PhysicalReady = 1u;

    return BC250_GCVM_VMID8_OK;
}

BC250_GCVM_VMID8_STATUS Bc250GcvmVmid8Install(
    uint64_t PageTablePhysical,
    const BC250_GCVM_VMID8_INTERFACE *Interface,
    BC250_GCVM_VMID8_INSTALL_EVIDENCE *Evidence)
{
    return Bc250GcvmInstallVmid(BC250_GCVM_VMID8, PageTablePhysical, Interface, Evidence);
}

BC250_GCVM_VMID8_STATUS Bc250GcvmUninstallVmid(
    uint32_t Vmid,
    const BC250_GCVM_VMID8_INTERFACE *Interface,
    BC250_GCVM_VMID8_INSTALL_EVIDENCE *Evidence)
{
    uint32_t regs[6];
    uint32_t cntlReg;
    uint32_t value = 0u;
    uint32_t i;

    if (Evidence != NULL) memset(Evidence, 0, sizeof(*Evidence));
    if (Interface == NULL || Evidence == NULL || Interface->Read32 == NULL ||
        Interface->Write32 == NULL || Interface->FlushVmid == NULL ||
        Interface->Identity != BC250_GCVM_VMID8_IDENTITY ||
        Vmid >= BC250_GCVM_CONTEXT_COUNT) {
        return BC250_GCVM_VMID8_BAD_ARGUMENT;
    }

    Evidence->Vmid = Vmid;
    cntlReg = Bc250GcvmContextCntlOffset(Vmid);
    regs[0] = Bc250GcvmContextPtBaseLoOffset(Vmid);
    regs[1] = regs[0] + 4u;
    regs[2] = BC250_GCVM_PT_START_LO_BASE +
        (Vmid * BC250_GCVM_PT_BASE_STRIDE);
    regs[3] = regs[2] + 4u;
    regs[4] = BC250_GCVM_PT_END_LO_BASE +
        (Vmid * BC250_GCVM_PT_BASE_STRIDE);
    regs[5] = regs[4] + 4u;

    /*
     * 1. CNTL primeiro. Enquanto ENABLE_CONTEXT estiver ligado, o walker pode
     *    nascer uma traducao nova a partir da raiz que estamos prestes a
     *    abandonar.
     */
    if (!Interface->Write32(Interface->Context, cntlReg, 0u)) {
        Evidence->FailureStep = 20u;
        Evidence->FailureRegister = cntlReg;
        return BC250_GCVM_VMID8_WRITE_FAILED;
    }
    /* O desarme e PROVADO por readback, nunca suposto. */
    if (!Interface->Read32(Interface->Context, cntlReg, &value) ||
        (value & BC250_GCVM_ENABLE_CONTEXT) != 0u) {
        Evidence->FailureStep = 21u;
        Evidence->FailureRegister = cntlReg;
        Evidence->FailureValue = value;
        return BC250_GCVM_VMID8_READBACK_FAILED;
    }
    Evidence->ProgramReadbackPassed = 1u;

    /* 2. Zerar os registradores de endereco, ja com o contexto desabilitado. */
    for (i = 0u; i < 6u; ++i) {
        if (!Interface->Write32(Interface->Context, regs[i], 0u)) {
            Evidence->FailureStep = 22u;
            Evidence->FailureRegister = regs[i];
            return BC250_GCVM_VMID8_WRITE_FAILED;
        }
    }

    /*
     * 3. Invalidar a TLB DESTE VMID. Traducao ja cacheada sobrevive ao
     *    desarme do CNTL -- sem isto o desarme seria parcial e pareceria
     *    completo.
     */
    if (!Interface->FlushVmid(Interface->Context, Vmid)) {
        Evidence->FailureStep = 23u;
        return BC250_GCVM_VMID8_TLB_FAILED;
    }
    Evidence->TlbFlushPassed = 1u;
    return BC250_GCVM_VMID8_OK;
}

BC250_GCVM_VMID8_STATUS Bc250GcvmVmid8Uninstall(
    const BC250_GCVM_VMID8_INTERFACE *Interface,
    BC250_GCVM_VMID8_INSTALL_EVIDENCE *Evidence)
{
    uint32_t i, value;

    if (Evidence != NULL) memset(Evidence, 0, sizeof(*Evidence));
    if (Interface == NULL || Interface->Read32 == NULL ||
        Interface->Write32 == NULL || Interface->Flush == NULL ||
        Interface->Identity != BC250_GCVM_VMID8_IDENTITY) {
        return BC250_GCVM_VMID8_BAD_ARGUMENT;
    }

    /* Desabilita CNTL primeiro */
    if (!Interface->Write32(Interface->Context, BC250_GCVM_VMID8_CNTL, 0u)) {
        if (Evidence != NULL) {
            Evidence->FailureStep = 10u;
            Evidence->FailureRegister = BC250_GCVM_VMID8_CNTL;
        }
        return BC250_GCVM_VMID8_WRITE_FAILED;
    }
    if (!Interface->Read32(Interface->Context, BC250_GCVM_VMID8_CNTL, &value) ||
        (value & BC250_GCVM_ENABLE_CONTEXT) != 0u) {
        if (Evidence != NULL) {
            Evidence->FailureStep = 11u;
            Evidence->FailureRegister = BC250_GCVM_VMID8_CNTL;
            Evidence->FailureValue = value;
        }
        return BC250_GCVM_VMID8_READBACK_FAILED;
    }

    /* Zera registradores de enderecamento */
    for (i = 0u; i < 6u; ++i) {
        (void)Interface->Write32(Interface->Context, g_Bc250Vmid8Registers[i], 0u);
    }

    /* Invalida TLB para garantir que traducoes antigas sumam */
    if (!Interface->Flush(Interface->Context)) {
        if (Evidence != NULL) {
            Evidence->FailureStep = 12u;
        }
        return BC250_GCVM_VMID8_TLB_FAILED;
    }

    return BC250_GCVM_VMID8_OK;
}
