#include "bc250_gcvm_vmid8_transaction.h"

#include <string.h>

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
