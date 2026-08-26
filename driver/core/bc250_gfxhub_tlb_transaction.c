#include "bc250_gfxhub_tlb_transaction.h"

#include <string.h>

static uint32_t Bc250TlbPlanIsExact(
    const BC250_GFXHUB_TLB_FLUSH_PLAN *Plan)
{
    if (Plan == NULL || Plan->Vmid == 0u || Plan->Vmid >= 16u ||
        Plan->EngineId != 17u || Plan->FlushType > 7u ||
        Plan->RequestRegisterOffset != 0xA2D0u ||
        Plan->AckRegisterOffset != 0xA318u ||
        Plan->RangeLowRegisterOffset != 0xA3A4u ||
        Plan->RangeHighRegisterOffset != 0xA3A8u ||
        Plan->AckMask != (1u << Plan->Vmid) ||
        Plan->ExpectedRangeLow != 0xFFFFFFFFu ||
        Plan->ExpectedRangeHigh != 0x0000001Fu ||
        Plan->RequiresDummyRequestRead != 1u ||
        Plan->UsesSemaphore != 0u) {
        return 0u;
    }
    return Plan->RequestValue == ((1u << Plan->Vmid) |
        (Plan->FlushType << 16) | 0x00F80000u);
}

BC250_TLB_TRANSACTION_STATUS Bc250RunGfxhubTlbTransaction(
    const BC250_GFXHUB_TLB_FLUSH_PLAN *Plan,
    const BC250_TLB_TRANSACTION_INTERFACE *Interface,
    uint32_t MaxPolls,
    BC250_TLB_TRANSACTION_EVIDENCE *Evidence)
{
    BC250_TLB_TRANSACTION_STATUS status = BC250_TLB_TRANSACTION_BAD_ARGUMENT;
    uint32_t rangeSnapshotValid = 0u;
    uint32_t value = 0u;
    uint32_t restoredLow = 0u;
    uint32_t restoredHigh = 0u;
    uint32_t poll;

    if (Evidence != NULL) memset(Evidence, 0, sizeof(*Evidence));
    if (Plan == NULL || Interface == NULL || Evidence == NULL ||
        Interface->Read32 == NULL || Interface->Write32 == NULL ||
        Interface->Stall == NULL || Interface->Context == NULL ||
        Interface->Identity != BC250_TLB_TRANSPORT_IDENTITY || MaxPolls == 0u) {
        return BC250_TLB_TRANSACTION_BAD_ARGUMENT;
    }
    Evidence->Vmid = Plan->Vmid;
    Evidence->EngineId = Plan->EngineId;
    Evidence->RequestValue = Plan->RequestValue;
    if (!Bc250TlbPlanIsExact(Plan)) return BC250_TLB_TRANSACTION_BAD_PLAN;

    if (!Interface->Read32(Interface->Context,
            Plan->RangeLowRegisterOffset, &Evidence->RangeLowBefore) ||
        !Interface->Read32(Interface->Context,
            Plan->RangeHighRegisterOffset, &Evidence->RangeHighBefore)) {
        return BC250_TLB_TRANSACTION_READ_FAILED;
    }
    rangeSnapshotValid = 1u;
    if (!Interface->Read32(Interface->Context,
            Plan->AckRegisterOffset, &Evidence->AckBefore)) {
        status = BC250_TLB_TRANSACTION_READ_FAILED;
        goto restore;
    }
    if (Evidence->AckBefore == 0xFFFFFFFFu) {
        status = BC250_TLB_TRANSACTION_UNDECODED;
        goto restore;
    }

    if (!Interface->Write32(Interface->Context,
            Plan->RangeLowRegisterOffset, Plan->ExpectedRangeLow) ||
        !Interface->Write32(Interface->Context,
            Plan->RangeHighRegisterOffset, Plan->ExpectedRangeHigh)) {
        status = BC250_TLB_TRANSACTION_WRITE_FAILED;
        goto restore;
    }
    if (!Interface->Read32(Interface->Context,
            Plan->RangeLowRegisterOffset, &Evidence->RangeLowProgrammed) ||
        !Interface->Read32(Interface->Context,
            Plan->RangeHighRegisterOffset, &Evidence->RangeHighProgrammed)) {
        status = BC250_TLB_TRANSACTION_READ_FAILED;
        goto restore;
    }
    if (Evidence->RangeLowProgrammed != Plan->ExpectedRangeLow ||
        Evidence->RangeHighProgrammed != Plan->ExpectedRangeHigh) {
        status = BC250_TLB_TRANSACTION_RANGE_READBACK_FAILED;
        goto restore;
    }

    if (!Interface->Write32(Interface->Context,
            Plan->RequestRegisterOffset, Plan->RequestValue)) {
        status = BC250_TLB_TRANSACTION_WRITE_FAILED;
        goto restore;
    }
    Evidence->RequestWritten = 1u;
    if (!Interface->Read32(Interface->Context,
            Plan->RequestRegisterOffset, &Evidence->RequestDummyRead)) {
        status = BC250_TLB_TRANSACTION_READ_FAILED;
        goto restore;
    }
    Evidence->DummyReadCompleted = 1u;

    status = BC250_TLB_TRANSACTION_TIMEOUT;
    for (poll = 0u; poll < MaxPolls; ++poll) {
        if (!Interface->Read32(
                Interface->Context, Plan->AckRegisterOffset, &value)) {
            status = BC250_TLB_TRANSACTION_READ_FAILED;
            break;
        }
        Evidence->AckAfter = value;
        Evidence->PollCount = poll + 1u;
        if (value == 0xFFFFFFFFu) {
            status = BC250_TLB_TRANSACTION_UNDECODED;
            break;
        }
        if ((value & Plan->AckMask) != 0u) {
            Evidence->AckObserved = 1u;
            status = BC250_TLB_TRANSACTION_OK;
            break;
        }
        Interface->Stall(Interface->Context, 1u);
    }

restore:
    if (rangeSnapshotValid != 0u) {
        if (!Interface->Write32(Interface->Context,
                Plan->RangeLowRegisterOffset, Evidence->RangeLowBefore) ||
            !Interface->Write32(Interface->Context,
                Plan->RangeHighRegisterOffset, Evidence->RangeHighBefore) ||
            !Interface->Read32(Interface->Context,
                Plan->RangeLowRegisterOffset, &restoredLow) ||
            !Interface->Read32(Interface->Context,
                Plan->RangeHighRegisterOffset, &restoredHigh) ||
            restoredLow != Evidence->RangeLowBefore ||
            restoredHigh != Evidence->RangeHighBefore) {
            return BC250_TLB_TRANSACTION_RESTORE_FAILED;
        }
        Evidence->RangeRestorePassed = 1u;
    }
    if (status == BC250_TLB_TRANSACTION_OK &&
        Evidence->RequestWritten != 0u &&
        Evidence->DummyReadCompleted != 0u &&
        Evidence->AckObserved != 0u &&
        Evidence->RangeRestorePassed != 0u) {
        Evidence->PhysicalReady = 1u;
    }
    return status;
}
