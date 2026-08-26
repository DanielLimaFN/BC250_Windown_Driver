#ifndef BC250_GFXHUB_TLB_TRANSACTION_H
#define BC250_GFXHUB_TLB_TRANSACTION_H

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

#include "bc250_paging_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_TLB_TRANSPORT_IDENTITY 0x544C4231u

typedef uint32_t (*BC250_TLB_READ32)(
    void *Context, uint32_t ByteOffset, uint32_t *Value);
typedef uint32_t (*BC250_TLB_WRITE32)(
    void *Context, uint32_t ByteOffset, uint32_t Value);
typedef void (*BC250_TLB_STALL)(void *Context, uint32_t Microseconds);

typedef struct BC250_TLB_TRANSACTION_INTERFACE {
    BC250_TLB_READ32 Read32;
    BC250_TLB_WRITE32 Write32;
    BC250_TLB_STALL Stall;
    void *Context;
    uint32_t Identity;
} BC250_TLB_TRANSACTION_INTERFACE;

typedef enum BC250_TLB_TRANSACTION_STATUS {
    BC250_TLB_TRANSACTION_OK = 0,
    BC250_TLB_TRANSACTION_BAD_ARGUMENT,
    BC250_TLB_TRANSACTION_BAD_PLAN,
    BC250_TLB_TRANSACTION_READ_FAILED,
    BC250_TLB_TRANSACTION_UNDECODED,
    BC250_TLB_TRANSACTION_WRITE_FAILED,
    BC250_TLB_TRANSACTION_RANGE_READBACK_FAILED,
    BC250_TLB_TRANSACTION_TIMEOUT,
    BC250_TLB_TRANSACTION_RESTORE_FAILED
} BC250_TLB_TRANSACTION_STATUS;

typedef struct BC250_TLB_TRANSACTION_EVIDENCE {
    uint32_t Vmid;
    uint32_t EngineId;
    uint32_t RequestValue;
    uint32_t RangeLowBefore;
    uint32_t RangeHighBefore;
    uint32_t RangeLowProgrammed;
    uint32_t RangeHighProgrammed;
    uint32_t RequestDummyRead;
    uint32_t AckBefore;
    uint32_t AckAfter;
    uint32_t PollCount;
    uint32_t RequestWritten;
    uint32_t DummyReadCompleted;
    uint32_t AckObserved;
    uint32_t RangeRestorePassed;
    uint32_t PhysicalReady;
} BC250_TLB_TRANSACTION_EVIDENCE;

BC250_TLB_TRANSACTION_STATUS Bc250RunGfxhubTlbTransaction(
    const BC250_GFXHUB_TLB_FLUSH_PLAN *Plan,
    const BC250_TLB_TRANSACTION_INTERFACE *Interface,
    uint32_t MaxPolls,
    BC250_TLB_TRANSACTION_EVIDENCE *Evidence);

#ifdef __cplusplus
}
#endif

#endif
