#ifndef BC250_PSP_RING_TRANSACTION_H
#define BC250_PSP_RING_TRANSACTION_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stdint.h>
#endif

#define BC250_PSP_RING_C2PMSG_64 0x00058200u
#define BC250_PSP_RING_C2PMSG_67 0x0005820Cu
#define BC250_PSP_RING_C2PMSG_69 0x00058214u
#define BC250_PSP_RING_C2PMSG_70 0x00058218u
#define BC250_PSP_RING_C2PMSG_71 0x0005821Cu
#define BC250_PSP_RING_TOS_READY 0x80000000u
#define BC250_PSP_RING_ACK_MASK  0x8000FFFFu
#define BC250_PSP_RING_ACK_VALUE 0x80000000u
#define BC250_PSP_RING_CREATE_KM 0x00020000u
#define BC250_PSP_RING_DESTROY   0x000C0000u
#define BC250_PSP_RING_BYTES     0x00001000u
#define BC250_PSP_RING_IDENTITY  0x50535052u /* PSPR */

typedef uint32_t (*BC250_PSP_RING_READ32)(
    void *Context, uint32_t Offset, uint32_t *Value);
typedef uint32_t (*BC250_PSP_RING_WRITE32)(
    void *Context, uint32_t Offset, uint32_t Value);
typedef void (*BC250_PSP_RING_STALL)(void *Context, uint32_t Microseconds);

typedef struct BC250_PSP_RING_INTERFACE {
    BC250_PSP_RING_READ32 Read32;
    BC250_PSP_RING_WRITE32 Write32;
    BC250_PSP_RING_STALL Stall;
    void *Context;
    uint32_t Identity;
} BC250_PSP_RING_INTERFACE;

typedef enum BC250_PSP_RING_STATUS {
    BC250_PSP_RING_OK = 0,
    BC250_PSP_RING_BAD_ARGUMENT,
    BC250_PSP_RING_TOS_NOT_READY,
    BC250_PSP_RING_INHERITED_ACTIVE,
    BC250_PSP_RING_READ_FAILED,
    BC250_PSP_RING_WRITE_FAILED,
    BC250_PSP_RING_CREATE_TIMEOUT,
    BC250_PSP_RING_DESTROY_TIMEOUT,
    BC250_PSP_RING_RESTORE_FAILED
} BC250_PSP_RING_STATUS;

typedef struct BC250_PSP_RING_EVIDENCE {
    uint64_t RingPhysical;
    uint32_t C2pmsg64Before;
    uint32_t C2pmsg67Before;
    uint32_t RingLowBefore;
    uint32_t RingHighBefore;
    uint32_t RingSizeBefore;
    uint32_t CreateAck;
    uint32_t DestroyAck;
    uint32_t CreateCommandObserved;
    uint32_t DestroyCommandObserved;
    uint32_t CreatePolls;
    uint32_t DestroyPolls;
    uint32_t CreatePassed;
    uint32_t DestroyPassed;
    uint32_t RestorePassed;
    uint32_t PhysicalReady;
} BC250_PSP_RING_EVIDENCE;

BC250_PSP_RING_STATUS Bc250RunPspRingTransaction(
    uint64_t RingPhysical,
    const BC250_PSP_RING_INTERFACE *Interface,
    uint32_t MaxPolls,
    BC250_PSP_RING_EVIDENCE *Evidence);

#endif
