#include "bc250_psp_load_fw_transaction.h"

#include <string.h>

static uint32_t Bc250PspFwPollAck(
    const BC250_PSP_LOAD_FW_INTERFACE *Interface,
    uint32_t MaxPolls, uint32_t *Polls, uint32_t *Ack)
{
    uint32_t i;
    for (i = 1u; i <= MaxPolls; ++i) {
        if (!Interface->Read32(Interface->Context,
                BC250_PSP_RING_C2PMSG_64, Ack)) return 0u;
        *Polls = i;
        if (*Ack != 0xFFFFFFFFu &&
            (*Ack & BC250_PSP_RING_ACK_MASK) ==
                BC250_PSP_RING_ACK_VALUE) return 1u;
        Interface->Stall(Interface->Context, 1000u);
    }
    return 0u;
}

static uint32_t Bc250PspFwRestore(
    const BC250_PSP_LOAD_FW_INTERFACE *Interface,
    const BC250_PSP_LOAD_FW_EVIDENCE *Evidence)
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

BC250_PSP_LOAD_FW_STATUS Bc250RunPspLoadFirmwareTransaction(
    uint64_t RingPhysical, uint32_t *RingVirtual,
    uint64_t CommandPhysical, uint32_t *CommandVirtual,
    uint64_t FencePhysical, volatile uint32_t *FenceVirtual,
    uint64_t FirmwarePhysical, uint32_t FirmwareBytes,
    uint32_t FirmwareType, const BC250_PSP_LOAD_FW_INTERFACE *Interface,
    uint32_t MaxPolls, BC250_PSP_LOAD_FW_EVIDENCE *Evidence)
{
    BC250_PSP_LOAD_FW_STATUS status = BC250_PSP_LOAD_FW_OK;
    uint32_t *response;
    uint32_t i;
    if (Evidence != NULL) memset(Evidence, 0, sizeof(*Evidence));
    if (RingVirtual == NULL || CommandVirtual == NULL || FenceVirtual == NULL ||
        Interface == NULL || Evidence == NULL || Interface->Read32 == NULL ||
        Interface->Write32 == NULL || Interface->Stall == NULL ||
        Interface->Barrier == NULL || Interface->Identity !=
            BC250_PSP_LOAD_FW_IDENTITY || MaxPolls == 0u || FirmwareBytes == 0u ||
        (FirmwareType != BC250_PSP_LOAD_FW_SDMA0_TYPE &&
         FirmwareType != BC250_PSP_LOAD_FW_SDMA1_TYPE &&
         FirmwareType != BC250_PSP_LOAD_FW_RLC_TYPE &&
         FirmwareType != BC250_PSP_LOAD_FW_CP_ME_TYPE &&
         FirmwareType != BC250_PSP_LOAD_FW_CP_PFP_TYPE &&
         FirmwareType != BC250_PSP_LOAD_FW_CP_CE_TYPE &&
         FirmwareType != BC250_PSP_LOAD_FW_CP_MEC_TYPE &&
         FirmwareType != BC250_PSP_LOAD_FW_CP_MEC_JT1_TYPE) ||
        (RingPhysical & 0xFFFu) != 0u || (CommandPhysical & 0xFFFu) != 0u ||
        (FencePhysical & 0xFFFu) != 0u ||
        (FirmwarePhysical & 0xFFFu) != 0u || RingPhysical > 0xFFFFFFFFFFull ||
        CommandPhysical > 0xFFFFFFFFFFull || FencePhysical > 0xFFFFFFFFFFull ||
        FirmwarePhysical > 0xFFFFFFFFFFull) return BC250_PSP_LOAD_FW_BAD_ARGUMENT;

    Evidence->RingPhysical = RingPhysical;
    Evidence->CommandPhysical = CommandPhysical;
    Evidence->FencePhysical = FencePhysical;
    Evidence->FirmwarePhysical = FirmwarePhysical;
    Evidence->FirmwareBytes = FirmwareBytes;
    Evidence->FirmwareType = FirmwareType;
    if (!Interface->Read32(Interface->Context, BC250_PSP_RING_C2PMSG_64,
            &Evidence->C2pmsg64Before) ||
        !Interface->Read32(Interface->Context, BC250_PSP_RING_C2PMSG_67,
            &Evidence->C2pmsg67Before) ||
        !Interface->Read32(Interface->Context, BC250_PSP_RING_C2PMSG_69,
            &Evidence->RingLowBefore) ||
        !Interface->Read32(Interface->Context, BC250_PSP_RING_C2PMSG_70,
            &Evidence->RingHighBefore) ||
        !Interface->Read32(Interface->Context, BC250_PSP_RING_C2PMSG_71,
            &Evidence->RingSizeBefore)) return BC250_PSP_LOAD_FW_READ_FAILED;
    if (FirmwareType == BC250_PSP_LOAD_FW_SDMA0_TYPE &&
        !Interface->Read32(Interface->Context, BC250_SDMA0_UCODE_CHECKSUM,
            &Evidence->ChecksumBefore)) return BC250_PSP_LOAD_FW_READ_FAILED;
    if (FirmwareType == BC250_PSP_LOAD_FW_SDMA1_TYPE &&
        !Interface->Read32(Interface->Context, BC250_SDMA1_UCODE_CHECKSUM,
            &Evidence->ChecksumBefore)) return BC250_PSP_LOAD_FW_READ_FAILED;
    if ((Evidence->C2pmsg64Before & BC250_PSP_RING_TOS_READY) == 0u ||
        Evidence->C2pmsg64Before == 0xFFFFFFFFu)
        return BC250_PSP_LOAD_FW_TOS_NOT_READY;
    if (Evidence->C2pmsg67Before != 0u || Evidence->RingLowBefore != 0u ||
        Evidence->RingHighBefore != 0u || Evidence->RingSizeBefore != 0u)
        return BC250_PSP_LOAD_FW_INHERITED_ACTIVE;

    memset(RingVirtual, 0, BC250_PSP_RING_BYTES);
    memset(CommandVirtual, 0, BC250_PSP_LOAD_FW_COMMAND_BYTES);
    *FenceVirtual = 0u;
    Interface->Barrier(Interface->Context);
    if (!Interface->Write32(Interface->Context, BC250_PSP_RING_C2PMSG_69,
            (uint32_t)RingPhysical) ||
        !Interface->Write32(Interface->Context, BC250_PSP_RING_C2PMSG_70,
            (uint32_t)(RingPhysical >> 32)) ||
        !Interface->Write32(Interface->Context, BC250_PSP_RING_C2PMSG_71,
            BC250_PSP_RING_BYTES) ||
        !Interface->Write32(Interface->Context, BC250_PSP_RING_C2PMSG_64,
            BC250_PSP_RING_CREATE_KM)) {
        status = BC250_PSP_LOAD_FW_WRITE_FAILED;
        goto Restore;
    }
    Interface->Stall(Interface->Context, 20000u);
    if (!Bc250PspFwPollAck(Interface, MaxPolls, &Evidence->CreatePolls,
            &Evidence->CreateAck)) {
        status = BC250_PSP_LOAD_FW_CREATE_TIMEOUT;
        goto Destroy;
    }
    Evidence->CreatePassed = 1u;

    CommandVirtual[0] = BC250_PSP_LOAD_FW_DECLARED_BYTES;
    CommandVirtual[1] = 1u;
    CommandVirtual[2] = BC250_PSP_LOAD_FW_COMMAND_ID;
    CommandVirtual[7] = (uint32_t)FirmwarePhysical;
    CommandVirtual[8] = (uint32_t)(FirmwarePhysical >> 32);
    CommandVirtual[9] = FirmwareBytes;
    CommandVirtual[10] = FirmwareType;
    Evidence->FenceExpected = 1u;
    RingVirtual[0] = (uint32_t)CommandPhysical;
    RingVirtual[1] = (uint32_t)(CommandPhysical >> 32);
    RingVirtual[2] = BC250_PSP_LOAD_FW_DECLARED_BYTES;
    RingVirtual[3] = (uint32_t)FencePhysical;
    RingVirtual[4] = (uint32_t)(FencePhysical >> 32);
    RingVirtual[5] = Evidence->FenceExpected;
    Interface->Barrier(Interface->Context);
    if (!Interface->Write32(Interface->Context, BC250_PSP_RING_C2PMSG_67,
            BC250_PSP_LOAD_FW_FRAME_DWORDS)) {
        status = BC250_PSP_LOAD_FW_WRITE_FAILED;
        goto Destroy;
    }
    for (i = 1u; i <= MaxPolls; ++i) {
        Interface->Barrier(Interface->Context);
        Evidence->FenceObserved = *FenceVirtual;
        Evidence->FencePolls = i;
        if (Evidence->FenceObserved == Evidence->FenceExpected) break;
        Interface->Stall(Interface->Context, 1000u);
    }
    if (Evidence->FenceObserved != Evidence->FenceExpected) {
        status = BC250_PSP_LOAD_FW_FENCE_TIMEOUT;
        goto Destroy;
    }
    Evidence->FencePassed = 1u;
    Interface->Stall(Interface->Context, 1000u);
    Interface->Barrier(Interface->Context);
    response = CommandVirtual + BC250_PSP_LOAD_FW_RESPONSE_OFFSET / 4u;
    Evidence->ResponseStatus = response[0];
    Evidence->ResponseFwLow = response[2];
    Evidence->ResponseFwHigh = response[3];
    Evidence->ResponseTmrBytes = response[4];
    if (Evidence->ResponseStatus != 0u) {
        status = BC250_PSP_LOAD_FW_RESPONSE_FAILED;
        goto Destroy;
    }
    Evidence->ResponsePassed = 1u;
    if (FirmwareType == BC250_PSP_LOAD_FW_SDMA0_TYPE ||
        FirmwareType == BC250_PSP_LOAD_FW_SDMA1_TYPE) {
        if (!Interface->Read32(Interface->Context,
                FirmwareType == BC250_PSP_LOAD_FW_SDMA1_TYPE ?
                    BC250_SDMA1_UCODE_CHECKSUM : BC250_SDMA0_UCODE_CHECKSUM,
                &Evidence->ChecksumAfter)) {
            status = BC250_PSP_LOAD_FW_READ_FAILED;
            goto Destroy;
        }
        if (Evidence->ChecksumAfter == 0u ||
            Evidence->ChecksumAfter == 0xFFFFFFFFu) {
            status = BC250_PSP_LOAD_FW_CHECKSUM_FAILED;
            goto Destroy;
        }
        Evidence->ChecksumPassed = 1u;
    }

Destroy:
    if (!Interface->Write32(Interface->Context, BC250_PSP_RING_C2PMSG_64,
            BC250_PSP_RING_DESTROY)) return BC250_PSP_LOAD_FW_WRITE_FAILED;
    Interface->Stall(Interface->Context, 20000u);
    if (!Bc250PspFwPollAck(Interface, MaxPolls, &Evidence->DestroyPolls,
            &Evidence->DestroyAck)) return BC250_PSP_LOAD_FW_DESTROY_TIMEOUT;
    Evidence->DestroyPassed = 1u;
Restore:
    Evidence->RestorePassed = Bc250PspFwRestore(Interface, Evidence);
    if (Evidence->RestorePassed == 0u) return BC250_PSP_LOAD_FW_RESTORE_FAILED;
    if (status == BC250_PSP_LOAD_FW_OK) Evidence->PhysicalReady = 1u;
    return status;
}
