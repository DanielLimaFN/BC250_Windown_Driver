#include "bc250_psp_ring_transaction.h"

#include <string.h>

static uint32_t Bc250PspPollAck(
    const BC250_PSP_RING_INTERFACE *Interface,
    uint32_t MaxPolls, uint32_t *Polls, uint32_t *Ack)
{
    uint32_t i;
    for (i = 1u; i <= MaxPolls; ++i) {
        if (!Interface->Read32(Interface->Context,
                BC250_PSP_RING_C2PMSG_64, Ack)) return 0u;
        *Polls = i;
        if (*Ack != 0xFFFFFFFFu &&
            (*Ack & BC250_PSP_RING_TOS_READY) != 0u) return 1u;
        Interface->Stall(Interface->Context, 1000u);
    }
    return 0u;
}

static uint32_t Bc250PspRestore(
    const BC250_PSP_RING_INTERFACE *Interface,
    const BC250_PSP_RING_EVIDENCE *Evidence)
{
    const uint32_t offsets[4] = {
        BC250_PSP_RING_C2PMSG_67, BC250_PSP_RING_C2PMSG_69,
        BC250_PSP_RING_C2PMSG_70, BC250_PSP_RING_C2PMSG_71
    };
    const uint32_t values[4] = {
        Evidence->C2pmsg67Before, Evidence->RingLowBefore,
        Evidence->RingHighBefore, Evidence->RingSizeBefore
    };
    uint32_t i, value;
    for (i = 0u; i < 4u; ++i) {
        if (!Interface->Write32(Interface->Context, offsets[i], values[i]))
            return 0u;
    }
    for (i = 0u; i < 4u; ++i) {
        if (!Interface->Read32(Interface->Context, offsets[i], &value) ||
            value != values[i]) return 0u;
    }
    return 1u;
}

BC250_PSP_RING_STATUS Bc250RunPspRingTransaction(
    uint64_t RingPhysical,
    const BC250_PSP_RING_INTERFACE *Interface,
    uint32_t MaxPolls,
    BC250_PSP_RING_EVIDENCE *Evidence)
{
    BC250_PSP_RING_STATUS status = BC250_PSP_RING_OK;
    if (Evidence != NULL) memset(Evidence, 0, sizeof(*Evidence));
    if (Interface == NULL || Evidence == NULL || Interface->Read32 == NULL ||
        Interface->Write32 == NULL || Interface->Stall == NULL ||
        Interface->Identity != BC250_PSP_RING_IDENTITY || MaxPolls == 0u ||
        (RingPhysical & 0xFFFu) != 0u || RingPhysical > 0xFFFFFFFFu) {
        return BC250_PSP_RING_BAD_ARGUMENT;
    }
    Evidence->RingPhysical = RingPhysical;
    if (!Interface->Read32(Interface->Context, BC250_PSP_RING_C2PMSG_64,
            &Evidence->C2pmsg64Before) ||
        !Interface->Read32(Interface->Context, BC250_PSP_RING_C2PMSG_67,
            &Evidence->C2pmsg67Before) ||
        !Interface->Read32(Interface->Context, BC250_PSP_RING_C2PMSG_69,
            &Evidence->RingLowBefore) ||
        !Interface->Read32(Interface->Context, BC250_PSP_RING_C2PMSG_70,
            &Evidence->RingHighBefore) ||
        !Interface->Read32(Interface->Context, BC250_PSP_RING_C2PMSG_71,
            &Evidence->RingSizeBefore)) return BC250_PSP_RING_READ_FAILED;
    if (Evidence->C2pmsg64Before == 0xFFFFFFFFu ||
        (Evidence->C2pmsg64Before & BC250_PSP_RING_TOS_READY) == 0u) {
        return BC250_PSP_RING_TOS_NOT_READY;
    }
    if (Evidence->C2pmsg67Before != 0u || Evidence->RingLowBefore != 0u ||
        Evidence->RingHighBefore != 0u || Evidence->RingSizeBefore != 0u) {
        return BC250_PSP_RING_INHERITED_ACTIVE;
    }
    if (!Interface->Write32(Interface->Context, BC250_PSP_RING_C2PMSG_69,
            (uint32_t)RingPhysical) ||
        !Interface->Write32(Interface->Context, BC250_PSP_RING_C2PMSG_70, 0u) ||
        !Interface->Write32(Interface->Context, BC250_PSP_RING_C2PMSG_71,
            BC250_PSP_RING_BYTES) ||
        !Interface->Write32(Interface->Context, BC250_PSP_RING_C2PMSG_64,
            BC250_PSP_RING_CREATE_KM)) {
        status = BC250_PSP_RING_WRITE_FAILED;
        goto Restore;
    }
    Interface->Stall(Interface->Context, 20000u);
    if (!Bc250PspPollAck(Interface, MaxPolls, &Evidence->CreatePolls,
            &Evidence->CreateAck)) {
        status = BC250_PSP_RING_CREATE_TIMEOUT;
    } else {
        Evidence->CreatePassed = 1u;
    }
    if (!Interface->Write32(Interface->Context, BC250_PSP_RING_C2PMSG_64,
            BC250_PSP_RING_DESTROY)) {
        return BC250_PSP_RING_WRITE_FAILED;
    }
    Interface->Stall(Interface->Context, 20000u);
    if (!Bc250PspPollAck(Interface, MaxPolls, &Evidence->DestroyPolls,
            &Evidence->DestroyAck)) {
        return BC250_PSP_RING_DESTROY_TIMEOUT;
    }
    Evidence->DestroyPassed = 1u;

Restore:
    Evidence->RestorePassed = Bc250PspRestore(Interface, Evidence);
    if (Evidence->RestorePassed == 0u) return BC250_PSP_RING_RESTORE_FAILED;
    if (status == BC250_PSP_RING_OK) Evidence->PhysicalReady = 1u;
    return status;
}
