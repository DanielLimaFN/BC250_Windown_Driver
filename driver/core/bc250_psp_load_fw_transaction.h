#ifndef BC250_PSP_LOAD_FW_TRANSACTION_H
#define BC250_PSP_LOAD_FW_TRANSACTION_H

#include "bc250_psp_ring_transaction.h"

#define BC250_PSP_LOAD_FW_IDENTITY        0x50534657u /* PSFW */
#define BC250_PSP_LOAD_FW_COMMAND_BYTES   0x1000u
#define BC250_PSP_LOAD_FW_DECLARED_BYTES  0x0400u
#define BC250_PSP_LOAD_FW_RESPONSE_OFFSET 864u
#define BC250_PSP_LOAD_FW_FRAME_DWORDS    16u
#define BC250_PSP_LOAD_FW_COMMAND_ID      6u
#define BC250_PSP_LOAD_FW_SDMA0_TYPE      9u
#define BC250_PSP_LOAD_FW_RLC_TYPE        8u
#define BC250_PSP_LOAD_FW_CP_ME_TYPE      1u
#define BC250_PSP_LOAD_FW_CP_PFP_TYPE     2u
#define BC250_PSP_LOAD_FW_CP_CE_TYPE      3u
#define BC250_PSP_LOAD_FW_CP_MEC_TYPE     4u
#define BC250_PSP_LOAD_FW_CP_MEC_JT1_TYPE 5u
#define BC250_SDMA0_UCODE_CHECKSUM        0x00004A24u

typedef void (*BC250_PSP_LOAD_FW_BARRIER)(void *Context);

typedef struct BC250_PSP_LOAD_FW_INTERFACE {
    BC250_PSP_RING_READ32 Read32;
    BC250_PSP_RING_WRITE32 Write32;
    BC250_PSP_RING_STALL Stall;
    BC250_PSP_LOAD_FW_BARRIER Barrier;
    void *Context;
    uint32_t Identity;
} BC250_PSP_LOAD_FW_INTERFACE;

typedef enum BC250_PSP_LOAD_FW_STATUS {
    BC250_PSP_LOAD_FW_OK = 0,
    BC250_PSP_LOAD_FW_BAD_ARGUMENT,
    BC250_PSP_LOAD_FW_TOS_NOT_READY,
    BC250_PSP_LOAD_FW_INHERITED_ACTIVE,
    BC250_PSP_LOAD_FW_READ_FAILED,
    BC250_PSP_LOAD_FW_WRITE_FAILED,
    BC250_PSP_LOAD_FW_CREATE_TIMEOUT,
    BC250_PSP_LOAD_FW_FENCE_TIMEOUT,
    BC250_PSP_LOAD_FW_RESPONSE_FAILED,
    BC250_PSP_LOAD_FW_CHECKSUM_FAILED,
    BC250_PSP_LOAD_FW_DESTROY_TIMEOUT,
    BC250_PSP_LOAD_FW_RESTORE_FAILED
} BC250_PSP_LOAD_FW_STATUS;

typedef struct BC250_PSP_LOAD_FW_EVIDENCE {
    uint64_t RingPhysical;
    uint64_t CommandPhysical;
    uint64_t FencePhysical;
    uint64_t FirmwarePhysical;
    uint32_t FirmwareBytes;
    uint32_t FirmwareType;
    uint32_t C2pmsg64Before;
    uint32_t C2pmsg67Before;
    uint32_t RingLowBefore;
    uint32_t RingHighBefore;
    uint32_t RingSizeBefore;
    uint32_t ChecksumBefore;
    uint32_t CreateAck;
    uint32_t CreatePolls;
    uint32_t FenceExpected;
    uint32_t FenceObserved;
    uint32_t FencePolls;
    uint32_t ResponseStatus;
    uint32_t ResponseFwLow;
    uint32_t ResponseFwHigh;
    uint32_t ResponseTmrBytes;
    uint32_t ChecksumAfter;
    uint32_t DestroyAck;
    uint32_t DestroyPolls;
    uint32_t CreatePassed;
    uint32_t FencePassed;
    uint32_t ResponsePassed;
    uint32_t ChecksumPassed;
    uint32_t DestroyPassed;
    uint32_t RestorePassed;
    uint32_t PhysicalReady;
} BC250_PSP_LOAD_FW_EVIDENCE;

BC250_PSP_LOAD_FW_STATUS Bc250RunPspLoadFirmwareTransaction(
    uint64_t RingPhysical,
    uint32_t *RingVirtual,
    uint64_t CommandPhysical,
    uint32_t *CommandVirtual,
    uint64_t FencePhysical,
    volatile uint32_t *FenceVirtual,
    uint64_t FirmwarePhysical,
    uint32_t FirmwareBytes,
    uint32_t FirmwareType,
    const BC250_PSP_LOAD_FW_INTERFACE *Interface,
    uint32_t MaxPolls,
    BC250_PSP_LOAD_FW_EVIDENCE *Evidence);

#endif
