#include "bc250_sdma_f32_transaction.h"

#include <string.h>

static void Bc250SdmaF32Delay(
    const BC250_SDMA_F32_INTERFACE *Interface, uint32_t Milliseconds)
{
    uint32_t i;
    for (i = 0u; i < Milliseconds; ++i)
        Interface->Stall(Interface->Context, 1000u);
}

static uint32_t Bc250SdmaF32Restore(
    const BC250_SDMA_F32_INTERFACE *Interface,
    BC250_SDMA_F32_EVIDENCE *Evidence)
{
    uint32_t value;
    if (!Interface->Write32(Interface->Context, BC250_SDMA0_F32_CNTL,
            BC250_SDMA_F32_HALT) ||
        !Interface->Write32(Interface->Context, BC250_SDMA0_CNTL,
            Evidence->ControlBefore) ||
        !Interface->Write32(Interface->Context, BC250_SDMA0_CLK_CTRL,
            Evidence->ClockBefore) ||
        !Interface->Write32(Interface->Context, BC250_SDMA0_F32_CNTL,
            Evidence->F32Before)) return 0u;
    if (!Interface->Read32(Interface->Context, BC250_SDMA0_CLK_CTRL,
            &Evidence->ClockRestored) ||
        !Interface->Read32(Interface->Context, BC250_SDMA0_CNTL,
            &Evidence->ControlRestored) ||
        !Interface->Read32(Interface->Context, BC250_SDMA0_F32_CNTL,
            &Evidence->F32Restored)) return 0u;
    value = (Evidence->ClockRestored == Evidence->ClockBefore &&
        Evidence->ControlRestored == Evidence->ControlBefore &&
        Evidence->F32Restored == Evidence->F32Before) ? 1u : 0u;
    return value;
}

BC250_SDMA_F32_STATUS Bc250RunSdmaF32Transaction(
    const BC250_SDMA_F32_INTERFACE *Interface,
    BC250_SDMA_F32_EVIDENCE *Evidence)
{
    BC250_SDMA_F32_STATUS status = BC250_SDMA_F32_OK;
    uint32_t i, status2, pc, previousPc = 0u;
    if (Evidence != NULL) memset(Evidence, 0, sizeof(*Evidence));
    if (Interface == NULL || Evidence == NULL || Interface->Read32 == NULL ||
        Interface->Write32 == NULL || Interface->Stall == NULL ||
        Interface->Identity != BC250_SDMA_F32_IDENTITY)
        return BC250_SDMA_F32_BAD_ARGUMENT;
    if (!Interface->Read32(Interface->Context, BC250_SDMA0_CLK_CTRL,
            &Evidence->ClockBefore) ||
        !Interface->Read32(Interface->Context, BC250_SDMA0_CNTL,
            &Evidence->ControlBefore) ||
        !Interface->Read32(Interface->Context, BC250_SDMA0_F32_CNTL,
            &Evidence->F32Before) ||
        !Interface->Read32(Interface->Context, BC250_SDMA0_STATUS2_REG,
            &Evidence->Status2Before) ||
        Evidence->ClockBefore == 0xFFFFFFFFu ||
        Evidence->ControlBefore == 0xFFFFFFFFu ||
        Evidence->F32Before == 0xFFFFFFFFu ||
        Evidence->Status2Before == 0xFFFFFFFFu)
        return BC250_SDMA_F32_PREFLIGHT_FAILED;
    if (!Interface->Write32(Interface->Context, BC250_SDMA0_CLK_CTRL,
            Evidence->ClockBefore | BC250_SDMA_CLK_FORCE_MASK) ||
        !Interface->Write32(Interface->Context, BC250_SDMA0_CNTL,
            Evidence->ControlBefore | BC250_SDMA_CNTL_CTXEMPTY_INT_EN) ||
        !Interface->Read32(Interface->Context, BC250_SDMA0_CLK_CTRL,
            &Evidence->ClockApplied) ||
        !Interface->Read32(Interface->Context, BC250_SDMA0_CNTL,
            &Evidence->ControlApplied)) {
        status = BC250_SDMA_F32_WRITE_FAILED;
        goto Restore;
    }
    if ((Evidence->ClockApplied & BC250_SDMA_CLK_FORCE_MASK) !=
            BC250_SDMA_CLK_FORCE_MASK ||
        (Evidence->ControlApplied & BC250_SDMA_CNTL_CTXEMPTY_INT_EN) == 0u) {
        status = BC250_SDMA_F32_READBACK_FAILED;
        goto Restore;
    }
    Evidence->ApplyReadbackPassed = 1u;
    if (!Interface->Write32(Interface->Context, BC250_SDMA0_F32_CNTL,
            BC250_SDMA_F32_HALT) ||
        !Interface->Write32(Interface->Context, BC250_SDMA0_F32_CNTL,
            BC250_SDMA_F32_HALT | BC250_SDMA_F32_RESET)) {
        status = BC250_SDMA_F32_WRITE_FAILED;
        goto Restore;
    }
    Bc250SdmaF32Delay(Interface, 50u);
    if (!Interface->Write32(Interface->Context, BC250_SDMA0_F32_CNTL,
            BC250_SDMA_F32_HALT) ||
        !Interface->Write32(Interface->Context, BC250_SDMA0_F32_CNTL, 0u) ||
        !Interface->Read32(Interface->Context, BC250_SDMA0_F32_CNTL,
            &Evidence->F32Applied)) {
        status = BC250_SDMA_F32_WRITE_FAILED;
        goto Restore;
    }
    if ((Evidence->F32Applied & (BC250_SDMA_F32_HALT |
            BC250_SDMA_F32_RESET)) != 0u) {
        status = BC250_SDMA_F32_READBACK_FAILED;
        goto Restore;
    }
    Bc250SdmaF32Delay(Interface, 100u);
    for (i = 0u; i < BC250_SDMA_F32_SAMPLE_COUNT; ++i) {
        if (!Interface->Read32(Interface->Context, BC250_SDMA0_STATUS2_REG,
                &status2)) {
            status = BC250_SDMA_F32_PREFLIGHT_FAILED;
            goto Restore;
        }
        pc = (status2 & BC250_SDMA_F32_PC_MASK) >> BC250_SDMA_F32_PC_SHIFT;
        if (i == 0u) Evidence->PcFirst = pc;
        else if (pc != previousPc) ++Evidence->PcTransitions;
        previousPc = pc;
        Evidence->PcLast = pc;
        Interface->Stall(Interface->Context, 1000u);
    }
    if (Evidence->PcTransitions == 0u) status = BC250_SDMA_F32_PC_STATIC;
Restore:
    Evidence->RestorePassed = Bc250SdmaF32Restore(Interface, Evidence);
    if (Evidence->RestorePassed == 0u) return BC250_SDMA_F32_RESTORE_FAILED;
    if (status == BC250_SDMA_F32_OK) Evidence->PhysicalReady = 1u;
    return status;
}
